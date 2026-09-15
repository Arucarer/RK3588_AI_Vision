/*
 * @file    camera_buffer.h
 * @brief   定义RK3588 AI视觉检测项目中的摄像头帧缓存接口
 * @author  zhangyong
 * @date    2026-09-13
 * @version 1.0
 *
 * @note
 * 该文件主要包含：
 * 1. Camera Buffer初始化接口
 * 2. 图像帧写入接口
 * 3. 图像帧读取接口
 * 4. Buffer节点释放接口
 * 5. Camera Buffer资源释放接口
 *
 * Camera Buffer仅保存FrameData描述信息，
 * 不复制4K NV12原始图像数据。
 */

#ifndef CAMERA_BUFFER_H
#define CAMERA_BUFFER_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif


int camera_buffer_init(void);

int camera_buffer_push(FrameData *frame);

int camera_buffer_pop(FrameData *frame);

int camera_buffer_release(FrameData *frame);

void camera_buffer_deinit(void);


#ifdef __cplusplus
}
#endif

#endif