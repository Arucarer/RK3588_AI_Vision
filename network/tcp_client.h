#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

#include "detection_result.h"

/*
 * 启动后台线程。
 * 服务器离线不会导致启动失败，后台自动重连。
 * 只允许主线程调用 start/submit/stop。
 */
int tcp_client_start(
    const char *server_ip,
    int server_port,
    const char *device_id,
    const char *event_directory);

/*
 * 复制检测结果，不持有调用者的数据指针。
 * 1：成功入队
 * 0：队列已满，本次未入队
 * -1：未启动或参数错误
 */
int tcp_client_submit(const DetectionResult *result);

/* 停止后台线程；未发送的内存队列会丢弃并打印数量 */
void tcp_client_stop();

#endif
