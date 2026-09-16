#ifndef RESULT_DETECTION_RESULT_H
#define RESULT_DETECTION_RESULT_H

/*
 * 检测结果统一由 ai/detection_result.h 定义。
 */
#include "../ai/detection_result.h"

#ifdef __cplusplus
extern "C" {
#endif

void detection_result_init(
        DetectionResult *result);

void detection_result_clear(
        DetectionResult *result);

int detection_result_add(
        DetectionResult *result,
        int class_id,
        float confidence,
        int x1,
        int y1,
        int x2,
        int y2);

#ifdef __cplusplus
}
#endif

#endif