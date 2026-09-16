/*
 * @file    rga_display.cpp
 * @brief   RK3588 RGA图像转换
 *
 * NV12
 *   |
 *   ↓
 * RGB888
 *
 */


#include "rga_display.h"


#include <stdio.h>
#include <string.h>


#include <im2d.h>
#include <rga.h>




int rga_display_init()
{


    printf(
        "RGA display init success\n"
    );


    return 0;

}





int rga_nv12_to_rgb(
        uint8_t *src_nv12,
        int src_width,
        int src_height,
        uint8_t *dst_rgb,
        int dst_width,
        int dst_height
)
{


    if(src_nv12 == NULL ||
       dst_rgb == NULL)
    {

        return -1;

    }



    /*
     * 输入:
     *
     * NV12
     *
     */
    rga_buffer_t src =
        wrapbuffer_virtualaddr(
            src_nv12,
            src_width,
            src_height,
            RK_FORMAT_YCbCr_420_SP
        );

    rga_buffer_t dst =
        wrapbuffer_virtualaddr(
            dst_rgb,
            dst_width,
            dst_height,
            RK_FORMAT_RGB_888
        );


    IM_STATUS ret;



    ret =
        imcvtcolor(
            src,
            dst,
            src.format,
            dst.format
        );



    if(ret != IM_STATUS_SUCCESS)
    {

        printf(
            "RGA NV12 to RGB failed:%d\n",
            ret
        );


        return -1;

    }
    return 0;
}


void rga_display_deinit()
{


}