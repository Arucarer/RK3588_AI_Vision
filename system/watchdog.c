#define _POSIX_C_SOURCE 200809L

#include "watchdog.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define WATCHDOG_STAGE_SIZE 64
#define WATCHDOG_EXIT_CODE 124

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_condition;
static pthread_t g_thread;

static int g_started = 0;
static int g_stopping = 0;
static int g_timeout_ms = 0;

static struct timespec g_last_progress;
static char g_stage[WATCHDOG_STAGE_SIZE];

/* start/stop 由主线程串行调用，feed 通过互斥锁保护。 */

static struct timespec deadline_after(
    const struct timespec *start,
    int timeout_ms)
{
    struct timespec deadline = *start;

    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;

    if(deadline.tv_nsec >= 1000000000L)
    {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    return deadline;
}

static int deadline_reached(
    const struct timespec *now,
    const struct timespec *deadline)
{
    return now->tv_sec > deadline->tv_sec ||
           (now->tv_sec == deadline->tv_sec &&
            now->tv_nsec >= deadline->tv_nsec);
}

static void *watchdog_thread(void *argument)
{
    (void)argument;

    pthread_mutex_lock(&g_mutex);

    while(!g_stopping)
    {
        struct timespec deadline =
            deadline_after(&g_last_progress, g_timeout_ms);

        int ret = pthread_cond_timedwait(
            &g_condition,
            &g_mutex,
            &deadline);

        if(g_stopping)
            break;

        if(ret != 0 && ret != ETIMEDOUT)
        {
            /*
             * 监控机制自身失效，退出进程交给外部守护恢复。
             * 不尝试取消正在使用驱动资源的业务线程。
             */
            pthread_mutex_unlock(&g_mutex);
            _exit(WATCHDOG_EXIT_CODE);
        }

        struct timespec now;

        if(clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        {
            pthread_mutex_unlock(&g_mutex);
            _exit(WATCHDOG_EXIT_CODE);
        }

        /*
         * feed 可能已更新进度，因此必须重新计算截止时间，
         * 不能仅凭 timedwait 返回 ETIMEDOUT 就判定业务超时。
         */
        deadline = deadline_after(&g_last_progress, g_timeout_ms);

        if(deadline_reached(&now, &deadline))
        {
            /*
             * 此处不调用日志、资源释放或 pthread_join：
             * 它们可能依赖已卡住的线程或锁。
             * 外部守护脚本通过退出码 124 记录超时事件。
             */
            pthread_mutex_unlock(&g_mutex);
            _exit(WATCHDOG_EXIT_CODE);
        }
    }

    pthread_mutex_unlock(&g_mutex);
    return NULL;
}

int app_watchdog_start(int timeout_ms)
{
    if(timeout_ms < 1000 || timeout_ms > 600000)
    {
        fprintf(stderr, "[ERROR] watchdog timeout must be 1000..600000 ms\n");
        return -1;
    }

    pthread_mutex_lock(&g_mutex);

    if(g_started)
    {
        pthread_mutex_unlock(&g_mutex);
        fprintf(stderr, "[ERROR] watchdog already started\n");
        return -1;
    }

    pthread_condattr_t attributes;
    int ret = pthread_condattr_init(&attributes);

    if(ret != 0)
    {
        pthread_mutex_unlock(&g_mutex);
        fprintf(stderr, "[ERROR] watchdog condattr init: %s\n", strerror(ret));
        return -1;
    }

    ret = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);

    if(ret != 0)
    {
        pthread_condattr_destroy(&attributes);
        pthread_mutex_unlock(&g_mutex);
        fprintf(stderr, "[ERROR] watchdog set clock: %s\n", strerror(ret));
        return -1;
    }

    ret = pthread_cond_init(&g_condition, &attributes);
    pthread_condattr_destroy(&attributes);

    if(ret != 0)
    {
        pthread_mutex_unlock(&g_mutex);
        fprintf(stderr, "[ERROR] watchdog condition init: %s\n", strerror(ret));
        return -1;
    }

    if(clock_gettime(CLOCK_MONOTONIC, &g_last_progress) != 0)
    {
        int saved_errno = errno;

        pthread_cond_destroy(&g_condition);
        pthread_mutex_unlock(&g_mutex);

        fprintf(
            stderr,
            "[ERROR] watchdog read clock: %s\n",
            strerror(saved_errno));

        return -1;
    }

    g_timeout_ms = timeout_ms;
    g_stopping = 0;
    snprintf(g_stage, sizeof(g_stage), "%s", "starting");

    ret = pthread_create(&g_thread, NULL, watchdog_thread, NULL);

    if(ret != 0)
    {
        pthread_cond_destroy(&g_condition);
        pthread_mutex_unlock(&g_mutex);
        fprintf(stderr, "[ERROR] watchdog create thread: %s\n", strerror(ret));
        return -1;
    }

    g_started = 1;

    pthread_mutex_unlock(&g_mutex);
    return 0;
}

void app_watchdog_feed(const char *stage)
{
    pthread_mutex_lock(&g_mutex);

    if(!g_started || g_stopping)
    {
        pthread_mutex_unlock(&g_mutex);
        return;
    }

    struct timespec now;

    if(clock_gettime(CLOCK_MONOTONIC, &now) == 0)
    {
        g_last_progress = now;

        snprintf(
            g_stage,
            sizeof(g_stage),
            "%s",
            stage != NULL ? stage : "unknown");

        pthread_cond_signal(&g_condition);
    }

    pthread_mutex_unlock(&g_mutex);
}

void app_watchdog_stop(void)
{
    pthread_mutex_lock(&g_mutex);

    if(!g_started)
    {
        pthread_mutex_unlock(&g_mutex);
        return;
    }

    g_stopping = 1;
    pthread_cond_signal(&g_condition);

    pthread_mutex_unlock(&g_mutex);

    pthread_join(g_thread, NULL);

    pthread_mutex_lock(&g_mutex);

    pthread_cond_destroy(&g_condition);

    g_started = 0;
    g_stopping = 0;
    g_timeout_ms = 0;
    g_stage[0] = '\0';

    pthread_mutex_unlock(&g_mutex);
}