/*
 * @file    v4l2_capture.h
 * @brief   定义RK3588 AI视觉检测项目中的V4L2图像采集接口
 * @author  zhangyong
 * @date    2026-09-13
 * @version 1.0
 *
 * @note
 * 该文件主要包含：
 * 1. V4L2摄像头设备配置
 * 2. V4L2 Buffer数据结构
 * 3. V4L2初始化接口
 * 4. 图像帧获取接口
 * 5. Buffer归还接口
 * 6. V4L2资源释放接口
 *
 * 本模块负责通过V4L2采集IMX415摄像头输出的
 * 3840×2160 NV12图像，并导出DMA-BUF供RGA使用。
 */

#ifndef V4L2_CAPTURE_H
#define V4L2_CAPTURE_H

#include "common.h"

#include <linux/videodev2.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


#define V4L2_DEV "/dev/video22"




typedef struct
{
    void *start;        // MMAP虚拟地址
    size_t length;      // Buffer大小
    int dma_fd;         // DMA-BUF文件描述符

}V4l2Buffer;


int v4l2_init(void);
int v4l2_capture_frame(FrameData *frame);
int v4l2_release_frame(int index);
void v4l2_deinit(void);


#ifdef __cplusplus
}
#endif

#endif