#ifndef ALARM_SERVICE_H
#define ALARM_SERVICE_H

#include <string>

/* 在生产者线程和网络线程启动之前调用。 */
int alarm_service_start(
    const std::string &directory,
    const std::string &device);

/*
 * 线程安全。
 * 等待告警线程完成文件同步；true 表示持久化成功。
 * 不得在信号处理函数中调用。
 */
bool alarm_raise(const char *code, const char *stage);

/* 所有告警生产者、网络线程退出之后调用。 */
void alarm_service_stop();

/*
 * 以下接口只由网络线程调用。
 * pending：1 有记录，0 无记录，-1 存储错误。
 */
int alarm_pending(std::string &id, std::string &json);

/* 只能在收到服务端 ACK 后调用。 */
bool alarm_confirm(const std::string &id);

#endif