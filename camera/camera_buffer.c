#include "camera_buffer.h"

#include "v4l2_capture.h"

#include <stdlib.h>
#include <string.h>


static FrameData g_latest_frame;


static int g_frame_ready = 0;


static pthread_mutex_t g_mutex =
PTHREAD_MUTEX_INITIALIZER;


static pthread_cond_t g_cond =
PTHREAD_COND_INITIALIZER;



int camera_buffer_init()
{
    memset(
        &g_latest_frame,
        0,
        sizeof(FrameData)
    );


    g_frame_ready = 0;


    return 0;
}


int camera_buffer_push(FrameData *frame)
{
    pthread_mutex_lock(&g_mutex);


    /*
     * 如果已经存在旧AI帧
     * 释放旧copy数据
     */
    if(g_frame_ready)
    {

        if(g_latest_frame.vir_addr)
        {
            free(
                g_latest_frame.vir_addr
            );

            g_latest_frame.vir_addr = NULL;
        }

    }



    /*
     * 分配AI私有NV12 Buffer
     */
    g_latest_frame.vir_addr =
        malloc(frame->len);


    if(g_latest_frame.vir_addr == NULL)
    {
        pthread_mutex_unlock(&g_mutex);

        return -1;
    }



    /*
     * 复制NV12数据
     */
    memcpy(
        g_latest_frame.vir_addr,
        frame->vir_addr,
        frame->len
    );



    /*
     * 保存图像信息
     */
    g_latest_frame.len =
        frame->len;


    g_latest_frame.width =
        frame->width;


    g_latest_frame.height =
        frame->height;


    g_latest_frame.pts =
        frame->pts;


    /*
     * 注意:
     *
     * copy之后
     * V4L2 buffer可以立即归还
     *
     */
    v4l2_release_frame(
        frame->index
    );
    frame->index = -1;

    g_frame_ready = 1;



    LOG_INFO(
        "camera push AI frame pts=%lld",
        (long long)frame->pts
    );


    pthread_cond_signal(
        &g_cond
    );


    pthread_mutex_unlock(
        &g_mutex
    );


    return 0;
}



int camera_buffer_pop(FrameData *frame)
{

    pthread_mutex_lock(
        &g_mutex
    );



    while(!g_frame_ready)
    {

        pthread_cond_wait(
            &g_cond,
            &g_mutex
        );

    }



    /*
     * 返回AI私有Buffer
     */
    *frame =
        g_latest_frame;



    g_latest_frame.vir_addr = NULL;



    g_frame_ready = 0;



    LOG_INFO(
        "AI get frame pts=%lld",
        (long long)frame->pts
    );



    pthread_mutex_unlock(
        &g_mutex
    );



    return 0;
}






int camera_buffer_release(FrameData *frame)
{
    if(frame == NULL)
    {
        return -1;
    }


    if(frame->vir_addr)
    {
        free(frame->vir_addr);

        frame->vir_addr = NULL;
    }


    return 0;
}





void camera_buffer_deinit()
{

    pthread_mutex_lock(
        &g_mutex
    );


    if(g_latest_frame.vir_addr)
    {

        free(
            g_latest_frame.vir_addr
        );


        g_latest_frame.vir_addr=NULL;

    }



    pthread_mutex_unlock(
        &g_mutex
    );

}