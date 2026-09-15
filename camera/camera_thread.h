/*
 * @file    camera_thread.h
 * @brief   定义RK3588 AI视觉检测项目中的摄像头采集线程接口
 * @author  zhangyong
 * @date    2026-09-13
 * @version 1.0
 *
 * @note
 * 该文件主要定义摄像头采集线程入口，
 * 用于持续获取V4L2图像并写入Camera Buffer。
 */

#ifndef CAMERA_THREAD_H
#define CAMERA_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif


void *camera_thread(void *arg);


#ifdef __cplusplus
}
#endif

#endif