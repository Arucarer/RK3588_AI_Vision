/* 定义RK3588 AI视觉检测项目中的通用数据结构 */
/* @author  LYK | @date    2026-09-11 | @version 1.0 */

#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

// AI检测框
typedef struct
{
    int class_id; //目标类别

    float score; //置信度

    int x1; //检测框坐标
    int y1;
    int x2;
    int y2;
} DetectionBox;

// AI检测结果
#define MAX_DETECT_NUM 32

typedef struct
{
    int count;                          //检测数量
    DetectionBox boxes[MAX_DETECT_NUM]; //检测结果数组
    float inference_time;               //推理耗时(ms)
} DetectionResult;

// AI模型信息
typedef struct
{
    char model_path[256]; //模型路径

    int input_width; //输入宽高
    int input_height;

    int class_num; //类别数量
} ModelInfo;

#endif
