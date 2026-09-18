#!/bin/sh

SCRIPT_DIR=/opt/rk3588_ai/scripts

if ! "$SCRIPT_DIR/stop.sh"; then
    echo "[ERROR] stop failed; restart cancelled" >&2
    exit 1
fi

exec "$SCRIPT_DIR/start.sh"