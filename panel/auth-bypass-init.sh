#!/bin/sh /etc/rc.common
# Restart the terminal-auth bypass guard if the user previously enabled it.
# The guard itself does nothing unless /data/u60-panel/auth-bypass-enabled
# exists, so a factory-default device keeps the stock enforcement untouched.
START=98
STOP=90
USE_PROCD=1
start_service() {
 [ -f /data/u60-panel/auth-bypass-enabled ] || return 0
 [ -x /data/u60-panel/auth-bypass.sh ] || return 1
 procd_open_instance
 procd_set_param command /data/u60-panel/auth-bypass.sh watch
 procd_set_param respawn 3600 5 5
 procd_close_instance
}
