#include "display.h"

#include "drm_display.h"
#include "rga_display.h"
#include "json_protocol.h"

#include <fcntl.h>
#include <algorithm>
#include <cmath>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include <deque>
#include <vector>
#include <string>
#include <new>
#include <pthread.h>

/* 整个工程只能在一个 .cpp 中定义 */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb/stb_image_write.h"

/* 与当前 drm_display.cpp 的帧缓冲尺寸一致 */
#define DISPLAY_WIDTH 1920
#define DISPLAY_HEIGHT 1080

static int g_display_ready = 0;

static uint8_t *g_rgb_buffer = NULL;

/* 每个任务持有独立 RGB 副本。 队列最多两张，工作线程还可能正在保存一张。 */
struct SnapshotTask
{
    std::vector<uint8_t> rgb;
    std::string directory;
    std::string device_id;
    DetectionResult result;
};

static std::deque<SnapshotTask> g_snapshot_queue;

static pthread_mutex_t g_snapshot_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t g_snapshot_cond = PTHREAD_COND_INITIALIZER;

static pthread_t g_snapshot_thread;
static bool g_snapshot_started = false;
static bool g_snapshot_stopping = false;

static const size_t SNAPSHOT_QUEUE_CAPACITY = 2;

/* 后台持久化同帧图片与检测结果。 */
static int snapshot_write_file(const SnapshotTask &task);

static void *snapshot_thread_main(void *arg);

/*
 * @brief
 * 初始化显示模块
 */
int display_init()
{
    if(rga_display_init() != 0)
    {
        LOG_ERR("rga display init failed");

        return -1;
    }

    if(drm_display_init() != 0)
    {
        LOG_ERR("drm display init failed");

        return -1;
    }

    g_rgb_buffer = (uint8_t *)malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * 3);

    if(g_rgb_buffer == NULL)
    {
        LOG_ERR("malloc rgb buffer failed");

        drm_display_deinit();

        return -1;
    }

    g_snapshot_stopping = false;
    g_snapshot_queue.clear();

    const int thread_ret = pthread_create(&g_snapshot_thread, NULL, snapshot_thread_main, NULL);

    if(thread_ret != 0)
    {
        LOG_ERR("create snapshot worker failed: %s", strerror(thread_ret));

        free(g_rgb_buffer);
        g_rgb_buffer = NULL;

        drm_display_deinit();
        rga_display_deinit();

        return -1;
    }

    g_snapshot_started = true;

    g_display_ready = 1;

    LOG_INFO("display init success");

    return 0;
}

/*
 * 输入坐标已经转换到显示缓冲区坐标系。
 * 参数必须在图像范围内。
 */
static void draw_rectangle_rgb(uint8_t *rgb, int left, int top, int right, int bottom)
{
    const int thickness = 3;

    for(int t = 0; t < thickness; ++t)
    {
        const int x1 = left + t;
        const int y1 = top + t;
        const int x2 = right - t;
        const int y2 = bottom - t;

        if(x1 > x2 || y1 > y2)
        {
            break;
        }

        /* 上、下边 */
        for(int x = x1; x <= x2; ++x)
        {
            uint8_t *upper = rgb + ((size_t)y1 * DISPLAY_WIDTH + x) * 3;

            uint8_t *lower = rgb + ((size_t)y2 * DISPLAY_WIDTH + x) * 3;

            upper[0] = 0;
            upper[1] = 255;
            upper[2] = 0;

            lower[0] = 0;
            lower[1] = 255;
            lower[2] = 0;
        }

        /* 左、右边 */
        for(int y = y1; y <= y2; ++y)
        {
            uint8_t *left_pixel = rgb + ((size_t)y * DISPLAY_WIDTH + x1) * 3;

            uint8_t *right_pixel = rgb + ((size_t)y * DISPLAY_WIDTH + x2) * 3;

            left_pixel[0] = 0;
            left_pixel[1] = 255;
            left_pixel[2] = 0;

            right_pixel[0] = 0;
            right_pixel[1] = 255;
            right_pixel[2] = 0;
        }
    }
}

/* 5×7 字体：数字 0～9，字母 A～Z；每个字节是一列 */
static const unsigned char FONT_5X7[36][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}  // Z
};

static const unsigned char *font_glyph(char c)
{
    static const unsigned char blank[5] = {0, 0, 0, 0, 0};
    static const unsigned char dash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const unsigned char dot[5] = {0, 0x60, 0x60, 0, 0};
    static const unsigned char percent[5] = {0x63, 0x13, 0x08, 0x64, 0x63};

    if(c >= 'a' && c <= 'z')
    {
        c = (char)(c - 'a' + 'A');
    }

    if(c >= '0' && c <= '9')
    {
        return FONT_5X7[c - '0'];
    }

    if(c >= 'A' && c <= 'Z')
    {
        return FONT_5X7[10 + c - 'A'];
    }

    if(c == '-' || c == '_')
        return dash;
    if(c == '.')
        return dot;
    if(c == '%')
        return percent;

    return blank;
}

static void draw_label_rgb(uint8_t *rgb, int left, int top, const char *text)
{
    const int scale = 3;
    const int margin = 4;
    const int length = (int)strlen(text);

    const int label_width = length * 6 * scale + margin * 2;
    const int label_height = 7 * scale + margin * 2;

    if(label_width > DISPLAY_WIDTH)
    {
        return;
    }

    /* 优先画在框上方，空间不足则画在框顶部 */
    const int x = std::max(0, std::min(left, DISPLAY_WIDTH - label_width));

    const int preferred_y = top >= label_height ? top - label_height : top;

    const int y = std::max(0, std::min(preferred_y, DISPLAY_HEIGHT - label_height));

    /* 黑色背景 */
    for(int row = 0; row < label_height; ++row)
    {
        memset(rgb + ((size_t)(y + row) * DISPLAY_WIDTH + x) * 3, 0, (size_t)label_width * 3);
    }

    /* 白色文字 */
    for(int i = 0; i < length; ++i)
    {
        const unsigned char *glyph = font_glyph(text[i]);

        for(int col = 0; col < 5; ++col)
        {
            for(int row = 0; row < 7; ++row)
            {
                if((glyph[col] & (1u << row)) == 0)
                {
                    continue;
                }

                for(int dy = 0; dy < scale; ++dy)
                {
                    for(int dx = 0; dx < scale; ++dx)
                    {
                        const int px = x + margin + i * 6 * scale + col * scale + dx;

                        const int py = y + margin + row * scale + dy;

                        uint8_t *pixel = rgb + ((size_t)py * DISPLAY_WIDTH + px) * 3;

                        pixel[0] = 255;
                        pixel[1] = 255;
                        pixel[2] = 255;
                    }
                }
            }
        }
    }
}

int display_show(FrameData *frame, DetectionResult *result)
{
    if(frame == NULL || result == NULL || frame->vir_addr == NULL || frame->width <= 0 ||
       frame->height <= 0 || !g_display_ready || g_rgb_buffer == NULL)
    {
        return -1;
    }

    if(result->count < 0 || result->count > MAX_DETECTION_NUM)
    {
        LOG_ERR("invalid detection count");
        return -1;
    }

    /* 必须是同一帧、同一原图坐标系的结果， 防止图像与检测框错配。 */
    if(result->image_width != frame->width || result->image_height != frame->height ||
       result->pts != frame->pts)
    {
        LOG_ERR("detection result does not match frame");
        return -1;
    }

    /* 1. 生成显示用 RGB 图像 */
    if(rga_nv12_to_rgb((uint8_t *)frame->vir_addr, frame->width, frame->height, g_rgb_buffer,
                       DISPLAY_WIDTH, DISPLAY_HEIGHT) != 0)
    {
        LOG_ERR("rga display conversion failed");
        return -1;
    }

    /* 显示图像当前直接缩放铺满缓冲区。 框使用相同的横向、纵向缩放比例。 */
    const float scale_x = (float)DISPLAY_WIDTH / frame->width;

    const float scale_y = (float)DISPLAY_HEIGHT / frame->height;

    for(int i = 0; i < result->count; ++i)
    {
        const DetectionBox *box = &result->boxes[i];

        if(!std::isfinite(box->x1) || !std::isfinite(box->y1) || !std::isfinite(box->x2) ||
           !std::isfinite(box->y2))
        {
            continue;
        }

        /* 先限制原图坐标，再转换成像素索引 */
        const float x1 = std::max(0.0f, std::min(box->x1, (float)frame->width));
        const float y1 = std::max(0.0f, std::min(box->y1, (float)frame->height));
        const float x2 = std::max(0.0f, std::min(box->x2, (float)frame->width));
        const float y2 = std::max(0.0f, std::min(box->y2, (float)frame->height));

        if(x2 <= x1 || y2 <= y1)
        {
            continue;
        }

        const int left = std::min(DISPLAY_WIDTH - 1, (int)std::floor(x1 * scale_x));

        const int top = std::min(DISPLAY_HEIGHT - 1, (int)std::floor(y1 * scale_y));

        const int right = std::min(DISPLAY_WIDTH - 1, (int)std::ceil(x2 * scale_x) - 1);

        const int bottom = std::min(DISPLAY_HEIGHT - 1, (int)std::ceil(y2 * scale_y) - 1);

        if(right < left || bottom < top)
        {
            continue;
        }

        draw_rectangle_rgb(g_rgb_buffer, left, top, right, bottom);
        static const char *names[DETECT_CLASS_NUM] = {
            "CRAZING", "INCLUSION", "PATCHES", "PITTED_SURFACE", "ROLLED-IN_SCALE", "SCRATCHES"};

        const char *name = "UNKNOWN";

        if(box->class_id >= 0 && box->class_id < DETECT_CLASS_NUM)
        {
            name = names[box->class_id];
        }

        char label[64];

        if(std::isfinite(box->confidence))
        {
            snprintf(label, sizeof(label), "%s %.1f%%", name, box->confidence * 100.0f);
        }
        else
        {
            snprintf(label, sizeof(label), "%s", name);
        }

        draw_label_rgb(g_rgb_buffer, left, top, label);
    }

    /* 2. 输出已经叠加检测框的画面 */
    return drm_display_frame(g_rgb_buffer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

struct SnapshotWriter
{
    FILE *fp;
    bool failed;
};

static void snapshot_write_callback(void *context, void *data, int size)
{
    SnapshotWriter *writer = (SnapshotWriter *)context;

    if(writer->failed || size <= 0)
    {
        return;
    }

    if(fwrite(data, 1, (size_t)size, writer->fp) != (size_t)size)
    {
        writer->failed = true;
    }
}

/* 同步目录项，配合文件 fsync 和目录 rename */
static bool snapshot_sync_directory(const std::string &path)
{
    const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);

    if(fd < 0)
    {
        return false;
    }

    const bool ok = fsync(fd) == 0;
    close(fd);

    return ok;
}

static bool snapshot_write_json(const std::string &path, const std::string &json)
{
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);

    if(fd < 0)
    {
        return false;
    }

    size_t offset = 0;
    bool ok = true;

    while(offset < json.size())
    {
        const ssize_t written = write(fd, json.data() + offset, json.size() - offset);

        if(written > 0)
        {
            offset += static_cast<size_t>(written);
        }
        else if(written < 0 && errno == EINTR)
        {
            continue;
        }
        else
        {
            ok = false;
            break;
        }
    }

    if(ok && fsync(fd) != 0)
    {
        ok = false;
    }

    if(close(fd) != 0)
    {
        ok = false;
    }

    return ok;
}

static int snapshot_write_file(const SnapshotTask &task)
{
    const std::string &root = task.directory;

    if(mkdir(root.c_str(), 0755) != 0 && errno != EEXIST)
    {
        LOG_ERR("create event directory failed: %s", strerror(errno));
        return -1;
    }

    /* 保留容量限制。 临时目录也计入数量，避免异常中断产生的残留无限增长。 */
    DIR *dir = opendir(root.c_str());

    if(dir == NULL)
    {
        LOG_ERR("open event directory failed");
        return -1;
    }

    size_t count = 0;
    struct dirent *entry;

    while((entry = readdir(dir)) != NULL)
    {
        if(strncmp(entry->d_name, "event_", 6) == 0 || strncmp(entry->d_name, ".pending_", 9) == 0)
        {
            count++;
        }
    }

    closedir(dir);

    if(count >= 200)
    {
        LOG_INFO("event storage limit reached: 200 entries");
        return 0;
    }

    char temporary[1024];

    const int length = snprintf(temporary, sizeof(temporary), "%s/.pending_%lld_XXXXXX",
                                root.c_str(), task.result.pts);

    if(length < 0 || length >= static_cast<int>(sizeof(temporary)))
    {
        LOG_ERR("event path too long");
        return -1;
    }

    if(mkdtemp(temporary) == NULL)
    {
        LOG_ERR("create temporary event directory failed: %s", strerror(errno));
        return -1;
    }

    const std::string temporary_dir = temporary;

    const std::string basename = temporary_dir.substr(temporary_dir.find_last_of('/') + 1);

    /* .pending_123_ABCDEF → event_123_ABCDEF */
    const std::string event_id = "event_" + basename.substr(9);
    const std::string final_dir = root + "/" + event_id;

    const std::string image_path = temporary_dir + "/image.jpg";
    const std::string json_path = temporary_dir + "/event.json";

    bool published = false;

    do
    {
        std::string detection_json;

        if(!protocol_build_message(task.device_id, event_id, &task.result, detection_json))
        {
            LOG_ERR("invalid event detection metadata");
            break;
        }

        /* 检测框坐标仍属于摄像头原图。 JPEG 是已经画框的显示图，不应直接按原图坐标再次画框。 */
        const std::string metadata = "{\"schema_version\":1,"
                                     "\"event_id\":\"" +
                                     event_id +
                                     "\","
                                     "\"image\":{"
                                     "\"file\":\"image.jpg\","
                                     "\"width\":" +
                                     std::to_string(DISPLAY_WIDTH) +
                                     ","
                                     "\"height\":" +
                                     std::to_string(DISPLAY_HEIGHT) +
                                     ","
                                     "\"annotated\":true,"
                                     "\"mapping\":\"stretch\","
                                     "\"box_coordinate_space\":\"source_image\""
                                     "},"
                                     "\"detection\":" +
                                     detection_json + "}";

        FILE *fp = fopen(image_path.c_str(), "wb");

        if(fp == NULL)
        {
            LOG_ERR("open event JPEG failed");
            break;
        }

        SnapshotWriter writer = {fp, false};

        const int encoded = stbi_write_jpg_to_func(snapshot_write_callback, &writer, DISPLAY_WIDTH,
                                                   DISPLAY_HEIGHT, 3, task.rgb.data(), 90);

        bool image_ok = encoded != 0 && !writer.failed;

        if(fflush(fp) != 0)
        {
            image_ok = false;
        }

        if(image_ok && fsync(fileno(fp)) != 0)
        {
            image_ok = false;
        }

        if(fclose(fp) != 0)
        {
            image_ok = false;
        }

        if(!image_ok)
        {
            LOG_ERR("write event JPEG failed");
            break;
        }

        if(!snapshot_write_json(json_path, metadata))
        {
            LOG_ERR("write event JSON failed");
            break;
        }

        if(!snapshot_sync_directory(temporary_dir))
        {
            LOG_ERR("sync temporary event directory failed");
            break;
        }

        /* 已有事件绝不覆盖。 当前仅一个抓拍工作线程写入该目录。 */
        struct stat existing;

        if(lstat(final_dir.c_str(), &existing) == 0)
        {
            LOG_ERR("event ID collision: %s", event_id.c_str());
            break;
        }

        if(errno != ENOENT)
        {
            LOG_ERR("check event destination failed");
            break;
        }

        if(rename(temporary_dir.c_str(), final_dir.c_str()) != 0)
        {
            LOG_ERR("publish event failed: %s", strerror(errno));
            break;
        }

        published = true;

        if(!snapshot_sync_directory(root))
        {
            /* 事件已可见，保留它； 但不能宣称断电持久化已确认。 */
            LOG_ERR("event published but directory sync failed: %s", final_dir.c_str());
            return -1;
        }

        LOG_INFO("event saved: %s boxes=%d", final_dir.c_str(), task.result.count);

        return 1;
    } while(false);

    if(!published)
    {
        unlink(image_path.c_str());
        unlink(json_path.c_str());
        rmdir(temporary_dir.c_str());
    }

    return -1;
}

void display_deinit()
{
    /* 主线程停止提交任务。 保存线程不再访问 g_display_ready。 */
    g_display_ready = 0;

    pthread_mutex_lock(&g_snapshot_mutex);

    const bool join_needed = g_snapshot_started;

    g_snapshot_stopping = true;
    pthread_cond_broadcast(&g_snapshot_cond);

    pthread_mutex_unlock(&g_snapshot_mutex);

    if(join_needed)
    {
        /*
         * 等待已入队任务保存完成。
         * 正常磁盘状态下任务有限；
         * 若内核磁盘 I/O 卡死，此处仍可能等待。
         */
        pthread_join(g_snapshot_thread, NULL);
    }

    pthread_mutex_lock(&g_snapshot_mutex);

    g_snapshot_started = false;
    g_snapshot_queue.clear();

    pthread_mutex_unlock(&g_snapshot_mutex);

    free(g_rgb_buffer);
    g_rgb_buffer = NULL;

    drm_display_deinit();
    rga_display_deinit();
}

/*
 * 保存线程：
 * 等待任务 → 取出独立副本 → JPEG 编码和写盘。
 * 收到停止请求后，处理完已入队任务再退出。
 */
static void *snapshot_thread_main(void *arg)
{
    (void)arg;

    for(;;)
    {
        SnapshotTask task;

        pthread_mutex_lock(&g_snapshot_mutex);

        while(g_snapshot_queue.empty() && !g_snapshot_stopping)
        {
            pthread_cond_wait(&g_snapshot_cond, &g_snapshot_mutex);
        }

        if(g_snapshot_queue.empty() && g_snapshot_stopping)
        {
            pthread_mutex_unlock(&g_snapshot_mutex);
            break;
        }

        /* swap 转移图像内存所有权，不再复制图像。 */
        task.rgb.swap(g_snapshot_queue.front().rgb);
        task.directory.swap(g_snapshot_queue.front().directory);
        task.device_id.swap(g_snapshot_queue.front().device_id);
        task.result = g_snapshot_queue.front().result;

        g_snapshot_queue.pop_front();

        pthread_mutex_unlock(&g_snapshot_mutex);

        /* 编码和磁盘操作在锁外进行， 不阻塞主线程向队列提交下一张图片。 */
        const long long started = monotonic_us();

        const int saved = snapshot_write_file(task);

        const double elapsed_ms = (monotonic_us() - started) / 1000.0;

        if(saved > 0)
        {
            LOG_INFO("snapshot worker saved pts=%lld time=%.2f ms", task.result.pts, elapsed_ms);
        }
        else if(saved < 0)
        {
            LOG_ERR("snapshot worker failed pts=%lld", task.result.pts);
        }

        /* 到本轮结束，task 析构，释放独立 RGB 副本。 */
    }

    return NULL;
}

/*
 * 主线程调用：
 * 将当前显示图像复制到有界队列。
 * 返回：
 * 1：成功入队，不代表已经写入磁盘
 * 0：队列已满，跳过本次抓拍
 * -1：未启动、正在停止或内存分配失败
 */
int display_save_snapshot(const char *directory, const char *device_id,
                          const DetectionResult *result)
{
    if(directory == NULL || directory[0] == '\0' || device_id == NULL || device_id[0] == '\0' ||
       result == NULL || result->count <= 0 || result->count > MAX_DETECTION_NUM ||
       result->image_width <= 0 || result->image_height <= 0 || !g_display_ready ||
       g_rgb_buffer == NULL)
    {
        return -1;
    }

    pthread_mutex_lock(&g_snapshot_mutex);

    if(!g_snapshot_started || g_snapshot_stopping)
    {
        pthread_mutex_unlock(&g_snapshot_mutex);
        return -1;
    }

    if(g_snapshot_queue.size() >= SNAPSHOT_QUEUE_CAPACITY)
    {
        pthread_mutex_unlock(&g_snapshot_mutex);
        return 0;
    }

    try
    {
        SnapshotTask task;

        task.directory = directory;
        task.device_id = device_id;
        task.result = *result;

        const size_t bytes = static_cast<size_t>(DISPLAY_WIDTH) * DISPLAY_HEIGHT * 3;

        task.rgb.assign(g_rgb_buffer, g_rgb_buffer + bytes);

        g_snapshot_queue.emplace_back();

        SnapshotTask &queued = g_snapshot_queue.back();

        queued.rgb.swap(task.rgb);
        queued.directory.swap(task.directory);
        queued.device_id.swap(task.device_id);
        queued.result = task.result;
    }
    catch(const std::bad_alloc &)
    {
        pthread_mutex_unlock(&g_snapshot_mutex);
        LOG_ERR("allocate event snapshot task failed");
        return -1;
    }

    pthread_cond_signal(&g_snapshot_cond);
    pthread_mutex_unlock(&g_snapshot_mutex);

    return 1;
}
