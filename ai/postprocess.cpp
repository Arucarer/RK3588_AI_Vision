#include "postprocess.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

/* 当前固定模型的输出布局 */
static const int CANDIDATE_COUNT = 8400;
static const int OUTPUT_CHANNELS = 4 + DETECT_CLASS_NUM;

/*
 * 后处理内部候选框。
 * 坐标仍在 640×640 模型输入坐标系中。
 */
struct Candidate
{
    int class_id;
    float confidence;

    float x1;
    float y1;
    float x2;
    float y2;
};

/* 计算交并比：重叠面积 / 总覆盖面积 */
static float box_iou(
        const Candidate &a,
        const Candidate &b)
{
    const float left = std::max(a.x1, b.x1);
    const float top = std::max(a.y1, b.y1);
    const float right = std::min(a.x2, b.x2);
    const float bottom = std::min(a.y2, b.y2);

    const float width = std::max(0.0f, right - left);
    const float height = std::max(0.0f, bottom - top);

    const float intersection = width * height;

    const float area_a =
        (a.x2 - a.x1) * (a.y2 - a.y1);

    const float area_b =
        (b.x2 - b.x1) * (b.y2 - b.y1);

    const float union_area =
        area_a + area_b - intersection;

    if(union_area <= 0.0f)
    {
        return 0.0f;
    }

    return intersection / union_area;
}

static float clamp_coordinate(float value, float upper)
{
    return std::max(0.0f, std::min(value, upper));
}

int postprocess_result(
        const RKNNEngine *engine,
        const RKNNInferenceResult *inference,
        const ProcessedImage *image,
        long long pts,
        float confidence_threshold,
        float nms_threshold,
        DetectionResult *result)
{
    if(result == NULL)
    {
        return -1;
    }

    /* 避免失败后误用上一帧结果 */
    std::memset(result, 0, sizeof(*result));

    if(engine == NULL ||
       inference == NULL ||
       image == NULL)
    {
        LOG_ERR("invalid postprocess parameter");
        return -1;
    }

    if(!std::isfinite(confidence_threshold) ||
       !std::isfinite(nms_threshold) ||
       confidence_threshold <= 0.0f ||
       confidence_threshold > 1.0f ||
       nms_threshold <= 0.0f ||
       nms_threshold > 1.0f)
    {
        LOG_ERR("invalid postprocess thresholds");
        return -1;
    }

    /* 1. 检查预处理信息 */
    if(image->original_width <= 0 ||
       image->original_height <= 0 ||
       image->rgb_width != MODEL_INPUT_WIDTH ||
       image->rgb_height != MODEL_INPUT_HEIGHT ||
       image->rgb_width != engine->input_width ||
       image->rgb_height != engine->input_height ||
       !std::isfinite(image->scale_x) ||
       !std::isfinite(image->scale_y) ||
       image->scale_x <= 0.0f ||
       image->scale_y <= 0.0f ||
       image->resized_width <= 0 ||
       image->resized_height <= 0 ||
       image->resized_width > image->rgb_width ||
       image->resized_height > image->rgb_height ||
       image->pad_left < 0 ||
       image->pad_top < 0 ||
       image->pad_left >
           image->rgb_width - image->resized_width ||
       image->pad_top >
           image->rgb_height - image->resized_height)
    {
        LOG_ERR("invalid preprocessing geometry");
        return -1;
    }

    /* 2. 只接受当前模型的单输出布局 */
    if(engine->output_num != 1 ||
       inference->count != 1)
    {
        LOG_ERR("expected one YOLOv8 output");
        return -1;
    }

    const rknn_tensor_attr &attr =
        engine->output_attrs[0];

    if(attr.n_dims != 3 ||
       attr.dims[0] != 1 ||
       attr.dims[1] != OUTPUT_CHANNELS ||
       attr.dims[2] != CANDIDATE_COUNT)
    {
        LOG_ERR("expected output shape [1,10,8400]");
        return -1;
    }

    const RKNNOutputTensor &tensor =
        inference->tensors[0];

    if(tensor.data == NULL ||
       tensor.element_count !=
           OUTPUT_CHANNELS * CANDIDATE_COUNT)
    {
        LOG_ERR("invalid YOLOv8 float output buffer");
        return -1;
    }

    result->image_width = image->original_width;
    result->image_height = image->original_height;
    result->pts = pts;

    const float *data = tensor.data;

    std::vector<Candidate> candidates;
    candidates.reserve(CANDIDATE_COUNT);

    /*
     * 3. 解析候选框。
     *
     * 布局是 [通道][候选框]，不是 [候选框][通道]。
     * 第 c 个类别、第 i 个候选框：
     * data[(4 + c) * 8400 + i]
     */
    for(int i = 0; i < CANDIDATE_COUNT; ++i)
    {
        int best_class = -1;
        float best_score = 0.0f;
        bool scores_valid = true;

        for(int c = 0; c < DETECT_CLASS_NUM; ++c)
        {
            const float score =
                data[(4 + c) * CANDIDATE_COUNT + i];

            if(!std::isfinite(score))
            {
                scores_valid = false;
                break;
            }

            if(score > best_score)
            {
                best_score = score;
                best_class = c;
            }
        }

        if(!scores_valid ||
           best_class < 0 ||
           best_score < confidence_threshold)
        {
            continue;
        }

        const float cx = data[i];
        const float cy = data[CANDIDATE_COUNT + i];
        const float width = data[2 * CANDIDATE_COUNT + i];
        const float height = data[3 * CANDIDATE_COUNT + i];

        if(!std::isfinite(cx) ||
           !std::isfinite(cy) ||
           !std::isfinite(width) ||
           !std::isfinite(height) ||
           width <= 0.0f ||
           height <= 0.0f)
        {
            continue;
        }

        Candidate box;
        box.class_id = best_class;
        box.confidence = best_score;

        box.x1 = cx - width * 0.5f;
        box.y1 = cy - height * 0.5f;
        box.x2 = cx + width * 0.5f;
        box.y2 = cy + height * 0.5f;

        if(!std::isfinite(box.x1) ||
           !std::isfinite(box.y1) ||
           !std::isfinite(box.x2) ||
           !std::isfinite(box.y2) ||
           box.x2 <= box.x1 ||
           box.y2 <= box.y1)
        {
            continue;
        }

        candidates.push_back(box);
    }

    /* 4. 分数从高到低排序 */
    std::stable_sort(
        candidates.begin(),
        candidates.end(),
        [](const Candidate &a, const Candidate &b)
        {
            return a.confidence > b.confidence;
        }
    );

    /*
     * 已保留的框维持模型输入坐标，
     * 用于 NMS，避免先裁边改变 IoU。
     */
    std::vector<Candidate> kept;
    kept.reserve(MAX_DETECTION_NUM);

    for(size_t i = 0; i < candidates.size(); ++i)
    {
        const Candidate &candidate = candidates[i];

        bool suppressed = false;

        for(size_t j = 0; j < kept.size(); ++j)
        {
            if(candidate.class_id == kept[j].class_id &&
               box_iou(candidate, kept[j]) > nms_threshold)
            {
                suppressed = true;
                break;
            }
        }

        if(suppressed)
        {
            continue;
        }

        /* 5. 去补边，再还原原图坐标 */
        DetectionBox box;
        box.class_id = candidate.class_id;
        box.confidence = candidate.confidence;

        box.x1 =
            (candidate.x1 - image->pad_left) / image->scale_x;
        box.y1 =
            (candidate.y1 - image->pad_top) / image->scale_y;
        box.x2 =
            (candidate.x2 - image->pad_left) / image->scale_x;
        box.y2 =
            (candidate.y2 - image->pad_top) / image->scale_y;

        if(!std::isfinite(box.x1) ||
           !std::isfinite(box.y1) ||
           !std::isfinite(box.x2) ||
           !std::isfinite(box.y2))
        {
            continue;
        }

        /*
         * 框边界使用连续坐标，允许右边界等于 width、
         * 下边界等于 height。绘制时再转为合法像素索引。
         */
        box.x1 = clamp_coordinate(
            box.x1, (float)image->original_width);
        box.y1 = clamp_coordinate(
            box.y1, (float)image->original_height);
        box.x2 = clamp_coordinate(
            box.x2, (float)image->original_width);
        box.y2 = clamp_coordinate(
            box.y2, (float)image->original_height);

        /* 丢弃完全落在补边区等无效框 */
        if(box.x2 <= box.x1 || box.y2 <= box.y1)
        {
            continue;
        }

        kept.push_back(candidate);
        result->boxes[result->count++] = box;

        if(result->count >= MAX_DETECTION_NUM)
        {
            break;
        }
    }

    return 0;
}