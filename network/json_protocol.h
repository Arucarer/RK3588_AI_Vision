#ifndef JSON_PROTOCOL_H
#define JSON_PROTOCOL_H

#include <string>
#include "detection_result.h"

/*
 * device_id 和 message_id 只允许字母、数字、下划线、连字符。
 * 返回 false 表示参数或检测数据非法。
 */
bool protocol_build_message(
        const std::string &device_id,
        const std::string &message_id,
        const DetectionResult *result,
        std::string &json);

#endif