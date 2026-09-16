/* RK3588 RKNN通用推理引擎接口 */
/* @author  zhangyong | @date    2026-09-14 | @version 1.0 */

#ifndef RKNN_ENGINE_H
#define RKNN_ENGINE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rknn_api.h"

#include "common.h"

#ifdef __cplusplus
extern "C"
{
#endif

// RKNN Engine结构
/* 推理接口内部各阶段 */
typedef enum
{
    RKNN_PERF_INPUT_SET = 0,
    RKNN_PERF_RUN,
    RKNN_PERF_OUTPUT_GET,
    RKNN_PERF_OUTPUT_COPY,
    RKNN_PERF_OUTPUT_RELEASE,
    RKNN_PERF_COUNT
} RKNNPerfStage;

/*
 * 最近一次推理的阶段耗时，单位 ms。
 * -1 表示本次没有执行到该阶段。
 * 已执行但失败的阶段，也保留耗时。
 */
typedef struct
{
    double ms[RKNN_PERF_COUNT];
} RKNNInferencePerf;

typedef struct
{
    rknn_context ctx;

    int input_num;

    int output_num;

    int input_width;

    int input_height;

    int input_channel;

    /* 输出tensor信息 */
    rknn_tensor_attr output_attrs[10];

    RKNNInferencePerf last_perf;
} RKNNEngine;

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

int rknn_engine_init(RKNNEngine *engine, const char *model_path);

/* result 首次使用前必须清零。 再次推理前，必须释放上一次结果。 */
int rknn_engine_inference(RKNNEngine *engine, unsigned char *input_data, int input_size,
                          RKNNInferenceResult *result);

/* 释放一次推理的全部输出，并清空结果。 */
void rknn_engine_result_release(RKNNInferenceResult *result);

/* 释放模型和 RKNN 上下文。 */
void rknn_engine_release(RKNNEngine *engine);

#ifdef __cplusplus
}
#endif

#endif
