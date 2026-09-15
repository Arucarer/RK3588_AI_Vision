/*
 * @file    detection_result.h
 * @brief   定义RK3588 AI视觉检测项目中的检测结果数据结构
 *
 * @author  zhangyong
 * @date    2026-09-14
 * @version 1.0
 *
 * @note
 * 该文件主要负责：
 *
 * 1. 定义目标检测框结构
 * 2. 定义一次推理结果结构
 * 3. 为后续绘制框、显示、网络传输提供统一接口
 *
 * 数据流程：
 *
 * RKNN推理
 *      |
 *      ↓
 * YOLOv8后处理
 *      |
 *      ↓
 * DetectionResult
 *      |
 *      ↓
 * Draw / Display / Save
 *
 */

#ifndef DETECTION_RESULT_H
#define DETECTION_RESULT_H


#ifdef __cplusplus
extern "C" {
#endif


#define MAX_DETECTION_NUM 100


/*
 * @brief 单个目标检测结果
 */
typedef struct
{

    /*
     * 类别编号
     *
     * 对应YOLO类别索引
     */
    int class_id;



    /*
     * 检测置信度
     *
     * 范围:
     * 0.0 ~ 1.0
     */
    float confidence;



    /*
     * 检测框坐标
     *
     * 已经映射回原始图像坐标
     */
    int x1;
    int y1;

    int x2;
    int y2;


}DetectionBox;



/*
 * @brief 一帧图像的检测结果
 */
typedef struct
{

    /*
     * 当前帧检测到目标数量
     */
    int count;



    /*
     * 检测目标数组
     */
    DetectionBox boxes[MAX_DETECTION_NUM];



    /*
     * 原始图像尺寸
     *
     * 用于后续显示和坐标转换
     */
    int image_width;

    int image_height;



    /*
     * 当前帧时间戳
     *
     * 与FrameData中的pts对应
     */
    long long pts;


}DetectionResult;



/*
 * 初始化检测结果
 */
void detection_result_init(
        DetectionResult *result);



/*
 * 清空检测结果
 */
void detection_result_clear(
        DetectionResult *result);



/*
 * 添加一个检测框
 */
int detection_result_add(
        DetectionResult *result,
        int class_id,
        float confidence,
        int x1,
        int y1,
        int x2,
        int y2);



#ifdef __cplusplus
}
#endif


#endif