/*
 * @file    postprocess.cpp
 * @brief   RKNN输出后处理实现
 *
 * @author  zhangyong
 * @date    2026-09-14
 * @version 1.0
 *
 * @note
 *
 * 本模块负责：
 *
 * 1. RKNN输出tensor解析
 * 2. Softmax概率计算
 * 3. 最大概率类别选择
 * 4. 生成DetectionResult
 *
 *
 * 数据流程：
 *
 * RKNN Output
 *
 *      |
 *      ↓
 *
 * postprocess
 *
 *      |
 *      ↓
 *
 * DetectionResult
 *
 */


#include "postprocess.h"
#include "common.h"

#include <string.h>
#include <math.h>



/*
 * @brief softmax计算
 *
 * 将RKNN输出logits转换为概率
 *
 */
static void softmax(
        float *input,
        float *output,
        int size)
{

    float max_value =
        input[0];


    /*
     * 查找最大值
     *
     * 防止exp溢出
     */
    for(int i = 1;
        i < size;
        i++)
    {

        if(input[i] > max_value)
        {
            max_value =
                input[i];
        }

    }



    float sum = 0.0f;



    /*
     * 指数计算
     */
    for(int i = 0;
        i < size;
        i++)
    {

        output[i] =
            expf(
                input[i] - max_value
            );


        sum +=
            output[i];

    }



    /*
     * 归一化
     */
    for(int i = 0;
        i < size;
        i++)
    {

        output[i] =
            output[i] / sum;

    }

}





/*
 * @brief RKNN分类结果解析
 *
 * 输入：
 *
 * output:
 *      RKNN输出float数组
 *
 * output_size:
 *      输出数量
 *
 * class_num:
 *      缺陷类别数量
 *
 *
 * 输出：
 *
 * DetectionResult
 *
 */
int postprocess_result(
        float *output,
        int output_size,
        int class_num,
        DetectionResult *result)
{

    /*
     * 参数检查
     */
    if(output == NULL ||
       result == NULL)
    {

        LOG_ERR(
            "invalid postprocess parameter"
        );

        return -1;
    }



    if(output_size < class_num)
    {

        LOG_ERR(
            "invalid output size=%d class_num=%d",
            output_size,
            class_num
        );

        return -1;
    }




    /*
     * 类别数量限制
     */
    if(class_num > 64)
    {

        LOG_ERR(
            "class num too large"
        );

        return -1;
    }




    /*
     * 清空旧结果
     */
    memset(
        result,
        0,
        sizeof(DetectionResult)
    );




    float probability[64];



    /*
     * logits转概率
     */
    softmax(
        output,
        probability,
        class_num
    );




    int max_class = 0;


    float max_probability =
        probability[0];




    /*
     * 查找最大概率类别
     */
    for(int i = 1;
        i < class_num;
        i++)
    {

        if(probability[i] >
           max_probability)
        {

            max_probability =
                probability[i];


            max_class =
                i;

        }

    }




    /*
     * 保存检测结果
     */
    result->class_id =
        max_class;


    result->confidence =
        max_probability;




    /*
     * 判断是否为有效缺陷
     *
     * 当前阈值：
     *
     * confidence > 0.5
     *
     */
    if(max_probability > 0.5f)
    {

        result->valid = 1;

    }
    else
    {

        result->valid = 0;

    }



    LOG_INFO(
        "postprocess result class=%d confidence=%.3f valid=%d",
        result->class_id,
        result->confidence,
        result->valid
    );



    return 0;
}