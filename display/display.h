#ifndef DISPLAY_H
#define DISPLAY_H


#include "common.h"
#include "detection_result.h"


#ifdef __cplusplus
extern "C"
{
#endif


int display_init();


int display_show(
        FrameData *frame,
        DetectionResult *result);

/*
 * 将最近一次成功显示的带标注图像复制到后台保存队列。
 * 必须在主线程中紧接成功的 display_show() 调用。
 *
 * 返回：
 *  1：成功入队，实际保存结果由后台日志报告
 *  0：队列已满，跳过
 * -1：提交失败
 *
 * 工作线程保留独立 RGB 副本。
 * 文件数量上限由工作线程检查。
 */
int display_save_snapshot(
        const char *directory,
        long long pts);

void display_deinit();


#ifdef __cplusplus
}
#endif


#endif