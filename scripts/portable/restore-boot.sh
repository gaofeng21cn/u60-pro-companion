#!/bin/sh
# Restore the factory boot behaviour. Keep all app data and new credentials.
#
# This must work after the in-place upgrade path, not only after a first
# install. Upgrades rename the installed release, so requiring an install
# backup whose name equals the current release would always fail once the
# device has been upgraded (it would look for
# /data/u60-install-backups/<current> while only the original install backup
# and a chain of upgrade backups exist). Find the factory rc.local instead:
# the first-install backup holds it, and every upgrade backup carries the
# release it was upgrading from.
set -eu
umask 077
cd "$(dirname "$0")"
sha256sum -c SHA256SUMS >/dev/null
ID=$(cat RELEASE-ID)
case "$ID" in u60-pro-B28-[0-9]*|u60-pro-B31-[0-9]*) ;; *) exit 2;; esac
case "$ID" in *[!a-zA-Z0-9-]*) exit 2;; esac
INSTALLED=$(cat /data/u60-panel/portable-release 2>/dev/null || true)
case "$INSTALLED" in u60-pro-B28-[0-9]*|u60-pro-B31-[0-9]*) ;; *) echo 'No project installation found' >&2;exit 1;; esac
# Refuse to mix firmware variants: a B28 package must not touch a B31 device.
case "$ID" in u60-pro-B28-*) PKG_VARIANT=B28;; *) PKG_VARIANT=B31;; esac
case "$INSTALLED" in u60-pro-B28-*) DEV_VARIANT=B28;; *) DEV_VARIANT=B31;; esac
[ "$PKG_VARIANT" = "$DEV_VARIANT" ] || {
 echo 'Prepared package targets a different firmware variant' >&2;exit 1; }

# Locate the factory rc.local (the one without the project boot hook).
FACTORY_RC=""
for d in /data/u60-install-backups/*; do
 [ -d "$d" ] || continue
 [ -s "$d/rc.local" ] || continue
 if grep -q 'portable-boot.sh' "$d/rc.local"; then continue; fi
 FACTORY_RC="$d/rc.local"
 break
done
[ -n "$FACTORY_RC" ] || { echo 'Factory rc.local backup not found; use the documented manual recovery path' >&2;exit 1; }
sh -n "$FACTORY_RC"
[ "$(grep -c 'portable-boot.sh' /etc/rc.local)" = 1 ] || {
 echo 'Current rc.local does not carry the expected single project hook' >&2;exit 1; }

if grep -F ' /sbin/usb/compositions/usb_switch ' /proc/mounts >/dev/null || [ "$(cat /data/u60-panel/usb-macnet-enabled 2>/dev/null || true)" = 1 ];then
 echo 'Turn off Mac USB networking through its UI before restoring boot' >&2
 exit 1
fi

# Stop and disable every service this project installs, including the ones a
# first-install-only revert used to miss (terminal-auth guard and the USB NCM
# and ECM maintenance transactions).
SERVICES='u60-usb-isolate u60-usb-role u60-wifi-relay u60-standby u60-web u60-auth-bypass u60-ncm-trial u60-ecm-trial'
for name in $SERVICES;do
 [ ! -x "/etc/init.d/$name" ] || "/etc/init.d/$name" stop >/dev/null 2>&1 || true
 [ ! -x "/etc/init.d/$name" ] || "/etc/init.d/$name" disable >/dev/null 2>&1 || true
done
# The terminal-auth guard is procd-managed; make sure no stray watcher remains.
for p in $(ls /proc 2>/dev/null | grep -E '^[0-9]+$');do
 a1=$(tr '\0' '\n' < "/proc/$p/cmdline" 2>/dev/null | sed -n 2p)
 a2=$(tr '\0' '\n' < "/proc/$p/cmdline" 2>/dev/null | sed -n 3p)
 [ "$a1" = /data/u60-panel/auth-bypass.sh ] && [ "$a2" = watch ] && kill "$p" 2>/dev/null || true
done
# The prepared package is staged under /data/u60-packages/<release>, not the
# installed version, so the original install backup is the right place to
# journal this restore attempt.
BACKUP=$(dirname "$FACTORY_RC")
cp -p /etc/rc.local "$BACKUP/rc.local.before-restore"
cp -p "$FACTORY_RC" /etc/rc.local.portable-restore
mv /etc/rc.local.portable-restore /etc/rc.local
printf 'boot-restored\n' > "$BACKUP/status"
echo 'Stock boot restored; application data was retained. Current processes remain until power-off.'
echo 'Power off and turn on manually, then verify the stock UI and Wi-Fi. Do not erase the data directories.'
