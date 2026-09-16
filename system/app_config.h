#ifndef APP_CONFIG_H
#define APP_CONFIG_H
#include <string>
#include <vector>
struct AppConfig {
    std::string model = "/opt/rk3588_ai/model/best_fp16.rknn";
    std::vector<std::string> labels = {"crazing", "inclusion", "patches", "pitted_surface", "rolled-in_scale", "scratches"};
    float confidence = 0.25f, nms = 0.45f;
    bool snapshot_enabled = true, debug = false, network_enabled = false;
    std::string snapshot_dir = "/opt/rk3588_ai/captures";
    int snapshot_interval_ms = 5000, snapshot_max_files = 200;
    int snapshot_queue = 2, jpeg_quality = 90, min_free_mb = 64;
    int stats_interval_ms = 1000, camera_timeout_ms = 5000;
    int max_errors = 10, duration_seconds = 0;
    std::string device_id = "RK3588_001", server_ip = "127.0.0.1";
    int server_port = 9000, network_timeout_ms = 3000;
};
bool load_config(const char *path, AppConfig &config, std::string &error);
bool validate_config(const AppConfig &config, std::string &error);
#endif
