/*
 * @file    main.cpp
 * @brief   RK3588 AI视觉检测程序入口
 *
 * @author  zhangyong
 * @date    2026-09-14
 * @version 2.0
 *
 * @note
 *
 * 当前版本:
 *
 * 1. 加载RKNN模型
 * 2. 初始化RKNN Runtime
 * 3. 初始化图像预处理
 * 4. 初始化V4L2摄像头
 * 5. 创建Camera采集线程
 * 6. 获取最新Camera Buffer
 * 7. NV12转换RGB
 * 8. RK3588 NPU推理
 * 9. 缺陷分类后处理
 * 10. 输出缺陷类别和置信度
 * 11. Ctrl+C优雅退出
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#include "common.h"



#include "v4l2_capture.h"
#include "camera_buffer.h"
#include "camera_thread.h"
#include "preprocess.h"
#include "rknn_engine.h"
#include "postprocess.h"
#include "detection_result.h"
#include "display.h"

/*
 * STB_IMAGE_IMPLEMENTATION 只能在一个 .cpp 中定义。
 * 本次仅启用 JPG、PNG 解码。
 */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_MAX_DIMENSIONS 8192
#include "../third_party/stb/stb_image.h"

volatile sig_atomic_t g_run_state = RUN_STATE_OK;



static void signal_handler(int sig)
{
    (void)sig;

    g_run_state =
        RUN_STATE_EXIT;
}

/* 单图检测函数前置声明 */
static int run_image_mode(
        const char *model_path,
        const char *image_path);

        

int main(int argc,char *argv[])
{
    if(argc > 1 && strcmp(argv[1], "--image") == 0)
    {
        if(argc != 4)
        {
            fprintf(
                stderr,
                "Usage: %s --image model.rknn image.jpg\n",
                argv[0]
            );
            return 1;
        }

        return run_image_mode(argv[2], argv[3]);
    }
    int ret = -1;
    const char *model_path = "/opt/rk3588_ai/model/best.rknn";

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



    RKNNInferenceResult inference_result = {};


    long frame_count = 0;
    long long last_time = get_time_us();
    double fps = 0;


    memset( &engine, 0, sizeof(engine));
    memset(&processed_image, 0, sizeof(processed_image));
    memset(&frame,0,sizeof(frame));
    memset(&detect_result,0,sizeof(detect_result));

    signal(SIGINT,signal_handler);


    signal(SIGTERM,signal_handler);



    LOG_INFO(
        "================================"
    );
    LOG_INFO(
        "RK3588 AI Vision Start"
    );
    LOG_INFO(
        "================================"
    );



    if(argc > 1)
    {
        model_path =
            argv[1];
    }



    LOG_INFO(
        "model:%s",
        model_path
    );




    /*
     * 1. RKNN初始化
     */


    if(rknn_engine_init(
            &engine,
            model_path) != 0)
    {

        LOG_ERR(
            "rknn engine init failed"
        );

        goto EXIT;

    }


    engine_initialized = 1;
    /*
     * 2. 初始化图像预处理
     */

    if(preprocess_init() != 0)
    {

        LOG_ERR(
            "preprocess init failed"
        );

        goto EXIT;

    }
    preprocess_initialized = 1;

    if(display_init() != 0)
    {
        LOG_ERR(
            "display init failed"
        );

        goto EXIT;
    }

    display_initialized = 1;




    /*
     * 3. 初始化Camera Buffer
     */

    if(camera_buffer_init() != 0)
    {

        LOG_ERR(
            "camera buffer init failed"
        );

        goto EXIT;

    }


    camera_buffer_initialized = 1;




    /*
     * 4. 初始化V4L2摄像头
     */

    if(v4l2_init() != 0)
    {

        LOG_ERR(
            "v4l2 init failed"
        );

        goto EXIT;

    }


    v4l2_initialized = 1;




    /*
     * 5. 创建摄像头采集线程
     */

    ret =
        pthread_create(
            &camera_tid,
            NULL,
            camera_thread,
            NULL
        );


    if(ret != 0)
    {

        LOG_ERR(
            "create camera thread failed"
        );

        goto EXIT;

    }


    camera_thread_created = 1;



    LOG_INFO(
        "camera thread start"
    );


    while(g_run_state == RUN_STATE_OK)
    {
        if(camera_buffer_pop(&frame) != 0)
        {
            continue;
        }

        const long long preprocess_start = get_time_us();

        if(preprocess_run(&frame, &processed_image) != 0)
        {
            LOG_ERR("preprocess failed");

            preprocess_release(&processed_image);
            camera_buffer_release(&frame);
            continue;
        }

        const double preprocess_time =
            (get_time_us() - preprocess_start) / 1000.0;

        if(processed_image.rgb_width != engine.input_width ||
        processed_image.rgb_height != engine.input_height ||
        engine.input_channel != 3)
        {
            LOG_ERR("model input shape does not match preprocessing");

            preprocess_release(&processed_image);
            camera_buffer_release(&frame);
            break;
        }

        /* 每帧从空结果开始，避免显示上一帧的框 */
        memset(&detect_result, 0, sizeof(detect_result));

        const long long inference_start = get_time_us();

        const int inference_ret = rknn_engine_inference(
            &engine,
            processed_image.rgb,
            MODEL_INPUT_SIZE,
            &inference_result
        );

        const double inference_time =
            (get_time_us() - inference_start) / 1000.0;

        int post_ret = -1;
        double postprocess_time = 0.0;

        if(inference_ret != 0)
        {
            LOG_ERR("rknn inference failed");
        }
        else
        {
            const long long postprocess_start = get_time_us();

            post_ret = postprocess_result(
                &engine,
                &inference_result,
                &processed_image,
                frame.pts,
                0.25f,
                0.45f,
                &detect_result
            );

            postprocess_time =
                (get_time_us() - postprocess_start) / 1000.0;

            if(post_ret != 0)
            {
                LOG_ERR("YOLO postprocess failed");
            }
        }

        /*
        * 失败时仍可显示当前摄像头画面，但清空所有框。
        * 日志中的返回值区分“检测失败”和“成功但无目标”。
        */
        if(inference_ret != 0 || post_ret != 0)
        {
            memset(&detect_result, 0, sizeof(detect_result));
        }

        detect_result.image_width = frame.width;
        detect_result.image_height = frame.height;
        detect_result.pts = frame.pts;
        detect_result.inference_time_ms = (float)inference_time;

        const long long display_start = get_time_us();

        const int display_ret = display_show(
            &frame,
            &detect_result
        );

        const double display_time =
            (get_time_us() - display_start) / 1000.0;

        if(display_ret != 0)
        {
            LOG_ERR("display frame failed");
        }

        /* 统计成功完成推理和后处理的帧 */
        if(inference_ret == 0 && post_ret == 0)
        {
            frame_count++;
        }

        const long long now = get_time_us();

        if(now - last_time >= 1000000)
        {
            fps = frame_count * 1000000.0 / (now - last_time);

            LOG_INFO(
                "detect FPS=%.2f boxes=%d "
                "infer_ret=%d post_ret=%d "
                "preprocess=%.2f ms inference=%.2f ms "
                "postprocess=%.2f ms display=%.2f ms",
                fps,
                detect_result.count,
                inference_ret,
                post_ret,
                preprocess_time,
                inference_time,
                postprocess_time,
                display_time
            );

            static const char *class_names[DETECT_CLASS_NUM] =
            {
                "crazing",
                "inclusion",
                "patches",
                "pitted_surface",
                "rolled-in_scale",
                "scratches"
            };

            /* 每秒打印当前帧检测结果 */
            for(int i = 0; i < detect_result.count; ++i)
            {
                const DetectionBox *box = &detect_result.boxes[i];

                const char *name = "unknown";

                if(box->class_id >= 0 &&
                box->class_id < DETECT_CLASS_NUM)
                {
                    name = class_names[box->class_id];
                }

                LOG_INFO(
                    "box[%d] %s confidence=%.3f "
                    "xyxy=(%.1f, %.1f, %.1f, %.1f)",
                    i,
                    name,
                    box->confidence,
                    box->x1,
                    box->y1,
                    box->x2,
                    box->y2
                );
            }

            frame_count = 0;
            last_time = now;
        }

        /* 全部正常及推理失败路径统一释放 */
        rknn_engine_result_release(&inference_result);
        preprocess_release(&processed_image);
        camera_buffer_release(&frame);
    }

EXIT:


    LOG_INFO(
        "AI application exit"
    );



    g_run_state =
        RUN_STATE_EXIT;





    /*
     * 等待Camera线程退出
     */

    if(camera_thread_created)
    {

        pthread_join(
            camera_tid,
            NULL
        );

    }


    if(display_initialized)
    {
        display_deinit();
    }




    /* 释放可能残留的推理输出 */
    rknn_engine_result_release(&inference_result);

    /* 释放可能残留的摄像头帧副本 */
    camera_buffer_release(&frame);

    preprocess_release(&processed_image);





    /*
     * 关闭V4L2
     */

    if(v4l2_initialized)
    {

        v4l2_deinit();

    }





    /*
     * 释放Camera Buffer
     */

    if(camera_buffer_initialized)
    {

        camera_buffer_deinit();

    }





    /*
     * 释放预处理
     */

    if(preprocess_initialized)
    {

        preprocess_deinit();

    }





    /*
     * 释放RKNN
     */

    if(engine_initialized)
    {

        rknn_engine_release(
            &engine
        );

    }



    LOG_INFO(
        "RK3588 AI Vision stopped"
    );


    return 0;
}

static int run_image_mode(
        const char *model_path,
        const char *image_path)
{
    RKNNEngine engine = {};
    RKNNInferenceResult inference = {};
    ProcessedImage processed = {};
    DetectionResult result = {};

    int width = 0;
    int height = 0;
    int channels = 0;

    /*
     * 强制解码为 RGB 三通道。
     * 即使原始图片是灰度图，也会输出三通道 RGB。
     */
    unsigned char *rgb = stbi_load(
        image_path,
        &width,
        &height,
        &channels,
        3
    );

    if(rgb == NULL)
    {
        const char *reason = stbi_failure_reason();

        LOG_ERR(
            "load image failed: %s reason=%s",
            image_path,
            reason != NULL ? reason : "unknown"
        );
        return 1;
    }

    int status = 1;
    bool engine_ready = false;

    /*
     * 用单次 do/while 统一处理失败清理。
     */
    do
    {
        LOG_INFO(
            "image=%s width=%d height=%d source_channels=%d",
            image_path,
            width,
            height,
            channels
        );

        if(rknn_engine_init(&engine, model_path) != 0)
        {
            break;
        }

        engine_ready = true;

        if(engine.input_width != MODEL_INPUT_WIDTH ||
           engine.input_height != MODEL_INPUT_HEIGHT ||
           engine.input_channel != 3)
        {
            LOG_ERR("image mode requires 640x640 RGB model input");
            break;
        }

        if(preprocess_rgb(
                rgb,
                width,
                height,
                (size_t)width * height * 3,
                &processed) != 0)
        {
            break;
        }

        LOG_INFO(
            "letterbox resized=%dx%d pad=(%d,%d) scale=(%.6f,%.6f)",
            processed.resized_width,
            processed.resized_height,
            processed.pad_left,
            processed.pad_top,
            processed.scale_x,
            processed.scale_y
        );

        const long long start = get_time_us();

        if(rknn_engine_inference(
                &engine,
                processed.rgb,
                MODEL_INPUT_SIZE,
                &inference) != 0)
        {
            break;
        }

        const double inference_ms =
            (get_time_us() - start) / 1000.0;

        if(postprocess_result(
                &engine,
                &inference,
                &processed,
                0,          // 单图没有摄像头时间戳
                0.25f,
                0.45f,
                &result) != 0)
        {
            break;
        }

        result.inference_time_ms = (float)inference_ms;

        static const char *names[DETECT_CLASS_NUM] =
        {
            "crazing",
            "inclusion",
            "patches",
            "pitted_surface",
            "rolled-in_scale",
            "scratches"
        };

        LOG_INFO(
            "image inference success: boxes=%d inference=%.2f ms",
            result.count,
            inference_ms
        );

        /*
         * CSV 格式，坐标属于原始输入图片。
         * PC 对照时也打印相同字段。
         */
        printf("class_id,class_name,confidence,x1,y1,x2,y2\n");

        for(int i = 0; i < result.count; ++i)
        {
            const DetectionBox *box = &result.boxes[i];

            const char *name =
                box->class_id >= 0 &&
                box->class_id < DETECT_CLASS_NUM
                ? names[box->class_id]
                : "unknown";

            printf(
                "%d,%s,%.6f,%.3f,%.3f,%.3f,%.3f\n",
                box->class_id,
                name,
                box->confidence,
                box->x1,
                box->y1,
                box->x2,
                box->y2
            );
        }

        status = 0;
    }
    while(false);

    rknn_engine_result_release(&inference);
    preprocess_release(&processed);
    stbi_image_free(rgb);

    if(engine_ready)
    {
        rknn_engine_release(&engine);
    }

    return status;
}