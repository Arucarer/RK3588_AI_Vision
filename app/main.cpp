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


volatile sig_atomic_t g_run_state = RUN_STATE_OK;



static void signal_handler(int sig)
{
    (void)sig;

    g_run_state =
        RUN_STATE_EXIT;
}



int main(int argc,char *argv[])
{
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



    float *output_data = NULL;
    int output_size = 0;

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

        frame_count++;
        long long now = get_time_us();


        if(now - last_time >= 1000000)
        {

            fps =
                frame_count * 1000000.0 /
                (now-last_time);


            LOG_INFO(
                "AI FPS = %.2f",
                fps
            );


            frame_count = 0;

            last_time = now;
        }
        if(camera_buffer_pop(
                &frame) != 0)
        {

            continue;

        }


        LOG_INFO(
            "AI process frame index=%d pts=%lld",
            frame.index,
            (long long)frame.pts
        );

        long long preprocess_start = get_time_us();

        if(preprocess_run(
                &frame,
                &processed_image) != 0)
        {
            LOG_ERR(
                "preprocess failed"
            );

            camera_buffer_release(
                &frame
            );

            continue;
        }


        long long preprocess_end = get_time_us();

        double preprocess_time =
            (preprocess_end - preprocess_start)/1000.0;


        /*
         * RK3588 NPU推理
         */

        long long inference_start = get_time_us();

        if(rknn_engine_inference(
                &engine,
                processed_image.rgb,
                640 * 640 * 3,
                &output_data,
                &output_size) != 0)
        {
            LOG_ERR(
                "rknn inference failed"
            );


            if(processed_image.gray)
            {
                free(processed_image.gray);
                processed_image.gray=NULL;
            }


            if(processed_image.edge)
            {
                free(processed_image.edge);
                processed_image.edge=NULL;
            }


            if(processed_image.rgb)
            {
                free(processed_image.rgb);
                processed_image.rgb=NULL;
            }


            continue;
        }

        long long inference_end = get_time_us();


        double inference_time =
            (inference_end - inference_start)/1000.0;



        long long postprocess_start = get_time_us();


        if(postprocess_result(
                output_data,
                output_size,
                DETECT_CLASS_NUM,
                &detect_result) != 0)
        {

            LOG_ERR(
                "postprocess failed"
            );


            free(output_data);

            output_data = NULL;


            camera_buffer_release(
                &frame
            );


            if(processed_image.gray)
            {
                free(processed_image.gray);
                processed_image.gray = NULL;
            }


            if(processed_image.edge)
            {
                free(processed_image.edge);
                processed_image.edge = NULL;
            }


            if(processed_image.rgb)
            {
                free(processed_image.rgb);
                processed_image.rgb = NULL;
            }

            continue;
        }



        /*
        * postprocess之后再赋值
        *
        * 防止被memset清空
        */
        detect_result.inference_time_ms = inference_time;

        display_show(
            &frame,
            &detect_result
        );

        long long postprocess_end = get_time_us();


        double postprocess_time =
            (postprocess_end - postprocess_start)/1000.0;



        /*
         * 输出检测结果
         */
        LOG_INFO(
            "detect result:"
            " class=%d"
            " confidence=%.3f"
            " valid=%d"
            " inference=%.2f ms"
            " preprocess=%.2f ms"
            " postprocess=%.2f ms",
            detect_result.class_id,
            detect_result.confidence,
            detect_result.valid,
            detect_result.inference_time_ms,
            preprocess_time,
            postprocess_time
        );


        free(output_data);

        output_data = NULL;


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


    /*
     * 释放输出buffer
     */

    if(output_data)
    {

        free(output_data);

        output_data = NULL;

    }

    if(processed_image.gray)
    {
        free(processed_image.gray);
    }


    if(processed_image.edge)
    {
        free(processed_image.edge);
    }


    if(processed_image.rgb)
    {
        free(processed_image.rgb);
    }





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