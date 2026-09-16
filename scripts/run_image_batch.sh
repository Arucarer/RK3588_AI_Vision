#!/bin/sh

# 出现未处理错误或使用未定义变量时退出。
set -eu

# 用法：
# sh run_image_batch.sh 测试目录 [程序路径] [模型路径]

if [ "$#" -lt 1 ] || [ "$#" -gt 3 ]; then
    echo "Usage: $0 test_directory [application] [model]"
    exit 1
fi

TEST_DIR=$1
APP=${2:-/opt/rk3588_ai/rk3588_ai_detect}
MODEL=${3:-/opt/rk3588_ai/model/best_fp16.rknn}

if [ ! -d "$TEST_DIR/images" ]; then
    echo "ERROR: image directory missing: $TEST_DIR/images"
    exit 1
fi

if [ ! -x "$APP" ]; then
    echo "ERROR: application missing or not executable: $APP"
    exit 1
fi

if [ ! -r "$MODEL" ]; then
    echo "ERROR: model missing or unreadable: $MODEL"
    exit 1
fi

LOG_DIR="$TEST_DIR/board_logs"
mkdir -p "$LOG_DIR"

total=0
passed=0
failed=0

for img in "$TEST_DIR"/images/*.png; do
    # 没有匹配文件时，跳过未展开的通配符。
    [ -f "$img" ] || continue

    name=${img##*/}
    total=$((total + 1))

    echo "[$total] Testing: $name"

    code=0

    # 支持 timeout 时，限制每张图片最多运行 60 秒。
    if command -v timeout >/dev/null 2>&1; then
        timeout 60 "$APP" --image "$MODEL" "$img" \
            >"$LOG_DIR/$name.log" 2>&1 || code=$?
    else
        "$APP" --image "$MODEL" "$img" \
            >"$LOG_DIR/$name.log" 2>&1 || code=$?
    fi

    # 单独记录退出码，防止将失败误判为“没有检测框”。
    printf '%s\n' "$code" >"$LOG_DIR/$name.status"

    if [ "$code" -eq 0 ]; then
        passed=$((passed + 1))
        echo "    Run completed"
    else
        failed=$((failed + 1))
        echo "    Run failed, exit=$code"
    fi
done

if [ "$total" -eq 0 ]; then
    echo "ERROR: no PNG images found"
    exit 1
fi

echo "================================"
echo "Total:     $total"
echo "Completed: $passed"
echo "Failed:    $failed"
echo "Logs:      $LOG_DIR"
echo "================================"
echo "Completed means execution succeeded, not detection accuracy."

if [ "$failed" -ne 0 ]; then
    exit 1
fi