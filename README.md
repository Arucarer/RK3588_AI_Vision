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

以下为当前目录职责；预留文件不代表功能已完成。

```text
RK3588_AI_Vision/
├── app/main.cpp             # 主程序、单图模式和业务调度
├── camera/                  # V4L2、最新帧缓存和采集线程
├── image/                   # 预处理、RGA 缩放，以及待接入的 ROI/传统算法
├── ai/                      # RKNN 引擎、后处理、统一检测结果定义
├── display/                 # DRM/RGA 显示、绘图、后台缺陷事件保存
├── network/                 # TCP 工作线程（含心跳）、JSON 序列化
├── system/app_config.*      # 当前配置解析与校验
├── system/log.*             # 日志模块预留；当前使用 common.h 日志宏
├── system/watchdog.*        # 守护模块预留
├── storage/                 # 持久化缓存模块预留
├── include/                 # common.h、version.h
├── config/app.conf          # 当前唯一运行配置文件
├── tools/tcp_server.py      # Ubuntu TCP 接收端与 SQLite 存储
├── scripts/                 # 单图验证脚本，以及部署/守护预留脚本
├── third_party/stb/         # 图像解码和 JPEG 编码
├── model/                   # 模型目录预留，实际模型不随源码提交
├── lib/                     # 库资料预留，当前依赖从 SDK sysroot 链接
├── docs/                    # 文档
├── tests/                   # 测试预留
├── output/                  # 服务端运行产物
├── build/                   # 编译产物
├── bin/                     # 可执行程序
├── .clang-format            # 排版规则
└── Makefile                 # 交叉编译入口
```

2026-09-17 清理了无调用方的旧结果/绘图模块、重复模型加载器、旧
Letterbox、重复类型定义、独立心跳占位和旧配置文件。
功能归属与保留依据见 [文件整理记录](docs/file-cleanup.md)。

> 本文其他章节包含早期规划，不能作为当前功能完成度的依据。

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
