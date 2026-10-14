#!/bin/sh
# Root-only coordinator for the one-shot eSIM worker. The worker owns all
# QMI/APDU/SM-DP+ operations; this wrapper only serialises jobs and persists
# the last readback so the existing panel controller can poll it.
set -eu
umask 077
ROOT=/data/u60-panel
BIN=$ROOT/u60-esim
STATE_DIR=/tmp/u60-esim
STATE=$STATE_DIR/state.json
LOCK=$STATE_DIR/job.lock

[ ! -L "$STATE_DIR" ] || exit 1
mkdir -p "$STATE_DIR"
chmod 700 "$STATE_DIR"
[ -x "$BIN" ] || { printf '%s\n' '{"ok":false,"error":"eSIM 程序尚未安装"}'; exit 1; }

status() {
    if [ -s "$STATE" ]; then
        if [ "$(jsonfilter -i "$STATE" -e '@.job.state' 2>/dev/null || true)" = running ];then
            exec 8>"$LOCK"
            if flock -n 8;then
                sed 's/"state"[ ]*:[ ]*"running"/"state":"interrupted"/' "$STATE"
                return
            fi
        fi
        cat "$STATE"
    else
        "$BIN" status
    fi
}

case "${1:-status}" in
status)
    status
    ;;
run)
    exec 9>"$LOCK"
    if ! flock -n 9; then
        printf '%s\n' '{"ok":false,"message":"已有 eSIM 操作正在执行，请稍候"}'
        exit 1
    fi
    "$BIN" queued > "$STATE.new"
    mv -f "$STATE.new" "$STATE"
    request=$(cat)
    (
        printf '%s' "$request" | "$BIN" run >/dev/null 2>&1
    ) </dev/null >/dev/null 2>&1 &
    printf '%s\n' '{"ok":true,"message":"eSIM 操作已提交；请刷新状态查看结果"}'
    ;;
*)
    printf '%s\n' '{"ok":false,"error":"用法：status 或 run"}'
    exit 2
    ;;
esac
