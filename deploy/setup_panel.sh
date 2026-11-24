#!/bin/bash
# One installer for the panel: USB headset settings and PTT device access.
#   sudo ./deploy/setup_panel.sh              install or update (safe to run again)
#        ./deploy/setup_panel.sh --dry-run    show what would be done; changes nothing; with or without sudo
#        ./deploy/setup_panel.sh --verify     check an installation; no sudo
#   sudo ./deploy/setup_panel.sh --uninstall  remove it (/etc/atc-audio.conf is kept)
#   --device VID:PID   choose the headset if several USB microphones are connected
# Writes only in /usr/local/libexec, /etc/systemd/user, /etc/udev/rules.d and /etc/atc-audio.conf,
# plus the report ~/atc-setup-report.txt. No network. Any failed step undoes everything.
# ATC_NO_INSTALL=1 refuses a real install or uninstall (set by the test, which must never do either).
set -Eeuo pipefail
umask 022
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/panel"
if [ "${ATC_TEST:-}" = 1 ]; then ROOT="${ATC_ROOT:?}"; else ROOT=""; PATH=/usr/sbin:/usr/bin:/sbin:/bin; fi
SYSSND="${ATC_SYS_SOUND:-/sys/class/sound}"; SYSIN="${ATC_SYS_INPUT:-/sys/class/input}"; DEVIN="${ATC_DEV_INPUT:-/dev/input}"
export LC_ALL=C

LIBEXEC=/usr/local/libexec/atc-audio-setup
UNIT=/etc/systemd/user/atc-audio.service
WANTS=/etc/systemd/user/default.target.wants/atc-audio.service
RULE_AUDIO=/etc/udev/rules.d/92-atc-audio.rules
RULE_PTT=/etc/udev/rules.d/72-atc-ptt.rules
CONF=/etc/atc-audio.conf

MODE=""; DEVICE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run|--verify|--uninstall)
      [ -z "$MODE" ] || { echo "only one of --dry-run, --verify, --uninstall" >&2; exit 2; }
      MODE=${1#--} ;;
    --device) DEVICE="${2:-}"; [[ "$DEVICE" =~ ^[0-9a-f]{4}:[0-9a-f]{4}$ ]] || { echo "--device needs vid:pid, e.g. 046d:0a37" >&2; exit 2; }; shift ;;
    *) sed -n '2,10p' "$0"; exit 2 ;;
  esac
  shift
done
MODE="${MODE:-install}"

# ---- who ----------------------------------------------------------------------------------
# Test mode counts as root, since it writes only under ATC_ROOT; ATC_TEST_ROOT=0 keeps the real check.
is_root() { [ "$(id -u)" = 0 ] || { [ "${ATC_TEST:-}" = 1 ] && [ "${ATC_TEST_ROOT:-1}" = 1 ]; }; }
case "$MODE" in
  install|uninstall)
    if [ -n "${ATC_NO_INSTALL:-}" ] && [ "${ATC_TEST:-}" != 1 ]; then echo "$MODE refused: ATC_NO_INSTALL is set" >&2; exit 2; fi
    is_root || { echo "run with sudo: sudo $0" >&2; exit 2; }
    U="${SUDO_USER:-}"; [ "${ATC_TEST:-}" = 1 ] && U="$(id -un)"
    [ -n "$U" ] && [ "$U" != root ] || { echo "run through sudo from the panel user's own session, not as root" >&2; exit 2; } ;;
  dry-run)
    U="$(id -un)"; [ "$(id -u)" != 0 ] || U="${SUDO_USER:-}"
    [ -n "$U" ] && [ "$U" != root ] || { echo "run --dry-run as the panel user, or through sudo from that user's session" >&2; exit 2; } ;;
  *) U="$(id -un)"; [ "$U" != root ] || { echo "run $MODE as the panel user, without sudo" >&2; exit 2; } ;;
esac
UIDN="$(id -u "$U")"; HOMEDIR="$(getent passwd "$U" | cut -d: -f6)"
[ "${ATC_TEST:-}" = 1 ] && HOMEDIR="$ROOT/home"
as_user() {
  if [ "$(id -u)" = 0 ]; then
    runuser -u "$U" -- env HOME="$HOMEDIR" XDG_RUNTIME_DIR="/run/user/$UIDN" DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$UIDN/bus" LC_ALL=C "$@"
  else "$@"; fi
}
audio() { as_user env ATC_AUDIO_CONF="$ROOT$CONF" bash "$SRC/atc-audio-setup" "$@"; }

# ---- results ------------------------------------------------------------------------------
RESULTS=(); FAILS=0
note() { RESULTS+=("$(printf '%-4s  %s' "$1" "$2")"); [ "$1" != FAIL ] || FAILS=$((FAILS + 1)); }
report() {
  local text
  text="$(echo "ATC panel setup: $MODE   $(date '+%F %T')   user $U"
    [ ${#RESULTS[@]} -eq 0 ] || printf '%s\n' "${RESULTS[@]}"
    echo "RESULT: $1")"
  echo; echo "$text"
  [ "$MODE" = dry-run ] && return 0
  printf '%s\n' "$text" | as_user tee "$HOMEDIR/atc-setup-report.txt" >/dev/null && echo "(report: $HOMEDIR/atc-setup-report.txt)" || echo "cannot write the report" >&2
}

# ---- transaction --------------------------------------------------------------------------
ACTIVE=0; BK=""; UNDO=(); PREV=""
rollback() {
  local i kind path
  ACTIVE=0; set +e
  echo "Undoing changes..."
  for ((i = ${#UNDO[@]} - 1; i >= 0; i--)); do
    kind=${UNDO[i]%% *}; path=${UNDO[i]#* }
    case "$kind" in
      new) rm -f "$ROOT$path" ;;
      old) cp -a "$BK$path" "$ROOT$path" ;;
      dir) rmdir "$ROOT$path" 2>/dev/null ;;
    esac
  done
  udevadm control --reload; as_user systemctl --user daemon-reload
  [ -z "$PREV" ] || audio --set $PREV
  note INFO "all changes undone"
}
die() {
  trap - ERR INT TERM
  note FAIL "$1"
  [ "$ACTIVE" = 0 ] || rollback
  [ -z "$BK" ] || rm -rf "$BK"
  report "FAILED"
  exit 1
}
would() { echo "WOULD $*"; }
mkparents() {
  local d todo=()
  d=$(dirname "$1")
  while [ ! -d "$ROOT$d" ]; do todo=("$d" "${todo[@]}"); d=$(dirname "$d"); done
  for d in "${todo[@]}"; do mkdir -m 755 "$ROOT$d"; UNDO+=("dir $d"); done
}
save() { if [ -e "$ROOT$1" ] || [ -L "$ROOT$1" ]; then mkdir -p "$(dirname "$BK$1")"; cp -a "$ROOT$1" "$BK$1"; UNDO+=("old $1"); else UNDO+=("new $1"); fi; }
put() {  # source, destination, mode
  if [ -f "$ROOT$2" ] && [ ! -L "$ROOT$2" ] && cmp -s "$1" "$ROOT$2" && [ "$(stat -c %a "$ROOT$2")" = "$3" ]; then echo "unchanged  $2"; return; fi
  [ "$MODE" != dry-run ] || { would "install $2"; return; }
  mkparents "$2"; save "$2"
  install -m "$3" "$1" "$ROOT$2.atc-new" || { rm -f "$ROOT$2.atc-new"; return 1; }
  mv -f "$ROOT$2.atc-new" "$ROOT$2"; echo "installed  $2"
}

# ---- checks -------------------------------------------------------------------------------
check_audio() {
  local out rc=0 l
  out="$(audio --check 2>&1)" || rc=$?
  while read -r l; do
    case "$l" in ok*) note PASS "${l#ok }" ;; "") ;; *) note FAIL "$l" ;; esac
  done <<<"$out"
  return $rc
}
ptt_ids() { sed -n 's/^ATTRS{idVendor}=="\([0-9a-f]*\)", ATTRS{idProduct}=="\([0-9a-f]*\)".*/\1:\2/p' "$SRC/72-atc-ptt.rules"; }
ptt_events() {  # event names of connected PTT devices, never a keyboard
  local e id
  for e in "$SYSIN"/event*; do
    [ -r "$e/device/id/vendor" ] || continue
    id="$(cat "$e/device/id/vendor"):$(cat "$e/device/id/product")"
    ptt_ids | grep -qx "$id" || continue
    udevadm info -q property -p "$e" 2>/dev/null | grep -qx 'ID_INPUT_KEYBOARD=1' && continue
    udevadm info -q property -p "$e" 2>/dev/null | grep -qx 'ID_INPUT_MOUSE=1' || continue
    echo "${e##*/}"
  done
}
check_ptt() {
  local e n=0 bad=0
  for e in $(ptt_events); do
    n=$((n + 1))
    if ! udevadm info -q property -p "$SYSIN/$e" 2>/dev/null | grep -qE '^(CURRENT_)?TAGS=.*:uaccess:'; then
      note FAIL "PTT device $e does not carry the rule's access tag"; bad=1   # readable by luck is not enough
    elif as_user test -r "$DEVIN/$e"; then note PASS "PTT device $e readable by $U without sudo"; else note FAIL "PTT device $e not readable by $U"; bad=1; fi
  done
  [ $n -gt 0 ] || note SKIP "no PTT device connected: access not tested"
  for e in "$SYSIN"/event*; do
    udevadm info -q property -p "$e" 2>/dev/null | grep -qx 'ID_INPUT_KEYBOARD=1' || continue
    as_user test -r "$DEVIN/${e##*/}" && note WARN "keyboard-type device ${e##*/} is readable by $U (not caused by this installer)" || true
  done
  return $bad
}
check_files() {
  local p bad=0
  for p in "atc-audio-setup $LIBEXEC" "atc-audio.service $UNIT" "92-atc-audio.rules $RULE_AUDIO" "72-atc-ptt.rules $RULE_PTT"; do
    if cmp -s "$SRC/${p%% *}" "$ROOT${p#* }"; then note PASS "installed ${p#* }"; else note FAIL "missing or different: ${p#* }"; bad=1; fi
  done
  [ "$(readlink "$ROOT$WANTS" 2>/dev/null)" = "$UNIT" ] && note PASS "service starts at login" || { note FAIL "service not enabled at login"; bad=1; }
  return $bad
}

# ---- verify -------------------------------------------------------------------------------
if [ "$MODE" = verify ]; then
  check_files || true
  check_audio || true
  check_ptt || true
  [ $FAILS = 0 ] && { report "PASS"; exit 0; }
  report "FAILED ($FAILS)"; exit 1
fi

# ---- uninstall ----------------------------------------------------------------------------
if [ "$MODE" = uninstall ]; then
  trap 'die "uninstall stopped at line $LINENO"' ERR
  trap 'die "interrupted"' INT TERM
  BK="$(mktemp -d)"; ACTIVE=1
  for p in "$WANTS" "$UNIT" "$RULE_AUDIO" "$RULE_PTT" "$LIBEXEC"; do
    if [ -e "$ROOT$p" ] || [ -L "$ROOT$p" ]; then save "$p"; rm -f "$ROOT$p"; note PASS "removed $p"; else note SKIP "not installed: $p"; fi
  done
  rmdir "$ROOT$(dirname "$WANTS")" 2>/dev/null || true
  udevadm control --reload
  as_user systemctl --user daemon-reload
  for e in $(ptt_events); do udevadm trigger --action=change "$SYSIN/$e"; done
  udevadm settle --timeout=20
  ACTIVE=0; trap - ERR INT TERM; rm -rf "$BK"
  note INFO "$CONF kept (delete it yourself if not wanted); AGC and level stay as they are now"
  report "PASS"; exit 0
fi

# ---- install / dry-run --------------------------------------------------------------------
for c in amixer pactl udevadm systemctl install cmp awk; do command -v "$c" >/dev/null || die "needed command not found: $c"; done
for f in atc-audio-setup atc-audio.service 92-atc-audio.rules 72-atc-ptt.rules; do [ -r "$SRC/$f" ] || die "missing file: $SRC/$f"; done

# 1. headset
mapfile -t FOUND < <(audio --detect ${DEVICE:+"$DEVICE"})
[ ${#FOUND[@]} -gt 0 ] || die "no USB microphone${DEVICE:+ $DEVICE} connected"
PICK="${FOUND[0]}"
if [ ${#FOUND[@]} -gt 1 ]; then
  PICK=""
  for l in "${FOUND[@]}"; do IFS='|' read -r _ _ _ _ _ _ def _ <<<"$l"; [ "$def" != default ] || PICK="$l"; done
  [ -n "$PICK" ] || die "several USB microphones, none is the default; use --device: $(printf '%s; ' "${FOUND[@]%%|*}")"
fi
IFS='|' read -r ID CARD AGCCTL CAPCTL AGCNOW PCTNOW _ NAME <<<"$PICK"
[[ "$ID" =~ ^[0-9a-f]{4}:[0-9a-f]{4}$ && "$CARD" =~ ^[0-9]+$ ]] || die "unexpected device data: $PICK"
[[ "$PCTNOW" =~ ^[0-9]+$ ]] || die "PulseAudio does not show the microphone of $ID, or left and right levels differ"
[ -n "$CAPCTL" ] || die "no capture volume control found on $ID"
[[ "$AGCCTL$CAPCTL$NAME" =~ ^[A-Za-z0-9\ ._/\(\)-]*$ ]] || die "unexpected characters in control names"
note PASS "headset $ID ${NAME:-} (card $CARD): AGC control '$AGCCTL', capture control '$CAPCTL'"
[ "$AGCCTL" != none ] || note SKIP "this headset has no AGC control"
PREV="$ID $AGCNOW $PCTNOW"

# 2. files
if [ "$MODE" = install ]; then
  trap 'die "stopped at line $LINENO"' ERR
  trap 'die "interrupted"' INT TERM
  BK="$(mktemp -d)"; ACTIVE=1
fi
put "$SRC/atc-audio-setup" "$LIBEXEC" 755
put "$SRC/atc-audio.service" "$UNIT" 644
put "$SRC/92-atc-audio.rules" "$RULE_AUDIO" 644
put "$SRC/72-atc-ptt.rules" "$RULE_PTT" 644
if [ "$(readlink "$ROOT$WANTS" 2>/dev/null || true)" = "$UNIT" ]; then echo "unchanged  $WANTS"
elif [ "$MODE" = dry-run ]; then would "enable the service at login"
else mkparents "$WANTS"; save "$WANTS"; ln -sfn "$UNIT" "$ROOT$WANTS"; echo "installed  $WANTS"; fi

# 3. configuration: an existing section is never changed
if grep -qxF "[$ID]" "$ROOT$CONF" 2>/dev/null; then
  note PASS "kept the existing settings for $ID in $CONF"
else
  AGCWANT=off; [ "$AGCCTL" != none ] || AGCWANT=none
  SECTION="[$ID]
NAME=$NAME
AGC_CONTROL=$([ "$AGCCTL" = none ] || echo "$AGCCTL")
CAPTURE_CONTROL=$CAPCTL
AGC=$AGCWANT
CAPTURE_PERCENT=$PCTNOW"
  if [ "$MODE" = dry-run ]; then would "add to $CONF:"; sed 's/^/        /' <<<"$SECTION"
  else
    mkparents "$CONF"; TMP="$BK/conf.new"
    if [ -f "$ROOT$CONF" ]; then cat "$ROOT$CONF" >"$TMP"; echo >>"$TMP"
    else printf '# USB headset settings by vendor:product. Edit a value, then: systemctl --user restart atc-audio.service\n' >"$TMP"; fi
    printf '%s\n' "$SECTION" >>"$TMP"
    put "$TMP" "$CONF" 644
    note PASS "added settings for $ID (AGC $AGCWANT, capture $PCTNOW %)"
  fi
fi

if [ "$MODE" = dry-run ]; then
  would "reload udev rules and user services, start atc-audio.service, read back AGC and level"
  would "simulate replug: set AGC and level wrong, udevadm trigger remove/add card$CARD, check they are restored"
  for e in $(ptt_events); do would "re-apply PTT access to $e and test it as $U"; done
  [ -n "$(ptt_events)" ] || would "skip the PTT access test: no PTT device connected"
  report "DRY RUN, nothing changed"; exit 0
fi

# 4. apply and read back
udevadm control --reload
as_user systemctl --user daemon-reload
as_user systemctl --user restart atc-audio.service || die "atc-audio.service failed: journalctl --user -u atc-audio.service"
note PASS "service applied the settings"
check_audio || die "settings do not read back as configured"

# 5. replug: wrong values first, so that only the rule and the service can put them right
CPCT="$(awk -v s="[$ID]" '$0==s {i=1;next} /^\[/ {i=0} i && /^CAPTURE_PERCENT=/ {print substr($0,17); exit}' "$ROOT$CONF")"
CAGC="$(awk -v s="[$ID]" '$0==s {i=1;next} /^\[/ {i=0} i && /^AGC=/ {print substr($0,5); exit}' "$ROOT$CONF")"
[[ "$CPCT" =~ ^[0-9]+$ ]] || die "CAPTURE_PERCENT for $ID is not a number"
WRONG=$((10#$CPCT > 50 ? 10#$CPCT - 10 : 10#$CPCT + 10))
case "$CAGC" in off) WAGC=on ;; on) WAGC=off ;; *) WAGC=none ;; esac
audio --set "$ID" "$WAGC" "$WRONG" || die "cannot set test values"
if audio --check >/dev/null 2>&1; then die "test values did not take effect, replug test impossible"; fi
DEFBEFORE="$(audio --detect "$ID" | cut -d'|' -f7)"
udevadm trigger --action=remove "$SYSSND/card$CARD/controlC$CARD"; udevadm trigger --action=remove "$SYSSND/card$CARD"
udevadm settle --timeout=20
udevadm trigger --action=add "$SYSSND/card$CARD"; udevadm trigger --action=add "$SYSSND/card$CARD/controlC$CARD"
udevadm settle --timeout=20
ok=0
for ((i = 0; i < ${ATC_REPLUG_WAIT:-40}; i++)); do
  if audio --check >/dev/null 2>&1; then ok=1; break; fi
  sleep 1
done
[ $ok = 1 ] || die "settings were not re-applied after the simulated replug"
note PASS "settings re-applied after a simulated replug"
[ "$(audio --detect "$ID" | cut -d'|' -f7)" = "$DEFBEFORE" ] || note WARN "the default microphone changed during the replug test"

# 6. PTT device
for e in $(ptt_events); do udevadm trigger --action=change "$SYSIN/$e"; done
udevadm settle --timeout=20
check_ptt || die "PTT device access"
check_files || die "installed files"

ACTIVE=0; trap - ERR INT TERM; rm -rf "$BK"
report "PASS"
