#include "protocol.h"

#include <stdio.h>

#define DEVICE_ID "RK3588_001"

int protocol_build_hello(char *buf, size_t size)
{
    int len;

    if(buf == NULL || size == 0)
    {
        return -1;
    }

    len = snprintf(buf, size,
                   "{\"type\":\"hello\",\"device_id\":\"%s\"}",
                   DEVICE_ID);

    if(len < 0 || (size_t)len >= size)
    {
        buf[0] = '\0';

        return -1;
    }

    return len;
}

int protocol_build_heartbeat(char *buf, size_t size)
{
    if(buf != NULL && size > 0)
    {
        buf[0] = '\0';
    }

    return -1; /* TODO: 后续实现心跳JSON */
}

int protocol_build_detection(char *buf, size_t size, const void *result)
{
    (void)result;

    if(buf != NULL && size > 0)
    {
        buf[0] = '\0';
    }

    return -1; /* TODO: DetectionResult确定后实现检测结果JSON */
}