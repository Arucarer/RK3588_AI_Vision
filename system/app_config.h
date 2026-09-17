#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <string>

struct AppConfig
{
    std::string model = "/opt/rk3588_ai/model/best_fp16.rknn";

    float confidence = 0.25f;
    float nms = 0.45f;

    bool snapshot_enabled = true;

    std::string snapshot_dir = "/opt/rk3588_ai/captures";

    int snapshot_interval_ms = 5000;

    int stats_interval_ms = 1000;
    int camera_timeout_ms = 5000;
    int max_errors = 10;

    bool debug = false;

    bool network_enabled = false;
    std::string server_ip = "127.0.0.1";
    int server_port = 9000;
    std::string device_id = "RK3588_001";

    /* 周期采样上报间隔，包括没有检测框的成功帧 */
    int report_interval_ms = 1000;
    
    /* 本地事件目录数量上限 */
    int event_max_count = 200;

    /* 保存新事件前要求保留的可用空间，单位 MiB */
    int event_min_free_mb = 128;
};

/* 检查配置内容和数值范围 */
bool validate_config(const AppConfig &config, std::string &error);

/*
 * 读取 key=value 配置文件。
 * 未填写的参数保留传入配置的值。
 * 读取或校验失败时，不修改传入配置。
 */
bool load_config(const char *path, AppConfig &config, std::string &error);

#endif
