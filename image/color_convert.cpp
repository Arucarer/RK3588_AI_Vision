/*
 * @file    color_convert.cpp
 * @brief   图像颜色转换实现
 */


#include "color_convert.h"

#include <string.h>



/*
 * RGB888 -> Gray
 *
 * 灰度公式:
 *
 * Y =
 * 0.299R
 * +0.587G
 * +0.114B
 *
 */
int rgb_to_gray(
        uint8_t *rgb,
        uint8_t *gray,
        int width,
        int height)
{

    if(rgb == NULL ||
       gray == NULL)
    {
        return -1;
    }



    int pixel_num =
        width *
        height;



    for(int i=0;
        i<pixel_num;
        i++)
    {

        uint8_t r =
            rgb[i*3+0];


        uint8_t g =
            rgb[i*3+1];


        uint8_t b =
            rgb[i*3+2];



        gray[i] =
            (uint8_t)
            (
                0.299f*r +
                0.587f*g +
                0.114f*b
            );

    }


    return 0;
}






/*
 * NV12 -> Gray
 *
 * NV12结构:
 *
 * Y plane:
 *
 * width*height
 *
 *
 * UV plane:
 *
 * width*height/2
 *
 *
 * 灰度只需要Y
 */
int nv12_to_gray(
        uint8_t *nv12,
        uint8_t *gray,
        int width,
        int height)
{

    if(nv12 == NULL ||
       gray == NULL)
    {
        return -1;
    }



    memcpy(
        gray,
        nv12,
        width*height
    );


    return 0;
}