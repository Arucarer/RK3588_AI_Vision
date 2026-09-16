#ifndef POSTPROCESS_H
#define POSTPROCESS_H

#include "detection_result.h"
#include "rknn_engine.h"
#include "preprocess.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 当前只支持：
 * YOLOv8 六类检测模型，输出 [1, 10, 8400]。
 *
 * inference 的数据必须由 want_float=1 获取。
 *
 * 返回值：
 *  0：处理成功，允许 result->count == 0。
 * -1：参数或输出格式错误。
 *
 * result 中的框坐标位于摄像头原图坐标系。
 */
int postprocess_result(
        const RKNNEngine *engine,
        const RKNNInferenceResult *inference,
        const ProcessedImage *image,
        long long pts,
        float confidence_threshold,
        float nms_threshold,
        DetectionResult *result);

#ifdef __cplusplus
}
#endif

#endif