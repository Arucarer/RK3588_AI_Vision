#!/bin/sh

. /opt/rk3588_ai/scripts/service_common.sh

if [ "$(id -u)" != "0" ]; then
    echo "[ERROR] run this script as root" >&2
    exit 1
fi

if ! read_service_pid; then
    if application_running; then
        echo "[ERROR] application is running but supervisor PID is unavailable" >&2
        echo "[ERROR] stop the manually started application first" >&2
        exit 1
    fi

    if [ -d "$LOCK_DIR" ]; then
        echo "[ERROR] incomplete supervisor lock: $LOCK_DIR" >&2
        echo "[ERROR] inspect it before removing it manually" >&2
        exit 1
    fi

    echo "[INFO] AI service is already stopped"
    exit 0
fi

if ! kill -0 "$SERVICE_PID" 2>/dev/null; then
    if application_running; then
        echo "[ERROR] supervisor is gone but application is still running" >&2
        exit 1
    fi

    # 已确认 PID 不存在，应用也没有运行，可以清理残留锁。
    if ! rm -f "$PID_FILE" || ! rmdir "$LOCK_DIR"; then
        echo "[ERROR] cannot remove stale supervisor lock" >&2
        exit 1
    fi

    echo "[INFO] removed stale lock; AI service is stopped"
    exit 0
fi

if ! is_watchdog_process; then
    echo "[ERROR] PID $SERVICE_PID does not match the AI supervisor" >&2
    echo "[ERROR] refusing to signal an unrelated process" >&2
    exit 1
fi

echo "[INFO] stopping AI supervisor pid=$SERVICE_PID"

# 由守护脚本负责停止自己的应用子进程。
if ! kill -TERM "$SERVICE_PID"; then
    echo "[ERROR] cannot stop AI supervisor" >&2
    exit 1
fi

waited=0

while is_watchdog_process; do
    if [ "$waited" -ge 30 ]; then
        echo "[ERROR] AI supervisor did not exit within 30 seconds" >&2
        echo "[ERROR] do not start another instance" >&2
        exit 1
    fi

    sleep 1
    waited=$((waited + 1))
done

if application_running; then
    echo "[ERROR] application is still running after supervisor exit" >&2
    exit 1
fi

if [ -d "$LOCK_DIR" ]; then
    echo "[ERROR] supervisor exited but its lock remains" >&2
    exit 1
fi

echo "[INFO] AI service stopped"
exit 0