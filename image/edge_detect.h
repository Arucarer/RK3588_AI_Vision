/* RK3588工业视觉边缘检测算法接口 */
/* @author  zhangyong | @date    2026-09-14 | @version 1.0 */

#ifndef EDGE_DETECT_H
#define EDGE_DETECT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Sobel边缘检测
 * 参数：
 * gray:
 * 输入灰度图
 * edge:
 * 输出边缘图
 * width:
 * 图像宽度
 * height:
 * 图像高度
 * threshold:
 * 梯度阈值
 */
int sobel_detect(uint8_t *gray, uint8_t *edge, int width, int height, int threshold);

/*
 * Canny边缘检测
 * 参数：
 * gray:
 * 输入灰度图
 * edge:
 * 输出边缘图
 */
int canny_detect(uint8_t *gray, uint8_t *edge, int width, int height, int low_threshold,
                 int high_threshold);

#ifdef __cplusplus
}
#endif

#endif
