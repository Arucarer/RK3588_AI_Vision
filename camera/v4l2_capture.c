#include "v4l2_capture.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define BUF_COUNT 4

static int g_v4l2_fd = -1;
static V4l2Buffer g_bufs[BUF_COUNT];
static unsigned int g_buf_count = 0;


// ==================== V4L2初始化 ====================
int v4l2_init()
{
    int ret;

    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;

    // 初始化buffer状态
    for(int i=0;i<BUF_COUNT;i++)
    {
        g_bufs[i].start = NULL;
        g_bufs[i].length = 0;
        g_bufs[i].dma_fd = -1;
    }


    // #1.打开摄像头

    g_v4l2_fd = open(V4L2_DEV, O_RDWR);
    if(g_v4l2_fd < 0){
        LOG_ERR("open camera failed");
        return -1;
    }


    // 查询设备能力

    memset(&cap,0,sizeof(cap));
    ret = ioctl( g_v4l2_fd, VIDIOC_QUERYCAP, &cap);

    if(ret < 0){
        LOG_ERR("query cap failed");
        goto ERROR;
    }


    LOG_INFO("camera driver:%s card:%s", cap.driver, cap.card);



    // #2.设置NV12格式

    memset(&fmt,0,sizeof(fmt));

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;//设置多平面格式
    fmt.fmt.pix_mp.width = VIDEO_WIDTH; //设置摄像头输出的分辨率
    fmt.fmt.pix_mp.height = VIDEO_HEIGHT; //设置摄像头输出的分辨率
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;//设置摄像头输出的格式为NV12
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE; //设置帧为连续模式

    ret = ioctl( g_v4l2_fd, VIDIOC_S_FMT, &fmt); //设置格式

    if(ret < 0){
        LOG_ERR("set format failed");
        goto ERROR;
    }


    LOG_INFO("format:%dx%d planes:%d",
             fmt.fmt.pix_mp.width,
             fmt.fmt.pix_mp.height,
             fmt.fmt.pix_mp.num_planes);



    // #3.申请buffer

    memset(&req,0,sizeof(req));

    req.count = BUF_COUNT;

    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    req.memory = V4L2_MEMORY_MMAP;


    ret = ioctl( g_v4l2_fd, VIDIOC_REQBUFS, &req);

    if(ret < 0){
        LOG_ERR("request buffer failed");
        goto ERROR;
    }

    //防止驱动返回的 buffer 数量超过#define BUF_COUNT 4
    g_buf_count = req.count > BUF_COUNT ? BUF_COUNT : req.count;



    // QUERYBUF + MMAP
    // 获取buffer,遍历初始化每一个buffer
    for(unsigned int i = 0; i < g_buf_count; i++)
    {
        struct v4l2_buffer buf;
        struct v4l2_plane planes[VIDEO_MAX_PLANES];


        memset(&buf,0,sizeof(buf));
        memset(planes,0,sizeof(planes));


        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.length = VIDEO_MAX_PLANES;
        buf.m.planes = planes;



        ret = ioctl( g_v4l2_fd, VIDIOC_QUERYBUF, &buf);

        if(ret < 0){
            LOG_ERR("query buffer failed");
            goto ERROR;
        }


        g_bufs[i].length = planes[0].length;


        g_bufs[i].start = mmap(
            NULL,
            planes[0].length,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            g_v4l2_fd,
            planes[0].m.mem_offset);


        if(g_bufs[i].start == MAP_FAILED){
            LOG_ERR("mmap failed");
            goto ERROR;
        }



        // 导出DMA fd 给RGA使用

        struct v4l2_exportbuffer expbuf;


        memset(&expbuf,0,sizeof(expbuf));


        expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        expbuf.index = i;
        expbuf.plane = 0;
        expbuf.flags = O_CLOEXEC;


        ret = ioctl( g_v4l2_fd, VIDIOC_EXPBUF, &expbuf);//这个是导出DMA fd的关键ioctl操作，expbuf.fd就是导出的DMA fd

        if(ret < 0){
            LOG_ERR("export dma failed");
            goto ERROR;
        }


        g_bufs[i].dma_fd = expbuf.fd;



        // 放回驱动队列

        ret = ioctl( g_v4l2_fd, VIDIOC_QBUF, &buf);
        if(ret < 0){
            LOG_ERR("queue buffer failed");
            goto ERROR;
        }

    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;//设置多平面格式


    ret = ioctl( g_v4l2_fd, VIDIOC_STREAMON, &type);
    if(ret < 0){
        LOG_ERR("stream on failed");
        goto ERROR;
    }

    LOG_INFO("v4l2 init success");
    return 0;

    ERROR:
        v4l2_deinit();
        return -1;
}

// ==================== 获取一帧 ====================
int v4l2_capture_frame(FrameData *frame)
{
    struct v4l2_buffer buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];

    memset(&buf,0,sizeof(buf));
    memset(planes,0,sizeof(planes));


    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.length = VIDEO_MAX_PLANES;
    buf.m.planes = planes;


    //如果获取失败，则返回-1
    if(ioctl( g_v4l2_fd, VIDIOC_DQBUF, &buf) < 0){
        return -1;
    }

    if(buf.index >= g_buf_count){
        LOG_ERR("invalid buffer index:%u", buf.index);
        return -1;
    }

    frame->vir_addr = g_bufs[buf.index].start;     // V4L2 MMAP虚拟地址
    frame->dma_fd = g_bufs[buf.index].dma_fd;      // DMA-BUF fd，后续给RGA使用

    frame->index = buf.index;                      // V4L2 Buffer索引

    frame->width = VIDEO_WIDTH;                    // 原始图像宽度
    frame->height = VIDEO_HEIGHT;                  // 原始图像高度

    frame->len = planes[0].bytesused;              // 当前帧有效数据长度
    frame->pts = get_time_us();               // 当前帧时间戳


    return 0;
}


// ==================== 释放buffer ====================
int v4l2_release_frame(int index)
{
    if(index < 0 || (unsigned int)index >= g_buf_count){
        LOG_ERR("invalid buffer index:%d", index);
        return -1;
    }

    struct v4l2_buffer buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];

    memset(&buf,0,sizeof(buf));
    memset(planes,0,sizeof(planes));

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;
    buf.length = VIDEO_MAX_PLANES;
    buf.m.planes = planes;

    return ioctl(g_v4l2_fd, VIDIOC_QBUF, &buf);
}


// ==================== 释放资源 ====================

void v4l2_deinit()
{
    if(g_v4l2_fd < 0)
        return;


    enum v4l2_buf_type type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;


    ioctl( g_v4l2_fd, VIDIOC_STREAMOFF, &type);


    for(unsigned int i = 0; i < g_buf_count; i++)
    {

        if(g_bufs[i].start){
            munmap(g_bufs[i].start,g_bufs[i].length);
            g_bufs[i].start = NULL;
        }


        if(g_bufs[i].dma_fd >= 0){
            close(g_bufs[i].dma_fd);
            g_bufs[i].dma_fd = -1;
        }

    }

    close(g_v4l2_fd);

    g_v4l2_fd = -1;
    g_buf_count = 0;

    LOG_INFO("v4l2 closed");
}