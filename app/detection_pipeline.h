#ifndef DETECTION_PIPELINE_H
#define DETECTION_PIPELINE_H

#include "app_config.h"
#include "rknn_engine.h"
#include "preprocess.h"
#include "detection_result.h"

#include <memory>

struct DetectionTask
{
    FrameData frame = {};
    ProcessedImage image = {};
    DetectionResult result = {};
    RKNNInferencePerf rknn_perf;

    long long started_us = 0;

    double preprocess_ms = -1;
    double inference_ms = -1;
    double postprocess_ms = -1;

    const char *error = nullptr;

    DetectionTask();
    ~DetectionTask();

    DetectionTask(const DetectionTask &) = delete;
    DetectionTask &operator=(const DetectionTask &) = delete;
};

class DetectionPipeline
{
public:
    DetectionPipeline();
    ~DetectionPipeline();

    int start(const AppConfig &config, RKNNEngine *engine);

    /*
     * 0：取得任务
     * 1：等待 200ms 后仍无任务
     * -1：流水线停止
     */
    int pop(std::unique_ptr<DetectionTask> &task);

    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

#endif