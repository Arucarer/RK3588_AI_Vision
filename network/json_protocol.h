#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

int protocol_build_hello(char *buf, size_t size); /* 构建设备上线报文 */

int protocol_build_heartbeat(char *buf, size_t size); /* 构建心跳报文，占位 */

int protocol_build_detection(char *buf, size_t size, const void *result); /* 构建检测结果报文，占位 */

#endif