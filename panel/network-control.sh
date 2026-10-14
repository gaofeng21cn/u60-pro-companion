#!/bin/sh
# LAN policy data plane for the existing network-profile/Mihomo owner.
# stdin manifest: "udp=<0|1> guard6=<0|1>" then "<MAC> <TCP port>".
# Port 0 means DIRECT; other ports are even 17920..17982, UDP uses port+1.
# Never flush stock chains, replace main routes, alter USB or start a daemon.
set -u
ROOT=${NETWORK_CONTROL_ROOT:-/data/u60-panel/network-control}
PANEL_ROOT=${PROFILE_ROOT:-/data/u60-panel}
PROFILE_LOCK=${PROFILE_LOCK:-/tmp/u60-network-profile.lock}
IPT=${IPTABLES:-iptables}
IP6T=${IP6TABLES:-ip6tables}
IP=${IP_CMD:-ip}
DEFAULT_UDP=17892
MARK=0x02000000/0x02000000
TABLE=10422
PREF=10422
MANIFEST=$ROOT/manifest
OWNED=$ROOT/resources-owned
PRIVATE='192.168.0.0/16 10.0.0.0/8 172.16.0.0/12 100.64.0.0/10 127.0.0.0/8 169.254.0.0/16 224.0.0.0/4'
say() { printf '%s\n' "$1"; }
profile() { cat "$PANEL_ROOT/network-profile" 2>/dev/null || printf direct; }
valid_manifest() {
 awk 'NR==1 {if($0 !~ /^udp=[01] guard6=[01]$/)exit 1;next}
 NF!=2 {exit 1}
 $1 !~ /^[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]$/ {exit 1}
 $2 !~ /^[0-9]+$/ || ($2!=0&&($2<17920||$2>17982||$2%2)) {exit 1}
 {if(seen[tolower($1)]++ || ++count>32)exit 1}
 END {if(NR==0)exit 1}' "$1"
}
flag() { sed -n "1s/.*$1=\([01]\).*/\1/p" "$1"; }
lock() {
 if mkdir "$PROFILE_LOCK" 2>/dev/null;then printf '%s\n' $$ > "$PROFILE_LOCK/owner";locked=1;return 0;fi
 holder=$(cat "$PROFILE_LOCK/owner" 2>/dev/null || true)
 # A child invoked by network-profile or the C adapter joins its parent's
 # existing lock. No independent writer can join another live process.
 if [ "$holder" = "$PPID" ] && kill -0 "$holder" 2>/dev/null;then locked=0;return 0;fi
 case "$holder" in ''|*[!0-9]*) ;; *) kill -0 "$holder" 2>/dev/null && return 1;; esac
 rm -f "$PROFILE_LOCK/owner";rmdir "$PROFILE_LOCK" 2>/dev/null || return 1
 mkdir "$PROFILE_LOCK" 2>/dev/null || return 1
 printf '%s\n' $$ > "$PROFILE_LOCK/owner";locked=1
}
cleanup() {
 rm -f "$ROOT/next.$$" "$ROOT/previous.$$"
 [ "${locked:-0}" = 1 ] || return 0
 rm -f "$PROFILE_LOCK/owner";rmdir "$PROFILE_LOCK" 2>/dev/null || true
}
conflicts() {
 [ -f "$OWNED" ] && return 0
 for spec in 'nat U60_NC_NAT' 'mangle U60_NC_UDP' 'filter U60_NC_FILTER' 'filter U60_NC_HOLD';do
  set -- $spec;"$IPT" -t "$1" -S "$2" >/dev/null 2>&1 && return 1
 done
 for chain in U60_NC_V6 U60_NC_DNS6;do "$IP6T" -S "$chain" >/dev/null 2>&1 && return 1;done
 "$IP" -4 rule show 2>/dev/null | grep -Eq '^10422:|fwmark 0x2000000(/0x2000000)?' && return 1
 [ -z "$("$IP" -4 route show table "$TABLE" 2>/dev/null)" ]
}
del4() { table=$1;chain=$2;shift 2;while "$IPT" -t "$table" -D "$chain" "$@" >/dev/null 2>&1;do :;done; }
del6() { chain=$1;shift;while "$IP6T" -D "$chain" "$@" >/dev/null 2>&1;do :;done; }
owned_chain4() { "$IPT" -t "$1" -S "$2" >/dev/null 2>&1; }
remove_rules() {
 [ -f "$OWNED" ] || return 0
 del4 nat PREROUTING -i br-lan -j U60_NC_NAT
 del4 mangle PREROUTING -i br-lan -p udp -j U60_NC_UDP
 del4 filter FORWARD -i br-lan ! -o tailscale0 -j U60_NC_FILTER
 for spec in 'nat U60_NC_NAT' 'mangle U60_NC_UDP' 'filter U60_NC_FILTER';do
  set -- $spec
  if owned_chain4 "$1" "$2";then "$IPT" -t "$1" -F "$2" >/dev/null || return 1;"$IPT" -t "$1" -X "$2" >/dev/null || return 1;fi
 done
 del6 FORWARD -i br-lan -j U60_NC_V6
 del6 INPUT -i br-lan -p udp --dport 53 -j U60_NC_DNS6
 del6 INPUT -i br-lan -p tcp --dport 53 -j U60_NC_DNS6
 for chain in U60_NC_V6 U60_NC_DNS6;do
  if "$IP6T" -S "$chain" >/dev/null 2>&1;then "$IP6T" -F "$chain" >/dev/null || return 1;"$IP6T" -X "$chain" >/dev/null || return 1;fi
 done
 while "$IP" -4 rule del pref "$PREF" fwmark "$MARK" lookup "$TABLE" >/dev/null 2>&1;do :;done
 "$IP" -4 route del local 0.0.0.0/0 dev lo table "$TABLE" >/dev/null 2>&1 || true
 ! "$IPT" -t nat -C PREROUTING -i br-lan -j U60_NC_NAT >/dev/null 2>&1 &&
 ! "$IPT" -t mangle -C PREROUTING -i br-lan -p udp -j U60_NC_UDP >/dev/null 2>&1
}
unpause() {
 [ -f "$OWNED" ] || return 0
 del4 filter FORWARD -i br-lan -j U60_NC_HOLD
 del4 filter INPUT -i br-lan -p tcp --dport 17891:17983 -j REJECT --reject-with tcp-reset
 del4 filter INPUT -i br-lan -p udp --dport 17892:17983 -j REJECT --reject-with icmp-port-unreachable
 if owned_chain4 filter U60_NC_HOLD;then "$IPT" -F U60_NC_HOLD >/dev/null || return 1;"$IPT" -X U60_NC_HOLD >/dev/null || return 1;fi
}
pause() {
 unpause || return 1
 "$IPT" -N U60_NC_HOLD >/dev/null || return 1
 for net in $PRIVATE;do "$IPT" -A U60_NC_HOLD -d "$net" -j RETURN >/dev/null || return 1;done
 "$IPT" -A U60_NC_HOLD -j REJECT --reject-with icmp-port-unreachable >/dev/null || return 1
 "$IPT" -I FORWARD 1 -i br-lan -j U60_NC_HOLD >/dev/null || return 1
 "$IPT" -I INPUT 1 -i br-lan -p tcp --dport 17891:17983 -j REJECT --reject-with tcp-reset >/dev/null || return 1
 "$IPT" -I INPUT 1 -i br-lan -p udp --dport 17892:17983 -j REJECT --reject-with icmp-port-unreachable >/dev/null
}
restore_quic() {
 [ "$(profile)" = clash ] || return 0
 "$IPT" -C FORWARD -i br-lan ! -o tailscale0 -p udp --dport 443 -j REJECT --reject-with icmp-port-unreachable >/dev/null 2>&1 ||
 "$IPT" -I FORWARD 1 -i br-lan ! -o tailscale0 -p udp --dport 443 -j REJECT --reject-with icmp-port-unreachable >/dev/null
}
build_rules() {
 manifest=$1
 udp=$(sed -n '1s/^udp=\([01]\) .*/\1/p' "$manifest")
 guard6=$(sed -n '1s/.*guard6=\([01]\)$/\1/p' "$manifest")
 "$IPT" -t nat -N U60_NC_NAT >/dev/null || return 1
 "$IPT" -N U60_NC_FILTER >/dev/null || return 1
 # Local administration and existing Tailscale subnet routes retain their owner.
 for net in $PRIVATE;do "$IPT" -t nat -A U60_NC_NAT -d "$net" -j RETURN >/dev/null || return 1;done
 # DNS to the router is private: DIRECT exceptions must precede local returns.
 # Insert ACCEPT for each direct MAC at the head; ACCEPT terminates only nat.
 while read -r mac port;do
  [ "$port" = 0 ] || continue
  "$IPT" -t nat -I U60_NC_NAT 1 -m mac --mac-source "$mac" -j ACCEPT >/dev/null || return 1
  "$IPT" -A U60_NC_FILTER -m mac --mac-source "$mac" -j RETURN >/dev/null || return 1
 done <<EOF
$(sed '1d' "$manifest")
EOF
 while read -r mac port;do
  [ -n "$mac" ] && [ "$port" != 0 ] || continue
  "$IPT" -t nat -A U60_NC_NAT -m mac --mac-source "$mac" -p tcp ! --dport 53 -j REDIRECT --to-ports "$port" >/dev/null || return 1
 done <<EOF
$(sed '1d' "$manifest")
EOF
 "$IPT" -t nat -I PREROUTING 1 -i br-lan -j U60_NC_NAT >/dev/null || return 1
 # Replace exactly our network-profile QUIC guard, retaining all stock filters.
 del4 filter FORWARD -i br-lan ! -o tailscale0 -p udp --dport 443 -j REJECT --reject-with icmp-port-unreachable
 if [ "$udp" = 0 ];then "$IPT" -A U60_NC_FILTER -p udp --dport 443 -j REJECT --reject-with icmp-port-unreachable >/dev/null || return 1;fi
 "$IPT" -I FORWARD 1 -i br-lan ! -o tailscale0 -j U60_NC_FILTER >/dev/null || return 1
 if [ "$udp" = 1 ];then
  "$IPT" -t mangle -N U60_NC_UDP >/dev/null || return 1
  "$IPT" -t mangle -A U60_NC_UDP -p udp --dport 53 -j RETURN >/dev/null || return 1
  for net in $PRIVATE;do "$IPT" -t mangle -A U60_NC_UDP -d "$net" -j RETURN >/dev/null || return 1;done
  # Tailscale advertised non-RFC1918 ranges also remain outside the proxy.
  "$IP" -o -4 route show table all dev tailscale0 2>/dev/null | while read -r destination rest;do
   case "$destination" in *.*.*.*/*) "$IPT" -t mangle -A U60_NC_UDP -d "$destination" -j RETURN >/dev/null || exit 1;; esac
  done || return 1
  while read -r mac port;do
   [ -n "$mac" ] || continue
   if [ "$port" = 0 ];then "$IPT" -t mangle -A U60_NC_UDP -m mac --mac-source "$mac" -j RETURN >/dev/null || return 1
   else "$IPT" -t mangle -A U60_NC_UDP -m mac --mac-source "$mac" -p udp -j TPROXY --on-ip 127.0.0.1 --on-port "$((port+1))" --tproxy-mark "$MARK" >/dev/null || return 1;fi
  done <<EOF
$(sed '1d' "$manifest")
EOF
  "$IPT" -t mangle -A U60_NC_UDP -p udp -j TPROXY --on-ip 127.0.0.1 --on-port "$DEFAULT_UDP" --tproxy-mark "$MARK" >/dev/null || return 1
  "$IP" -4 route add local 0.0.0.0/0 dev lo table "$TABLE" >/dev/null || return 1
  "$IP" -4 rule add pref "$PREF" fwmark "$MARK" lookup "$TABLE" >/dev/null || return 1
  "$IPT" -t mangle -I PREROUTING 1 -i br-lan -p udp -j U60_NC_UDP >/dev/null || return 1
 fi
 if [ "$guard6" = 1 ];then
  "$IP6T" -N U60_NC_V6 >/dev/null || return 1
  "$IP6T" -N U60_NC_DNS6 >/dev/null || return 1
  for net in fc00::/7 fe80::/10 ff00::/8;do "$IP6T" -A U60_NC_V6 -d "$net" -j RETURN >/dev/null || return 1;done
  while read -r mac port;do
   [ "$port" = 0 ] || continue
   "$IP6T" -A U60_NC_V6 -m mac --mac-source "$mac" -j RETURN >/dev/null || return 1
   "$IP6T" -A U60_NC_DNS6 -m mac --mac-source "$mac" -j RETURN >/dev/null || return 1
  done <<EOF
$(sed '1d' "$manifest")
EOF
  "$IP6T" -A U60_NC_V6 -j REJECT --reject-with icmp6-port-unreachable >/dev/null || return 1
  "$IP6T" -A U60_NC_DNS6 -j REJECT --reject-with icmp6-port-unreachable >/dev/null || return 1
  "$IP6T" -I FORWARD 1 -i br-lan -j U60_NC_V6 >/dev/null || return 1
  "$IP6T" -I INPUT 1 -i br-lan -p udp --dport 53 -j U60_NC_DNS6 >/dev/null || return 1
  "$IP6T" -I INPUT 1 -i br-lan -p tcp --dport 53 -j U60_NC_DNS6 >/dev/null || return 1
 fi
 "$IPT" -t nat -C PREROUTING -i br-lan -j U60_NC_NAT >/dev/null || return 1
 [ "$udp" = 0 ] || "$IPT" -t mangle -C PREROUTING -i br-lan -p udp -j U60_NC_UDP >/dev/null || return 1
 [ "$guard6" = 0 ] || "$IP6T" -C FORWARD -i br-lan -j U60_NC_V6 >/dev/null || return 1
}
status() {
 tcp=false;udp=false;guard=false;paused=false;route=false;rule=false
 available=false;iptables_ready=false;tproxy_supported=false;ipv6_guard_supported=false;mihomo_ready=false
 command -v "$IPT" >/dev/null 2>&1 && command -v "$IP" >/dev/null 2>&1 && iptables_ready=true
 if [ "$iptables_ready" = true ] && "$IPT" -m tproxy -h >/dev/null 2>&1;then tproxy_supported=true;fi
 if command -v "$IP6T" >/dev/null 2>&1 && command -v "$IP" >/dev/null 2>&1 && "$IP6T" -m mac -h >/dev/null 2>&1;then ipv6_guard_supported=true;fi
 [ "$iptables_ready" = true ] && available=true
 if [ "$(profile)" = clash ] && command -v curl >/dev/null 2>&1;then
  code=$(curl --noproxy '*' -s --connect-timeout 1 --max-time 1 -o /dev/null -w '%{http_code}' http://127.0.0.1:19090/version 2>/dev/null || true)
  case "$code" in 200|401) mihomo_ready=true;; esac
 fi
 "$IPT" -t nat -C PREROUTING -i br-lan -j U60_NC_NAT >/dev/null 2>&1 && tcp=true
 "$IPT" -t mangle -C PREROUTING -i br-lan -p udp -j U60_NC_UDP >/dev/null 2>&1 && udp=true
 "$IP6T" -C FORWARD -i br-lan -j U60_NC_V6 >/dev/null 2>&1 && guard=true
 "$IPT" -C FORWARD -i br-lan -j U60_NC_HOLD >/dev/null 2>&1 && paused=true
 "$IP" -4 route show table "$TABLE" 2>/dev/null | grep -Eq '^local (default|0.0.0.0/0).* dev lo' && route=true
 "$IP" -4 rule show 2>/dev/null | grep -Eq '^10422:.*fwmark 0x2000000/0x2000000.*lookup 10422' && rule=true
 [ "$route" = true ] && [ "$rule" = true ] || udp=false
 printf '{"ok":true,"available":%s,"iptables_ready":%s,"tproxy_supported":%s,"ipv6_guard_supported":%s,"mihomo_ready":%s,"profile":"%s","tcp_policy_hook":%s,"udp_tproxy_hook":%s,"ipv6_guard_hook":%s,"transition_guard":%s,"ipv6_proxy":false,"observed_at":%s}\n' "$available" "$iptables_ready" "$tproxy_supported" "$ipv6_guard_supported" "$mihomo_ready" "$(profile)" "$tcp" "$udp" "$guard" "$paused" "$(date +%s)"
}
clients() {
 # DHCP leases are the only portable source available to the panel owner.
 # Emit an explicit unknown state when no lease exists; never invent an IP.
 first=true;printf '{"ok":true,"clients":['
 if [ -r "${DHCP_LEASES:-/tmp/dhcp.leases}" ];then
  while read -r expiry mac ip host clientid rest;do
   printf '%s' "$mac" | grep -Eq '^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$' || continue
   printf '%s' "$ip" | awk -F. 'NF==4 {for(i=1;i<=4;i++)if($i !~ /^[0-9]+$/||$i>255)exit 1;exit 0}{exit 1}' || continue
   case "$host" in *[!A-Za-z0-9._-]*|'')host=unknown;;esac
   policy=unmanaged
   row=$(grep -i "^$mac[[:space:]]" "$MANIFEST" 2>/dev/null | head -n1 || true)
   [ -n "$row" ] && policy=managed
   [ "$first" = true ] || printf ',';first=false
   printf '{"mac":"%s","ip":"%s","hostname":"%s","policy":"%s"}' "$(printf '%s' "$mac"|tr A-F a-f)" "$ip" "$host" "$policy"
  done < "${DHCP_LEASES:-/tmp/dhcp.leases}"
 fi
 printf ']}\n'
}
action=${1:-status}
if [ "$action" = status ];then status;exit 0;fi
case "$action" in clients) clients;exit 0;; apply|refresh|off|pause) ;; *) say '{"ok":false,"error":"invalid_action"}';exit 2;; esac
umask 077;mkdir -p "$ROOT" || exit 1;chmod 700 "$ROOT" || exit 1
lock || { say '{"ok":false,"error":"network_busy"}';exit 3; }
trap cleanup EXIT HUP INT TERM
if [ "$action" = off ];then
 if remove_rules && restore_quic && unpause && rm -f "$MANIFEST" "$OWNED";then
  say '{"ok":true,"active":false}';exit 0
 fi
 say '{"ok":false,"error":"remove_failed"}';exit 1
fi
conflicts || { say '{"ok":false,"error":"resource_conflict"}';exit 4; }
: > "$OWNED" || exit 1
if [ "$action" = pause ];then pause && { say '{"ok":true,"paused":true}';exit 0; };say '{"ok":false,"error":"transition_guard_failed"}';exit 1;fi
if [ "$action" = refresh ];then
 [ -r "$MANIFEST" ] || { say '{"ok":true,"active":false}';exit 0; }
 cp "$MANIFEST" "$ROOT/next.$$" || exit 1
else cat > "$ROOT/next.$$" || exit 1;fi
valid_manifest "$ROOT/next.$$" || { say '{"ok":false,"error":"invalid_manifest"}';exit 2; }
if [ "$(profile)" != clash ];then
 remove_rules && unpause && mv "$ROOT/next.$$" "$MANIFEST" && { say '{"ok":true,"active":false,"saved":true}';exit 0; }
 say '{"ok":false,"error":"inactive_cleanup_failed"}';exit 1
fi
[ ! -r "$MANIFEST" ] || cp "$MANIFEST" "$ROOT/previous.$$"
if pause && remove_rules && build_rules "$ROOT/next.$$" && mv "$ROOT/next.$$" "$MANIFEST" && unpause;then say '{"ok":true,"active":true,"rules_verified":true}';exit 0;fi
# Restore only our previous manifest. Keep the transition guard if this fails;
# local management/ADB remain outside the guarded forwarding path.
if remove_rules && { [ ! -r "$ROOT/previous.$$" ] || build_rules "$ROOT/previous.$$"; } && restore_quic && unpause;then
 say '{"ok":false,"error":"apply_failed","rolled_back":true}';exit 1
fi
pause >/dev/null 2>&1 || true
say '{"ok":false,"error":"rollback_failed","transition_guard":true}';exit 1
