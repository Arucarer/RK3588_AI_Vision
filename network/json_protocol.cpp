#include "json_protocol.h"

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

static bool valid_id(const std::string &value)
{
    return !value.empty() &&
           value.size() <= 128 &&
           value.find_first_not_of(
               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
               "abcdefghijklmnopqrstuvwxyz"
               "0123456789_-"
           ) == std::string::npos;
}

bool protocol_build_message(
        const std::string &device_id,
        const std::string &message_id,
        const DetectionResult *result,
        std::string &json)
{
    json.clear();

    if(!valid_id(device_id) || !valid_id(message_id))
    {
        return false;
    }

    if(result != NULL &&
       (result->count < 0 ||
        result->count > MAX_DETECTION_NUM ||
        result->image_width <= 0 ||
        result->image_height <= 0 ||
        !std::isfinite(result->inference_time_ms)))
    {
        return false;
    }

    static const char *names[DETECT_CLASS_NUM] =
    {
        "crazing",
        "inclusion",
        "patches",
        "pitted_surface",
        "rolled-in_scale",
        "scratches"
    };

    std::ostringstream out;

    /* 保证小数点为 "."，不受系统语言环境影响 */
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(6);

    out << "{\"version\":1"
        << ",\"type\":\""
        << (result != NULL ? "detection" : "heartbeat")
        << "\",\"device_id\":\"" << device_id
        << "\",\"message_id\":\"" << message_id << "\"";

    if(result != NULL)
    {
        out << ",\"timestamp_us\":" << result->pts
            << ",\"image_width\":" << result->image_width
            << ",\"image_height\":" << result->image_height
            << ",\"inference_ms\":" << result->inference_time_ms
            << ",\"boxes\":[";

        for(int i = 0; i < result->count; ++i)
        {
            const DetectionBox &box = result->boxes[i];

            if(box.class_id < 0 ||
               box.class_id >= DETECT_CLASS_NUM ||
               !std::isfinite(box.confidence) ||
               box.confidence < 0.0f ||
               box.confidence > 1.0f ||
               !std::isfinite(box.x1) ||
               !std::isfinite(box.y1) ||
               !std::isfinite(box.x2) ||
               !std::isfinite(box.y2) ||
               box.x2 < box.x1 ||
               box.y2 < box.y1)
            {
                return false;
            }

            if(i != 0)
            {
                out << ",";
            }

            out << "{\"class_id\":" << box.class_id
                << ",\"class_name\":\"" << names[box.class_id]
                << "\",\"confidence\":" << box.confidence
                << ",\"x1\":" << box.x1
                << ",\"y1\":" << box.y1
                << ",\"x2\":" << box.x2
                << ",\"y2\":" << box.y2
                << "}";
        }

        out << "]";
    }

    out << "}";

    json = out.str();

    return true;
}