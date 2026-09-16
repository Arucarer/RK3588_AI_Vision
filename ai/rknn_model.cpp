/* 实现RK3588 AI视觉检测项目中的RKNN模型加载与释放 */
/* @author  zhangyong | @date    2026-09-13 | @version 1.0 */

#include "rknn_model.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rknn_model_load(const char *model_path, RknnModel *model)
{
    FILE *fp = NULL;

    long file_size;

    size_t read_size;

    if(model_path == NULL || model == NULL)
    {
        LOG_ERR("invalid model parameter");

        return -1;
    }

    memset(model, 0, sizeof(RknnModel));

    fp = fopen(model_path, "rb");

    if(fp == NULL)
    {
        LOG_ERR("open rknn model failed: %s", model_path);

        return -1;
    }

    if(fseek(fp, 0, SEEK_END) != 0)
    {
        LOG_ERR("seek rknn model end failed");

        goto ERROR;
    }

    file_size = ftell(fp);

    if(file_size <= 0)
    {
        LOG_ERR("invalid rknn model size: %ld", file_size);

        goto ERROR;
    }

    if(fseek(fp, 0, SEEK_SET) != 0)
    {
        LOG_ERR("seek rknn model start failed");

        goto ERROR;
    }

    model->data = malloc((size_t)file_size);

    if(model->data == NULL)
    {
        LOG_ERR("malloc rknn model memory failed");

        goto ERROR;
    }

    read_size = fread(model->data, 1, (size_t)file_size, fp);

    if(read_size != (size_t)file_size)
    {
        LOG_ERR("read rknn model failed expect=%ld actual=%zu", file_size, read_size);

        goto ERROR;
    }

    model->size = (size_t)file_size;

    strncpy(model->path, model_path, sizeof(model->path) - 1);

    model->path[sizeof(model->path) - 1] = '\0';

    fclose(fp);

    LOG_INFO("rknn model load success: %s size=%zu", model->path, model->size);

    return 0;

ERROR:

    if(fp != NULL)
    {
        fclose(fp);
    }

    if(model->data != NULL)
    {
        free(model->data);

        model->data = NULL;
    }

    model->size = 0;

    model->path[0] = '\0';

    return -1;
}

void rknn_model_unload(RknnModel *model)
{
    if(model == NULL)
    {
        return;
    }

    if(model->data != NULL)
    {
        free(model->data);

        model->data = NULL;
    }

    model->size = 0;

    model->path[0] = '\0';

    LOG_INFO("rknn model unloaded");
}
