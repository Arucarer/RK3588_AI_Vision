/* AI检测结果绘制接口 */

#ifndef DRAW_RESULT_H
#define DRAW_RESULT_H

#include "detection_result.h"

#include <opencv2/opencv.hpp>

#ifdef __cplusplus
extern "C"
{
#endif

int draw_detection_result(cv::Mat &image, DetectionResult *result);

#ifdef __cplusplus
}
#endif

#endif
