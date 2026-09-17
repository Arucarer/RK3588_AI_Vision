#include "alarm_service.h"
#include "common.h"

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <deque>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>

namespace
{

const size_t MAX_PENDING = 1024;

struct Request
{
    std::string code;
    std::string stage;
    long long timestamp = 0;

    bool done = false;
    bool ok = false;

    std::condition_variable completed;
};

std::mutex mutex;
std::condition_variable ready;
std::deque<Request *> requests;
std::thread worker;

bool running = false;
bool stopping = false;
bool unclean_start = false;

std::string root;
std::string device_id;
std::string session;

unsigned long sequence = 0;

bool identifier(const std::string &value)
{
    return !value.empty() &&
           value.size() <= 128 &&
           value.find_first_not_of(
               "abcdefghijklmnopqrstuvwxyz"
               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
               "0123456789_-") == std::string::npos;
}

bool sync_directory()
{
    const int fd = open(
        root.c_str(),
        O_RDONLY | O_DIRECTORY | O_CLOEXEC);

    if(fd < 0)
        return false;

    const bool ok = fsync(fd) == 0;
    close(fd);

    return ok;
}

struct DirectoryCloser
{
    void operator()(DIR *directory) const
    {
        if(directory != nullptr)
            closedir(directory);
    }
};

bool list_records(std::vector<std::string> &ids)
{
    std::unique_ptr<DIR, DirectoryCloser> directory(
        opendir(root.c_str()));

    if(!directory)
        return false;

    for(;;)
    {
        errno = 0;
        struct dirent *entry = readdir(directory.get());

        if(entry == nullptr)
        {
            if(errno != 0)
                return false;

            break;
        }

        const std::string name(entry->d_name);

        if(name.size() <= 5 ||
           name.compare(name.size() - 5, 5, ".json") != 0)
        {
            continue;
        }

        const std::string id = name.substr(0, name.size() - 5);

        if(identifier(id))
            ids.push_back(id);
    }

    std::sort(ids.begin(), ids.end());
    return true;
}

bool save_record(const Request &request)
{
    std::vector<std::string> ids;

    if(!list_records(ids) || ids.size() >= MAX_PENDING)
        return false;

    char serial[32];

    snprintf(serial, sizeof(serial), "%020lu", ++sequence);

    const std::string id = session + "-" + serial;

    const char *severity =
        request.code.find("recovered") != std::string::npos
            ? "info"
            : "error";

    const std::string json =
        "{\"version\":1,\"type\":\"alarm\",\"device_id\":\"" +
        device_id +
        "\",\"message_id\":\"" + id +
        "\",\"timestamp_us\":" + std::to_string(request.timestamp) +
        ",\"code\":\"" + request.code +
        "\",\"stage\":\"" + request.stage +
        "\",\"severity\":\"" + severity + "\"}";

    const std::string temporary = root + "/.pending-" + id;
    const std::string destination = root + "/" + id + ".json";

    const int fd = open(
        temporary.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
        0600);

    if(fd < 0)
        return false;

    bool ok = true;
    size_t offset = 0;

    while(offset < json.size())
    {
        const ssize_t written = write(
            fd,
            json.data() + offset,
            json.size() - offset);

        if(written < 0 && errno == EINTR)
            continue;

        if(written <= 0)
        {
            ok = false;
            break;
        }

        offset += static_cast<size_t>(written);
    }

    if(ok && fsync(fd) != 0)
        ok = false;

    if(close(fd) != 0)
        ok = false;

    if(ok && rename(temporary.c_str(), destination.c_str()) != 0)
        ok = false;

    if(!ok)
        unlink(temporary.c_str());

    return ok && sync_directory();
}

void worker_loop()
{
    if(unclean_start)
    {
        Request previous;

        previous.code = "process_unclean_exit";
        previous.stage = "previous_run";
        previous.timestamp = get_time_us();

        try
        {
            if(!save_record(previous))
                LOG_ERR("cannot persist previous-run alarm");
        }
        catch(...)
        {
            LOG_ERR("cannot persist previous-run alarm");
        }
    }

    for(;;)
    {
        Request *request = nullptr;

        {
            std::unique_lock<std::mutex> lock(mutex);

            ready.wait(lock, []
            {
                return stopping || !requests.empty();
            });

            if(requests.empty())
                return;

            request = requests.front();
            requests.pop_front();
        }

        bool ok = false;

        try
        {
            ok = save_record(*request);
        }
        catch(...)
        {
            ok = false;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);

            request->ok = ok;
            request->done = true;
            request->completed.notify_one();
        }
    }
}

} // namespace

int alarm_service_start(
    const std::string &directory,
    const std::string &device)
{
    std::lock_guard<std::mutex> lock(mutex);

    if(running || directory.empty() || directory[0] != '/' ||
       !identifier(device))
    {
        return -1;
    }

    if(mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST)
        return -1;

    struct stat info;

    if(lstat(directory.c_str(), &info) != 0 ||
       !S_ISDIR(info.st_mode))
    {
        return -1;
    }

    std::ifstream file("/proc/sys/kernel/random/uuid");
    std::string uuid;

    if(!(file >> uuid) || !identifier(uuid))
        return -1;

    root = directory;
    device_id = device;
    session = uuid;
    sequence = 0;

    /*
     * 正常退出时删除标记。
     * _exit、SIGKILL、断电等情况留下标记，供下次启动补记。
     */
    const std::string marker_path = root + "/.running";
    struct stat marker;

    unclean_start = lstat(marker_path.c_str(), &marker) == 0;

    const int marker_fd = open(
        marker_path.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
        0600);

    if(marker_fd < 0)
        return -1;

    const bool marker_ok = fsync(marker_fd) == 0;
    close(marker_fd);

    if(!marker_ok || !sync_directory())
        return -1;

    stopping = false;

    try
    {
        worker = std::thread(worker_loop);
    }
    catch(...)
    {
        return -1;
    }

    running = true;
    return 0;
}

bool alarm_raise(const char *code, const char *stage)
{
    try
    {
        Request request;

        request.code = code != nullptr ? code : "unknown";
        request.stage = stage != nullptr ? stage : "unknown";
        request.timestamp = get_time_us();

        if(!identifier(request.code) || !identifier(request.stage))
            return false;

        std::unique_lock<std::mutex> lock(mutex);

        if(!running || stopping || requests.size() >= 64)
            return false;

        requests.push_back(&request);
        ready.notify_one();

        request.completed.wait(lock, [&]
        {
            return request.done;
        });

        return request.ok;
    }
    catch(...)
    {
        return false;
    }
}

void alarm_service_stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex);

        if(!running)
            return;

        stopping = true;
        ready.notify_all();
    }

    worker.join();

    std::lock_guard<std::mutex> lock(mutex);

    running = false;

    if(unlink((root + "/.running").c_str()) != 0 ||
       !sync_directory())
    {
        LOG_ERR("cannot clear alarm service running marker");
    }
}

int alarm_pending(std::string &id, std::string &json)
{
    std::vector<std::string> ids;

    if(!list_records(ids))
        return -1;

    if(ids.empty())
        return 0;

    id.clear();

    long long oldest = 0;

    /* 根据文件落盘时间选择较早记录，不按随机 UUID 排序。 */
    for(const auto &candidate : ids)
    {
        struct stat info;

        const std::string path = root + "/" + candidate + ".json";

        if(lstat(path.c_str(), &info) != 0 ||
           !S_ISREG(info.st_mode))
        {
            return -1;
        }

        const long long timestamp =
            static_cast<long long>(info.st_mtim.tv_sec) * 1000000000LL +
            info.st_mtim.tv_nsec;

        if(id.empty() || timestamp < oldest)
        {
            oldest = timestamp;
            id = candidate;
        }
    }

    const int fd = open(
        (root + "/" + id + ".json").c_str(),
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);

    if(fd < 0)
        return -1;

    struct stat info;

    if(fstat(fd, &info) != 0 ||
       !S_ISREG(info.st_mode) ||
       info.st_size <= 0 ||
       info.st_size > 4096)
    {
        close(fd);
        return -1;
    }

    char buffer[4096];
    size_t offset = 0;

    while(offset < static_cast<size_t>(info.st_size))
    {
        const ssize_t count = read(
            fd,
            buffer + offset,
            static_cast<size_t>(info.st_size) - offset);

        if(count < 0 && errno == EINTR)
            continue;

        if(count <= 0)
        {
            close(fd);
            return -1;
        }

        offset += static_cast<size_t>(count);
    }

    close(fd);

    json.assign(buffer, offset);
    return 1;
}

bool alarm_confirm(const std::string &id)
{
    if(!identifier(id))
        return false;

    if(unlink((root + "/" + id + ".json").c_str()) != 0)
        return false;

    return sync_directory();
}