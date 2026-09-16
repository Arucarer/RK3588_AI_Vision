/* 定义RK3588 AI视觉检测项目中的RGA图像缩放接口 */
/* @author  zhangyong | @date    2026-09-13 | @version 1.0 */

#ifndef RESIZE_H
#define RESIZE_H

#include "common.h"

#include <stdint.h>

int image_resize_nv12_to_rgb(const FrameData *src, uint8_t *dst_rgb, int dst_width, int dst_height);

#endif
