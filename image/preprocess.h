#ifndef PREPROCESS_H
#define PREPROCESS_H

#include "common.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define MODEL_INPUT_WIDTH 640
#define MODEL_INPUT_HEIGHT 640
#define MODEL_INPUT_CHANNELS 3

#define MODEL_INPUT_SIZE (MODEL_INPUT_WIDTH * MODEL_INPUT_HEIGHT * MODEL_INPUT_CHANNELS)

typedef struct
{
    /* 模型输入：连续存储的 UINT8 RGB */
    uint8_t *rgb;
    int rgb_width;
    int rgb_height;

    /* 摄像头原图尺寸 */
    int original_width;
    int original_height;

    /* 实际缩放后的有效图像尺寸 */
    int resized_width;
    int resized_height;

    /* 使用实际整数缩放尺寸计算两个方向的比例， 避免尺寸取整造成坐标误差。 */
    float scale_x;
    float scale_y;

    /* 左侧、顶部补边像素数 */
    int pad_left;
    int pad_top;

    /* 暂保留，兼容当前 main.cpp 的释放代码。 本版本不生成灰度图和边缘图，始终为 NULL。 */
    uint8_t *gray;
    uint8_t *edge;
} ProcessedImage;

int preprocess_init(void);

/*
 * output 首次使用前必须清零。
 * 再次使用前必须释放上次结果。
 * 输入要求：紧密排列的 NV12，当前不支持额外行跨度。
 */
int preprocess_run(FrameData *frame, ProcessedImage *output);

/*
 * 对连续存储的 UINT8 RGB 图片进行预处理。
 * rgb_size 是输入缓冲区字节数。
 * output 首次使用前必须清零；
 * 再次使用前必须 preprocess_release()。
 * 输入 RGB 由调用者管理，本函数不释放它。
 */
int preprocess_rgb(const uint8_t *rgb, int width, int height, size_t rgb_size,
                   ProcessedImage *output);

/* 释放图像并清空结构体 */
void preprocess_release(ProcessedImage *output);

void preprocess_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
