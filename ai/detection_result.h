#ifndef AI_DETECTION_RESULT_H
#define AI_DETECTION_RESULT_H

#define DETECT_CLASS_NUM 6
#define MAX_DETECTION_NUM 100

/* 类别顺序与 defect.yaml 一致。 */
typedef enum
{
    DEFECT_CRAZING = 0,
    DEFECT_INCLUSION,
    DEFECT_PATCHES,
    DEFECT_PITTED_SURFACE,
    DEFECT_ROLLED_IN_SCALE,
    DEFECT_SCRATCHES
} DefectClass;

/*
 * 单个缺陷框。
 * 坐标位于摄像头原图坐标系。
 * 使用浮点数保存，绘制时再转换为整数。
 */
typedef struct
{
    int class_id;
    float confidence;

    float x1;
    float y1;
    float x2;
    float y2;
} DetectionBox;

/*
 * 一帧的检测结果。
 * count == 0：没有通过筛选的检测框。
 * 推理失败应由函数返回值表示，不用 count 表示。
 */
typedef struct
{
    int count;
    DetectionBox boxes[MAX_DETECTION_NUM];

    int image_width;
    int image_height;

    /* 对应输入帧的时间戳 */
    long long pts;

    /* 推理接口调用耗时 */
    float inference_time_ms;
} DetectionResult;

#endif
