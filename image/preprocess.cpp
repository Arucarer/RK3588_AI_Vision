#include "preprocess.h"
#include "resize.h"

#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <cmath>

int preprocess_init(void)
{
    return 0;
}

void preprocess_release(ProcessedImage *output)
{
    if(output == NULL)
    {
        return;
    }

    free(output->rgb);
    free(output->gray);
    free(output->edge);

    memset(output, 0, sizeof(*output));
}

int preprocess_run(
        FrameData *frame,
        ProcessedImage *output)
{
    if(frame == NULL || output == NULL)
    {
        LOG_ERR("invalid preprocess parameter");
        return -1;
    }

    /*
     * 调用者必须先初始化，并释放上一帧结果。
     * 不直接 memset，以免覆盖尚未释放的指针。
     */
    if(output->rgb != NULL ||
       output->gray != NULL ||
       output->edge != NULL)
    {
        LOG_ERR("release previous processed image first");
        return -1;
    }

    memset(output, 0, sizeof(*output));

    if(frame->vir_addr == NULL ||
       frame->width <= 0 ||
       frame->height <= 0 ||
       frame->len <= 0 ||
       frame->width % 2 != 0 ||
       frame->height % 2 != 0)
    {
        LOG_ERR("invalid NV12 frame");
        return -1;
    }

    /*
     * 当前支持紧密排列的 NV12：
     * Y 占 width*height 字节，UV 占一半。
     * 长度一致只是基本检查，采集端仍需保证行跨度等于宽度。
     */
    const uint64_t pixels =
        (uint64_t)frame->width * (uint64_t)frame->height;

    const uint64_t expected_bytes = pixels + pixels / 2;

    if((uint64_t)frame->len != expected_bytes)
    {
        LOG_ERR(
            "unsupported NV12 buffer length: actual=%d expected=%llu",
            frame->len,
            (unsigned long long)expected_bytes
        );
        return -1;
    }

    /* 1. 计算等比例缩放尺寸 */
    const double scale_w =
        (double)MODEL_INPUT_WIDTH / frame->width;

    const double scale_h =
        (double)MODEL_INPUT_HEIGHT / frame->height;

    const double scale =
        scale_w < scale_h ? scale_w : scale_h;

    int resized_width =
        (int)(frame->width * scale + 0.5);

    int resized_height =
        (int)(frame->height * scale + 0.5);

    if(resized_width < 1)
    {
        resized_width = 1;
    }

    if(resized_height < 1)
    {
        resized_height = 1;
    }

    if(resized_width > MODEL_INPUT_WIDTH)
    {
        resized_width = MODEL_INPUT_WIDTH;
    }

    if(resized_height > MODEL_INPUT_HEIGHT)
    {
        resized_height = MODEL_INPUT_HEIGHT;
    }

    const int pad_left =
        (MODEL_INPUT_WIDTH - resized_width) / 2;

    const int pad_top =
        (MODEL_INPUT_HEIGHT - resized_height) / 2;

    /* 2. 分配中间图像和最终输入图像 */
    const size_t resized_bytes =
        (size_t)resized_width * resized_height * 3;

    uint8_t *resized_rgb =
        (uint8_t *)malloc(resized_bytes);

    if(resized_rgb == NULL)
    {
        LOG_ERR("malloc resized RGB failed");
        return -1;
    }

    uint8_t *input_rgb =
        (uint8_t *)malloc(MODEL_INPUT_SIZE);

    if(input_rgb == NULL)
    {
        LOG_ERR("malloc model input failed");
        free(resized_rgb);
        return -1;
    }

    /* 3. 使用现有 RGA 接口转换颜色并缩放 */
    if(image_resize_nv12_to_rgb(
            frame,
            resized_rgb,
            resized_width,
            resized_height) != 0)
    {
        LOG_ERR("RGA preprocessing failed");
        free(resized_rgb);
        free(input_rgb);
        return -1;
    }

    /* 4. 灰色补边，将有效 RGB 图像复制到中央 */
    memset(input_rgb, 114, MODEL_INPUT_SIZE);

    const size_t src_row_bytes =
        (size_t)resized_width * 3;

    const size_t dst_row_bytes =
        (size_t)MODEL_INPUT_WIDTH * 3;

    for(int y = 0; y < resized_height; ++y)
    {
        const uint8_t *src_row =
            resized_rgb + (size_t)y * src_row_bytes;

        uint8_t *dst_row =
            input_rgb +
            (size_t)(y + pad_top) * dst_row_bytes +
            (size_t)pad_left * 3;

        memcpy(dst_row, src_row, src_row_bytes);
    }

    free(resized_rgb);

    /* 5. 成功后才交付图像所有权和坐标信息 */
    output->rgb = input_rgb;
    output->rgb_width = MODEL_INPUT_WIDTH;
    output->rgb_height = MODEL_INPUT_HEIGHT;

    output->original_width = frame->width;
    output->original_height = frame->height;

    output->resized_width = resized_width;
    output->resized_height = resized_height;

    output->scale_x =
        (float)resized_width / frame->width;

    output->scale_y =
        (float)resized_height / frame->height;

    output->pad_left = pad_left;
    output->pad_top = pad_top;

    return 0;
}

void preprocess_deinit(void)
{
}

int preprocess_rgb(
        const uint8_t *rgb,
        int width,
        int height,
        size_t rgb_size,
        ProcessedImage *output)
{
    if(rgb == NULL || output == NULL ||
       width <= 0 || height <= 0)
    {
        LOG_ERR("invalid RGB input");
        return -1;
    }

    if(output->rgb != NULL ||
       output->gray != NULL ||
       output->edge != NULL)
    {
        LOG_ERR("release previous processed image first");
        return -1;
    }

    memset(output, 0, sizeof(*output));

    const uint64_t required =
        (uint64_t)width * (uint64_t)height * 3;

    if(required != (uint64_t)rgb_size)
    {
        LOG_ERR("RGB buffer size mismatch");
        return -1;
    }

    const double scale = std::min(
        (double)MODEL_INPUT_WIDTH / width,
        (double)MODEL_INPUT_HEIGHT / height
    );

    const int resized_width = std::max(
        1,
        std::min(
            MODEL_INPUT_WIDTH,
            (int)std::lround(width * scale)
        )
    );

    const int resized_height = std::max(
        1,
        std::min(
            MODEL_INPUT_HEIGHT,
            (int)std::lround(height * scale)
        )
    );

    const int pad_left =
        (MODEL_INPUT_WIDTH - resized_width) / 2;

    const int pad_top =
        (MODEL_INPUT_HEIGHT - resized_height) / 2;

    uint8_t *input =
        (uint8_t *)malloc(MODEL_INPUT_SIZE);

    if(input == NULL)
    {
        LOG_ERR("malloc RGB model input failed");
        return -1;
    }

    memset(input, 114, MODEL_INPUT_SIZE);

    /* 像素中心对齐的双线性缩放 */
    for(int y = 0; y < resized_height; ++y)
    {
        double sy =
            (y + 0.5) * height / resized_height - 0.5;

        sy = std::max(
            0.0,
            std::min(sy, (double)(height - 1))
        );

        const int y0 = (int)std::floor(sy);
        const int y1 = std::min(y0 + 1, height - 1);
        const double wy = sy - y0;

        for(int x = 0; x < resized_width; ++x)
        {
            double sx =
                (x + 0.5) * width / resized_width - 0.5;

            sx = std::max(
                0.0,
                std::min(sx, (double)(width - 1))
            );

            const int x0 = (int)std::floor(sx);
            const int x1 = std::min(x0 + 1, width - 1);
            const double wx = sx - x0;

            const uint8_t *p00 =
                rgb + ((size_t)y0 * width + x0) * 3;
            const uint8_t *p01 =
                rgb + ((size_t)y0 * width + x1) * 3;
            const uint8_t *p10 =
                rgb + ((size_t)y1 * width + x0) * 3;
            const uint8_t *p11 =
                rgb + ((size_t)y1 * width + x1) * 3;

            uint8_t *dst =
                input +
                ((size_t)(y + pad_top) * MODEL_INPUT_WIDTH +
                 x + pad_left) * 3;

            for(int c = 0; c < 3; ++c)
            {
                const double upper =
                    p00[c] * (1.0 - wx) + p01[c] * wx;

                const double lower =
                    p10[c] * (1.0 - wx) + p11[c] * wx;

                const double value =
                    upper * (1.0 - wy) + lower * wy;

                dst[c] = (uint8_t)std::max(
                    0,
                    std::min(255, (int)std::lround(value))
                );
            }
        }
    }

    output->rgb = input;
    output->rgb_width = MODEL_INPUT_WIDTH;
    output->rgb_height = MODEL_INPUT_HEIGHT;

    output->original_width = width;
    output->original_height = height;
    output->resized_width = resized_width;
    output->resized_height = resized_height;

    output->scale_x = (float)resized_width / width;
    output->scale_y = (float)resized_height / height;
    output->pad_left = pad_left;
    output->pad_top = pad_top;

    return 0;
}