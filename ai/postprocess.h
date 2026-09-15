/*
 * @file    postprocess.h
 * @brief   RKNN模型输出后处理接口
 *
 * @author  zhangyong
 * @date    2026-09-14
 * @version 1.0
 *
 * @note
 *
 * 本模块负责：
 *
 * 1. 解析RKNN输出tensor
 * 2. 计算最大概率类别
 * 3. 生成缺陷检测结果
 *
 *
 * 数据流程：
 *
 * RKNN Output
 *
 *      |
 *      ↓
 *
 * postprocess
 *
 *      |
 *      ↓
 *
 * DefectResult
 *
 */


#ifndef POSTPROCESS_H
#define POSTPROCESS_H


#include "detection_result.h"



#ifdef __cplusplus
extern "C" {
#endif



/*
 * RKNN分类结果后处理
 *
 * 参数：
 *
 * output:
 *      RKNN输出float数组
 *
 * output_size:
 *      输出数量
 *
 * class_num:
 *      类别数量
 *
 * result:
 *      输出检测结果
 *
 */
int postprocess_result(
        float *output,
        int output_size,
        int class_num,
        DetectionResult *result);




#ifdef __cplusplus
}
#endif



#endif