# ============================================================
# @file    Makefile
# @brief   RK3588 AI视觉检测项目交叉编译配置
#
# @note
# 实现：
#
# 1. RK3588 ARM64交叉编译
# 2. C/C++混合编译
# 3. Camera模块构建
# 4. Image视觉处理模块构建
# 5. RKNN Runtime链接
# 6. RGA硬件加速链接
# 7. pthread/dl/math链接
# 8. 自动生成依赖文件
#
# 当前视觉流程：
#
# Camera
#   ↓
# V4L2
#   ↓
# FrameData
#   ↓
# Image Processing
#   ↓
# Sobel/Canny
#   ↓
# RKNN
#   ↓
# Result
#
# ============================================================

# ==================== 程序名称 ====================

TARGET := rk3588_ai_detect

# ==================== 工程目录 ====================

APP_DIR     := app
CAMERA_DIR  := camera
IMAGE_DIR   := image
AI_DIR      := ai
INCLUDE_DIR := include
DISPLAY_DIR := display
SYSTEM_DIR  := system

BUILD_DIR := build
BIN_DIR   := bin

NETWORK_DIR := network
STORAGE_DIR := storage
# ==================== SDK ====================

RK3588_SDK := /home/aaa/rk3588_linux_sdk

# ==================== 工具链 ====================

TOOLCHAIN_DIR := \
$(RK3588_SDK)/buildroot/output/rockchip_atk_dlrk3588/host/bin

CROSS_COMPILE := \
$(TOOLCHAIN_DIR)/aarch64-buildroot-linux-gnu-

CC  := $(CROSS_COMPILE)gcc

CXX := $(CROSS_COMPILE)g++

AR := $(CROSS_COMPILE)ar

STRIP := $(CROSS_COMPILE)strip

# ==================== SYSROOT ====================

SYSROOT := $(shell $(CXX) --print-sysroot)

# ==================== RKNN ====================

RKNN_INCLUDE := \
$(SYSROOT)/usr/include/rknn

RKNN_LIB := \
$(SYSROOT)/usr/lib

# ==================== RGA ====================

RGA_INCLUDE := \
$(SYSROOT)/usr/include/rga

RGA_LIB := \
$(SYSROOT)/usr/lib

# ==================== DRM ====================

DRM_INCLUDE := \
$(SYSROOT)/usr/include/libdrm

DRM_LIB := \
$(SYSROOT)/usr/lib

# ==================== 头文件路径 ====================

INCLUDES := \
-I$(INCLUDE_DIR) \
-I$(CAMERA_DIR) \
-I$(IMAGE_DIR) \
-I$(AI_DIR) \
-I$(DISPLAY_DIR) \
-I$(SYSTEM_DIR) \
-I$(RKNN_INCLUDE) \
-I$(RGA_INCLUDE) \
-I$(NETWORK_DIR) \
-I$(STORAGE_DIR) \
-I$(DRM_INCLUDE)

# ==================== C参数 ====================

CFLAGS := \
-Wall \
-Wextra \
-O2 \
-g \
$(INCLUDES)

# ==================== C++参数 ====================

CXXFLAGS := \
-Wall \
-Wextra \
-O2 \
-g \
-std=c++11 \
$(INCLUDES)

# ==================== 链接路径 ====================

LDFLAGS := \
-L$(RKNN_LIB) \
-L$(RGA_LIB)

# ==================== 链接库 ====================

LDLIBS := \
-lrknnrt \
-lrga \
-ldrm \
-lpthread \
-ldl \
-lm

# ============================================================
# C源码
# ============================================================

C_SRCS := \
$(CAMERA_DIR)/v4l2_capture.c \
$(CAMERA_DIR)/camera_buffer.c \
$(CAMERA_DIR)/camera_thread.c \
$(SYSTEM_DIR)/watchdog.c

# ============================================================
# C++源码
#
# Image:
#   resize
#   color_convert
#   roi
#   edge_detect
#
# AI:
#   RKNN engine
#   postprocess
#
# ============================================================

CPP_SRCS := \
$(APP_DIR)/main.cpp \
$(APP_DIR)/detection_pipeline.cpp \
$(SYSTEM_DIR)/alarm_service.cpp \
$(SYSTEM_DIR)/device_status.cpp \
$(SYSTEM_DIR)/app_config.cpp \
$(IMAGE_DIR)/preprocess.cpp \
$(IMAGE_DIR)/resize.cpp \
$(IMAGE_DIR)/color_convert.cpp \
$(IMAGE_DIR)/roi.cpp \
$(IMAGE_DIR)/edge_detect.cpp \
$(AI_DIR)/rknn_engine.cpp \
$(AI_DIR)/postprocess.cpp \
$(DISPLAY_DIR)/display.cpp \
$(DISPLAY_DIR)/drm_display.cpp \
$(NETWORK_DIR)/tcp_client.cpp \
$(NETWORK_DIR)/json_protocol.cpp \
$(STORAGE_DIR)/event_store.cpp \
$(IMAGE_DIR)/roi_analysis.cpp \
$(DISPLAY_DIR)/rga_display.cpp

# ==================== 目标文件 ====================

C_OBJS := \
$(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SRCS))

CPP_OBJS := \
$(patsubst %.cpp,$(BUILD_DIR)/%.o,$(CPP_SRCS))

OBJS := \
$(C_OBJS) \
$(CPP_OBJS)

# ==================== 依赖 ====================

DEPS := $(OBJS:.o=.d)

# ==================== 默认 ====================

.PHONY: all

all: $(BIN_DIR)/$(TARGET)

# ==================== 链接 ====================

$(BIN_DIR)/$(TARGET):$(OBJS)

	@mkdir -p $(BIN_DIR)

	$(CXX) \
	$(OBJS) \
	$(LDFLAGS) \
	$(LDLIBS) \
	-o $@

	@echo ""
	@echo "========================================"
	@echo " RK3588 AI Detect Build Success "
	@echo " Target:$@"
	@echo "========================================"
	@echo ""

# ==================== C编译 ====================

$(BUILD_DIR)/%.o:%.c

	@mkdir -p $(dir $@)

	$(CC) \
	$(CFLAGS) \
	-MMD \
	-MP \
	-c $< \
	-o $@

# ==================== C++编译 ====================

$(BUILD_DIR)/%.o:%.cpp

	@mkdir -p $(dir $@)

	$(CXX) \
	$(CXXFLAGS) \
	-MMD \
	-MP \
	-c $< \
	-o $@

# ==================== 自动依赖 ====================

-include $(DEPS)

# ==================== 清理 ====================

.PHONY:clean

clean:

	rm -rf $(BUILD_DIR)

	rm -rf $(BIN_DIR)

	@echo "clean success"

# ==================== 重编译 ====================

.PHONY: rebuild

rebuild: clean all

# ==================== 查看架构 ====================

.PHONY:check

check:
	file $(BIN_DIR)/$(TARGET)

# ==================== strip ====================

.PHONY:strip

strip:

	$(STRIP) $(BIN_DIR)/$(TARGET)

	@echo "strip success"

# ==================== 查看配置 ====================

.PHONY:info

info:

	@echo ""
	@echo "TARGET=$(TARGET)"
	@echo ""
	@echo "CC=$(CC)"
	@echo "CXX=$(CXX)"
	@echo ""
	@echo "SYSROOT=$(SYSROOT)"
	@echo ""
	@echo "RKNN_INCLUDE=$(RKNN_INCLUDE)"
	@echo "RKNN_LIB=$(RKNN_LIB)"
	@echo ""
	@echo "RGA_INCLUDE=$(RGA_INCLUDE)"
	@echo "RGA_LIB=$(RGA_LIB)"
	@echo ""
	@echo ""
	@echo "DRM_INCLUDE=$(DRM_INCLUDE)"
	@echo "DRM_LIB=$(DRM_LIB)"
	@echo ""

# ==================== 查看源文件 ====================

.PHONY:sources

sources:

	@echo "C Sources:"
	@printf "%s\n" $(C_SRCS)

	@echo ""

	@echo "C++ Sources:"
	@printf "%s\n" $(CPP_SRCS)

# ==================== 工具检查 ====================

.PHONY:check-toolchain

check-toolchain:

	$(CC) --version

	echo ""

	$(CXX) --version

.PHONY:check-rknn

check-rknn:

	ls -l $(RKNN_INCLUDE)/rknn_api.h

	ls -l $(RKNN_LIB)/librknnrt.so*

.PHONY:check-rga

check-rga:

	ls -l $(RGA_INCLUDE)/im2d.h

	ls -l $(RGA_LIB)/librga.so*
