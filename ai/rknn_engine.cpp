/*
 * @file    rknn_engine.cpp
 * @brief   RK3588 RKNN通用推理引擎实现
 *
 */


#include "rknn_engine.h"



/*
 * 初始化RKNN
 */
int rknn_engine_init(
        RKNNEngine *engine,
        const char *model_path)
{

    if(engine == NULL ||
       model_path == NULL)
    {
        return -1;
    }


    memset(
        engine,
        0,
        sizeof(RKNNEngine)
    );



    FILE *fp =
        fopen(
            model_path,
            "rb"
        );


    if(fp == NULL)
    {
        LOG_ERR(
            "open rknn model failed:%s",
            model_path
        );

        return -1;
    }



    fseek(
        fp,
        0,
        SEEK_END
    );


    int model_size =
        ftell(fp);



    rewind(fp);



    void *model_data =
        malloc(model_size);



    if(model_data == NULL)
    {
        fclose(fp);

        return -1;
    }



    fread(
        model_data,
        1,
        model_size,
        fp
    );


    fclose(fp);



    /*
     * 创建RKNN Context
     */
    int ret =
        rknn_init(
            &engine->ctx,
            model_data,
            model_size,
            0,
            NULL
        );



    free(model_data);



    if(ret < 0)
    {

        LOG_ERR(
            "rknn init failed ret=%d",
            ret
        );

        return -1;
    }



    /*
     * 查询输入输出数量
     */

    rknn_input_output_num io_num;


    ret =
        rknn_query(
            engine->ctx,
            RKNN_QUERY_IN_OUT_NUM,
            &io_num,
            sizeof(io_num)
        );



    if(ret < 0)
    {

        LOG_ERR(
            "query io num failed"
        );

        return -1;
    }



    engine->input_num =
        io_num.n_input;


    engine->output_num =
        io_num.n_output;



    LOG_INFO(
        "rknn input=%d output=%d",
        engine->input_num,
        engine->output_num
    );



    /*
     * 查询输入tensor尺寸
     */

    rknn_tensor_attr input_attr;


    memset(
        &input_attr,
        0,
        sizeof(input_attr)
    );


    input_attr.index = 0;



    ret =
        rknn_query(
            engine->ctx,
            RKNN_QUERY_INPUT_ATTR,
            &input_attr,
            sizeof(input_attr)
        );



    if(ret >= 0)
    {

        engine->input_width =
            input_attr.dims[2];


        engine->input_height =
            input_attr.dims[1];


        engine->input_channel =
            input_attr.dims[3];


        LOG_INFO(
            "input shape:%dx%dx%d",
            engine->input_width,
            engine->input_height,
            engine->input_channel
        );
    }




    /*
     * 查询输出信息
     */

    for(int i=0;
        i<engine->output_num;
        i++)
    {

        memset(
            &engine->output_attrs[i],
            0,
            sizeof(rknn_tensor_attr)
        );


        engine->output_attrs[i].index=i;



        rknn_query(
            engine->ctx,
            RKNN_QUERY_OUTPUT_ATTR,
            &engine->output_attrs[i],
            sizeof(rknn_tensor_attr)
        );

    }



    LOG_INFO(
        "rknn engine init success"
    );



    return 0;
}






/*
 * 执行推理
 */
int rknn_engine_inference(
        RKNNEngine *engine,
        unsigned char *input_data,
        int input_size,
        float **output_data,
        int *output_size)
{

    if(engine == NULL ||
       input_data == NULL ||
       output_data == NULL ||
       output_size == NULL)
    {
        return -1;
    }



    rknn_input input;


    memset(
        &input,
        0,
        sizeof(input)
    );



    input.index = 0;


    input.type =
        RKNN_TENSOR_UINT8;


    input.size =
        input_size;


    input.fmt =
        RKNN_TENSOR_NHWC;


    input.buf =
        input_data;



    int ret =
        rknn_inputs_set(
            engine->ctx,
            1,
            &input
        );



    if(ret < 0)
    {

        LOG_ERR(
            "rknn input set failed"
        );

        return -1;
    }




    /*
     * NPU执行
     */

    ret =
        rknn_run(
            engine->ctx,
            NULL
        );



    if(ret < 0)
    {

        LOG_ERR(
            "rknn run failed"
        );

        return -1;
    }




    /*
     * 获取输出
     */

    rknn_output outputs[10];


    memset(
        outputs,
        0,
        sizeof(outputs)
    );



    for(int i=0;
        i<engine->output_num;
        i++)
    {

        outputs[i].want_float = 1;

    }



    ret =
        rknn_outputs_get(
            engine->ctx,
            engine->output_num,
            outputs,
            NULL
        );



    if(ret < 0)
    {

        LOG_ERR(
            "rknn output get failed"
        );

        return -1;
    }



    /*
     * 当前默认取第一个输出
     */
    float *result =
        (float *)malloc(
            outputs[0].size
        );



    if(result == NULL)
    {
        rknn_outputs_release(
            engine->ctx,
            engine->output_num,
            outputs
        );

        return -1;
    }



    memcpy(
        result,
        outputs[0].buf,
        outputs[0].size
    );



    *output_data =
        result;



    *output_size =
        outputs[0].size /
        sizeof(float);




    rknn_outputs_release(
        engine->ctx,
        engine->output_num,
        outputs
    );



    return 0;
}






/*
 * 释放资源
 */
void rknn_engine_release(
        RKNNEngine *engine)
{

    if(engine == NULL)
        return;



    if(engine->ctx)
    {

        rknn_destroy(
            engine->ctx
        );


        engine->ctx = 0;

    }


    LOG_INFO(
        "rknn engine release"
    );
}