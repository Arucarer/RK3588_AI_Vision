/* RK3588 AI视觉检测程序入口 */
/* @author  zhangyong | @date    2026-09-14 | @version 2.0 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#include "common.h"
#include "tcp_client.h"
#include "event_store.h"
#include "roi_analysis.h"
#include "watchdog.h"

#include <time.h>

#include "v4l2_capture.h"
#include "camera_buffer.h"
#include "camera_thread.h"
#include "preprocess.h"
#include "rknn_engine.h"
#include "postprocess.h"
#include "detection_result.h"
#include "display.h"
#include "app_config.h"
#include "alarm_service.h"
#include "detection_pipeline.h"



/* STB_IMAGE_IMPLEMENTATION 只能在一个 .cpp 中定义。 仅启用 JPG、PNG 解码。 */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_MAX_DIMENSIONS 8192
#include "../third_party/stb/stb_image.h"

volatile sig_atomic_t g_run_state = RUN_STATE_OK;

/* 由 debug 配置控制逐帧日志，默认关闭。 */
int g_debug_logs = 0;

static void signal_handler(int sig)
{
    (void)sig;

    g_run_state = RUN_STATE_EXIT;
}

/* 单图检测函数前置声明 */
static int run_image_mode(const char *model_path, const char *image_path);

/* 单个处理阶段的统计 */
struct StageStats
{
    unsigned long calls;
    double sum_ms;
    double max_ms;
};

enum PerfStage
{
    PERF_PREPROCESS = 0,
    PERF_INFERENCE,

    PERF_RKNN_INPUT_SET,
    PERF_RKNN_RUN,
    PERF_RKNN_OUTPUT_GET,
    PERF_RKNN_OUTPUT_COPY,
    PERF_RKNN_OUTPUT_RELEASE,

    PERF_POSTPROCESS,
    PERF_DISPLAY,
    PERF_ENQUEUE,
    PERF_PROCESS,
    PERF_STAGE_COUNT
};

struct RuntimeStats
{
    long long started_us;

    unsigned long frames;
    unsigned long successful;
    unsigned long failed;
    unsigned long wait_timeouts;

    unsigned long snapshot_queued;
    unsigned long snapshot_skipped;
    unsigned long snapshot_failed;

    StageStats stages[PERF_STAGE_COUNT];
};

static void record_stage(RuntimeStats *stats, PerfStage stage, long long started_us)
{
    const double elapsed_ms = (monotonic_us() - started_us) / 1000.0;

    StageStats *item = &stats->stages[stage];

    item->calls++;
    item->sum_ms += elapsed_ms;

    if(elapsed_ms > item->max_ms)
    {
        item->max_ms = elapsed_ms;
    }
}

static void record_duration(
    RuntimeStats *stats,
    PerfStage stage,
    double milliseconds)
{
    if(milliseconds < 0)
        return;

    StageStats *item = &stats->stages[stage];

    ++item->calls;
    item->sum_ms += milliseconds;

    if(milliseconds > item->max_ms)
        item->max_ms = milliseconds;
}

static void record_inference_perf(RuntimeStats *stats, const RKNNInferencePerf *perf)
{
    static const PerfStage stage_map[RKNN_PERF_COUNT] = {
        PERF_RKNN_INPUT_SET, PERF_RKNN_RUN, PERF_RKNN_OUTPUT_GET, PERF_RKNN_OUTPUT_COPY,
        PERF_RKNN_OUTPUT_RELEASE};

    for(int i = 0; i < RKNN_PERF_COUNT; ++i)
    {
        const double elapsed_ms = perf->ms[i];

        /* 没有执行到的阶段，不参与统计 */
        if(elapsed_ms < 0.0)
        {
            continue;
        }

        StageStats *item = &stats->stages[stage_map[i]];

        item->calls++;
        item->sum_ms += elapsed_ms;

        if(elapsed_ms > item->max_ms)
        {
            item->max_ms = elapsed_ms;
        }
    }
}

/*
 * force=false：至少间隔一秒才输出。
 * force=true：退出前输出剩余统计。
 * 各阶段统计包含失败调用；
 * 没有执行的阶段不计入调用次数。
 */
static void report_stats(RuntimeStats *stats, int consecutive_errors, bool force, int interval_ms)
{
    if(stats->started_us == 0)
    {
        return;
    }

    const long long now = monotonic_us();
    const long long elapsed_us = now - stats->started_us;

    if(elapsed_us <= 0 || (!force && elapsed_us < interval_ms * 1000LL))
    {
        return;
    }

    if(force && stats->frames == 0 && stats->wait_timeouts == 0)
    {
        return;
    }

    const double seconds = elapsed_us / 1000000.0;
    const double fps = stats->successful / seconds;

    LOG_INFO("PERF window=%.2fs frames=%lu ok=%lu failed=%lu "
             "wait_timeouts=%lu success_fps=%.2f consecutive_errors=%d",
             seconds, stats->frames, stats->successful, stats->failed, stats->wait_timeouts, fps,
             consecutive_errors);

    static const char *stage_names[PERF_STAGE_COUNT] = {
        "preprocess",          "inference",

        "rknn_input_set",      "rknn_run",  "rknn_output_get",  "rknn_output_copy",
        "rknn_output_release",

        "postprocess",         "display",   "snapshot_enqueue", "frame_pipeline_latency"};

    for(int i = 0; i < PERF_STAGE_COUNT; ++i)
    {
        const StageStats *item = &stats->stages[i];

        if(item->calls == 0)
        {
            LOG_INFO("PERF %s calls=0", stage_names[i]);
            continue;
        }

        LOG_INFO("PERF %s calls=%lu avg=%.2f ms max=%.2f ms", stage_names[i], item->calls,
                 item->sum_ms / item->calls, item->max_ms);
    }

    LOG_INFO("PERF snapshot queued=%lu skipped=%lu failed=%lu", stats->snapshot_queued,
             stats->snapshot_skipped, stats->snapshot_failed);

    memset(stats, 0, sizeof(*stats));

    /* 排除本次打印时间对下一周期起点的影响 */
    stats->started_us = monotonic_us();
}

int main(int argc, char *argv[])
{
    if(argc > 1 && strcmp(argv[1], "--image") == 0)
    {
        if(argc != 4)
        {
            fprintf(stderr, "Usage: %s --image model.rknn image.jpg\n", argv[0]);
            return 1;
        }

        return run_image_mode(argv[2], argv[3]);
    }
    AppConfig config;
    std::string config_error;

    if(argc == 3 && strcmp(argv[1], "--config") == 0)
    {
        if(!load_config(argv[2], config, config_error))
        {
            LOG_ERR("configuration error: %s", config_error.c_str());
            return 1;
        }

        LOG_INFO("config loaded: %s", argv[2]);
    }
    else if(argc == 2 && argv[1][0] != '-')
    {
        /* 兼容原来的直接指定模型路径方式 */
        config.model = argv[1];
    }
    else if(argc != 1)
    {
        fprintf(stderr,
                "Usage:\n"
                "  %s\n"
                "  %s model.rknn\n"
                "  %s --config app.conf\n"
                "  %s --image model.rknn image.jpg\n",
                argv[0], argv[0], argv[0], argv[0]);

        return 1;
    }

    if(!validate_config(config, config_error))
    {
        LOG_ERR("configuration error: %s", config_error.c_str());
        return 1;
    }

    g_debug_logs = config.debug ? 1 : 0;
    event_store_set_limits(
        config.event_max_count,
        config.event_min_free_mb
    );

    LOG_INFO(
        "CONFIG event_max_count=%d event_min_free_mb=%d",
        config.event_max_count,
        config.event_min_free_mb
    );
    
    LOG_INFO("CONFIG model=%s", config.model.c_str());

    LOG_INFO("CONFIG confidence=%.3f nms=%.3f", config.confidence, config.nms);

    LOG_INFO("CONFIG snapshot_enabled=%s dir=%s interval_ms=%d",
             config.snapshot_enabled ? "true" : "false", config.snapshot_dir.c_str(),
             config.snapshot_interval_ms);

    LOG_INFO("CONFIG stats_interval_ms=%d camera_timeout_ms=%d "
             "max_errors=%d debug=%s",
             config.stats_interval_ms, config.camera_timeout_ms, config.max_errors,
             config.debug ? "true" : "false");
    int ret = -1;
    /* 初始化失败默认返回错误，完成启动后改为成功 */
    int exit_code = 1;
    long long last_frame_us = 0;

    const char *model_path = config.model.c_str();

    RKNNEngine engine;

    ProcessedImage processed_image;

    FrameData frame;
    DetectionResult detect_result;

    pthread_t camera_tid;
    int camera_thread_created = 0;
    int v4l2_initialized = 0;
    int camera_buffer_initialized = 0;
    int preprocess_initialized = 0;
    int engine_initialized = 0;
    int display_initialized = 0;
    int watchdog_initialized = 0;
    int alarm_initialized = 0;

    DetectionPipeline pipeline;
    std::unique_ptr<DetectionTask> pipeline_task;
    /* TCP 后台线程是否已启动，用于退出时清理 */
    int network_initialized = 0;

    /* 上一次尝试提交检测结果的单调时钟时间，单位 ms */
    long long last_report_ms = -1;

    /* 本次程序运行期间的累计提交统计 */
    unsigned long network_queued = 0;
    unsigned long network_dropped = 0;
    unsigned long network_submit_errors = 0;

    RKNNInferenceResult inference_result = {};

    /* 使用单调时钟计算间隔，不受系统时间调整影响 */
    long long last_snapshot_attempt_ms = -1;


    RuntimeStats stats = {};

    int consecutive_errors = 0;
    const int max_consecutive_errors = config.max_errors;

    memset(&engine, 0, sizeof(engine));
    memset(&processed_image, 0, sizeof(processed_image));
    memset(&frame, 0, sizeof(frame));
    memset(&detect_result, 0, sizeof(detect_result));

    signal(SIGINT, signal_handler);

    signal(SIGTERM, signal_handler);

    LOG_INFO("================================");
    LOG_INFO("RK3588 AI Vision Start");
    LOG_INFO("================================");

    LOG_INFO("model:%s", model_path);

    LOG_INFO(
        "CONFIG roi_enabled=%s roi=(%d,%d,%d,%d) edge_analysis=%s thresholds=%d/%d",
        config.roi_enabled ? "true" : "false",
        config.roi_x,
        config.roi_y,
        config.roi_width,
        config.roi_height,
        config.edge_analysis_enabled ? "true" : "false",
        config.edge_low_threshold,
        config.edge_high_threshold
    );
    LOG_INFO("CONFIG watchdog_enabled=%s timeout_ms=%d",
            config.watchdog_enabled ? "true" : "false",
            config.watchdog_timeout_ms);
    if(alarm_service_start(config.alarm_dir, config.device_id) != 0)
    {
        LOG_ERR("alarm service initialization failed");
        goto EXIT;
    }

    alarm_initialized = 1;

    if(config.watchdog_enabled)
    {
        if(app_watchdog_start(config.watchdog_timeout_ms) != 0)
        {
            LOG_ERR("watchdog initialization failed");
            goto EXIT;
        }

        watchdog_initialized = 1;
    }

    app_watchdog_feed("rknn_init");

    /* 1. RKNN初始化 */

    if(rknn_engine_init(&engine, model_path) != 0)
    {
        LOG_ERR("rknn engine init failed");

        goto EXIT;
    }

    engine_initialized = 1;
    /* 2. 初始化图像预处理 */
    app_watchdog_feed("preprocess_init");
    if(preprocess_init() != 0)
    {
        LOG_ERR("preprocess init failed");

        goto EXIT;
    }
    preprocess_initialized = 1;

    app_watchdog_feed("display_init");
    if(display_init() != 0)
    {
        LOG_ERR("display init failed");

        goto EXIT;
    }

    display_initialized = 1;

    if(config.network_enabled)
    {
        app_watchdog_feed("network_init");
        LOG_INFO("TCP config server=%s:%d device=%s report_interval_ms=%d",
                 config.server_ip.c_str(), config.server_port, config.device_id.c_str(),
                 config.report_interval_ms);

        if(tcp_client_start(
                config.server_ip.c_str(),
                config.server_port,
                config.device_id.c_str(),
                config.snapshot_dir.c_str()) != 0)
        {
            LOG_ERR("TCP worker initialization failed");
            goto EXIT;
        }

        network_initialized = 1;
    }

    /* 3. 初始化Camera Buffer */
    app_watchdog_feed("camera_buffer_init");
    if(camera_buffer_init() != 0)
    {
        LOG_ERR("camera buffer init failed");

        goto EXIT;
    }

    camera_buffer_initialized = 1;

    /* 4. 初始化V4L2摄像头 */
    LOG_INFO("CONFIG camera_device=%s size=%dx%d requested_fps=%d",
            config.camera_device.c_str(),
            config.camera_width,
            config.camera_height,
            config.camera_fps);

    app_watchdog_feed("v4l2_init");

    if(v4l2_init(config.camera_device.c_str(),
                config.camera_width,
                config.camera_height,
                config.camera_fps) != 0)
    {
        LOG_ERR("v4l2 init failed");

        goto EXIT;
    }

    v4l2_initialized = 1;

    /* 5. 创建摄像头采集线程 */
    app_watchdog_feed("camera_thread_create");
    ret = pthread_create(&camera_tid, NULL, camera_thread, NULL);

    if(ret != 0)
    {
        LOG_ERR("create camera thread failed");

        goto EXIT;
    }

    camera_thread_created = 1;

    LOG_INFO("camera thread start");

    if(pipeline.start(config, &engine) != 0)
    {
        LOG_ERR("pipeline worker initialization failed");
        goto EXIT;
    }
    exit_code = 0;
    last_frame_us = monotonic_us();


    stats.started_us = last_frame_us;

    app_watchdog_feed("camera_wait");
    while(g_run_state == RUN_STATE_OK)
    {
        const int pop_ret = pipeline.pop(pipeline_task);

        if(g_run_state != RUN_STATE_OK)
            break;

        if(pop_ret < 0)
        {
            LOG_ERR("detection pipeline stopped unexpectedly");
            exit_code = 1;
            break;
        }

        if(pop_ret == 1)
        {
            stats.wait_timeouts++;

            report_stats(
                &stats,
                consecutive_errors,
                false,
                config.stats_interval_ms);

            /*
            * 无帧超时由预处理线程判断。
            * 等待结果期间不喂狗，避免掩盖推理线程卡死。
            */
            continue;
        }

        /*
        * 原图所有权从任务转移到 main 的 frame。
        * 清空任务里的副本，避免任务析构时重复释放。
        */
        frame = pipeline_task->frame;
        memset(&pipeline_task->frame, 0, sizeof(pipeline_task->frame));

        detect_result = pipeline_task->result;

        const long long frame_started = pipeline_task->started_us;

        record_duration(
            &stats, PERF_PREPROCESS, pipeline_task->preprocess_ms);

        record_duration(
            &stats, PERF_INFERENCE, pipeline_task->inference_ms);

        record_duration(
            &stats, PERF_POSTPROCESS, pipeline_task->postprocess_ms);

        record_inference_perf(&stats, &pipeline_task->rknn_perf);

        stats.frames++;

        bool frame_ok = false;
        const char *failed_stage = "unknown";

        do
        {
            if(pipeline_task->error != nullptr)
            {
                failed_stage = pipeline_task->error;
                break;
            }

            failed_stage = "display";
            app_watchdog_feed("display");

            const long long display_started = monotonic_us();

            const int display_ret = display_show(&frame, &detect_result);

            record_stage(&stats, PERF_DISPLAY, display_started);

            if(display_ret != 0)
                break;

            frame_ok = true;

            if(network_initialized)
            {
                const long long now_ms = monotonic_us() / 1000;

                if(last_report_ms < 0 ||
                now_ms - last_report_ms >= config.report_interval_ms)
                {
                    last_report_ms = now_ms;

                    app_watchdog_feed("network_submit");

                    const int submitted = tcp_client_submit(&detect_result);

                    if(submitted > 0)
                    {
                        network_queued++;
                    }
                    else if(submitted == 0)
                    {
                        network_dropped++;

                        LOG_INFO(
                            "TCP queue full; dropped_total=%lu",
                            network_dropped);
                    }
                    else
                    {
                        network_submit_errors++;
                        LOG_ERR("TCP result submit failed");
                    }
                }
            }

            if(config.snapshot_enabled && detect_result.count > 0)
            {
                const long long now_ms = monotonic_us() / 1000;

                if(last_snapshot_attempt_ms < 0 ||
                now_ms - last_snapshot_attempt_ms >=
                    config.snapshot_interval_ms)
                {
                    last_snapshot_attempt_ms = now_ms;

                    app_watchdog_feed("snapshot_enqueue");

                    const long long enqueue_started = monotonic_us();

                    const int queued = display_save_snapshot(
                        config.snapshot_dir.c_str(),
                        config.device_id.c_str(),
                        &detect_result);

                    record_stage(&stats, PERF_ENQUEUE, enqueue_started);

                    if(queued > 0)
                    {
                        stats.snapshot_queued++;
                    }
                    else if(queued == 0)
                    {
                        stats.snapshot_skipped++;
                    }
                    else
                    {
                        stats.snapshot_failed++;

                        LOG_ERR("snapshot enqueue failed");

                        if(!alarm_raise(
                            "snapshot_enqueue_failed",
                            "snapshot"))
                        {
                            LOG_ERR("snapshot alarm persistence failed");
                        }
                    }
                }
            }

            for(int i = 0; i < detect_result.count; ++i)
            {
                const DetectionBox *box = &detect_result.boxes[i];

                LOG_DEBUG(
                    "box[%d] class=%d confidence=%.3f "
                    "xyxy=(%.1f,%.1f,%.1f,%.1f)",
                    i,
                    box->class_id,
                    box->confidence,
                    box->x1,
                    box->y1,
                    box->x2,
                    box->y2);
            }
        } while(false);

        app_watchdog_feed("frame_release");

        camera_buffer_release(&frame);
        pipeline_task.reset();

        /*
        * 现在包含预处理、队列等待、推理及主线程显示提交时间。
        * 不再是单线程各阶段耗时相加的含义。
        */
        record_stage(&stats, PERF_PROCESS, frame_started);

        if(frame_ok)
        {
            stats.successful++;

            if(consecutive_errors > 0 &&
            !alarm_raise("frame_recovered", "pipeline"))
            {
                LOG_ERR("recovery alarm persistence failed");
            }

            consecutive_errors = 0;
        }
        else
        {
            stats.failed++;
            consecutive_errors++;

            if(consecutive_errors == 1 &&
            !alarm_raise("frame_failed", failed_stage))
            {
                LOG_ERR("frame alarm persistence failed");
            }

            LOG_ERR(
                "frame failed stage=%s consecutive=%d/%d",
                failed_stage,
                consecutive_errors,
                max_consecutive_errors);
        }

        report_stats(
            &stats,
            consecutive_errors,
            false,
            config.stats_interval_ms);

        if(consecutive_errors >= max_consecutive_errors)
        {
            LOG_ERR("stopping after repeated processing errors");
            exit_code = 1;
            break;
        }

        app_watchdog_feed("pipeline_wait");
    }


    /* 正常退出或异常退出前，输出不足一秒的剩余统计 */
    report_stats(&stats, consecutive_errors, true, config.stats_interval_ms);

EXIT:
    app_watchdog_feed("shutdown");
    g_run_state = RUN_STATE_EXIT;

    if(exit_code != 0 && alarm_initialized &&
       !alarm_raise("application_exit_error", "main"))
    {
        LOG_ERR("exit alarm persistence failed");
    }

    LOG_INFO("AI application exit");

    /*
     * 先停止流水线、唤醒队列并等待工作线程退出，
     * 再释放摄像头、预处理模块和 RKNN。
     */
    app_watchdog_feed("pipeline_stop");
    pipeline.stop();
    pipeline_task.reset();

    if(camera_buffer_initialized)
    {
        app_watchdog_feed("camera_buffer_stop");
        camera_buffer_stop();
    }

    if(camera_thread_created)
    {
        app_watchdog_feed("camera_thread_join");
        pthread_join(camera_tid, NULL);
        camera_thread_created = 0;
    }

    if(network_initialized)
    {
        app_watchdog_feed("network_stop");
        tcp_client_stop();
        network_initialized = 0;

        LOG_INFO("TCP submit totals queued=%lu dropped=%lu errors=%lu",
                 network_queued, network_dropped, network_submit_errors);
    }

    if(display_initialized)
    {
        app_watchdog_feed("display_deinit");
        display_deinit();
        display_initialized = 0;
    }

    app_watchdog_feed("remaining_buffers_release");

    rknn_engine_result_release(&inference_result);
    camera_buffer_release(&frame);
    preprocess_release(&processed_image);

    if(v4l2_initialized)
    {
        app_watchdog_feed("v4l2_deinit");
        v4l2_deinit();
        v4l2_initialized = 0;
    }

    if(camera_buffer_initialized)
    {
        app_watchdog_feed("camera_buffer_deinit");
        camera_buffer_deinit();
        camera_buffer_initialized = 0;
    }

    if(preprocess_initialized)
    {
        app_watchdog_feed("preprocess_deinit");
        preprocess_deinit();
        preprocess_initialized = 0;
    }

    if(engine_initialized)
    {
        app_watchdog_feed("rknn_release");
        rknn_engine_release(&engine);
        engine_initialized = 0;
    }

    if(alarm_initialized)
    {
        app_watchdog_feed("alarm_stop");
        alarm_service_stop();
        alarm_initialized = 0;
    }

    app_watchdog_feed("shutdown_complete");
    LOG_INFO("RK3588 AI Vision stopped");

    if(watchdog_initialized)
    {
        app_watchdog_stop();
        watchdog_initialized = 0;
    }

    return exit_code;
}

static int run_image_mode(const char *model_path, const char *image_path)
{
    RKNNEngine engine = {};
    RKNNInferenceResult inference = {};
    ProcessedImage processed = {};
    DetectionResult result = {};

    int width = 0;
    int height = 0;
    int channels = 0;

    /* 强制解码为 RGB 三通道。 即使原始图片是灰度图，也会输出三通道 RGB。 */
    unsigned char *rgb = stbi_load(image_path, &width, &height, &channels, 3);

    if(rgb == NULL)
    {
        const char *reason = stbi_failure_reason();

        LOG_ERR("load image failed: %s reason=%s", image_path, reason != NULL ? reason : "unknown");
        return 1;
    }

    int status = 1;
    bool engine_ready = false;

    /* 用单次 do/while 统一处理失败清理。 */
    do
    {
        LOG_INFO("image=%s width=%d height=%d source_channels=%d", image_path, width, height,
                 channels);

        if(rknn_engine_init(&engine, model_path) != 0)
        {
            break;
        }

        engine_ready = true;

        if(engine.input_width != MODEL_INPUT_WIDTH || engine.input_height != MODEL_INPUT_HEIGHT ||
           engine.input_channel != 3)
        {
            LOG_ERR("image mode requires 640x640 RGB model input");
            break;
        }

        if(preprocess_rgb(rgb, width, height, (size_t)width * height * 3, &processed) != 0)
        {
            break;
        }

        LOG_INFO("letterbox resized=%dx%d pad=(%d,%d) scale=(%.6f,%.6f)", processed.resized_width,
                 processed.resized_height, processed.pad_left, processed.pad_top, processed.scale_x,
                 processed.scale_y);

        const long long start = monotonic_us();

        if(rknn_engine_inference(&engine, processed.rgb, MODEL_INPUT_SIZE, &inference) != 0)
        {
            break;
        }

        const double inference_ms = (monotonic_us() - start) / 1000.0;

        if(postprocess_result(&engine, &inference, &processed,
                              0, // 单图没有摄像头时间戳
                              0.25f, 0.45f, &result) != 0)
        {
            break;
        }

        result.inference_time_ms = (float)inference_ms;

        static const char *names[DETECT_CLASS_NUM] = {
            "crazing", "inclusion", "patches", "pitted_surface", "rolled-in_scale", "scratches"};

        LOG_INFO("image inference success: boxes=%d inference=%.2f ms", result.count, inference_ms);

        /* CSV 格式，坐标属于原始输入图片。 PC 对照时也打印相同字段。 */
        printf("class_id,class_name,confidence,x1,y1,x2,y2\n");

        for(int i = 0; i < result.count; ++i)
        {
            const DetectionBox *box = &result.boxes[i];

            const char *name = box->class_id >= 0 && box->class_id < DETECT_CLASS_NUM
                                   ? names[box->class_id]
                                   : "unknown";

            printf("%d,%s,%.6f,%.3f,%.3f,%.3f,%.3f\n", box->class_id, name, box->confidence,
                   box->x1, box->y1, box->x2, box->y2);
        }

        status = 0;
    } while(false);

    rknn_engine_result_release(&inference);
    preprocess_release(&processed);
    stbi_image_free(rgb);

    if(engine_ready)
    {
        rknn_engine_release(&engine);
    }

    return status;
}
