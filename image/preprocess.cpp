/*
 * @file    preprocess.cpp
 * @brief   RK3588工业视觉图像预处理实现
 *
 * 流程：
 *
 * FrameData(NV12)
 *
 *      ↓
 *
 * NV12 Y分量提取
 *
 *      ↓
 *
 * 灰度图
 *
 *      ↓
 *
 * ROI裁剪
 *
 *      ↓
 *
 * Sobel/Canny
 *
 *      ↓
 *
 * 输出边缘特征
 *
 */


#include "preprocess.h"

#include "color_convert.h"
#include "resize.h"
#include "letterbox.h"
#include "roi.h"
#include "edge_detect.h"


#include <stdlib.h>
#include <string.h>



/*
 * 当前默认检测区域
 *
 * 后续根据实际工业工件调整
 */
static ROIRegion g_default_roi =
{
    .x = 0,
    .y = 0,
    .width = 1280,
    .height = 720
};



int preprocess_init()
{

    return 0;

}





int preprocess_run(
        FrameData *frame,
        ProcessedImage *output)
{

    if(frame == NULL ||
       output == NULL)
    {
        return -1;
    }



    memset(
        output,
        0,
        sizeof(ProcessedImage)
    );



    /*
     * 原始图像尺寸
     */
    int width =
        frame->width;


    int height =
        frame->height;



    /*
     * ===============================
     *
     * 1. NV12转换灰度图
     *
     * ===============================
     */


    uint8_t *gray_full =
        (uint8_t *)malloc(
            width * height
        );


    if(gray_full == NULL)
    {
        return -1;
    }



    nv12_to_gray(
        (uint8_t *)frame->vir_addr,
        gray_full,
        width,
        height
    );



    /*
     * ===============================
     *
     * 2. ROI区域裁剪
     *
     * ===============================
     */


    ROIRegion roi =
        g_default_roi;



    /*
     * 防止摄像头分辨率变化导致越界
     */

    if(roi.x + roi.width > width)
    {
        roi.width =
            width - roi.x;
    }


    if(roi.y + roi.height > height)
    {
        roi.height =
            height - roi.y;
    }



    uint8_t *gray_roi =
        (uint8_t *)malloc(
            roi.width *
            roi.height
        );



    if(gray_roi == NULL)
    {
        free(gray_full);

        return -1;
    }



    roi_crop_gray(
        gray_full,
        gray_roi,
        width,
        height,
        &roi
    );



    free(gray_full);



    /*
     * ===============================
     *
     * 3. 边缘检测
     *
     * ===============================
     */


    uint8_t *edge =
        (uint8_t *)malloc(
            roi.width *
            roi.height
        );



    if(edge == NULL)
    {
        free(gray_roi);

        return -1;
    }



    /*
     * 默认使用Sobel
     *
     * 后续可以切换Canny
     */

    sobel_detect(
        gray_roi,
        edge,
        roi.width,
        roi.height,
        80
    );



    /*
     * ===============================
     *
     * 4. 输出结果
     *
     * ===============================
     */


    output->gray =
        gray_roi;


    output->edge =
        edge;



    /*
    * ===============================
    *
    * 4. AI输入生成
    *
    * ROI
    *
    * ↓
    *
    * RGB
    *
    * ↓
    *
    * Letterbox
    *
    * ↓
    *
    * 640×640
    *
    * ===============================
    */

    uint8_t *rgb_resize =
        (uint8_t *)malloc(
            640 *
            360 *
            3
        );


    if(rgb_resize == NULL)
    {
        free(gray_roi);
        free(edge);

        return -1;
    }


    /*
    * NV12转RGB
    *
    * 注意：
    *
    * 这里需要使用你已有的
    * color_convert.cpp
    *
    */
    if(image_resize_nv12_to_rgb(
            frame,
            rgb_resize,
            640,
            360)!=0)
    {

        free(rgb_resize);
        free(gray_roi);
        free(edge);

        return -1;

    }



    uint8_t *rgb_letterbox =
        (uint8_t *)malloc(
            640 *
            640 *
            3
        );


    if(rgb_letterbox == NULL)
    {
        free(rgb_resize);
        free(gray_roi);
        free(edge);

        return -1;
    }



    /*
    * resize + padding
    *
    * 输出:
    *
    * 640×640 RGB
    */
    LetterboxInfo info;


    if(letterbox_resize(
            rgb_resize,
            640,
            360,
            rgb_letterbox,
            640,
            640,
            &info)!=0)
    {

        free(rgb_resize);
        free(rgb_letterbox);
        free(gray_roi);
        free(edge);

        return -1;

    }



    free(rgb_resize);



    output->rgb =
        rgb_letterbox;


    output->rgb_width =
        640;


    output->rgb_height =
        640;


    output->width =
        640;


    output->height =
        640;

    return 0;
}





void preprocess_deinit()
{

}