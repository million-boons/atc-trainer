#!/bin/bash
# Installs the PTT access rule. Run with sudo:  sudo deploy/install_udev.sh USERNAME
# Undo:  sudo rm /etc/udev/rules.d/72-atc-ptt.rules && sudo udevadm control --reload
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 1; }
[ $# = 1 ] && id "$1" >/dev/null 2>&1 || { echo "usage: sudo $0 USERNAME" >&2; exit 2; }
cd "$(dirname "$0")"
groupadd -f atcptt
usermod -aG atcptt "$1"
install -m 644 72-atc-ptt.rules /etc/udev/rules.d/72-atc-ptt.rules
udevadm control --reload
udevadm trigger --subsystem-match=input --action=change
udevadm settle
ls -lL /dev/input/atc-* 2>/dev/null || echo "no PTT device connected"
echo "Log out and in (or reboot) so that $1 is in group atcptt."
