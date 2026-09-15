/*
 * @file    display.cpp
 * @brief   AI检测显示管理模块
 *
 * Camera
 *    |
 * FrameData(NV12)
 *    |
 * RGA
 *    |
 * RGB888
 *    |
 * DRM
 *    |
 * HDMI
 */


#include "display.h"

#include "drm_display.h"
#include "rga_display.h"


#include <stdlib.h>



#define DISPLAY_WIDTH  2560
#define DISPLAY_HEIGHT 1440



static int g_display_ready = 0;


static uint8_t *g_rgb_buffer = NULL;



/*
 * @brief
 * 初始化显示模块
 */
int display_init()
{


    if(rga_display_init()!=0)
    {

        LOG_ERR(
            "rga display init failed"
        );

        return -1;

    }



    if(drm_display_init()!=0)
    {

        LOG_ERR(
            "drm display init failed"
        );

        return -1;

    }



    g_rgb_buffer =
        (uint8_t *)malloc(
            DISPLAY_WIDTH *
            DISPLAY_HEIGHT *
            3
        );



    if(g_rgb_buffer==NULL)
    {

        LOG_ERR(
            "malloc rgb buffer failed"
        );


        drm_display_deinit();

        return -1;

    }



    g_display_ready = 1;



    LOG_INFO(
        "display init success"
    );



    return 0;

}





/*
 * @brief
 * 显示一帧AI结果
 *
 * 输入:
 *
 * FrameData:
 * 摄像头NV12
 *
 * DetectionResult:
 * AI结果
 */
int display_show(
        FrameData *frame,
        DetectionResult *result
)
{


    if(frame==NULL ||
       result==NULL)
    {

        return -1;

    }



    if(!g_display_ready)
    {

        return -1;

    }



    LOG_INFO(
        "display class=%d confidence=%.3f",
        result->class_id,
        result->confidence
    );




    /*
     * NV12
     *
     * ↓
     *
     * RGB888
     */
    if(
        rga_nv12_to_rgb(
            (uint8_t *)frame->vir_addr,
            frame->width,
            frame->height,
            g_rgb_buffer,
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT
        ) != 0
    )
    {

        LOG_ERR(
            "rga convert failed"
        );

        return -1;

    }




    /*
     * RGB888
     *
     * ↓
     *
     * DRM HDMI
     */
    if(
        drm_display_frame(
            g_rgb_buffer,
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT
        ) != 0
    )
    {

        LOG_ERR(
            "drm display frame failed"
        );


        return -1;

    }



    return 0;

}






/*
 * @brief
 * 释放显示资源
 */
void display_deinit()
{


    if(g_rgb_buffer)
    {

        free(
            g_rgb_buffer
        );


        g_rgb_buffer = NULL;

    }



    drm_display_deinit();


    rga_display_deinit();



    g_display_ready = 0;


}