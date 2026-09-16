#include "tcp_client.h"
#include "json_protocol.h"
#include "common.h"

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <deque>
#include <fstream>
#include <new>
#include <poll.h>
#include <pthread.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace
{

const size_t QUEUE_CAPACITY = 32;
const int IO_TIMEOUT_MS = 3000;
const int RETRY_INTERVAL_MS = 2000;
const int HEARTBEAT_INTERVAL_MS = 5000;
const size_t MAX_MESSAGE_SIZE = 65536;

struct PendingResult
{
    DetectionResult result;
    unsigned long long sequence;
};

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
std::deque<PendingResult> queue;

pthread_t worker;
std::atomic<bool> stopping(false);
bool started = false;

std::string server_ip;
std::string device_id;
std::string session_id;
int server_port = 0;

/* 由 queue_mutex 保护 */
unsigned long long next_sequence = 0;

bool valid_identifier(const std::string &value)
{
    return !value.empty() &&
           value.size() <= 64 &&
           value.find_first_not_of(
               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
               "abcdefghijklmnopqrstuvwxyz"
               "0123456789_-"
           ) == std::string::npos;
}

std::string make_id(unsigned long long sequence)
{
    return session_id + "-" + std::to_string(sequence);
}

/* 最多每 100 ms 检查一次停止标志 */
void interruptible_delay(int milliseconds)
{
    const long long deadline =
        monotonic_us() + milliseconds * 1000LL;

    while(!stopping.load())
    {
        const long long remaining = deadline - monotonic_us();

        if(remaining <= 0)
        {
            break;
        }

        const int wait_ms =
            remaining > 100000 ? 100 :
            static_cast<int>((remaining + 999) / 1000);

        poll(NULL, 0, wait_ms);
    }
}

/* deadline 为单调时钟绝对截止时间 */
bool wait_socket(int fd, short events, long long deadline)
{
    while(!stopping.load())
    {
        const long long remaining = deadline - monotonic_us();

        if(remaining <= 0)
        {
            return false;
        }

        const int wait_ms =
            remaining > 100000 ? 100 :
            static_cast<int>((remaining + 999) / 1000);

        struct pollfd item;
        item.fd = fd;
        item.events = events;
        item.revents = 0;

        const int ret = poll(&item, 1, wait_ms);

        if(ret < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }

            return false;
        }

        if(ret == 0)
        {
            continue;
        }

        /* POLLIN 与 POLLHUP 同时出现时，先读取已有数据 */
        if(item.revents & events)
        {
            return true;
        }

        if(item.revents & (POLLERR | POLLHUP | POLLNVAL))
        {
            return false;
        }
    }

    return false;
}

int connect_server()
{
    const int fd = socket(
        AF_INET,
        SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
        0
    );

    if(fd < 0)
    {
        return -1;
    }

    struct sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(server_port));

    if(inet_pton(
            AF_INET,
            server_ip.c_str(),
            &address.sin_addr) != 1)
    {
        close(fd);
        return -1;
    }

    const int ret = connect(
        fd,
        reinterpret_cast<struct sockaddr *>(&address),
        sizeof(address)
    );

    if(ret < 0)
    {
        if(errno != EINPROGRESS)
        {
            close(fd);
            return -1;
        }

        const long long deadline =
            monotonic_us() + IO_TIMEOUT_MS * 1000LL;

        if(!wait_socket(fd, POLLOUT, deadline))
        {
            close(fd);
            return -1;
        }

        int error = 0;
        socklen_t length = sizeof(error);

        if(getsockopt(
                fd, SOL_SOCKET, SO_ERROR,
                &error, &length) < 0 ||
           error != 0)
        {
            close(fd);
            return -1;
        }
    }

    return fd;
}

bool send_all(
        int fd,
        const void *data,
        size_t size,
        long long deadline)
{
    const unsigned char *bytes =
        static_cast<const unsigned char *>(data);

    size_t sent = 0;

    while(sent < size && !stopping.load())
    {
        if(!wait_socket(fd, POLLOUT, deadline))
        {
            return false;
        }

        const ssize_t count = send(
            fd,
            bytes + sent,
            size - sent,
            MSG_NOSIGNAL
        );

        if(count > 0)
        {
            sent += static_cast<size_t>(count);
            continue;
        }

        if(count < 0 &&
           (errno == EINTR ||
            errno == EAGAIN ||
            errno == EWOULDBLOCK))
        {
            continue;
        }

        return false;
    }

    return sent == size;
}

/*
 * 请求：4 字节网络字节序长度 + UTF-8 JSON。
 * 响应：单字节 0x06，代表该消息已被服务端确认。
 *
 * 每个连接同一时间只有一条待确认消息，
 * 因此 ACK 与当前请求一一对应。
 * 超时后关闭连接，不能继续复用旧连接的 ACK。
 */
bool send_and_confirm(int fd, const std::string &json)
{
    if(json.empty() || json.size() > MAX_MESSAGE_SIZE)
    {
        return false;
    }

    const uint32_t length =
        htonl(static_cast<uint32_t>(json.size()));

    const long long deadline =
        monotonic_us() + IO_TIMEOUT_MS * 1000LL;

    if(!send_all(fd, &length, sizeof(length), deadline) ||
       !send_all(fd, json.data(), json.size(), deadline))
    {
        return false;
    }

    while(!stopping.load())
    {
        if(!wait_socket(fd, POLLIN, deadline))
        {
            return false;
        }

        unsigned char ack = 0;
        const ssize_t count = recv(fd, &ack, 1, 0);

        if(count == 1)
        {
            return ack == 0x06;
        }

        if(count < 0 &&
           (errno == EINTR ||
            errno == EAGAIN ||
            errno == EWOULDBLOCK))
        {
            continue;
        }

        return false;
    }

    return false;
}

void *network_thread(void *)
{
    int fd = -1;
    long long next_heartbeat = 0;

    while(!stopping.load())
    {
        if(fd < 0)
        {
            fd = connect_server();

            if(fd < 0)
            {
                if(!stopping.load())
                {
                    LOG_INFO("TCP connect failed; retry in %d ms",
                             RETRY_INTERVAL_MS);
                }

                interruptible_delay(RETRY_INTERVAL_MS);
                continue;
            }

            LOG_INFO("TCP connected %s:%d",
                     server_ip.c_str(), server_port);

            next_heartbeat = 0;
        }

        /*
         * 每五秒发一次心跳，包括持续有检测消息时。
         * 心跳也需要 ACK，确认服务端能响应。
         */
        if(monotonic_us() >= next_heartbeat)
        {
            unsigned long long sequence;

            pthread_mutex_lock(&queue_mutex);
            sequence = ++next_sequence;
            pthread_mutex_unlock(&queue_mutex);

            std::string json;

            const bool built = protocol_build_message(
                device_id, make_id(sequence), NULL, json
            );

            if(!built || !send_and_confirm(fd, json))
            {
                close(fd);
                fd = -1;
                interruptible_delay(RETRY_INTERVAL_MS);
                continue;
            }

            next_heartbeat =
                monotonic_us() + HEARTBEAT_INTERVAL_MS * 1000LL;

            LOG_INFO("TCP heartbeat acknowledged");
        }

        PendingResult item = {};
        bool have_result = false;

        /*
         * 这里只复制队首，不弹出。
         * 服务端 ACK 后才弹出，失败则重连后重发相同 ID。
         */
        pthread_mutex_lock(&queue_mutex);

        if(!queue.empty())
        {
            item = queue.front();
            have_result = true;
        }

        pthread_mutex_unlock(&queue_mutex);

        if(!have_result)
        {
            interruptible_delay(20);
            continue;
        }

        std::string json;

        if(!protocol_build_message(
                device_id,
                make_id(item.sequence),
                &item.result,
                json))
        {
            LOG_ERR("TCP invalid detection result; discarded");

            pthread_mutex_lock(&queue_mutex);
            queue.pop_front();
            pthread_mutex_unlock(&queue_mutex);
            continue;
        }

        const long long sent_at = monotonic_us();

        if(!send_and_confirm(fd, json))
        {
            LOG_INFO("TCP send/ACK failed; retain result and reconnect");

            close(fd);
            fd = -1;
            interruptible_delay(RETRY_INTERVAL_MS);
            continue;
        }

        pthread_mutex_lock(&queue_mutex);
        queue.pop_front();
        pthread_mutex_unlock(&queue_mutex);

        LOG_INFO(
            "TCP acknowledged seq=%llu boxes=%d round_trip=%.2f ms",
            item.sequence,
            item.result.count,
            (monotonic_us() - sent_at) / 1000.0
        );
    }

    if(fd >= 0)
    {
        close(fd);
    }

    LOG_INFO("TCP worker stopped");

    return NULL;
}

} // namespace

int tcp_client_start(
        const char *ip,
        int port,
        const char *id)
{
    if(started || ip == NULL || id == NULL ||
       port < 1 || port > 65535)
    {
        return -1;
    }

    struct in_addr address;

    if(inet_pton(AF_INET, ip, &address) != 1 ||
       !valid_identifier(id))
    {
        return -1;
    }

    /*
     * 每次启动生成独立会话 ID，避免程序重启后的消息 ID 冲突。
     * RK3588 Linux 上从内核 UUID 接口读取。
     */
    std::ifstream uuid_file("/proc/sys/kernel/random/uuid");
    std::string uuid;

    if(!(uuid_file >> uuid) || !valid_identifier(uuid))
    {
        LOG_ERR("TCP cannot obtain session UUID");
        return -1;
    }

    server_ip = ip;
    server_port = port;
    device_id = id;
    session_id = uuid;

    next_sequence = 0;
    stopping.store(false);
    queue.clear();

    const int ret = pthread_create(
        &worker, NULL, network_thread, NULL
    );

    if(ret != 0)
    {
        LOG_ERR("TCP pthread_create failed ret=%d", ret);
        return -1;
    }

    started = true;

    return 0;
}

int tcp_client_submit(const DetectionResult *result)
{
    if(!started || result == NULL ||
       result->count < 0 ||
       result->count > MAX_DETECTION_NUM)
    {
        return -1;
    }

    pthread_mutex_lock(&queue_mutex);

    if(queue.size() >= QUEUE_CAPACITY)
    {
        pthread_mutex_unlock(&queue_mutex);
        return 0;
    }

    try
    {
        PendingResult item;
        item.result = *result;
        item.sequence = ++next_sequence;
        queue.push_back(item);
    }
    catch(const std::bad_alloc &)
    {
        pthread_mutex_unlock(&queue_mutex);
        return -1;
    }

    pthread_mutex_unlock(&queue_mutex);

    return 1;
}

void tcp_client_stop()
{
    if(!started)
    {
        return;
    }

    stopping.store(true);
    pthread_join(worker, NULL);

    pthread_mutex_lock(&queue_mutex);

    const size_t discarded = queue.size();
    queue.clear();

    pthread_mutex_unlock(&queue_mutex);

    started = false;

    LOG_INFO("TCP stopped; unsent results discarded=%zu", discarded);
}