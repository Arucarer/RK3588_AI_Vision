#include "v4l2_capture.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <limits.h>

#define BUF_COUNT 4

static int g_v4l2_fd = -1;
static V4l2Buffer g_bufs[BUF_COUNT];
static unsigned int g_buf_count = 0;
static int g_frame_width = 0;
static int g_frame_height = 0;
static size_t g_frame_size = 0;


static int configure_frame_rate(int fd, int fps)
{
    struct v4l2_streamparm parameters;
    memset(&parameters, 0, sizeof(parameters));

    parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    if(fps > 0)
    {
        parameters.parm.capture.timeperframe.numerator = 1;
        parameters.parm.capture.timeperframe.denominator = (unsigned int)fps;

        if(ioctl(fd, VIDIOC_S_PARM, &parameters) < 0)
        {
            LOG_ERR("cannot set camera fps=%d on capture node: %s",
                    fps, strerror(errno));
            return -1;
        }
    }

    memset(&parameters, 0, sizeof(parameters));
    parameters.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    if(ioctl(fd, VIDIOC_G_PARM, &parameters) < 0)
    {
        if(fps > 0)
        {
            LOG_ERR("cannot verify camera frame interval: %s", strerror(errno));
            return -1;
        }

        LOG_INFO("camera frame interval unavailable; keeping driver setting");
        return 0;
    }

    const unsigned int numerator =
        parameters.parm.capture.timeperframe.numerator;

    const unsigned int denominator =
        parameters.parm.capture.timeperframe.denominator;

    if(numerator == 0 || denominator == 0)
    {
        if(fps > 0)
        {
            LOG_ERR("driver returned an invalid camera frame interval");
            return -1;
        }

        LOG_INFO("camera frame interval unspecified by driver");
        return 0;
    }

    LOG_INFO("camera driver frame interval=%u/%u seconds, nominal fps=%.3f",
             numerator, denominator,
             (double)denominator / numerator);

    if(fps > 0 &&
       (unsigned long long)denominator !=
           (unsigned long long)numerator * (unsigned int)fps)
    {
        LOG_ERR("camera fps differs from requested fps=%d", fps);
        return -1;
    }

    return 0;
}

// V4L2初始化
int v4l2_init(const char *device, int width, int height, int fps)
{
    if(g_v4l2_fd >= 0)
    {
        LOG_ERR("camera already initialized");
        return -1;
    }

    if(device == NULL || device[0] == '\0' ||
       width < 16 || width > 8192 ||
       height < 2 || height > 8192 ||
       width % 16 != 0 || height % 2 != 0 ||
       fps < 0 || fps > 120)
    {
        LOG_ERR("invalid camera configuration");
        return -1;
    }

    g_frame_width = width;
    g_frame_height = height;
    g_frame_size = (size_t)width * (size_t)height * 3 / 2;

    int ret;

    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;

    // 初始化buffer状态
    for(int i = 0; i < BUF_COUNT; i++)
    {
        g_bufs[i].start = NULL;
        g_bufs[i].length = 0;
        g_bufs[i].dma_fd = -1;
    }

    // #1.打开摄像头

    g_v4l2_fd = open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC);

    if(g_v4l2_fd < 0)
    {
        LOG_ERR("open camera %s failed: %s", device, strerror(errno));
        return -1;
    }

    // 查询设备能力

    memset(&cap, 0, sizeof(cap));
    ret = ioctl(g_v4l2_fd, VIDIOC_QUERYCAP, &cap);

    if(ret < 0)
    {
        LOG_ERR("query cap failed");
        goto ERROR;
    }

    LOG_INFO("camera driver:%s card:%s", cap.driver, cap.card);
    const unsigned int capabilities =
        (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
            ? cap.device_caps
            : cap.capabilities;

    if(!(capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) ||
    !(capabilities & V4L2_CAP_STREAMING))
    {
        LOG_ERR("camera requires MPLANE capture and streaming support");
        goto ERROR;
    }
    // #2.设置NV12格式

    memset(&fmt, 0, sizeof(fmt));

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;  //设置多平面格式
    fmt.fmt.pix_mp.width = (unsigned int)width;
    fmt.fmt.pix_mp.height = (unsigned int)height;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12; //设置摄像头输出的格式为NV12
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;         //设置帧为连续模式

    ret = ioctl(g_v4l2_fd, VIDIOC_S_FMT, &fmt); //设置格式

    if(ret < 0)
    {
        LOG_ERR("set format failed");
        goto ERROR;
    }

    LOG_INFO("format:%dx%d planes:%d", fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
             fmt.fmt.pix_mp.num_planes);

    /* 当前预处理只支持固定尺寸、紧密排列的单平面 NV12。 不接受驱动静默调整为其他格式。 */
    /* 必须使用请求的尺寸和紧密排列、单内存平面的 NV12。 */
    if(fmt.fmt.pix_mp.width != (unsigned int)width ||
    fmt.fmt.pix_mp.height != (unsigned int)height ||
    fmt.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV12 ||
    fmt.fmt.pix_mp.num_planes != 1 ||
    fmt.fmt.pix_mp.plane_fmt[0].bytesperline != (unsigned int)width ||
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage < g_frame_size)
    {
        LOG_ERR("unsupported camera format: size=%ux%u stride=%u sizeimage=%u",
                fmt.fmt.pix_mp.width,
                fmt.fmt.pix_mp.height,
                fmt.fmt.pix_mp.plane_fmt[0].bytesperline,
                fmt.fmt.pix_mp.plane_fmt[0].sizeimage);
        goto ERROR;
    }

    if(configure_frame_rate(g_v4l2_fd, fps) != 0)
    {
        goto ERROR;
    }

    // #3.申请buffer

    memset(&req, 0, sizeof(req));

    req.count = BUF_COUNT;

    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    req.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(g_v4l2_fd, VIDIOC_REQBUFS, &req);

    if(ret < 0)
    {
        LOG_ERR("request buffer failed");
        goto ERROR;
    }

    //防止驱动返回的 buffer 数量超过#define BUF_COUNT 4
    g_buf_count = req.count > BUF_COUNT ? BUF_COUNT : req.count;
    if(g_buf_count == 0)
    {
        LOG_ERR("driver allocated no camera buffers");
        goto ERROR;
    }
    // QUERYBUF + MMAP
    // 获取buffer,遍历初始化每一个buffer
    for(unsigned int i = 0; i < g_buf_count; i++)
    {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[VIDEO_MAX_PLANES];

        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = VIDEO_MAX_PLANES;
        buf.m.planes = planes;

        ret = ioctl(g_v4l2_fd, VIDIOC_QUERYBUF, &buf);

        if(ret < 0)
        {
            LOG_ERR("query buffer failed");
            goto ERROR;
        }

        if(planes[0].length < g_frame_size)
        {
            LOG_ERR("camera buffer too small: size=%u required=%zu",
                    planes[0].length, g_frame_size);
            goto ERROR;
        }
        g_bufs[i].length = planes[0].length;

        g_bufs[i].start = mmap(NULL, planes[0].length, PROT_READ | PROT_WRITE, MAP_SHARED,
                               g_v4l2_fd, planes[0].m.mem_offset);

        if(g_bufs[i].start == MAP_FAILED)
        {
            LOG_ERR("mmap failed");
            goto ERROR;
        }

        // 导出DMA fd 给RGA使用

        struct v4l2_exportbuffer expbuf;

        memset(&expbuf, 0, sizeof(expbuf));

        expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        expbuf.index = i;
        expbuf.plane = 0;
        expbuf.flags = O_CLOEXEC;

        ret = ioctl(g_v4l2_fd, VIDIOC_EXPBUF,
                    &expbuf); //这个是导出DMA fd的关键ioctl操作，expbuf.fd就是导出的DMA fd

        if(ret < 0)
        {
            LOG_ERR("export dma failed");
            goto ERROR;
        }

        g_bufs[i].dma_fd = expbuf.fd;

        // 放回驱动队列

        ret = ioctl(g_v4l2_fd, VIDIOC_QBUF, &buf);
        if(ret < 0)
        {
            LOG_ERR("queue buffer failed");
            goto ERROR;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE; //设置多平面格式

    ret = ioctl(g_v4l2_fd, VIDIOC_STREAMON, &type);
    if(ret < 0)
    {
        LOG_ERR("stream on failed");
        goto ERROR;
    }

    LOG_INFO("v4l2 init success");
    return 0;

ERROR:
    v4l2_deinit();
    return -1;
}

/*
 * 返回：
 * 0：获得一帧
 * 1：暂时无帧或等待被信号打断
 * -1：采集错误
 */
int v4l2_capture_frame(FrameData *frame)
{
    if(frame == NULL || g_v4l2_fd < 0)
    {
        return -1;
    }

    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));

    pfd.fd = g_v4l2_fd;
    pfd.events = POLLIN;

    const int ready = poll(&pfd, 1, 200);

    if(ready == 0)
    {
        return 1;
    }

    if(ready < 0)
    {
        if(errno == EINTR)
        {
            return 1;
        }

        LOG_ERR("camera poll failed: %s", strerror(errno));
        return -1;
    }

    if(pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
    {
        LOG_ERR("camera poll error events=0x%x", pfd.revents);
        return -1;
    }

    if(!(pfd.revents & POLLIN))
    {
        return 1;
    }

    struct v4l2_buffer buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];

    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.length = VIDEO_MAX_PLANES;
    buf.m.planes = planes;

    if(ioctl(g_v4l2_fd, VIDIOC_DQBUF, &buf) < 0)
    {
        if(errno == EAGAIN || errno == EINTR)
        {
            return 1;
        }

        LOG_ERR("camera dequeue failed: %s", strerror(errno));
        return -1;
    }

    if(buf.index >= g_buf_count)
    {
        LOG_ERR("invalid camera buffer index=%u", buf.index);
        return -1;
    }

    if((buf.flags & V4L2_BUF_FLAG_ERROR) || planes[0].data_offset != 0 ||
       (size_t)planes[0].bytesused != g_frame_size || planes[0].bytesused > g_bufs[buf.index].length ||
       planes[0].bytesused > INT_MAX)
    {
        LOG_ERR("invalid camera frame: bytes=%u offset=%u flags=0x%x", planes[0].bytesused,
                planes[0].data_offset, buf.flags);

        /* 已经出队的缓冲区必须归还 */
        v4l2_release_frame((int)buf.index);
        return -1;
    }

    memset(frame, 0, sizeof(*frame));

    frame->vir_addr = g_bufs[buf.index].start;
    frame->dma_fd = g_bufs[buf.index].dma_fd;
    frame->index = (int)buf.index;
    frame->width = g_frame_width;
    frame->height = g_frame_height;
    frame->len = (int)planes[0].bytesused;
    frame->pts = get_time_us();

    return 0;
}

// 释放buffer
int v4l2_release_frame(int index)
{
    if(index < 0 || (unsigned int)index >= g_buf_count)
    {
        LOG_ERR("invalid buffer index:%d", index);
        return -1;
    }

    struct v4l2_buffer buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];

    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    buf.length = VIDEO_MAX_PLANES;
    buf.m.planes = planes;

    return ioctl(g_v4l2_fd, VIDIOC_QBUF, &buf);
}

// 释放资源
void v4l2_deinit()
{
    if(g_v4l2_fd < 0)
        return;

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    ioctl(g_v4l2_fd, VIDIOC_STREAMOFF, &type);

    for(unsigned int i = 0; i < g_buf_count; i++)
    {
        if(g_bufs[i].start != NULL && g_bufs[i].start != MAP_FAILED)
        {
            munmap(g_bufs[i].start, g_bufs[i].length);
            g_bufs[i].start = NULL;
        }

        if(g_bufs[i].dma_fd >= 0)
        {
            close(g_bufs[i].dma_fd);
            g_bufs[i].dma_fd = -1;
        }
    }

    close(g_v4l2_fd);

    g_v4l2_fd = -1;
    g_buf_count = 0;

    LOG_INFO("v4l2 closed");
}
