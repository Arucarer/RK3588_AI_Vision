/* ROI区域裁剪实现 */

#include "roi.h"

#include <string.h>

int roi_crop_gray(uint8_t *src, uint8_t *dst, int src_width, int src_height, ROIRegion *roi)
{
    if(src == NULL || dst == NULL || roi == NULL)
    {
        return -1;
    }

    /* 边界检查 */

    if(roi->x < 0 || roi->y < 0 || roi->x + roi->width > src_width ||
       roi->y + roi->height > src_height)
    {
        return -1;
    }

    for(int y = 0; y < roi->height; y++)
    {
        memcpy(dst + y * roi->width, src + (roi->y + y) * src_width + roi->x,

               roi->width);
    }

    return 0;
}
