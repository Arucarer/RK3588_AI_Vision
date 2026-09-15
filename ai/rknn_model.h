/*
 * @file    rknn_model.h
 * @brief   定义RK3588 AI视觉检测项目中的RKNN模型管理接口
 * @author  zhangyong
 * @date    2026-09-13
 * @version 1.0
 *
 * @note
 * 该文件主要包含：
 * 1. RKNN模型数据结构
 * 2. RKNN模型文件加载接口
 * 3. RKNN模型资源释放接口
 *
 * 本模块仅负责将.rknn模型文件读取到内存，
 * 不负责RKNN Runtime初始化与NPU推理。
 */

#ifndef RKNN_MODEL_H
#define RKNN_MODEL_H

#include <stddef.h>


typedef struct
{
    void *data;             // 模型数据内存地址

    size_t size;            // 模型文件大小

    char path[256];         // 模型路径

}RknnModel;


int rknn_model_load(const char *model_path,
                    RknnModel *model);

void rknn_model_unload(RknnModel *model);


#endif