#include "app_config.h"
#include <fstream>
#include <sstream>
#include <set>
#include <cmath>
#include <stdexcept>
static std::string trim(const std::string &s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(" \t\r\n")-a+1);
}
static int number(const std::string &s) {
    size_t used; int v = std::stoi(s, &used);
    if(used != s.size()) throw std::runtime_error("invalid integer");
    return v;
}
static float real(const std::string &s) {
    size_t used; float v = std::stof(s, &used);
    if(used != s.size() || !std::isfinite(v)) throw std::runtime_error("invalid float");
    return v;
}
static bool boolean(const std::string &s) {
    if(s == "true") return true;
    if(s == "false") return false;
    throw std::runtime_error("boolean must be true/false");
}
bool validate_config(const AppConfig &c, std::string &error) {
    if(c.model.empty() || c.snapshot_dir.empty() || c.snapshot_dir[0] != '/' ||
       c.labels.size() != 6 || c.confidence <= 0 || c.confidence > 1 ||
       c.nms <= 0 || c.nms > 1 || !std::isfinite(c.confidence) || !std::isfinite(c.nms) ||
       c.snapshot_interval_ms < 100 || c.snapshot_max_files < 1 ||
       c.snapshot_queue < 1 || c.snapshot_queue > 8 || c.jpeg_quality < 1 || c.jpeg_quality > 100 ||
       c.min_free_mb < 1 || c.min_free_mb > 1048576 || c.stats_interval_ms < 100 ||
       c.camera_timeout_ms < 500 || c.max_errors < 1 || c.duration_seconds < 0 ||
       c.server_port < 1 || c.server_port > 65535 || c.network_timeout_ms < 100 ||
       c.network_timeout_ms > 30000 || c.device_id.empty() || c.device_id.size() > 64 ||
       c.device_id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos) {
        error = "invalid configuration range (model contract: six classes, 640x640)"; return false;
    }
    for(const auto &s : c.labels) {
        if(s.empty() || s.size() > 32 || s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos) {
            error = "labels must be 1..32 ASCII letters/digits/_/-"; return false;
        }
    }
    return true;
}
bool load_config(const char *path, AppConfig &config, std::string &error) {
    std::ifstream file(path);
    if(!file) { error = std::string("cannot open config: ") + path; return false; }
    AppConfig c = config;
    std::string line; int lineno = 0; std::set<std::string> seen;
    try {
        while(std::getline(file, line)) {
            ++lineno; line = trim(line);
            if(line.empty() || line[0] == '#') continue;
            const auto sep = line.find('=');
            if(sep == std::string::npos) throw std::runtime_error("expected key=value");
            std::string k = trim(line.substr(0, sep)), v = trim(line.substr(sep+1));
            if(!seen.insert(k).second) throw std::runtime_error("duplicate key: " + k);
            if(k == "model") c.model = v;
            else if(k == "labels") {
                c.labels.clear(); std::istringstream list(v); std::string item;
                if(v.empty() || v.back() == ',') throw std::runtime_error("empty label");
                while(std::getline(list, item, ',')) c.labels.push_back(trim(item));
            }
            else if(k == "confidence") c.confidence = real(v);
            else if(k == "nms") c.nms = real(v);
            else if(k == "snapshot_enabled") c.snapshot_enabled = boolean(v);
            else if(k == "debug") c.debug = boolean(v);
            else if(k == "network_enabled") c.network_enabled = boolean(v);
            else if(k == "snapshot_dir") c.snapshot_dir = v;
            else if(k == "device_id") c.device_id = v;
            else if(k == "server_ip") c.server_ip = v;
#define INT_KEY(name) else if(k == #name) c.name = number(v)
            INT_KEY(snapshot_interval_ms); INT_KEY(snapshot_max_files); INT_KEY(snapshot_queue);
            INT_KEY(jpeg_quality); INT_KEY(min_free_mb); INT_KEY(stats_interval_ms);
            INT_KEY(camera_timeout_ms); INT_KEY(max_errors); INT_KEY(duration_seconds);
            INT_KEY(server_port); INT_KEY(network_timeout_ms);
#undef INT_KEY
            else throw std::runtime_error("unknown key: " + k);
        }
    } catch(const std::exception &e) {
        error = "config line " + std::to_string(lineno) + ": " + e.what(); return false;
    }
    if(file.bad()) { error = "config read error"; return false; }
    if(!validate_config(c, error)) return false;
    config = c; return true;
}
