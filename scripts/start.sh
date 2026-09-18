#!/bin/sh

. /opt/rk3588_ai/scripts/service_common.sh

if [ "$(id -u)" != "0" ]; then
    echo "[ERROR] run this script as root" >&2
    exit 1
fi

if read_service_pid && is_watchdog_process; then
    echo "[INFO] AI supervisor already running pid=$SERVICE_PID"
    exit 0
fi

# 无法确认锁的归属时，不自动删除，也不启动第二份程序。
if [ -d "$LOCK_DIR" ]; then
    echo "[ERROR] supervisor lock exists: $LOCK_DIR" >&2
    echo "[ERROR] run stop.sh and inspect existing processes first" >&2
    exit 1
fi

if application_running; then
    echo "[ERROR] rk3588_ai_detect is already running without this supervisor" >&2
    echo "[ERROR] stop the existing application before starting the service" >&2
    exit 1
fi

if [ ! -x "$APP_BIN" ]; then
    echo "[ERROR] executable missing or not executable: $APP_BIN" >&2
    exit 1
fi

if [ ! -r "$APP_CONFIG" ]; then
    echo "[ERROR] application configuration missing: $APP_CONFIG" >&2
    exit 1
fi

if [ ! -x "$WATCHDOG" ]; then
    echo "[ERROR] watchdog script missing or not executable: $WATCHDOG" >&2
    exit 1
fi

if ! command -v nohup >/dev/null 2>&1; then
    echo "[ERROR] nohup is not installed" >&2
    exit 1
fi

if ! mkdir -p "$APP_DIR/logs" /run; then
    echo "[ERROR] cannot create runtime directories" >&2
    exit 1
fi

if [ -x "$NETWORK_INIT" ]; then
    if ! "$NETWORK_INIT"; then
        echo "[WARN] network initialization failed; starting local detection"
    fi
else
    echo "[WARN] network initialization script unavailable"
fi

cd "$APP_DIR" || exit 1

# 后台运行：
# 守护事件仍由 watchdog.sh 写入 logs/watchdog.log。
# 主程序的逐帧输出不写入无限增长的文件。
nohup sh "$WATCHDOG" "$APP_CONFIG" </dev/null >/dev/null 2>&1 &
launcher_pid=$!

waited=0

while [ "$waited" -lt 10 ]; do
    if read_service_pid && is_watchdog_process; then
        echo "[INFO] AI supervisor started pid=$SERVICE_PID"
        echo "[INFO] log: $APP_DIR/logs/watchdog.log"
        exit 0
    fi

    if ! kill -0 "$launcher_pid" 2>/dev/null; then
        echo "[ERROR] AI supervisor exited during startup" >&2
        tail -n 10 "$APP_DIR/logs/watchdog.log" 2>/dev/null
        exit 1
    fi

    sleep 1
    waited=$((waited + 1))
done

echo "[ERROR] timed out waiting for supervisor PID" >&2
exit 1