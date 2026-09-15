#ifndef DISPLAY_H
#define DISPLAY_H


#include "common.h"
#include "detection_result.h"


#ifdef __cplusplus
extern "C"
{
#endif


int display_init();


int display_show(
        FrameData *frame,
        DetectionResult *result);


void display_deinit();


#ifdef __cplusplus
}
#endif


#endif