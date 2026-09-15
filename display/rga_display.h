/*
 * @file    rga_display.h
 * @brief   RGA显示转换模块
 *
 * @author  zhangyong
 * @date    2026-09-15
 *
 */


#ifndef RGA_DISPLAY_H
#define RGA_DISPLAY_H


#include <stdint.h>


/*
 * @brief
 * 初始化RGA显示转换
 *
 */
int rga_display_init();



/*
 * @brief
 * NV12转换RGB888
 *
 * 输入:
 *
 * NV12:
 * 3840x2160
 *
 * 输出:
 *
 * RGB888:
 * 2560x1440
 *
 */
int rga_nv12_to_rgb(
        uint8_t *src_nv12,
        int src_width,
        int src_height,
        uint8_t *dst_rgb,
        int dst_width,
        int dst_height
);



/*
 * @brief
 * 释放RGA资源
 *
 */
void rga_display_deinit();



#endif