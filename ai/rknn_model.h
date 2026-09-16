/* 定义RK3588 AI视觉检测项目中的RKNN模型管理接口 */
/* @author  zhangyong | @date    2026-09-13 | @version 1.0 */

#ifndef RKNN_MODEL_H
#define RKNN_MODEL_H

#include <stddef.h>

typedef struct
{
    void *data; // 模型数据内存地址

    size_t size; // 模型文件大小

    char path[256]; // 模型路径
} RknnModel;

int rknn_model_load(const char *model_path, RknnModel *model);

void rknn_model_unload(RknnModel *model);

#endif
