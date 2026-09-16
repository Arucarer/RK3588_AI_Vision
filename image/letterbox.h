/* RK3588 AI视觉Letterbox处理接口 */

#ifndef LETTERBOX_H
#define LETTERBOX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define LETTERBOX_SIZE 640

typedef struct
{
    float scale;

    int pad_x;

    int pad_y;
} LetterboxInfo;

int letterbox_resize(uint8_t *src_rgb, int src_width, int src_height, uint8_t *dst_rgb,
                     int dst_width, int dst_height, LetterboxInfo *info);

#ifdef __cplusplus
}
#endif

#endif
