/*
 * @file    rknn_engine.cpp
 * @brief   RK3588 RKNN推理引擎
 *
 * 当前阶段：
 * 1. 加载RKNN模型
 * 2. 查询并打印输入、输出tensor属性
 * 3. 执行NPU推理
 * 4. 返回第一个输出的float数据
 *
 * 本模块不执行YOLOv8后处理。
 */

#include "rknn_engine.h"

#include <limits.h>


/*
 * 当前头文件中output_attrs的容量为10，
 * 推理时的outputs数组也使用相同容量。
 */
static const int MAX_ENGINE_OUTPUTS =
    RKNN_ENGINE_MAX_OUTPUTS;


/*
 * 返回常用tensor布局名称。
 * 其他布局仍会在日志中打印其枚举数值。
 */
static const char *tensor_format_name(rknn_tensor_format fmt)
{
    switch(fmt)
    {
        case RKNN_TENSOR_NCHW:
            return "NCHW";

        case RKNN_TENSOR_NHWC:
            return "NHWC";

        default:
            return "OTHER";
    }
}


/*
 * 返回常用tensor类型名称。
 */
static const char *tensor_type_name(rknn_tensor_type type)
{
    switch(type)
    {
        case RKNN_TENSOR_FLOAT32:
            return "FLOAT32";

        case RKNN_TENSOR_FLOAT16:
            return "FLOAT16";

        case RKNN_TENSOR_INT8:
            return "INT8";

        case RKNN_TENSOR_UINT8:
            return "UINT8";

        case RKNN_TENSOR_INT16:
            return "INT16";

        case RKNN_TENSOR_INT32:
            return "INT32";

        default:
            return "OTHER";
    }
}


/*
 * 打印一个tensor的属性。
 *
 * dims按Runtime返回的原始顺序打印，
 * 不在这里转置或解释输出布局。
 */
static void print_tensor_attr(
        const char *direction,
        const rknn_tensor_attr *attr)
{
    if(direction == NULL || attr == NULL)
    {
        return;
    }

    LOG_INFO(
        "========== %s tensor[%u] ==========",
        direction,
        (unsigned int)attr->index
    );

    LOG_INFO(
        "name=%.*s",
        (int)sizeof(attr->name),
        attr->name
    );

    LOG_INFO(
        "n_dims=%u",
        (unsigned int)attr->n_dims
    );

    const unsigned int dims_capacity =
        (unsigned int)(
            sizeof(attr->dims) / sizeof(attr->dims[0])
        );

    for(unsigned int i = 0;
        i < attr->n_dims && i < dims_capacity;
        ++i)
    {
        LOG_INFO(
            "dims[%u]=%u",
            i,
            (unsigned int)attr->dims[i]
        );
    }

    LOG_INFO(
        "n_elems=%u size=%u bytes",
        (unsigned int)attr->n_elems,
        (unsigned int)attr->size
    );

    LOG_INFO(
        "fmt=%s(%d) type=%s(%d)",
        tensor_format_name(attr->fmt),
        (int)attr->fmt,
        tensor_type_name(attr->type),
        (int)attr->type
    );

    LOG_INFO(
        "qnt_type=%d zp=%d scale=%.9g fl=%d",
        (int)attr->qnt_type,
        (int)attr->zp,
        (double)attr->scale,
        (int)attr->fl
    );
}


/*
 * 初始化RKNN。
 *
 * 调用要求：
 * engine尚未初始化，或已经执行过release。
 */
int rknn_engine_init(
        RKNNEngine *engine,
        const char *model_path)
{
    if(engine == NULL || model_path == NULL)
    {
        LOG_ERR("invalid rknn init parameter");
        return -1;
    }

    memset(engine, 0, sizeof(*engine));

    /*
     * 1. 读取模型文件
     */
    FILE *fp = fopen(model_path, "rb");

    if(fp == NULL)
    {
        LOG_ERR("open rknn model failed: %s", model_path);
        return -1;
    }

    if(fseek(fp, 0, SEEK_END) != 0)
    {
        LOG_ERR("seek rknn model failed");
        fclose(fp);
        return -1;
    }

    const long model_size = ftell(fp);

    if(model_size <= 0 || model_size > INT_MAX)
    {
        LOG_ERR("invalid rknn model size=%ld", model_size);
        fclose(fp);
        return -1;
    }

    if(fseek(fp, 0, SEEK_SET) != 0)
    {
        LOG_ERR("rewind rknn model failed");
        fclose(fp);
        return -1;
    }

    void *model_data = malloc((size_t)model_size);

    if(model_data == NULL)
    {
        LOG_ERR("malloc rknn model buffer failed");
        fclose(fp);
        return -1;
    }

    const size_t read_size = fread(
        model_data,
        1,
        (size_t)model_size,
        fp
    );

    fclose(fp);

    if(read_size != (size_t)model_size)
    {
        LOG_ERR(
            "read rknn model failed: expected=%ld actual=%zu",
            model_size,
            read_size
        );

        free(model_data);
        return -1;
    }

    /*
     * 2. 初始化Runtime
     */
    int ret = rknn_init(
        &engine->ctx,
        model_data,
        (unsigned int)model_size,
        0,
        NULL
    );

    free(model_data);

    if(ret < 0)
    {
        LOG_ERR("rknn init failed ret=%d", ret);
        engine->ctx = 0;
        return -1;
    }

    /*
     * 提前声明，方便后续统一跳转到失败清理位置。
     */
    rknn_input_output_num io_num;
    rknn_tensor_attr input_attr;

    memset(&io_num, 0, sizeof(io_num));
    memset(&input_attr, 0, sizeof(input_attr));

    /*
     * 3. 查询输入、输出数量
     */
    ret = rknn_query(
        engine->ctx,
        RKNN_QUERY_IN_OUT_NUM,
        &io_num,
        sizeof(io_num)
    );

    if(ret < 0)
    {
        LOG_ERR("query rknn io num failed ret=%d", ret);
        goto INIT_FAILED;
    }

    LOG_INFO(
        "rknn input=%u output=%u",
        (unsigned int)io_num.n_input,
        (unsigned int)io_num.n_output
    );

    /*
     * 当前推理接口只提交一个输入，
     * 最多保存10个输出属性。
     */
    if(io_num.n_input != 1 ||
       io_num.n_output == 0 ||
       io_num.n_output > (unsigned int)MAX_ENGINE_OUTPUTS)
    {
        LOG_ERR(
            "unsupported io count: input=%u output=%u",
            (unsigned int)io_num.n_input,
            (unsigned int)io_num.n_output
        );

        goto INIT_FAILED;
    }

    engine->input_num = (int)io_num.n_input;
    engine->output_num = (int)io_num.n_output;

    /*
     * 4. 查询并打印输入属性
     */
    input_attr.index = 0;

    ret = rknn_query(
        engine->ctx,
        RKNN_QUERY_INPUT_ATTR,
        &input_attr,
        sizeof(input_attr)
    );

    if(ret < 0)
    {
        LOG_ERR("query input[0] attr failed ret=%d", ret);
        goto INIT_FAILED;
    }

    print_tensor_attr("INPUT", &input_attr);

    /*
     * 5. 查询并打印全部输出属性
     */
    for(int i = 0; i < engine->output_num; ++i)
    {
        rknn_tensor_attr *attr = &engine->output_attrs[i];

        memset(attr, 0, sizeof(*attr));
        attr->index = (unsigned int)i;

        ret = rknn_query(
            engine->ctx,
            RKNN_QUERY_OUTPUT_ATTR,
            attr,
            sizeof(*attr)
        );

        if(ret < 0)
        {
            LOG_ERR(
                "query output[%d] attr failed ret=%d",
                i,
                ret
            );

            goto INIT_FAILED;
        }

        print_tensor_attr("OUTPUT", attr);
    }

    /*
     * 6. 根据输入布局解析图像尺寸
     *
     * NHWC: [N, H, W, C]
     * NCHW: [N, C, H, W]
     */
    if(input_attr.n_dims != 4 || input_attr.dims[0] != 1)
    {
        LOG_ERR(
            "expected 4D batch=1 image input, n_dims=%u",
            (unsigned int)input_attr.n_dims
        );

        goto INIT_FAILED;
    }

    if(input_attr.fmt == RKNN_TENSOR_NHWC)
    {
        engine->input_height = (int)input_attr.dims[1];
        engine->input_width = (int)input_attr.dims[2];
        engine->input_channel = (int)input_attr.dims[3];
    }
    else if(input_attr.fmt == RKNN_TENSOR_NCHW)
    {
        engine->input_channel = (int)input_attr.dims[1];
        engine->input_height = (int)input_attr.dims[2];
        engine->input_width = (int)input_attr.dims[3];
    }
    else
    {
        LOG_ERR(
            "unsupported image input format=%d",
            (int)input_attr.fmt
        );

        goto INIT_FAILED;
    }

    LOG_INFO(
        "input shape: width=%d height=%d channel=%d",
        engine->input_width,
        engine->input_height,
        engine->input_channel
    );

    LOG_INFO(
        "inference input buffer: UINT8 NHWC; "
        "output request: want_float=1"
    );

    LOG_INFO(
        "inference interface returns all %d outputs as float copies",
        engine->output_num
    );

    LOG_INFO("rknn engine init success");
    return 0;


INIT_FAILED:

    rknn_destroy(engine->ctx);
    memset(engine, 0, sizeof(*engine));

    return -1;
}


/*
 * 释放RKNN资源。
 */
void rknn_engine_release(RKNNEngine *engine)
{
    if(engine == NULL)
    {
        return;
    }

    if(engine->ctx)
    {
        rknn_destroy(engine->ctx);
    }

    memset(engine, 0, sizeof(*engine));

    LOG_INFO("rknn engine release");
}

/*
 * 释放一次推理生成的全部输出。
 *
 * result 必须是：
 * 1. 已清零的结果对象；或者
 * 2. 由本模块填充的结果对象。
 *
 * 释放后可以再次调用本函数。
 */
void rknn_engine_result_release(
        RKNNInferenceResult *result)
{
    if(result == NULL)
    {
        return;
    }

    for(int i = 0; i < RKNN_ENGINE_MAX_OUTPUTS; ++i)
    {
        free(result->tensors[i].data);
    }

    memset(result, 0, sizeof(*result));
}


/*
 * 执行一次推理。
 *
 * 输入：
 *   连续存储的 UINT8、NHWC RGB 图像。
 *
 * 输出：
 *   全部输出的 float 数据副本。
 *   tensors[i] 对应 engine->output_attrs[i]。
 *
 * 调用要求：
 *   result 首次使用前必须清零；
 *   再次使用前必须释放上一次结果。
 *
 * 成功后：
 *   调用者通过 rknn_engine_result_release() 释放。
 */
int rknn_engine_inference(
        RKNNEngine *engine,
        unsigned char *input_data,
        int input_size,
        RKNNInferenceResult *result)
{
    if(engine != NULL)
    {
        for(int i = 0; i < RKNN_PERF_COUNT; ++i)
        {
            engine->last_perf.ms[i] = -1.0;
        }
    }

    if(engine == NULL ||
       input_data == NULL ||
       result == NULL ||
       input_size <= 0)
    {
        LOG_ERR("invalid rknn inference parameter");
        return -1;
    }

    /*
     * 防止直接覆盖尚未释放的输出。
     * 此检查要求调用者已经初始化 result。
     */
    if(result->count != 0)
    {
        LOG_ERR("release previous inference result first");
        return -1;
    }

    for(int i = 0; i < RKNN_ENGINE_MAX_OUTPUTS; ++i)
    {
        if(result->tensors[i].data != NULL)
        {
            LOG_ERR("inference result is not empty");
            return -1;
        }
    }

    if(engine->ctx == 0 ||
       engine->input_num != 1 ||
       engine->output_num <= 0 ||
       engine->output_num > MAX_ENGINE_OUTPUTS)
    {
        LOG_ERR("rknn engine is not ready");
        return -1;
    }

    /*
     * 当前接口只接受三通道 RGB 图像。
     * 分步检查，避免尺寸乘法溢出。
     */
    if(engine->input_width <= 0 ||
       engine->input_height <= 0 ||
       engine->input_channel != 3)
    {
        LOG_ERR("unsupported input image shape");
        return -1;
    }

    if(engine->input_width > INT_MAX / engine->input_height)
    {
        LOG_ERR("input image dimensions are too large");
        return -1;
    }

    const int pixels =
        engine->input_width * engine->input_height;

    if(pixels > INT_MAX / engine->input_channel)
    {
        LOG_ERR("input image size is too large");
        return -1;
    }

    const int expected_size =
        pixels * engine->input_channel;

    if(input_size != expected_size)
    {
        LOG_ERR(
            "input size mismatch: expected=%d actual=%d",
            expected_size,
            input_size
        );
        return -1;
    }

    /* 1. 提交输入 */
    rknn_input input;
    memset(&input, 0, sizeof(input));

    input.index = 0;
    input.type = RKNN_TENSOR_UINT8;
    input.size = (unsigned int)input_size;
    input.fmt = RKNN_TENSOR_NHWC;
    input.pass_through = 0;
    input.buf = input_data;

    long long stage_started = monotonic_us();

    int ret = rknn_inputs_set(
        engine->ctx,
        1,
        &input
    );

    engine->last_perf.ms[RKNN_PERF_INPUT_SET] =
        (monotonic_us() - stage_started) / 1000.0;

    if(ret < 0)
    {
        LOG_ERR("rknn input set failed ret=%d", ret);
        return -1;
    }

    /* 2. 执行 NPU 推理 */
    stage_started = monotonic_us();

    ret = rknn_run(engine->ctx, NULL);

    engine->last_perf.ms[RKNN_PERF_RUN] = (monotonic_us() - stage_started) / 1000.0;

    if(ret < 0)
    {
        LOG_ERR("rknn run failed ret=%d", ret);
        return -1;
    }

    /*
     * 3. 获取全部输出。
     * want_float=1：后处理按 float 读取，
     * 不要重复进行 INT8 反量化。
     */
    rknn_output outputs[MAX_ENGINE_OUTPUTS];
    memset(outputs, 0, sizeof(outputs));

    for(int i = 0; i < engine->output_num; ++i)
    {
        outputs[i].index = (unsigned int)i;
        outputs[i].want_float = 1;
        outputs[i].is_prealloc = 0;
    }

    stage_started = monotonic_us();

    ret = rknn_outputs_get(
        engine->ctx,
        (unsigned int)engine->output_num,
        outputs,
        NULL
    );

    engine->last_perf.ms[RKNN_PERF_OUTPUT_GET] =
        (monotonic_us() - stage_started) / 1000.0;

    if(ret < 0)
    {
        LOG_ERR("rknn output get failed ret=%d", ret);
        return -1;
    }

    /*
     * 4. 先复制到临时结果。
     * 全部复制成功并释放 Runtime 输出后，
     * 才将所有权交给调用者。
     */
    stage_started = monotonic_us();
    RKNNInferenceResult copied_result;
    memset(&copied_result, 0, sizeof(copied_result));

    bool copy_ok = true;

    for(int i = 0; i < engine->output_num; ++i)
    {
        const size_t bytes = outputs[i].size;

        if(outputs[i].buf == NULL ||
           bytes == 0 ||
           bytes % sizeof(float) != 0 ||
           bytes / sizeof(float) > (size_t)INT_MAX)
        {
            LOG_ERR(
                "invalid float output[%d]: size=%u",
                i,
                (unsigned int)outputs[i].size
            );

            copy_ok = false;
            break;
        }

        RKNNOutputTensor *tensor =
            &copied_result.tensors[i];

        tensor->data = (float *)malloc(bytes);

        if(tensor->data == NULL)
        {
            LOG_ERR("malloc output[%d] copy failed", i);
            copy_ok = false;
            break;
        }

        memcpy(
            tensor->data,
            outputs[i].buf,
            bytes
        );

        tensor->element_count =
            (int)(bytes / sizeof(float));

        copied_result.count++;
    }
    engine->last_perf.ms[RKNN_PERF_OUTPUT_COPY] =
        (monotonic_us() - stage_started) / 1000.0;
    /*
     * 无论复制是否成功，
     * 都释放本次成功获取的 Runtime 输出。
     */
    stage_started = monotonic_us();

    ret = rknn_outputs_release(
        engine->ctx,
        (unsigned int)engine->output_num,
        outputs
    );

    engine->last_perf.ms[RKNN_PERF_OUTPUT_RELEASE] =
        (monotonic_us() - stage_started) / 1000.0;

    if(ret < 0)
    {
        LOG_ERR("rknn output release failed ret=%d", ret);
    }

    if(!copy_ok || ret < 0)
    {
        rknn_engine_result_release(&copied_result);
        return -1;
    }

    /* 转移数据所有权，之后由调用者释放 */
    *result = copied_result;

    return 0;
}