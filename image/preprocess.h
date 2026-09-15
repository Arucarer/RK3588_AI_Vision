/*
 * @file    preprocess.h
 * @brief   定义RK3588工业视觉图像预处理接口
 *
 * @author  zhangyong
 * @date    2026-09-14
 * @version 1.0
 *
 * @note
 *
 * 本模块主要实现：
 *
 * 1. NV12图像灰度转换
 * 2. ROI区域裁剪
 * 3. Sobel/Canny边缘检测
 * 4. RGB图像生成
 * 5. YOLO/RKNN输入尺寸适配
 *
 *
 * 数据流程：
 *
 * FrameData(NV12)
 *
 *        ↓
 *
 * NV12 Y分量提取
 *
 *        ↓
 *
 * Gray
 *
 *        ↓
 *
 * ROI
 *
 *        ↓
 *
 * Edge
 *
 *        ↓
 *
 * RGB Letterbox
 *
 *        ↓
 *
 * RKNN Input
 *
 */


#ifndef PREPROCESS_H
#define PREPROCESS_H


#include "common.h"

#include <stdint.h>



#ifdef __cplusplus
extern "C"
{
#endif



/*
 * ================================
 *
 * RKNN模型输入配置
 *
 * YOLOv8
 *
 * 输入:
 *
 * RGB888
 *
 * 640×640
 *
 * ================================
 */


#define MODEL_INPUT_WIDTH      640

#define MODEL_INPUT_HEIGHT     640

#define MODEL_INPUT_CHANNELS   3


#define MODEL_INPUT_SIZE \
    (MODEL_INPUT_WIDTH * \
     MODEL_INPUT_HEIGHT * \
     MODEL_INPUT_CHANNELS)





/*
 * ================================
 *
 * 预处理输出结果
 *
 * ================================
 */


typedef struct
{


    /*
     * 灰度图
     *
     * NV12 Y分量
     *
     */
    uint8_t *gray;



    /*
     * 边缘图
     *
     * Sobel/Canny输出
     *
     */
    uint8_t *edge;



    /*
     * RKNN输入RGB图像
     *
     * RGB888
     *
     * 640×640×3
     *
     */
    uint8_t *rgb;



    /*
     * 当前输出尺寸
     *
     * 主要用于灰度/边缘
     *
     */
    int width;

    int height;



    /*
     * RGB输入尺寸
     *
     * 固定:
     *
     * 640×640
     *
     */
    int rgb_width;

    int rgb_height;



    /*
     * 灰度图尺寸
     *
     * 原始ROI尺寸
     *
     */
    int gray_width;

    int gray_height;



}ProcessedImage;





/*
 * 初始化预处理模块
 */
int preprocess_init(void);





/*
 * 执行一次图像预处理
 *
 *
 * 输入:
 *
 * FrameData
 *
 *      V4L2采集NV12
 *
 *
 *
 * 输出:
 *
 * ProcessedImage
 *
 *      gray
 *
 *      edge
 *
 *      rgb
 *
 */
int preprocess_run(
        FrameData *frame,
        ProcessedImage *output);





/*
 * 释放资源
 */
void preprocess_deinit(void);





#ifdef __cplusplus
}
#endif


#endif