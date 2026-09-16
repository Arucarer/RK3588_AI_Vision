#include "tcp_client.h"
#include "common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TCP_IP_MAX_LEN 64

static int g_tcp_fd = -1; /* TCP Socket */

static char g_server_ip[TCP_IP_MAX_LEN] = {0}; /* 云端服务器IP */

static int g_server_port = 0; /* 云端服务器端口 */

static void tcp_client_close_socket()
{
    if(g_tcp_fd >= 0)
    {
        close(g_tcp_fd);

        g_tcp_fd = -1;
    }
}

int tcp_client_init(const char *server_ip, int server_port)
{
    struct sockaddr_in server_addr;

    if(server_ip == NULL || server_port <= 0)
    {
        LOG_ERR("invalid tcp server config");

        return -1;
    }

    tcp_client_close_socket();

    memset(g_server_ip, 0, sizeof(g_server_ip));

    strncpy(g_server_ip, server_ip, sizeof(g_server_ip) - 1);

    g_server_port = server_port;

    g_tcp_fd = socket(AF_INET, SOCK_STREAM, 0);

    if(g_tcp_fd < 0)
    {
        LOG_ERR("create tcp socket failed: %s", strerror(errno));

        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;

    server_addr.sin_port = htons((uint16_t)server_port);

    if(inet_pton(AF_INET, server_ip, &server_addr.sin_addr) != 1)
    {
        LOG_ERR("invalid server ip: %s", server_ip);

        tcp_client_close_socket();

        return -1;
    }

    if(connect(g_tcp_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        LOG_ERR("connect tcp server failed: %s", strerror(errno));

        tcp_client_close_socket();

        return -1;
    }

    LOG_INFO("tcp server connected: %s:%d", server_ip, server_port);

    return 0;
}

int tcp_client_send(const void *data, size_t len)
{
    const unsigned char *ptr = (const unsigned char *)data;

    size_t total = 0;

    ssize_t ret;

    if(g_tcp_fd < 0 || data == NULL || len == 0)
    {
        return -1;
    }

    while(total < len)
    {
        ret = send(g_tcp_fd, ptr + total, len - total, MSG_NOSIGNAL);

        if(ret < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }

            LOG_ERR("tcp send failed: %s", strerror(errno));

            tcp_client_close_socket();

            return -1;
        }

        if(ret == 0)
        {
            tcp_client_close_socket();

            return -1;
        }

        total += (size_t)ret;
    }

    return (int)total;
}

int tcp_client_recv(void *buf, size_t size)
{
    ssize_t ret;

    if(g_tcp_fd < 0 || buf == NULL || size == 0)
    {
        return -1;
    }

    while(1)
    {
        ret = recv(g_tcp_fd, buf, size, 0);

        if(ret < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }

            LOG_ERR("tcp recv failed: %s", strerror(errno));

            tcp_client_close_socket();

            return -1;
        }

        if(ret == 0)
        {
            LOG_INFO("tcp server disconnected");

            tcp_client_close_socket();

            return 0;
        }

        return (int)ret;
    }
}

int tcp_client_is_connected()
{
    return g_tcp_fd >= 0 ? 1 : 0;
}

int tcp_client_reconnect()
{
    return -1; /* TODO: 后续实现TCP断线重连 */
}

int tcp_client_send_heartbeat()
{
    return -1; /* TODO: 后续由协议层生成心跳报文 */
}

void tcp_client_deinit()
{
    tcp_client_close_socket();

    memset(g_server_ip, 0, sizeof(g_server_ip));

    g_server_port = 0;

    LOG_INFO("tcp client deinit");
}