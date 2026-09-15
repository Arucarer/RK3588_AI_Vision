/*
 * @file    roi.h
 * @brief   工业视觉ROI区域处理接口
 */


#ifndef ROI_H
#define ROI_H


#include <stdint.h>



#ifdef __cplusplus
extern "C"
{
#endif



typedef struct
{

    int x;

    int y;

    int width;

    int height;


}ROIRegion;




/*
 * ROI裁剪
 *
 * 输入:
 *
 * src:
 *      原图
 *
 * 输出:
 *
 * dst:
 *      ROI图像
 *
 */
int roi_crop_gray(
        uint8_t *src,
        uint8_t *dst,
        int src_width,
        int src_height,
        ROIRegion *roi);



#ifdef __cplusplus
}
#endif



#endif