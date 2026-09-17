#include "app_config.h"

#include <cmath>
#include <exception>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <arpa/inet.h>

static std::string trim(const std::string &text)
{
    const size_t first = text.find_first_not_of(" \t\r\n");

    if(first == std::string::npos)
    {
        return "";
    }

    const size_t last = text.find_last_not_of(" \t\r\n");

    return text.substr(first, last - first + 1);
}

static int parse_integer(const std::string &text)
{
    size_t used = 0;
    const int value = std::stoi(text, &used);

    if(used != text.size())
    {
        throw std::runtime_error("invalid integer: " + text);
    }

    return value;
}

static float parse_float(const std::string &text)
{
    size_t used = 0;
    const float value = std::stof(text, &used);

    if(used != text.size() || !std::isfinite(value))
    {
        throw std::runtime_error("invalid float: " + text);
    }

    return value;
}

static bool parse_boolean(const std::string &text)
{
    if(text == "true")
    {
        return true;
    }

    if(text == "false")
    {
        return false;
    }

    throw std::runtime_error("boolean must be true or false: " + text);
}

bool validate_config(const AppConfig &config, std::string &error)
{
    error.clear();

    if(config.model.empty())
    {
        error = "model must not be empty";
        return false;
    }

    if(!std::isfinite(config.confidence) || config.confidence <= 0.0f || config.confidence > 1.0f)
    {
        error = "confidence must be in (0, 1]";
        return false;
    }

    if(!std::isfinite(config.nms) || config.nms <= 0.0f || config.nms > 1.0f)
    {
        error = "nms must be in (0, 1]";
        return false;
    }

    if(config.snapshot_dir.empty() || config.snapshot_dir[0] != '/')
    {
        error = "snapshot_dir must be an absolute path";
        return false;
    }

    if(config.snapshot_interval_ms < 100 || config.snapshot_interval_ms > 86400000)
    {
        error = "snapshot_interval_ms must be 100..86400000";
        return false;
    }

    if(config.stats_interval_ms < 100 || config.stats_interval_ms > 60000)
    {
        error = "stats_interval_ms must be 100..60000";
        return false;
    }

    if(config.camera_timeout_ms < 500 || config.camera_timeout_ms > 60000)
    {
        error = "camera_timeout_ms must be 500..60000";
        return false;
    }

    if(config.max_errors < 1 || config.max_errors > 1000)
    {
        error = "max_errors must be 1..1000";
        return false;
    }
    if(config.watchdog_timeout_ms < 1000 ||
    config.watchdog_timeout_ms > 600000)
    {
        error = "watchdog_timeout_ms must be 1000..600000";
        return false;
    }

    if(config.watchdog_enabled &&
    config.watchdog_timeout_ms <= config.camera_timeout_ms)
    {
        error = "watchdog_timeout_ms must be greater than camera_timeout_ms";
        return false;
    }
    struct in_addr address;

    if(inet_pton(AF_INET, config.server_ip.c_str(), &address) != 1)
    {
        error = "server_ip must be an IPv4 address";
        return false;
    }

    if(config.server_port < 1 || config.server_port > 65535)
    {
        error = "server_port must be 1..65535";
        return false;
    }

    if(config.device_id.empty() || config.device_id.size() > 64 ||
       config.device_id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                          "abcdefghijklmnopqrstuvwxyz"
                                          "0123456789_-") != std::string::npos)
    {
        error = "invalid device_id";
        return false;
    }

    if(config.report_interval_ms < 100 || config.report_interval_ms > 60000)
    {
        error = "report_interval_ms must be 100..60000";
        return false;
    }
    if(config.event_max_count < 1 || config.event_max_count > 10000)
    {
        error = "event_max_count must be 1..10000";
        return false;
    }

    if(config.event_min_free_mb < 16 || config.event_min_free_mb > 1048576)
    {
        error = "event_min_free_mb must be 16..1048576";
        return false;
    }
    if(config.roi_enabled)
    {
        /*
         * NV12 裁剪起点和高度需要偶数对齐。
         * 本版紧密排列的 ROI 宽度要求 16 对齐，
         * 以适配当前 RGA 路径。
         */
        if(config.roi_x < 0 || config.roi_y < 0 ||
           config.roi_x > 16384 || config.roi_y > 16384 ||
           config.roi_width < 16 || config.roi_width > 16384 ||
           config.roi_height < 2 || config.roi_height > 16384 ||
           config.roi_x % 2 != 0 || config.roi_y % 2 != 0 ||
           config.roi_width % 16 != 0 || config.roi_height % 2 != 0)
        {
            error = "invalid ROI: x/y/height must be even; width must be a multiple of 16";
            return false;
        }
    }

    if(config.edge_low_threshold < 1 ||
       config.edge_high_threshold <= config.edge_low_threshold ||
       config.edge_high_threshold > 2048)
    {
        error = "edge thresholds require 1 <= low < high <= 2048";
        return false;
    }
    if(config.camera_device.compare(0, 5, "/dev/") != 0 ||
    config.camera_device.size() <= 5)
    {
        error = "camera_device must be a device path under /dev/";
        return false;
    }

    if(config.camera_width < 16 || config.camera_width > 8192 ||
    config.camera_height < 2 || config.camera_height > 8192 ||
    config.camera_width % 16 != 0 ||
    config.camera_height % 2 != 0)
    {
        error = "invalid camera size: width requires 16 alignment; height requires even alignment";
        return false;
    }

    if(config.camera_fps < 0 || config.camera_fps > 120)
    {
        error = "camera_fps must be 0..120; 0 keeps the driver setting";
        return false;
    }

    if(config.roi_enabled &&
    (config.roi_width > config.camera_width ||
        config.roi_height > config.camera_height ||
        config.roi_x > config.camera_width - config.roi_width ||
        config.roi_y > config.camera_height - config.roi_height))
    {
        error = "ROI is outside configured camera dimensions";
        return false;
    }
    if(config.alarm_dir.empty() || config.alarm_dir[0] != '/')
    {
        error = "alarm_dir must be an absolute path";
        return false;
    }
    return true;
}

bool load_config(const char *path, AppConfig &config, std::string &error)
{
    error.clear();

    if(path == NULL || path[0] == '\0')
    {
        error = "config path must not be empty";
        return false;
    }

    std::ifstream file(path);

    if(!file)
    {
        error = std::string("cannot open config: ") + path;
        return false;
    }

    AppConfig candidate = config;

    std::set<std::string> seen;
    std::string line;
    size_t line_number = 0;

    try
    {
        while(std::getline(file, line))
        {
            ++line_number;

            line = trim(line);

            /* 支持空行和整行 # 注释 */
            if(line.empty() || line[0] == '#')
            {
                continue;
            }

            const size_t separator = line.find('=');

            if(separator == std::string::npos)
            {
                throw std::runtime_error("expected key=value");
            }

            const std::string key = trim(line.substr(0, separator));

            const std::string value = trim(line.substr(separator + 1));

            if(key.empty() || value.empty())
            {
                throw std::runtime_error("empty key or value");
            }

            if(!seen.insert(key).second)
            {
                throw std::runtime_error("duplicate key: " + key);
            }

            if(key == "model")
            {
                candidate.model = value;
            }
            else if(key == "confidence")
            {
                candidate.confidence = parse_float(value);
            }
            else if(key == "nms")
            {
                candidate.nms = parse_float(value);
            }
            else if(key == "snapshot_enabled")
            {
                candidate.snapshot_enabled = parse_boolean(value);
            }
            else if(key == "snapshot_dir")
            {
                candidate.snapshot_dir = value;
            }
            else if(key == "snapshot_interval_ms")
            {
                candidate.snapshot_interval_ms = parse_integer(value);
            }
            else if(key == "stats_interval_ms")
            {
                candidate.stats_interval_ms = parse_integer(value);
            }
            else if(key == "camera_timeout_ms")
            {
                candidate.camera_timeout_ms = parse_integer(value);
            }
            else if(key == "max_errors")
            {
                candidate.max_errors = parse_integer(value);
            }
            else if(key == "watchdog_enabled")
            {
                candidate.watchdog_enabled = parse_boolean(value);
            }
            else if(key == "watchdog_timeout_ms")
            {
                candidate.watchdog_timeout_ms = parse_integer(value);
            }            
            else if(key == "debug")
            {
                candidate.debug = parse_boolean(value);
            }
            else if(key == "network_enabled")
            {
                candidate.network_enabled = parse_boolean(value);
            }
            else if(key == "server_ip")
            {
                candidate.server_ip = value;
            }
            else if(key == "server_port")
            {
                candidate.server_port = parse_integer(value);
            }
            else if(key == "device_id")
            {
                candidate.device_id = value;
            }
            else if(key == "report_interval_ms")
            {
                candidate.report_interval_ms = parse_integer(value);
            }
            else if(key == "event_max_count")
            {
                candidate.event_max_count = parse_integer(value);
            }
            else if(key == "event_min_free_mb")
            {
                candidate.event_min_free_mb = parse_integer(value);
            }
            else if(key == "roi_enabled")
                candidate.roi_enabled = parse_boolean(value);
            else if(key == "roi_x")
                candidate.roi_x = parse_integer(value);
            else if(key == "roi_y")
                candidate.roi_y = parse_integer(value);
            else if(key == "roi_width")
                candidate.roi_width = parse_integer(value);
            else if(key == "roi_height")
                candidate.roi_height = parse_integer(value);
            else if(key == "edge_analysis_enabled")
                candidate.edge_analysis_enabled = parse_boolean(value);
            else if(key == "edge_low_threshold")
                candidate.edge_low_threshold = parse_integer(value);
            else if(key == "edge_high_threshold")
                candidate.edge_high_threshold = parse_integer(value);
            else if(key == "camera_device")
            {
                candidate.camera_device = value;
            }
            else if(key == "camera_width")
            {
                candidate.camera_width = parse_integer(value);
            }
            else if(key == "camera_height")
            {
                candidate.camera_height = parse_integer(value);
            }
            else if(key == "camera_fps")
            {
                candidate.camera_fps = parse_integer(value);
            }
            else if(key == "alarm_dir")
            {
                candidate.alarm_dir = value;
            }
            else
            {
                throw std::runtime_error("unknown key: " + key);
            }
        }
    }
    catch(const std::exception &exception)
    {
        error = std::string(path) + ":" + std::to_string(line_number) + ": " + exception.what();

        return false;
    }

    if(file.bad())
    {
        error = std::string("config read failed: ") + path;
        return false;
    }

    if(!validate_config(candidate, error))
    {
        error = std::string(path) + ": " + error;
        return false;
    }

    config = candidate;

    return true;
}
