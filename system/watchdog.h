#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * 启动业务看门狗。
 * timeout_ms：允许主流程连续无进展的最长时间。
 * 返回 0 表示成功，-1 表示失败。
 */
int app_watchdog_start(int timeout_ms);

/*
 * 主检测流程更新进度。
 * stage 会复制到内部缓冲区，无需保持字符串生命周期。
 * 不要从网络线程或看门狗线程调用此接口“喂狗”。
 */
void app_watchdog_feed(const char *stage);

/* 停止监控并等待监控线程退出。 */
void app_watchdog_stop(void);

#ifdef __cplusplus
}
#endif

#endif