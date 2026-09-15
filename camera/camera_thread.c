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


        /*
         * V4L2获取一帧
         */
        if(v4l2_capture_frame(&frame) < 0)
        {
            continue;
        }



        LOG_INFO(
            "camera frame index=%d len=%d pts=%lld",
            frame.index,
            frame.len,
            (long long)frame.pts
        );



        /*
         * 推送到Latest Frame Buffer
         *
         * 内部:
         *
         * memcpy NV12
         *
         * QBUF释放V4L2
         */
        if(camera_buffer_push(&frame)<0)
        {
            LOG_ERR(
                "camera buffer push failed"
            );


            v4l2_release_frame(
                frame.index
            );


            continue;
        }

    }


    return NULL;
}