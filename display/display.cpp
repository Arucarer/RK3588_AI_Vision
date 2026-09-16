
#include "display.h"

#include "drm_display.h"
#include "rga_display.h"

#include <algorithm>
#include <cmath>

#include <stdlib.h>



/* 与当前 drm_display.cpp 的帧缓冲尺寸一致 */
#define DISPLAY_WIDTH  1920
#define DISPLAY_HEIGHT 1080



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
 * 输入坐标已经转换到显示缓冲区坐标系。
 * 参数必须在图像范围内。
 */
static void draw_rectangle_rgb(
        uint8_t *rgb,
        int left,
        int top,
        int right,
        int bottom)
{
    const int thickness = 3;

    for(int t = 0; t < thickness; ++t)
    {
        const int x1 = left + t;
        const int y1 = top + t;
        const int x2 = right - t;
        const int y2 = bottom - t;

        if(x1 > x2 || y1 > y2)
        {
            break;
        }

        /* 上、下边 */
        for(int x = x1; x <= x2; ++x)
        {
            uint8_t *upper =
                rgb + ((size_t)y1 * DISPLAY_WIDTH + x) * 3;

            uint8_t *lower =
                rgb + ((size_t)y2 * DISPLAY_WIDTH + x) * 3;

            upper[0] = 0;
            upper[1] = 255;
            upper[2] = 0;

            lower[0] = 0;
            lower[1] = 255;
            lower[2] = 0;
        }

        /* 左、右边 */
        for(int y = y1; y <= y2; ++y)
        {
            uint8_t *left_pixel =
                rgb + ((size_t)y * DISPLAY_WIDTH + x1) * 3;

            uint8_t *right_pixel =
                rgb + ((size_t)y * DISPLAY_WIDTH + x2) * 3;

            left_pixel[0] = 0;
            left_pixel[1] = 255;
            left_pixel[2] = 0;

            right_pixel[0] = 0;
            right_pixel[1] = 255;
            right_pixel[2] = 0;
        }
    }
}

int display_show(
        FrameData *frame,
        DetectionResult *result)
{
    if(frame == NULL ||
       result == NULL ||
       frame->vir_addr == NULL ||
       frame->width <= 0 ||
       frame->height <= 0 ||
       !g_display_ready ||
       g_rgb_buffer == NULL)
    {
        return -1;
    }

    if(result->count < 0 ||
       result->count > MAX_DETECTION_NUM)
    {
        LOG_ERR("invalid detection count");
        return -1;
    }

    /*
     * 必须是同一帧、同一原图坐标系的结果，
     * 防止图像与检测框错配。
     */
    if(result->image_width != frame->width ||
       result->image_height != frame->height ||
       result->pts != frame->pts)
    {
        LOG_ERR("detection result does not match frame");
        return -1;
    }

    /* 1. 生成显示用 RGB 图像 */
    if(rga_nv12_to_rgb(
            (uint8_t *)frame->vir_addr,
            frame->width,
            frame->height,
            g_rgb_buffer,
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT) != 0)
    {
        LOG_ERR("rga display conversion failed");
        return -1;
    }

    /*
     * 显示图像当前直接缩放铺满缓冲区。
     * 框使用相同的横向、纵向缩放比例。
     */
    const float scale_x =
        (float)DISPLAY_WIDTH / frame->width;

    const float scale_y =
        (float)DISPLAY_HEIGHT / frame->height;

    for(int i = 0; i < result->count; ++i)
    {
        const DetectionBox *box = &result->boxes[i];

        if(!std::isfinite(box->x1) ||
           !std::isfinite(box->y1) ||
           !std::isfinite(box->x2) ||
           !std::isfinite(box->y2))
        {
            continue;
        }

        /* 先限制原图坐标，再转换成像素索引 */
        const float x1 = std::max(
            0.0f, std::min(box->x1, (float)frame->width));
        const float y1 = std::max(
            0.0f, std::min(box->y1, (float)frame->height));
        const float x2 = std::max(
            0.0f, std::min(box->x2, (float)frame->width));
        const float y2 = std::max(
            0.0f, std::min(box->y2, (float)frame->height));

        if(x2 <= x1 || y2 <= y1)
        {
            continue;
        }

        const int left = std::min(
            DISPLAY_WIDTH - 1,
            (int)std::floor(x1 * scale_x));

        const int top = std::min(
            DISPLAY_HEIGHT - 1,
            (int)std::floor(y1 * scale_y));

        const int right = std::min(
            DISPLAY_WIDTH - 1,
            (int)std::ceil(x2 * scale_x) - 1);

        const int bottom = std::min(
            DISPLAY_HEIGHT - 1,
            (int)std::ceil(y2 * scale_y) - 1);

        if(right < left || bottom < top)
        {
            continue;
        }

        draw_rectangle_rgb(
            g_rgb_buffer,
            left,
            top,
            right,
            bottom
        );
    }

    /* 2. 输出已经叠加检测框的画面 */
    return drm_display_frame(
        g_rgb_buffer,
        DISPLAY_WIDTH,
        DISPLAY_HEIGHT
    );
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