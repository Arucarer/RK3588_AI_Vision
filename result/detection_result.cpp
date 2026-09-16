/* 实现检测结果管理接口 */
/* @author  zhangyong | @date    2026-09-14 | @version 1.0 */

#include "detection_result.h"

#include <string.h>

/* 初始化检测结果 */
void detection_result_init(DetectionResult *result)
{
    if(result == NULL)
        return;

    memset(result, 0, sizeof(DetectionResult));
}

/* 清空检测结果 */
void detection_result_clear(DetectionResult *result)
{
    if(result == NULL)
        return;

    result->count = 0;
}

/* 添加检测框 */
int detection_result_add(DetectionResult *result, int class_id, float confidence, int x1, int y1,
                         int x2, int y2)
{
    if(result == NULL)
        return -1;

    /* 超过最大检测数量 */
    if(result->count >= MAX_DETECTION_NUM)
        return -1;

    DetectionBox *box = &result->boxes[result->count];

    box->class_id = class_id;

    box->confidence = confidence;

    box->x1 = x1;
    box->y1 = y1;

    box->x2 = x2;
    box->y2 = y2;

    result->count++;

    return 0;
}
