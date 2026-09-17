#ifndef EVENT_STORE_H
#define EVENT_STORE_H

#include <string>
#include <vector>

struct StoredEvent
{
    std::string id;
    std::string directory;
    std::string metadata;
    std::string jpeg;
};

/* 返回完整事件目录名，忽略 .pending_ 目录。 */
bool event_store_list(
    const std::string &root,
    std::vector<std::string> &ids);

/* 读取一个事件。JPEG 最大 8 MiB，JSON 最大 64 KiB。 */
bool event_store_load(
    const std::string &root,
    const std::string &id,
    StoredEvent &event);

/*
 * target 用于区分接收端，例如 192_168_88_100_9000。
 * 相同服务端地址对应相同确认标记。
 */
bool event_store_is_confirmed(
    const StoredEvent &event,
    const std::string &target);

/* 必须在收到服务端 ACK 后调用。 */
bool event_store_confirm(
    const StoredEvent &event,
    const std::string &target);

std::string event_store_base64(const std::string &bytes);

/*
 * 必须在启动抓拍和网络线程之前调用。
 * 配置在运行期间保持不变。
 */
void event_store_set_limits(int max_count, int min_free_mb);

/*
 * 1：允许保存一个新事件
 * 0：容量不足
 * -1：检查失败
 */
int event_store_check_capacity(const std::string &root);

/*
 * 只在 TCP 工作线程中调用，与事件上传串行。
 * 优先清理已确认事件，并恢复未完成的清理任务。
 */
void event_store_cleanup(
    const std::string &root,
    const std::string &target);


#endif