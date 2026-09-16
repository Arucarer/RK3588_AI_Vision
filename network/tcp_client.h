#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

#include <stddef.h>

int tcp_client_init(const char *server_ip, int server_port); /* 初始化TCP客户端并连接服务器 */

int tcp_client_send(const void *data, size_t len); /* 发送TCP数据 */

int tcp_client_recv(void *buf, size_t size); /* 接收TCP数据 */

int tcp_client_is_connected(); /* 获取当前TCP连接状态 */

int tcp_client_reconnect(); /* 断线重连，占位 */

int tcp_client_send_heartbeat(); /* 心跳发送，占位 */

void tcp_client_deinit(); /* 关闭TCP连接并释放资源 */

#endif