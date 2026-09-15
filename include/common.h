/*
 * @file    common.h
 * @brief   定义RK3588 AI视觉检测项目中的公共配置与基础数据结构
 * @author  zhangyong
 * @date    2026-09-11
 * @version 1.0
 *
 * @note
 * 该文件主要包含：
 * 1. 系统公共宏定义
 * 2. 全局运行状态
 * 3. 日志接口
 * 4. 时间戳函数
 * 5. V4L2 DMA-BUF图像帧数据结构
 *
 * 用于V4L2采集、RGA预处理以及RKNN推理模块之间的数据传递。
 */

#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <stdint.h>
#include <signal.h>


// ==================== 视频参数 ====================

#define VIDEO_WIDTH 3840

#define VIDEO_HEIGHT 2160

#define VIDEO_FPS 30

#define VIDEO_FRAME_SIZE \
    (VIDEO_WIDTH * VIDEO_HEIGHT * 3 / 2)


// ==================== 程序运行状态 ====================

typedef enum
{
    RUN_STATE_OK = 0,       // 正常运行

    RUN_STATE_EXIT,         // 程序退出

    RUN_STATE_ERROR         // 程序错误

}RunState;


// 全局运行状态
extern volatile sig_atomic_t g_run_state;


// ==================== 图像帧数据 ====================

typedef struct
{

    int index;

    void *vir_addr;

    int dma_fd;

    int len;

    int width;

    int height;

    long long pts;


}FrameData;

// ==================== 时间戳 ====================

static inline long long get_time_us()
{
    struct timeval tv;

    gettimeofday(
        &tv,
        NULL
    );

    return (long long)tv.tv_sec * 1000000 +
           tv.tv_usec;
}


// ==================== 日志 ====================

#define LOG_INFO(fmt, ...) \
printf("[INFO] %s:%d " fmt "\n", \
       __FILE__, \
       __LINE__, \
       ##__VA_ARGS__)


#define LOG_ERR(fmt, ...) \
printf("[ERROR] %s:%d " fmt "\n", \
       __FILE__, \
       __LINE__, \
       ##__VA_ARGS__)


#endif