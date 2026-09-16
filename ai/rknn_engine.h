/*
 * @file    rknn_engine.h
 * @brief   RK3588 RKNN通用推理引擎接口
 *
 * @author  zhangyong
 * @date    2026-09-14
 * @version 1.0
 *
 * @note
 *
 * 本模块负责：
 *
 * 1. 初始化RKNN Runtime
 * 2. 加载RKNN模型
 * 3. 设置输入数据
 * 4. 执行NPU推理
 * 5. 获取模型输出
 *
 *
 * 数据流程：
 *
 * Image
 *   |
 *   ↓
 * RKNN Engine
 *   |
 *   ↓
 * float output
 *
 *
 * 本模块不负责：
 *
 * 1. 图像预处理
 * 2. 模型后处理
 * 3. 缺陷分类逻辑
 *
 */


#ifndef RKNN_ENGINE_H
#define RKNN_ENGINE_H


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rknn_api.h"

#include "common.h"


#ifdef __cplusplus
extern "C" {
#endif



// ==================== RKNN Engine结构 ====================


typedef struct
{

    /*
     * RKNN Context
     *
     * 每一个模型对应一个context
     */
    rknn_context ctx;



    /*
     * 模型输入数量
     */
    int input_num;



    /*
     * 模型输出数量
     */
    int output_num;



    /*
     * 输入尺寸信息
     */
    int input_width;

    int input_height;

    int input_channel;



    /*
     * 输出tensor信息
     */
    rknn_tensor_attr output_attrs[10];


}RKNNEngine;

/* 与当前 engine->output_attrs 的容量保持一致 */
#define RKNN_ENGINE_MAX_OUTPUTS 10

/* 单个输出，数据为 want_float=1 返回的浮点值 */
typedef struct
{
    float *data;
    int element_count;
} RKNNOutputTensor;

/* 一次推理的全部输出 */
typedef struct
{
    int count;
    RKNNOutputTensor tensors[RKNN_ENGINE_MAX_OUTPUTS];
} RKNNInferenceResult;


int rknn_engine_init(
        RKNNEngine *engine,
        const char *model_path);


/*
 * result 首次使用前必须清零。
 * 再次推理前，必须释放上一次结果。
 */
int rknn_engine_inference(
        RKNNEngine *engine,
        unsigned char *input_data,
        int input_size,
        RKNNInferenceResult *result);



/*
 * 释放一次推理的全部输出，并清空结果。
 */
void rknn_engine_result_release(
        RKNNInferenceResult *result);

/*
 * 释放模型和 RKNN 上下文。
 */
void rknn_engine_release(
        RKNNEngine *engine);


#ifdef __cplusplus
}
#endif

#endif