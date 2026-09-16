#ifndef DRM_DISPLAY_H
#define DRM_DISPLAY_H

#include <stdint.h>

int drm_display_init();

int drm_display_frame(uint8_t *rgb, int width, int height);

void drm_display_deinit();

#endif
