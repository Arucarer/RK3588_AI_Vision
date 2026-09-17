#include "roi_analysis.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

int roi_analyze(
    const ProcessedImage *image,
    int low_threshold,
    int high_threshold,
    ROIAnalysis *analysis)
{
    if(analysis == NULL)
        return -1;

    std::memset(analysis, 0, sizeof(*analysis));

    if(image == NULL || image->rgb == NULL ||
       image->rgb_width != MODEL_INPUT_WIDTH ||
       image->rgb_height != MODEL_INPUT_HEIGHT ||
       image->resized_width < 5 || image->resized_height < 5 ||
       image->resized_width > image->rgb_width ||
       image->resized_height > image->rgb_height ||
       image->pad_left < 0 || image->pad_top < 0 ||
       image->pad_left > image->rgb_width - image->resized_width ||
       image->pad_top > image->rgb_height - image->resized_height ||
       low_threshold < 1 ||
       high_threshold <= low_threshold ||
       high_threshold > 2048)
    {
        return -1;
    }

    const int width = image->resized_width;
    const int height = image->resized_height;
    const int count = width * height;

    try
    {
        std::vector<float> gray(count);
        std::vector<float> smooth(count);
        std::vector<float> magnitude(count, 0.0f);
        std::vector<unsigned char> direction(count, 0);
        std::vector<unsigned char> state(count, 0);
        std::vector<int> queue;
        queue.reserve(count);

        double gray_sum = 0.0;

        /* RGB → 灰度；排除 114 补边。 */
        for(int y = 0; y < height; ++y)
        {
            for(int x = 0; x < width; ++x)
            {
                const uint8_t *pixel =
                    image->rgb +
                    (static_cast<size_t>(y + image->pad_top) *
                     image->rgb_width + x + image->pad_left) * 3;

                const float value =
                    (77 * pixel[0] + 150 * pixel[1] + 29 * pixel[2]) / 256.0f;

                gray[y * width + x] = value;
                gray_sum += value;
            }
        }

        /* 3×3 Gaussian，边界使用复制扩展。 */
        const int kernel[3] = {1, 2, 1};

        for(int y = 0; y < height; ++y)
        {
            for(int x = 0; x < width; ++x)
            {
                float sum = 0.0f;

                for(int dy = -1; dy <= 1; ++dy)
                {
                    const int sy = std::max(0, std::min(height - 1, y + dy));

                    for(int dx = -1; dx <= 1; ++dx)
                    {
                        const int sx = std::max(0, std::min(width - 1, x + dx));
                        sum += gray[sy * width + sx] *
                               kernel[dy + 1] * kernel[dx + 1];
                    }
                }

                smooth[y * width + x] = sum / 16.0f;
            }
        }

        double gradient_sum = 0.0;
        const int interior_count = (width - 2) * (height - 2);

        /* Sobel 梯度及四方向量化。 */
        for(int y = 1; y < height - 1; ++y)
        {
            for(int x = 1; x < width - 1; ++x)
            {
                const int i = y * width + x;

                const float gx =
                    -smooth[i - width - 1] + smooth[i - width + 1] -
                    2 * smooth[i - 1] + 2 * smooth[i + 1] -
                    smooth[i + width - 1] + smooth[i + width + 1];

                const float gy =
                    -smooth[i - width - 1] - 2 * smooth[i - width] -
                    smooth[i - width + 1] + smooth[i + width - 1] +
                    2 * smooth[i + width] + smooth[i + width + 1];

                magnitude[i] = std::sqrt(gx * gx + gy * gy);
                gradient_sum += magnitude[i];

                float angle = std::atan2(gy, gx) * 180.0f / 3.14159265f;
                if(angle < 0)
                    angle += 180.0f;

                if(angle < 22.5f || angle >= 157.5f)
                    direction[i] = 0;
                else if(angle < 67.5f)
                    direction[i] = 1;
                else if(angle < 112.5f)
                    direction[i] = 2;
                else
                    direction[i] = 3;
            }
        }

        /* Canny 非极大值抑制＋双阈值。 */
        for(int y = 1; y < height - 1; ++y)
        {
            for(int x = 1; x < width - 1; ++x)
            {
                const int i = y * width + x;
                int offset = 1;

                switch(direction[i])
                {
                    case 0: offset = 1; break;
                    case 1: offset = width + 1; break;
                    case 2: offset = width; break;
                    case 3: offset = width - 1; break;
                }

                const float value = magnitude[i];

                if(value < low_threshold ||
                   value < magnitude[i - offset] ||
                   value < magnitude[i + offset])
                {
                    continue;
                }

                if(value >= high_threshold)
                {
                    state[i] = 2;
                    queue.push_back(i);
                }
                else
                {
                    state[i] = 1;
                }
            }
        }

        /* 八邻域滞后连接：只保留连接到强边缘的弱边缘。 */
        for(size_t head = 0; head < queue.size(); ++head)
        {
            const int current = queue[head];
            const int x = current % width;
            const int y = current / width;

            for(int dy = -1; dy <= 1; ++dy)
            {
                for(int dx = -1; dx <= 1; ++dx)
                {
                    const int nx = x + dx;
                    const int ny = y + dy;

                    if(nx <= 0 || nx >= width - 1 ||
                       ny <= 0 || ny >= height - 1)
                    {
                        continue;
                    }

                    const int next = ny * width + nx;

                    if(state[next] == 1)
                    {
                        state[next] = 2;
                        queue.push_back(next);
                    }
                }
            }
        }

        analysis->width = width;
        analysis->height = height;
        analysis->mean_gray = gray_sum / count;
        analysis->sobel_mean = gradient_sum / interior_count;
        analysis->canny_density =
            static_cast<double>(queue.size()) / interior_count;

        return 0;
    }
    catch(const std::bad_alloc &)
    {
        LOG_ERR("allocate ROI analysis buffer failed");
        return -1;
    }
}