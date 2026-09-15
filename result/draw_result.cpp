/*
 * @file    draw_result.cpp
 * @brief   实现AI检测结果绘制功能
 *
 * @author  zhangyong
 * @date    2026-09-14
 * @version 1.0
 *
 * @note
 *
 * 功能：
 *
 * 1. 根据DetectionResult绘制检测框
 * 2. 绘制类别名称
 * 3. 绘制置信度
 *
 * 数据流程：
 *
 * RKNN
 *   |
 *   ↓
 * DetectionResult
 *   |
 *   ↓
 * draw_detection_result()
 *
 */


#include "draw_result.h"

#include <stdio.h>
#include <string.h>

#include <opencv2/opencv.hpp>


/*
 * 类别名称
 *
 * 根据你的YOLO类别修改
 */
static const char *g_class_names[] =
{
    "crazing",
    "inclusion",
    "patches",
    "pitted_surface",
    "rolled_in_scale",
    "scratches"
};



/*
 * @brief 获取类别名称
 */
static const char *get_class_name(
        int class_id)
{

    int class_num =
        sizeof(g_class_names) /
        sizeof(g_class_names[0]);


    if(class_id < 0 ||
       class_id >= class_num)
    {
        return "unknown";
    }


    return g_class_names[class_id];
}




/*
 * @brief 绘制检测结果
 *
 * @param image
 *      输入输出图像
 *
 *      格式：
 *      BGR Mat
 *
 * @param result
 *      AI检测结果
 *
 */
int draw_detection_result(
        cv::Mat &image,
        DetectionResult *result)
{

    if(image.empty() ||
       result == NULL)
    {
        return -1;
    }



    for(int i = 0;
        i < result->count;
        i++)
    {

        DetectionBox *box =
            &result->boxes[i];



        /*
         * 检测框
         */
        cv::rectangle(
            image,
            cv::Point(
                box->x1,
                box->y1
            ),
            cv::Point(
                box->x2,
                box->y2
            ),
            cv::Scalar(
                0,
                255,
                0
            ),
            2
        );



        /*
         * 标签内容
         */
        char label[64];


        snprintf(
            label,
            sizeof(label),
            "%s %.2f",
            get_class_name(
                box->class_id
            ),
            box->confidence
        );



        /*
         * 标签背景
         */
        int baseline;


        cv::Size text_size =
            cv::getTextSize(
                label,
                cv::FONT_HERSHEY_SIMPLEX,
                0.5,
                1,
                &baseline
            );


        int text_y =
            box->y1 -
            text_size.height -
            5;


        if(text_y < 0)
        {
            text_y =
                box->y1 +
                text_size.height +
                5;
        }



        cv::rectangle(
            image,
            cv::Rect(
                box->x1,
                text_y - text_size.height,
                text_size.width + 5,
                text_size.height + 5
            ),
            cv::Scalar(
                0,
                255,
                0
            ),
            -1
        );



        /*
         * 绘制文字
         */
        cv::putText(
            image,
            label,
            cv::Point(
                box->x1 + 2,
                text_y
            ),
            cv::FONT_HERSHEY_SIMPLEX,
            0.5,
            cv::Scalar(
                0,
                0,
                0
            ),
            1
        );

    }


    return 0;
}