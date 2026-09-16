/*
 * @file    resize.cpp
 * @brief   实现RK3588 AI视觉检测项目中的RGA图像缩放与颜色转换
 * @author  zhangyong
 * @date    2026-09-13
 * @version 1.0
 *
 * @note
 * 该文件主要实现：
 * 1. V4L2 DMA-BUF导入RGA
 * 2. NV12图像硬件缩放
 * 3. NV12到RGB888颜色转换
 * 4. RGA参数检查
 *
 * 当前数据流程：
 *
 * 3840×2160 NV12 DMA-BUF
 *          ↓
 *         RGA
 *          ↓
 * Resize + NV12→RGB888
 *          ↓
 * 640×360 RGB
 */

#include "resize.h"
#include "common.h"

#include "im2d.h"
#include "RgaUtils.h"


int image_resize_nv12_to_rgb(
    const FrameData *src,
    uint8_t *dst_rgb,
    int dst_width,
    int dst_height)
{
    IM_STATUS status;


    if(src == NULL ||
       dst_rgb == NULL)
    {
        LOG_ERR("invalid resize parameter");

        return -1;
    }


    /*
    * 当前使用 wrapbuffer_virtualaddr()，
    * 数据来自摄像头帧的内存副本。
    */
    if(src->vir_addr == NULL)
    {
        LOG_ERR("source virtual address is NULL");
        return -1;
    }


    if(src->width <= 0 ||
       src->height <= 0 ||
       dst_width <= 0 ||
       dst_height <= 0)
    {
        LOG_ERR(
            "invalid image size src=%dx%d dst=%dx%d",
            src->width,
            src->height,
            dst_width,
            dst_height);

        return -1;
    }


    /*
     * V4L2 NV12 DMA-BUF
     */
    rga_buffer_t src_buffer =
        wrapbuffer_virtualaddr(
            src->vir_addr,
            src->width,
            src->height,
            RK_FORMAT_YCbCr_420_SP);


    /*
     * CPU虚拟地址RGB888 Buffer
     */
    rga_buffer_t dst_buffer =
        wrapbuffer_virtualaddr(
            dst_rgb,
            dst_width,
            dst_height,
            RK_FORMAT_RGB_888);


    status = imcheck(
        src_buffer,
        dst_buffer,
        {},
        {});


    if(status != IM_STATUS_NOERROR)
    {
        LOG_ERR(
            "rga imcheck failed: %s",
            imStrError(status));

        return -1;
    }


    /*
     * RGA会根据src/dst尺寸自动缩放，
     * 同时根据src/dst格式完成NV12→RGB888。
     */
    status = imresize(
        src_buffer,
        dst_buffer);


    if(status != IM_STATUS_SUCCESS)
    {
        LOG_ERR(
            "rga resize failed: %s",
            imStrError(status));

        return -1;
    }


    LOG_DEBUG(
        "RGA success %dx%d NV12 -> %dx%d RGB",
        src->width,
        src->height,
        dst_width,
        dst_height);


    return 0;
}