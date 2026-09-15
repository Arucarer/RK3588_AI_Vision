/*
 * @file    letterbox.cpp
 * @brief   RGB图像Letterbox处理
 */


#include "letterbox.h"

#include <string.h>



int letterbox_resize(
        uint8_t *src_rgb,
        int src_width,
        int src_height,
        uint8_t *dst_rgb,
        int dst_width,
        int dst_height,
        LetterboxInfo *info)
{


    if(src_rgb == NULL ||
       dst_rgb == NULL)
    {
        return -1;
    }



    /*
     * 初始化背景色
     *
     * YOLO默认114
     */
    memset(
        dst_rgb,
        114,
        dst_width *
        dst_height *
        3
    );



    float scale_w =
        (float)dst_width /
        src_width;


    float scale_h =
        (float)dst_height /
        src_height;



    float scale =
        scale_w < scale_h ?
        scale_w :
        scale_h;



    int resize_w =
        src_width * scale;


    int resize_h =
        src_height * scale;



    int pad_x =
        (dst_width-resize_w)/2;


    int pad_y =
        (dst_height-resize_h)/2;



    /*
     * 这里先使用CPU简单缩放
     *
     * 后续可以替换RGA
     */

    for(int y=0;
        y<resize_h;
        y++)
    {


        for(int x=0;
            x<resize_w;
            x++)
        {


            int src_x =
                x / scale;


            int src_y =
                y / scale;



            uint8_t *src =
                src_rgb +
                (src_y*src_width+src_x)*3;



            uint8_t *dst =
                dst_rgb +
                ((y+pad_y)*dst_width+
                 (x+pad_x))*3;



            dst[0]=src[0];
            dst[1]=src[1];
            dst[2]=src[2];

        }

    }



    if(info)
    {

        info->scale =
            scale;


        info->pad_x =
            pad_x;


        info->pad_y =
            pad_y;

    }



    return 0;
}