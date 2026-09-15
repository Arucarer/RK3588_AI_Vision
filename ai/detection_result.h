/*
 * @file    detection_result.h
 * @brief   RK3588 AI视觉检测项目检测结果定义
 *
 * @author  zhangyong
 * @date    2026-09-14
 * @version 1.0
 *
 * @note
 *
 * 保存RKNN推理后的工业缺陷检测结果：
 *
 * RKNN Output
 *      |
 *      ↓
 * Postprocess
 *      |
 *      ↓
 * DetectionResult
 *
 */


#ifndef DETECTION_RESULT_H
#define DETECTION_RESULT_H



// ==================== 缺陷类别数量 ====================

#define DETECT_CLASS_NUM 6



// ==================== 缺陷类别定义 ====================

typedef enum
{

    DEFECT_CRAZING = 0,          // 龟裂

    DEFECT_INCLUSION,            // 夹杂

    DEFECT_PATCHES,              // 补丁

    DEFECT_PITTED_SURFACE,       // 点蚀表面

    DEFECT_ROLLED_IN_SCALE,      // 氧化铁皮

    DEFECT_SCRATCHES             // 划痕


}DefectClass;





// ==================== 单帧检测结果 ====================

typedef struct
{

    /*
     * 最大概率对应类别
     */
    int class_id;



    /*
     * 当前类别置信度
     */
    float confidence;



    /*
     * 是否检测到有效缺陷
     *
     * 0:
     *      无效
     *
     * 1:
     *      有效缺陷
     */
    int valid;



    /*
     * NPU推理耗时(ms)
     */
    float inference_time_ms;



}DetectionResult;



#endif