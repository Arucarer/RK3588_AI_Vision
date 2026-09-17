#ifndef ROI_ANALYSIS_H
#define ROI_ANALYSIS_H

#include "preprocess.h"

struct ROIAnalysis
{
    int width;
    int height;

    double mean_gray;
    double sobel_mean;
    double canny_density;
};

/*
 * 分析模型输入中的有效 RGB 区域，排除 Letterbox 补边。
 * canny_density 为有效内部像素中边缘像素所占比例，范围 0..1。
 */
int roi_analyze(
    const ProcessedImage *image,
    int low_threshold,
    int high_threshold,
    ROIAnalysis *analysis);

#endif