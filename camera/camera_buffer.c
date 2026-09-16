#include "camera_buffer.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static FrameData g_latest_frame;
static int g_frame_ready = 0;
static int g_stopped = 0;
static int g_cond_initialized = 0;

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond;

int camera_buffer_init(void)
{
    pthread_condattr_t attr;

    if(pthread_condattr_init(&attr) != 0)
    {
        return -1;
    }

    if(pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) != 0)
    {
        pthread_condattr_destroy(&attr);
        return -1;
    }

    int ret = pthread_cond_init(&g_cond, &attr);
    pthread_condattr_destroy(&attr);

    if(ret != 0)
    {
        return -1;
    }

    g_cond_initialized = 1;

    memset(&g_latest_frame, 0, sizeof(g_latest_frame));
    g_frame_ready = 0;
    g_stopped = 0;

    return 0;
}

/* 只复制图像。 成功或失败后，调用者均负责归还 V4L2 缓冲区。 */
int camera_buffer_push(FrameData *frame)
{
    if(frame == NULL || frame->vir_addr == NULL || frame->len <= 0)
    {
        return -1;
    }

    void *copy = malloc((size_t)frame->len);

    if(copy == NULL)
    {
        return -1;
    }

    memcpy(copy, frame->vir_addr, (size_t)frame->len);

    pthread_mutex_lock(&g_mutex);

    if(g_stopped)
    {
        pthread_mutex_unlock(&g_mutex);
        free(copy);
        return -1;
    }

    free(g_latest_frame.vir_addr);

    g_latest_frame = *frame;
    g_latest_frame.vir_addr = copy;

    /* 这是内存副本，不再代表驱动缓冲区 */
    g_latest_frame.index = -1;
    g_latest_frame.dma_fd = -1;

    g_frame_ready = 1;

    pthread_cond_signal(&g_cond);
    pthread_mutex_unlock(&g_mutex);

    return 0;
}

int camera_buffer_pop(FrameData *frame)
{
    if(frame == NULL)
    {
        return -1;
    }

    struct timespec deadline;

    if(clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
    {
        return -1;
    }

    deadline.tv_nsec += 200000000L;

    if(deadline.tv_nsec >= 1000000000L)
    {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&g_mutex);

    while(!g_frame_ready && !g_stopped)
    {
        const int ret = pthread_cond_timedwait(&g_cond, &g_mutex, &deadline);

        if(ret == ETIMEDOUT)
        {
            /* 超时与新帧到达可能同时发生，再检查状态 */
            if(!g_frame_ready && !g_stopped)
            {
                pthread_mutex_unlock(&g_mutex);
                return 1;
            }
        }
        else if(ret != 0)
        {
            pthread_mutex_unlock(&g_mutex);
            return -1;
        }
    }

    if(g_stopped)
    {
        pthread_mutex_unlock(&g_mutex);
        return -1;
    }

    *frame = g_latest_frame;

    memset(&g_latest_frame, 0, sizeof(g_latest_frame));
    g_frame_ready = 0;

    pthread_mutex_unlock(&g_mutex);
    return 0;
}

void camera_buffer_stop(void)
{
    pthread_mutex_lock(&g_mutex);

    g_stopped = 1;

    if(g_cond_initialized)
    {
        pthread_cond_broadcast(&g_cond);
    }

    pthread_mutex_unlock(&g_mutex);
}

int camera_buffer_release(FrameData *frame)
{
    if(frame == NULL)
    {
        return -1;
    }

    free(frame->vir_addr);
    memset(frame, 0, sizeof(*frame));

    frame->index = -1;
    frame->dma_fd = -1;

    return 0;
}

/* 必须在采集线程退出、主线程不再 pop 后调用 */
void camera_buffer_deinit(void)
{
    camera_buffer_stop();

    pthread_mutex_lock(&g_mutex);

    free(g_latest_frame.vir_addr);
    memset(&g_latest_frame, 0, sizeof(g_latest_frame));
    g_frame_ready = 0;

    pthread_mutex_unlock(&g_mutex);

    if(g_cond_initialized)
    {
        pthread_cond_destroy(&g_cond);
        g_cond_initialized = 0;
    }
}
