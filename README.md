# RK3588_AI_Vision

基于 RK3588 + RKNN NPU 的嵌入式 AI 边缘视觉检测系统。

本项目面向工业视觉检测应用场景，基于 Rockchip RK3588 ARM64 嵌入式平台，实现摄像头图像采集、图像预处理、RKNN NPU 本地 AI 推理、检测结果解析、云边数据通信以及设备长期稳定运行的一体化边缘 AI 视觉终端。


# 一、项目简介

传统工业视觉检测方案通常依赖云端服务器完成 AI 推理，存在网络延迟高、实时性不足、数据传输压力大等问题。

本项目采用 **边缘 AI + 云边协同架构**，将深度学习模型部署到 RK3588 本地 NPU，实现低延迟、高可靠性的工业视觉检测。


系统整体流程：

```
工业摄像头
      |
      ↓
V4L2图像采集
      |
      ↓
NV12/YUV原始图像
      |
      ↓
OpenCV图像预处理
      |
      ↓
RKNN Runtime
      |
      ↓
RK3588 NPU硬件推理
      |
      ↓
缺陷检测结果
      |
      ↓
TCP云边通信
      |
      ↓
云端数据管理
```


# 二、硬件平台


## 2.1 主控平台

RK3588开发板：

- ARM64架构
- 八核CPU
  - 4 × Cortex-A76
  - 4 × Cortex-A55
- 主频最高2.4GHz
- 集成RKNN NPU
- AI算力最高6 TOPS


## 2.2 图像采集设备

摄像头：

- Sony IMX415 MIPI CSI摄像头
- 800万像素
- 支持3840×2160@30fps
- 输出NV12格式视频数据

接口：

```
IMX415 Camera

      ↓

MIPI CSI

      ↓

RK3588 ISP

      ↓

V4L2

```


## 2.3 外设

- 千兆以太网
- USB接口
- SD存储
- 5V供电


# 三、软件环境


## 3.1 开发环境

PC：

```
Ubuntu 20.04 LTS
```


开发方式：

```
x86_64开发主机

        ↓

ARM64交叉编译

        ↓

RK3588运行
```


## 3.2 嵌入式系统

- Linux Kernel 5.10
- Buildroot定制Linux系统


## 3.3 开发语言

- C
- C++


# 四、核心技术栈


## 4.1 图像采集

Linux V4L2：

负责：

- 摄像头初始化
- Buffer管理
- NV12视频帧获取
- 多线程采集


流程：

```
Camera

↓

V4L2

↓

NV12 Frame

↓

AI Pipeline

```


---

## 4.2 图像处理

OpenCV：

包括：

- NV12格式转换
- Resize
- ROI区域裁剪
- 图像增强
- 数据归一化


流程：

```
Original Image

↓

Preprocess

↓

AI Input

```


---

## 4.3 AI推理

RKNN：

包括：

- 模型转换
- INT8量化
- 模型加载
- NPU推理
- 后处理


流程：

```
PyTorch/TensorFlow

↓

ONNX

↓

RKNN Model

↓

RK3588 NPU

↓

Detection Result

```


---

## 4.4 云边通信

采用：

- TCP Socket
- JSON协议
- 心跳检测
- 断线重连


流程：

```
RK3588 Edge

        |

        |

      TCP

        |

        |

Cloud Server

```


---

## 4.5 系统稳定性

包括：

- 多线程架构
- Ring Buffer缓存
- Watchdog守护
- 开机自启动
- 日志管理


# 五、工程目录结构


```
RK3588_AI_Vision
│
├── app
│   └── main.c                  # 程序入口
│
├── camera                      # 摄像头采集模块
│   ├── v4l2_capture.c
│   ├── v4l2_capture.h
│   ├── camera_buffer.c
│   └── camera_thread.c
│
├── image                       # 图像处理模块
│   ├── preprocess.c
│   ├── resize.c
│   ├── color_convert.c
│   └── roi.c
│
├── ai                          # RKNN推理模块
│   ├── rknn_engine.c
│   ├── rknn_model.c
│   ├── postprocess.c
│   └── detection_result.h
│
├── network                     # 云边通信模块
│   ├── tcp_client.c
│   ├── json_protocol.c
│   └── heartbeat.c
│
├── storage                     # 数据缓存模块
│   ├── ring_buffer.c
│   └── image_cache.c
│
├── system                      # 系统管理模块
│   ├── log.c
│   ├── watchdog.c
│   └── config.c
│
├── model                       # AI模型文件
│
├── config                      # 配置文件
│
├── include                     # 公共头文件
│
├── lib                         # 第三方库
│
├── scripts                     # 部署脚本
│
├── build                       # 编译目录
│
└── Makefile
```


# 六、系统软件架构


```
                 Cloud Server

                      ↑

                 TCP + JSON

                      ↑


+--------------------------------+

|          RK3588 Device         |

|                                |

| Camera                         |

|   ↓                            |

| V4L2 Capture                   |

|   ↓                            |

| Image Processing               |

|   ↓                            |

| RKNN Runtime                   |

|   ↓                            |

| NPU Inference                  |

|   ↓                            |

| Result Buffer                  |

|   ↓                            |

| Network Upload                 |

+--------------------------------+

```


# 七、开发计划


## Phase 1：工程框架搭建

状态：

- [x] 创建工程目录
- [x] 初始化Git仓库
- [x] 建立模块化代码结构


## Phase 2：V4L2摄像头采集

目标：

完成：

```
MIPI Camera

↓

V4L2

↓

NV12 Frame

```


功能：

- 摄像头初始化
- Buffer管理
- 多线程采集


## Phase 3：图像预处理

目标：

完成：

```
NV12

↓

RGB

↓

Resize

↓

AI Input

```


功能：

- OpenCV处理
- ROI裁剪
- 图像增强


## Phase 4：RKNN NPU推理

目标：

完成：

```
RKNN Model

↓

RK3588 NPU

↓

Detection Result

```


功能：

- 模型加载
- AI推理
- 后处理


## Phase 5：云边通信

目标：

完成：

```
RK3588

↓

TCP

↓

Cloud Server

```


功能：

- JSON协议
- 心跳
- 断线重连
- 数据上传


## Phase 6：工业稳定化

目标：

实现：

- 开机自启动
- Watchdog
- 日志系统
- 断网缓存
- 7×24小时运行


# 八、当前进度


| 模块 | 状态 |
| ---- | ---- |
| 工程框架 | ✅ 完成 |
| Git仓库 | ✅ 完成 |
| RK3588环境 | ⏳ 准备 |
| V4L2采集 | ⏳ 待开发 |
| OpenCV处理 | ⏳ 待开发 |
| RKNN推理 | ⏳ 待开发 |
| TCP通信 | ⏳ 待开发 |
| 系统优化 | ⏳ 待开发 |


# Author

Arucarer

RK3588 Embedded AI Vision Project
