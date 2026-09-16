#ifndef DISPLAY_H
#define DISPLAY_H

#include "common.h"
#include "detection_result.h"

#ifdef __cplusplus
extern "C"
{
#endif

int display_init();

int display_show(FrameData *frame, DetectionResult *result);

/*
 * 提交同帧图片和检测结果。
 * 必须在主线程成功调用 display_show() 后、
 * 下一次 display_show() 之前调用。
 * 1：已入队，不代表已持久化
 * 0：队列已满
 * -1：提交失败
 */
int display_save_snapshot(const char *directory, const char *device_id,
                          const DetectionResult *result);

void display_deinit();

#ifdef __cplusplus
}
#endif

#endif
