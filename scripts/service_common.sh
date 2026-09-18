#!/bin/sh

APP_DIR=/opt/rk3588_ai
APP_BIN="$APP_DIR/rk3588_ai_detect"
APP_CONFIG="$APP_DIR/config/app.conf"

WATCHDOG="$APP_DIR/scripts/watchdog.sh"
NETWORK_INIT="$APP_DIR/scripts/network_init.sh"

LOCK_DIR=/run/rk3588_ai_watchdog.lock
PID_FILE="$LOCK_DIR/pid"

SERVICE_PID=

read_service_pid()
{
    SERVICE_PID=

    if [ ! -r "$PID_FILE" ]; then
        return 1
    fi

    IFS= read -r SERVICE_PID < "$PID_FILE"

    case "$SERVICE_PID" in
        ""|*[!0-9]*)
            SERVICE_PID=
            return 1
            ;;
    esac

    if [ "$SERVICE_PID" -le 1 ]; then
        SERVICE_PID=
        return 1
    fi

    return 0
}

is_watchdog_process()
{
    if [ -z "$SERVICE_PID" ]; then
        return 1
    fi

    if ! kill -0 "$SERVICE_PID" 2>/dev/null; then
        return 1
    fi

    if [ ! -r "/proc/$SERVICE_PID/cmdline" ]; then
        return 1
    fi

    # 兼容之前在 /opt/rk3588_ai 内手动启动的形式。
    tr '\000' '\n' < "/proc/$SERVICE_PID/cmdline" |
        grep -F -x \
            -e "$WATCHDOG" \
            -e "scripts/watchdog.sh" >/dev/null 2>&1
}

application_running()
{
    pidof rk3588_ai_detect >/dev/null 2>&1
}