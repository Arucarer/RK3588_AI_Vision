/* 定义RK3588 AI视觉检测项目中的V4L2图像采集接口 */
/* @author  zhangyong | @date    2026-09-13 | @version 1.0 */

#ifndef V4L2_CAPTURE_H
#define V4L2_CAPTURE_H

#include "common.h"

#include <linux/videodev2.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif



typedef struct
{
    void *start;   // MMAP虚拟地址
    size_t length; // Buffer大小
    int dma_fd;    // DMA-BUF文件描述符
} V4l2Buffer;

int v4l2_init(const char *device, int width, int height, int fps);
int v4l2_capture_frame(FrameData *frame);
int v4l2_release_frame(int index);
void v4l2_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
