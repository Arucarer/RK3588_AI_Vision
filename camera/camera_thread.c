#include "camera_thread.h"
#include "common.h"
#include "v4l2_capture.h"
#include "camera_buffer.h"

#include <stddef.h>

void *camera_thread(void *arg)
{
    (void)arg;

    while(g_run_state == RUN_STATE_OK)
    {
        FrameData frame;
        memset(&frame, 0, sizeof(frame));

        const int capture_ret = v4l2_capture_frame(&frame);

        if(capture_ret == 1)
        {
            /* 暂时无帧，回到循环检查退出状态 */
            continue;
        }

        if(capture_ret < 0)
        {
            LOG_ERR("camera capture stopped after error");
            break;
        }

        if(g_run_state != RUN_STATE_OK)
        {
            v4l2_release_frame(frame.index);
            break;
        }

        const int push_ret = camera_buffer_push(&frame);

        /* 无论复制成功与否，驱动缓冲区都只归还一次 */
        const int release_ret = v4l2_release_frame(frame.index);

        if(release_ret != 0)
        {
            LOG_ERR("return camera buffer failed");
            break;
        }

        if(push_ret != 0)
        {
            if(g_run_state == RUN_STATE_OK)
            {
                LOG_ERR("copy camera frame failed");
            }

            break;
        }
    }

    /* 通知主线程采集已停止。 这里是普通线程上下文，可以使用互斥锁、条件变量。 */
    camera_buffer_stop();

    LOG_INFO("camera thread stopped");
    return NULL;
}
