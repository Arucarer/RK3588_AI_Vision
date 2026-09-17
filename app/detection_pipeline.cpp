#include "detection_pipeline.h"
#include "camera_buffer.h"
#include "postprocess.h"
#include "roi_analysis.h"
#include "alarm_service.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace
{

using Task = std::unique_ptr<DetectionTask>;

/* 固定容量环形队列；只转移任务所有权，不复制图像。 */
class TaskQueue
{
public:
    bool push(Task &task)
    {
        std::unique_lock<std::mutex> lock(mutex);

        changed.wait(lock, [&]
        {
            return closed || count < slots.size();
        });

        if(closed)
            return false;

        slots[(head + count) % slots.size()] = std::move(task);
        ++count;

        changed.notify_all();
        return true;
    }

    int pop(Task &task)
    {
        std::unique_lock<std::mutex> lock(mutex);

        const bool ready = changed.wait_for(
            lock,
            std::chrono::milliseconds(200),
            [&] { return closed || count != 0; });

        if(!ready)
            return 1;

        if(closed)
            return -1;

        task = std::move(slots[head]);
        head = (head + 1) % slots.size();
        --count;

        changed.notify_all();
        return 0;
    }

    void close()
    {
        std::lock_guard<std::mutex> lock(mutex);

        closed = true;
        changed.notify_all();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex);

        for(auto &slot : slots)
            slot.reset();

        head = 0;
        count = 0;
    }

private:
    std::array<Task, 2> slots;
    size_t head = 0;
    size_t count = 0;
    bool closed = false;

    std::mutex mutex;
    std::condition_variable changed;
};

void report_alarm(const char *code, const char *stage)
{
    if(!alarm_raise(code, stage))
    {
        LOG_ERR("alarm persistence failed code=%s stage=%s", code, stage);
    }
}

} // namespace

DetectionTask::DetectionTask()
{
    for(int i = 0; i < RKNN_PERF_COUNT; ++i)
        rknn_perf.ms[i] = -1;
}

DetectionTask::~DetectionTask()
{
    preprocess_release(&image);
    camera_buffer_release(&frame);
}

struct DetectionPipeline::Impl
{
    AppConfig config;
    RKNNEngine *engine = nullptr;

    TaskQueue prepared;
    TaskQueue completed;

    std::atomic<bool> stopping{false};

    std::thread preprocess_thread;
    std::thread inference_thread;

    void halt()
    {
        stopping.store(true);

        prepared.close();
        completed.close();

        camera_buffer_stop();
    }

    void fail(const char *code, const char *stage)
    {
        report_alarm(code, stage);
        halt();
    }

    void preprocess_loop()
    {
        try
        {
            long long last_frame = monotonic_us();
            long long last_analysis = 0;

            while(!stopping.load())
            {
                Task task(new DetectionTask);

                int ret = camera_buffer_pop(&task->frame);

                if(stopping.load())
                    break;

                if(ret < 0)
                {
                    fail("camera_stopped", "capture");
                    break;
                }

                if(ret == 1)
                {
                    if(monotonic_us() - last_frame >=
                       config.camera_timeout_ms * 1000LL)
                    {
                        fail("camera_timeout", "capture");
                        break;
                    }

                    continue;
                }

                last_frame = monotonic_us();
                task->started_us = last_frame;

                const long long begin = monotonic_us();

                ret = config.roi_enabled
                    ? preprocess_run_roi(
                        &task->frame,
                        &task->image,
                        config.roi_x,
                        config.roi_y,
                        config.roi_width,
                        config.roi_height)
                    : preprocess_run(&task->frame, &task->image);

                task->preprocess_ms =
                    (monotonic_us() - begin) / 1000.0;

                if(ret != 0)
                {
                    task->error = "preprocess";
                }
                else if(task->image.rgb_width != engine->input_width ||
                        task->image.rgb_height != engine->input_height ||
                        engine->input_channel != 3)
                {
                    fail("model_shape_mismatch", "preprocess");
                    break;
                }

                if(task->error == nullptr && config.edge_analysis_enabled)
                {
                    ROIAnalysis analysis = {};

                    ret = roi_analyze(
                        &task->image,
                        config.edge_low_threshold,
                        config.edge_high_threshold,
                        &analysis);

                    if(monotonic_us() - last_analysis >=
                       config.stats_interval_ms * 1000LL)
                    {
                        last_analysis = monotonic_us();

                        if(ret == 0)
                        {
                            LOG_INFO(
                                "ROI_ANALYSIS sample=%dx%d gray=%.2f "
                                "sobel=%.2f canny=%.4f",
                                analysis.width,
                                analysis.height,
                                analysis.mean_gray,
                                analysis.sobel_mean,
                                analysis.canny_density);
                        }
                        else
                        {
                            report_alarm(
                                "roi_analysis_failed",
                                "preprocess");
                        }
                    }
                }

                if(!prepared.push(task))
                    break;

                /*
                 * 等待下游队列腾出空间的时间，不计入摄像头无帧超时。
                 */
                last_frame = monotonic_us();
            }
        }
        catch(...)
        {
            fail("worker_exception", "preprocess");
        }
    }

    void inference_loop()
    {
        try
        {
            while(!stopping.load())
            {
                Task task;

                const int pop_ret = prepared.pop(task);

                if(pop_ret == 1)
                    continue;

                if(pop_ret < 0 || stopping.load())
                    break;

                if(task->error == nullptr)
                {
                    struct Output
                    {
                        RKNNInferenceResult value = {};

                        ~Output()
                        {
                            rknn_engine_result_release(&value);
                        }
                    } output;

                    long long begin = monotonic_us();

                    int ret = rknn_engine_inference(
                        engine,
                        task->image.rgb,
                        MODEL_INPUT_SIZE,
                        &output.value);

                    task->inference_ms =
                        (monotonic_us() - begin) / 1000.0;

                    task->rknn_perf = engine->last_perf;

                    if(ret != 0)
                    {
                        task->error = "inference";
                    }
                    else
                    {
                        begin = monotonic_us();

                        ret = postprocess_result(
                            engine,
                            &output.value,
                            &task->image,
                            task->frame.pts,
                            config.confidence,
                            config.nms,
                            &task->result);

                        task->postprocess_ms =
                            (monotonic_us() - begin) / 1000.0;

                        if(ret != 0)
                        {
                            task->error = "postprocess";
                        }
                        else
                        {
                            task->result.image_width = task->frame.width;
                            task->result.image_height = task->frame.height;
                            task->result.pts = task->frame.pts;
                            task->result.inference_time_ms =
                                static_cast<float>(task->inference_ms);
                        }
                    }
                }

                if(!completed.push(task))
                    break;
            }
        }
        catch(...)
        {
            fail("worker_exception", "inference");
        }
    }
};

DetectionPipeline::DetectionPipeline() = default;

DetectionPipeline::~DetectionPipeline()
{
    stop();
}

int DetectionPipeline::start(
    const AppConfig &config,
    RKNNEngine *engine)
{
    if(impl || engine == nullptr)
        return -1;

    try
    {
        impl.reset(new Impl);

        impl->config = config;
        impl->engine = engine;

        impl->inference_thread = std::thread(
            &Impl::inference_loop,
            impl.get());

        impl->preprocess_thread = std::thread(
            &Impl::preprocess_loop,
            impl.get());
    }
    catch(...)
    {
        stop();
        return -1;
    }

    return 0;
}

int DetectionPipeline::pop(
    std::unique_ptr<DetectionTask> &task)
{
    return impl ? impl->completed.pop(task) : -1;
}

void DetectionPipeline::stop()
{
    if(!impl)
        return;

    impl->halt();

    if(impl->preprocess_thread.joinable())
        impl->preprocess_thread.join();

    if(impl->inference_thread.joinable())
        impl->inference_thread.join();

    impl->prepared.clear();
    impl->completed.clear();

    impl.reset();
}