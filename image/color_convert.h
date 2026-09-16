/* RK3588工业视觉颜色转换接口 */

#ifndef COLOR_CONVERT_H
#define COLOR_CONVERT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * RGB转灰度
 * 输入:
 * rgb:
 * RGB888数据
 * 输出:
 * gray:
 * 灰度数据
 */
int rgb_to_gray(uint8_t *rgb, uint8_t *gray, int width, int height);

/*
 * NV12 Y分量提取
 * NV12格式:
 * YYYYYYYY
 * UVUVUV
 * 工业缺陷检测主要使用Y
 */
int nv12_to_gray(uint8_t *nv12, uint8_t *gray, int width, int height);

#ifdef __cplusplus
}
#endif

#endif
