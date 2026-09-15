/*
 * @file    resize.h
 * @brief   定义RK3588 AI视觉检测项目中的RGA图像缩放接口
 * @author  zhangyong
 * @date    2026-09-13
 * @version 1.0
 *
 * @note
 * 该文件主要包含：
 * 1. RGA图像缩放接口
 * 2. NV12到RGB888颜色转换接口
 * 3. DMA-BUF图像处理接口
 *
 * 使用RK3588 RGA硬件加速完成图像缩放
 * 和颜色格式转换。
 */

#ifndef RESIZE_H
#define RESIZE_H

#include "common.h"

#include <stdint.h>


int image_resize_nv12_to_rgb(
    const FrameData *src,
    uint8_t *dst_rgb,
    int dst_width,
    int dst_height);


#endif