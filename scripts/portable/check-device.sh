#!/bin/sh
# Read-only hardware/firmware prerequisites; no installation or credentials.
set -eu
cd "$(dirname "$0")"
fail() { echo "$1" >&2;exit 1; }
[ "$(id -u)" = 0 ] || fail 'Root ADB shell required'
FW=$(ubus -t 5 call zwrt_zte_mdm.api get_zwrt_common_info '{}' | jsonfilter -e '@.wa_inner_version')
ID=$(cat RELEASE-ID)
printf '%s\n' "$ID" | grep -Eq '^u60-pro-B(28|31)-[0-9]{8}-[0-9]{6}$' || fail 'Invalid release id'
case "$FW:$ID" in
 BD_FLYMODEMMU5250V1.0.0B28:u60-pro-B28-*) KERNEL=5.15.185-perf;[ ! -e payload/data/u60-panel/compat-mode ] || fail 'Unexpected B28 compatibility mode';;
 BD_CNMU5250V1.0.0B31:u60-pro-B31-*) KERNEL=5.15.194-perf;[ "$(cat payload/data/u60-panel/compat-mode 2>/dev/null)" = b31-ui-first ] || fail 'B31 UI-first mode missing';;
 *) fail 'Firmware and prepared package do not match';;
esac
[ "$(uname -m)" = aarch64 ] && [ "$(uname -r)" = "$KERNEL" ] || fail 'Kernel or architecture mismatch'
IMEI=$(ubus -t 5 call zwrt_web device_info '{}' | jsonfilter -e '@.imei')
printf '%s\n' "$IMEI" | grep -Eq '^[0-9]{15}$' || fail 'Device identity unavailable'
[ "$(printf 'u60-imei-v1:%s' "$IMEI" | sha256sum | cut -d ' ' -f 1)" = "$(cat TARGET-IDENTITY-SHA256)" ] || fail 'Prepared package belongs to another device'
unset IMEI
sha256sum -c FACTORY-SHA256SUMS >/dev/null || fail 'Factory API/font/library fingerprint mismatch'
for name in ubus uci curl jsonfilter flock ip iptables ip6tables ebtables hostapd_cli start-stop-daemon;do
 command -v "$name" >/dev/null || fail "Missing required command: $name"
done
[ -c /dev/dri/card0 ] && [ -c /dev/net/tun ] || fail 'DRM or TUN device missing'
[ "$(cat /sys/class/input/event0/device/name)" = pmic_pwrkey ] || fail 'Power key layout mismatch'
[ "$(cat /sys/class/input/event3/device/name)" = sitronix_ts_i2c ] || fail 'Touch input layout mismatch'
[ -x /etc/init.d/zte_topsw_devui ] && [ -s /etc/rc.local ] || fail 'Factory UI/boot file missing'
# Power delivery and USB data enumeration are separate facts. A configured UDC
# proves the maintenance data link is present even when the device is drawing
# no charge current; retain the power-supply check for older firmware layouts.
USB_READY=0
for state_file in /sys/class/android_usb/android0/state /sys/class/udc/*/state;do
 [ -f "$state_file" ] || continue
 case "$(cat "$state_file" 2>/dev/null || true)" in CONFIGURED|configured) USB_READY=1;; esac
done
if [ "$USB_READY" = 0 ];then
 [ "$(cat /sys/class/power_supply/usb/online 2>/dev/null || true)" = 1 ] || fail 'Keep a USB cable connected for installation'
fi
[ ! -e /sys/class/net/eth0 ] || fail 'Remove the USB Ethernet adapter before installation'
FREE=$(df -k /data | awk 'END {print $(NF-2)}')
case "$FREE" in ''|*[!0-9]*) fail 'Cannot read available data space';; esac
[ "$FREE" -ge 400000 ] || fail 'At least 400 MB free in /data required'
echo "PASS: live firmware, identity and stock ABI prerequisites"
