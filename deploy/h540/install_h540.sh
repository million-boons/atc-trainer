#!/bin/bash
# Installs the persistent H540 settings for every user who logs in.
#   sudo deploy/h540/install_h540.sh            install (keeps an existing /etc/atc-h540.conf)
#   sudo deploy/h540/install_h540.sh --remove   undo
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "run with sudo" >&2; exit 1; }
cd "$(dirname "$0")"
case "${1:-}" in
  --remove)
    systemctl --global disable atc-h540.service || true
    rm -f /etc/systemd/user/atc-h540.service /etc/udev/rules.d/92-atc-h540-user.rules /usr/local/libexec/atc-h540-setup
    udevadm control --reload
    echo "removed (/etc/atc-h540.conf kept)"; exit 0 ;;
  "") ;;
  *) echo "usage: sudo $0 [--remove]" >&2; exit 2 ;;
esac
install -D -m 755 atc-h540-setup /usr/local/libexec/atc-h540-setup
install -D -m 644 atc-h540.service /etc/systemd/user/atc-h540.service
install -D -m 644 92-atc-h540-user.rules /etc/udev/rules.d/92-atc-h540-user.rules
[ -e /etc/atc-h540.conf ] || install -m 644 atc-h540.conf /etc/atc-h540.conf
systemctl --global enable atc-h540.service
udevadm control --reload
echo "Installed. Now, as your normal user (no sudo):"
echo "  systemctl --user daemon-reload && systemctl --user start atc-h540.service"
