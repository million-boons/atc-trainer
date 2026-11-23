#!/bin/bash
# H540 microphone settings. No root needed except for "install".
#   tools/audio_setup.sh check              compare the headset with config/h540.conf (exit 1 on drift)
#   tools/audio_setup.sh apply              AGC off, not muted, volume from config/h540.conf
#   sudo tools/audio_setup.sh install       udev rule: AGC off at every plug-in and boot
set -uo pipefail
R="$(cd "$(dirname "$0")/.." && pwd)"
. "$R/config/h540.conf"
agc() { amixer -c "$CARD" cget name='Auto Gain Control' 2>/dev/null | sed -n 's/^  : values=//p'; }
src() { pactl list short sources | awk -v m="$SOURCE_MATCH" 'index($2, m) == 1 && $2 !~ /monitor/ {print $2; exit}'; }
vol() { LC_ALL=C pactl get-source-volume "$1" | grep -o '[0-9]*%' | head -1; }
muted() { LC_ALL=C pactl get-source-mute "$1" | grep -q yes; }
case "${1:-}" in
  install)
    [ "$(id -u)" = 0 ] || { echo "install needs sudo" >&2; exit 2; }
    install -m 644 "$R/deploy/91-atc-h540.rules" /etc/udev/rules.d/ && udevadm control --reload && echo "rule installed: replug the headset to test"
    ;;
  apply|check)
    S="$(src)"
    [ -n "$S" ] || { echo "FAIL headset not found"; exit 1; }
    if [ "$1" = apply ]; then
      amixer -q -c "$CARD" cset name='Auto Gain Control' off || exit 1
      pactl set-source-mute "$S" 0 && pactl set-source-volume "$S" "$SOURCE_VOLUME" || exit 1
    fi
    bad=0
    [ "$(agc)" = off ] && echo "ok   AGC off" || { echo "FAIL AGC is '$(agc)'"; bad=1; }
    [ "$(vol "$S")" = "$SOURCE_VOLUME" ] && echo "ok   volume $SOURCE_VOLUME" || { echo "FAIL volume $(vol "$S"), expected $SOURCE_VOLUME"; bad=1; }
    muted "$S" && { echo "FAIL muted"; bad=1; } || echo "ok   not muted"
    exit $bad
    ;;
  *) sed -n '2,5p' "$0"; exit 2 ;;
esac
