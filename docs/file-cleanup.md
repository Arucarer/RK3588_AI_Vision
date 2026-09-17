# 项目文件整理记录

日期：2026-09-17。此次只删除无调用方的重复实现及已被替代的配置占位，未调整运行算法。

## 删除与功能归属

| 删除文件 | 原因与当前实现 |
| --- | --- |
| `include/types.h` | 未引用，与 `ai/detection_result.h` 重复定义同名且字段不兼容的结果类型 |
| `result/detection_result.cpp/.h` | 未调用、未编译的结果辅助函数及转接头；统一类型保留在 `ai/detection_result.h` |
| `result/draw_result.cpp/.h` | 未调用、未编译的 OpenCV 绘图实现；当前使用 `display/display.cpp` |
| `ai/rknn_model.cpp/.h` | 无调用方的旧模型加载器；当前由 `rknn_engine.cpp` 加载模型 |
| `image/letterbox.cpp/.h` | 无调用方的旧缩放实现；当前摄像头/单图路径由 `preprocess.cpp` 完成 Letterbox |
| `network/heartbeat.cpp/.h` | 占位接口无调用方；心跳已在 `tcp_client.cpp` 网络线程中实现 |
| `system/config.c/.h`、`config/config.json` | 空的旧配置占位；当前使用 `app_config.cpp/.h` 和 `app.conf` |

Makefile 同步移除了旧模型加载器和旧 Letterbox 的编译项。

## 保留项目

- `image/roi.*`、`edge_detect.*`、`color_convert.*`：传统算法与 ROI 业务仍在计划中，保留现有代码。现有 Canny 接口仍需完善算法和验证。
- `storage/*`：离线持久化和补传尚待实现，保留预留文件。当前缺陷事件保存位于 `display.cpp`。
- `system/watchdog.*`、`scripts/start.sh`、`stop.sh`、`watchdog.sh`：自启守护仍在计划中，当前为空。
- `system/log.*`：日志持久化等后续扩展预留，当前为空；不代表日志系统已经完成。
- `scripts/build.sh`、`install.sh`：后续部署脚本预留，当前为空；当前直接使用 Makefile 和 SCP。
- `include/version.h`、`model/`、`lib/`：版本和部署资料保留，不因暂未使用而删除。
- 第三方代码、模型、数据库、测试资料和开发记录未删除。

## 验证范围

删除前检查源码及构建清单中的引用；删除后执行完整交叉编译和差异检查。
未执行开发板回归测试，保留模块未纳入当前构建的部分不属于编译验证范围。
