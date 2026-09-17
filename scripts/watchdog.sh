#!/bin/sh

APP_DIR=/opt/rk3588_ai
APP_BIN="$APP_DIR/rk3588_ai_detect"
CONFIG_FILE="${1:-$APP_DIR/config/app.conf}"

LOG_DIR="$APP_DIR/logs"
LOG_FILE="$LOG_DIR/watchdog.log"
LOG_LIMIT=262144

LOCK_DIR=/run/rk3588_ai_watchdog.lock

RESTART_DELAY=3
STABLE_SECONDS=60
MAX_FAILURES=5
STOP_GRACE_SECONDS=10

child_pid=
failure_count=0
lock_owned=0

# 使用开机后的秒数，避免系统日期调整影响运行时长。
uptime_seconds()
{
    read -r uptime_value uptime_rest < /proc/uptime || return 1
    printf '%s\n' "${uptime_value%%.*}"
}

# 仅记录守护事件，最多保留当前日志和上一份日志。
log_message()
{
    log_time=$(date '+%Y-%m-%d %H:%M:%S')
    log_line="[$log_time] $*"

    printf '%s\n' "$log_line"

    if [ -f "$LOG_FILE" ]; then
        log_size=$(wc -c < "$LOG_FILE")

        if [ "$log_size" -ge "$LOG_LIMIT" ]; then
            mv -f "$LOG_FILE" "$LOG_FILE.1" || return 1
        fi
    fi

    printf '%s\n' "$log_line" >> "$LOG_FILE"
}

stop_child()
{
    if [ -z "$child_pid" ]; then
        return
    fi

    if kill -0 "$child_pid" 2>/dev/null; then
        log_message "Stopping application pid=$child_pid"

        kill -TERM "$child_pid" 2>/dev/null

        stop_waited=0

        while kill -0 "$child_pid" 2>/dev/null; do
            if [ "$stop_waited" -ge "$STOP_GRACE_SECONDS" ]; then
                log_message "Application did not stop in time; sending SIGKILL"
                kill -KILL "$child_pid" 2>/dev/null
                break
            fi

            sleep 1
            stop_waited=$((stop_waited + 1))
        done
    fi

    wait "$child_pid" 2>/dev/null
    child_pid=
}

cleanup()
{
    cleanup_status=$?

    # 清理期间忽略重复停止信号，避免清理函数重入。
    trap '' INT TERM HUP
    trap - 0

    stop_child

    if [ "$lock_owned" -eq 1 ]; then
        rm -f "$LOCK_DIR/pid"
        rmdir "$LOCK_DIR" 2>/dev/null
    fi

    exit "$cleanup_status"
}

if [ "$#" -gt 1 ]; then
    printf 'Usage: %s [config_file]\n' "$0" >&2
    exit 1
fi

if [ ! -x "$APP_BIN" ]; then
    printf 'ERROR: executable missing or not executable: %s\n' "$APP_BIN" >&2
    exit 1
fi

if [ ! -r "$CONFIG_FILE" ]; then
    printf 'ERROR: cannot read config: %s\n' "$CONFIG_FILE" >&2
    exit 1
fi

if ! mkdir -p "$LOG_DIR"; then
    printf 'ERROR: cannot create log directory: %s\n' "$LOG_DIR" >&2
    exit 1
fi

# mkdir 提供互斥，避免同时启动两份守护脚本。
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
    printf 'ERROR: watchdog lock exists: %s\n' "$LOCK_DIR" >&2
    printf 'Check the existing watchdog process before removing a stale lock.\n' >&2
    exit 1
fi

lock_owned=1

trap cleanup 0
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

if ! printf '%s\n' "$$" > "$LOCK_DIR/pid"; then
    printf 'ERROR: cannot write watchdog pid\n' >&2
    exit 1
fi

if ! uptime_seconds >/dev/null; then
    printf 'ERROR: cannot read /proc/uptime\n' >&2
    exit 1
fi

log_message "Supervisor started pid=$$ config=$CONFIG_FILE"

while :; do
    started_seconds=$(uptime_seconds) || exit 1

    log_message "Starting application"

    # 应用日志继承终端输出，不写入无大小限制的文件。
    "$APP_BIN" --config "$CONFIG_FILE" &
    child_pid=$!

    log_message "Application started pid=$child_pid"

    wait "$child_pid"
    app_status=$?
    child_pid=

    ended_seconds=$(uptime_seconds) || exit 1
    runtime_seconds=$((ended_seconds - started_seconds))

    if [ "$app_status" -eq 0 ]; then
        log_message "Application exited normally; supervisor stopping"
        exit 0
    fi

    if [ "$app_status" -eq 124 ]; then
        log_message "Application watchdog timeout; runtime=${runtime_seconds}s"
    else
        log_message "Application failed status=$app_status runtime=${runtime_seconds}s"
    fi

    if [ "$runtime_seconds" -ge "$STABLE_SECONDS" ]; then
        failure_count=0
    else
        failure_count=$((failure_count + 1))
    fi

    if [ "$failure_count" -ge "$MAX_FAILURES" ]; then
        log_message "Too many consecutive short failures: $failure_count; supervisor stopping"
        exit 1
    fi

    log_message "Restarting in ${RESTART_DELAY}s; short_failures=$failure_count"

    sleep "$RESTART_DELAY"
done