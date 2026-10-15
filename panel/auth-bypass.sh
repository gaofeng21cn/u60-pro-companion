#!/bin/sh
# Hold the factory per-terminal "second_auth" enforcement gate at off.
#
# The factory daemon zwrt_wireless_auth reports terminal auth to the carrier
# and drives two local gates:
#   zwrt_router.tmp_router.auth_second_simcard_valid
#     -> firewall_redirect.sh builds the second_auth redirect/DROP chains.
#   zwrt_router.tmp_router.simcard_switch_flag
#     -> the only guarded call site of zte_router_second_auth_disconnect_dev.
# Both are read per event, but card events rewrite them to 1. This helper pins
# both to 0 and clears the rebuilt chains, so unrouted terminals keep the
# normal LAN forwarding path. It is reversible: disable removes the intent and
# stops the guard, after which the factory owns these fields again.
#
# This is a local user-owned device switch. It does not disable the carrier
# report path; it only stops local enforcement and client disconnects.
set -u

ROOT=${AUTH_BYPASS_ROOT:-/data/u60-panel/auth-bypass}
INTENT=${AUTH_BYPASS_INTENT:-/data/u60-panel/auth-bypass-enabled}
PIDFILE=${AUTH_BYPASS_PIDFILE:-/tmp/u60-auth-bypass.pid}
LOCKDIR=${AUTH_BYPASS_LOCK:-/tmp/u60-auth-bypass.lock}
INIT_SERVICE=${AUTH_BYPASS_INIT:-/etc/init.d/u60-auth-bypass}
UCI=${UCI_CMD:-uci}
IPT=${IPTABLES:-iptables}
IP6T=${IP6TABLES:-ip6tables}
WATCH_INTERVAL=${AUTH_BYPASS_INTERVAL:-5}

VALID_KEY=zwrt_router.tmp_router.auth_second_simcard_valid
SWITCH_KEY=zwrt_router.tmp_router.simcard_switch_flag
CHAINS='filter second_auth_redirect_chain'
CHAINS6='filter second_auth_redirect_chain'

say() { printf '%s\n' "$1"; }

locked=0
lock() {
  if mkdir "$LOCKDIR" 2>/dev/null; then printf '%s\n' $$ > "$LOCKDIR/owner"; locked=1; return 0; fi
  holder=$(cat "$LOCKDIR/owner" 2>/dev/null || true)
  case "$holder" in ''|*[!0-9]*) ;; *) kill -0 "$holder" 2>/dev/null && return 1 ;; esac
  rm -f "$LOCKDIR/owner"; rmdir "$LOCKDIR" 2>/dev/null || return 1
  mkdir "$LOCKDIR" 2>/dev/null || return 1
  printf '%s\n' $$ > "$LOCKDIR/owner"; locked=1
}
unlock() {
  [ "$locked" = 1 ] || return 0
  rm -f "$LOCKDIR/owner"; rmdir "$LOCKDIR" 2>/dev/null || true; locked=0
}

uci_get() { "$UCI" -q get "$1" 2>/dev/null; }
uci_zero() {
  # Set when present or different; commit through the app's UCI socket.
  cur=$(uci_get "$1")
  [ "$cur" = 0 ] && return 0
  "$UCI" -q set "$1=0" 2>/dev/null || return 1
  return 0
}
uci_commit() { "$UCI" -q commit zwrt_router 2>/dev/null || true; }

flush4() {
  for t in filter nat mangle; do "$IPT" -t "$t" -F second_auth_redirect_chain >/dev/null 2>&1 || true; done
}
flush6() {
  for t in filter nat mangle; do "$IP6T" -t "$t" -F second_auth_redirect_chain >/dev/null 2>&1 || true; done
}

# Drift = gate flipped back on, or any rule re-added to the chains.
drift() {
  [ "$(uci_get "$VALID_KEY")" = 0 ] || return 0
  [ "$(uci_get "$SWITCH_KEY")" = 0 ] || return 0
  n=$("$IPT" -S second_auth_redirect_chain 2>/dev/null | grep -c '^-A ' || true)
  [ "$n" = 0 ] || return 0
  n6=$("$IP6T" -S second_auth_redirect_chain 2>/dev/null | grep -c '^-A ' || true)
  [ "$n6" = 0 ] || return 0
  return 1
}

hold() {
  uci_zero "$VALID_KEY" || return 1
  uci_zero "$SWITCH_KEY" || return 1
  uci_commit
  flush4
  flush6
}

status_json() {
  valid=$(uci_get "$VALID_KEY"); switch=$(uci_get "$SWITCH_KEY")
  case "$valid" in 0|1) ;; *) valid=null ;; esac
  case "$switch" in 0|1) ;; *) switch=null ;; esac
  n=$("$IPT" -S second_auth_redirect_chain 2>/dev/null | grep -c '^-A ' || true)
  n6=$("$IP6T" -S second_auth_redirect_chain 2>/dev/null | grep -c '^-A ' || true)
  guard=false; [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE" 2>/dev/null || echo 0)" 2>/dev/null && guard=true
  enabled=false; [ -f "$INTENT" ] && enabled=true
  dr=false; drift && dr=true
  printf '{"ok":true,"enabled":%s,"guard_running":%s,"valid":%s,"switch":%s,"chain_rules":%s,"chain_rules6":%s,"drift":%s}\n' \
    "$enabled" "$guard" "$valid" "$switch" "$n" "$n6" "$dr"
}

start_guard() {
  # procd owns the watcher, independent of the UI request's process lifetime.
  "$INIT_SERVICE" enable >/dev/null 2>&1 || return 1
  "$INIT_SERVICE" start >/dev/null 2>&1 || return 1
  for attempt in 1 2 3 4 5; do
    if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE" 2>/dev/null || echo 0)" 2>/dev/null; then return 0; fi
    sleep 0.2
  done
  return 1
}
stop_guard() {
  if [ -x "$INIT_SERVICE" ]; then
    "$INIT_SERVICE" stop >/dev/null 2>&1 || true
    "$INIT_SERVICE" disable >/dev/null 2>&1 || true
  fi
  if [ -f "$PIDFILE" ]; then
    p=$(cat "$PIDFILE" 2>/dev/null || true)
    case "$p" in ''|*[!0-9]*) ;; *) kill "$p" 2>/dev/null || true ;; esac
    rm -f "$PIDFILE"
  fi
}

watch_loop() {
  umask 077
  # Single guard: a boot hook and an interactive enable may both try to start
  # one. The first live owner keeps the pidfile; later starters exit quietly.
  if [ -f "$PIDFILE" ]; then
    other=$(cat "$PIDFILE" 2>/dev/null || true)
    case "$other" in
      ''|*[!0-9]*) ;;
      *) [ "$other" != "$$" ] && kill -0 "$other" 2>/dev/null && return 0 ;;
    esac
  fi
  printf '%s\n' "$$" > "$PIDFILE"
  trap 'rm -f "$PIDFILE"' EXIT
  trap 'exit 0' HUP INT TERM
  while [ -f "$INTENT" ]; do
    if drift; then mkdir -p "$ROOT"; hold; fi
    sleep "$WATCH_INTERVAL"
  done
  rm -f "$PIDFILE"
}

action=${1:-status}
case "$action" in
  status) status_json; exit 0 ;;
  watch) watch_loop; exit 0 ;;
  enable|disable) ;;
  *) say '{"ok":false,"error":"invalid_action"}'; exit 2 ;;
esac

umask 077
mkdir -p "$ROOT" 2>/dev/null || true
lock || { say '{"ok":false,"error":"auth_busy"}'; exit 3; }
trap 'unlock' EXIT HUP INT TERM

if [ "$action" = disable ]; then
  rm -f "$INTENT"
  stop_guard
  say '{"ok":true,"enabled":false,"note":"factory owns the gate again; card events may re-enforce"}'
  exit 0
fi

[ -x "$INIT_SERVICE" ] || { say '{"ok":false,"error":"service_unavailable","message":"放行服务未安装，未修改门控"}'; exit 0; }
: > "$INTENT"
if ! hold; then
  say '{"ok":false,"error":"hold_failed","detail":"gate not confirmed off"}'
  exit 1
fi
start_guard || true
state=$(status_json)
case "$state" in
  *'"guard_running":true'*) printf '%s\n' "$state"; exit 0 ;;
  *) printf '%s\n' '{"ok":false,"error":"guard_not_running","message":"本机门控已写入，但守护未启动，请刷新重试"}'; exit 0 ;;
esac
