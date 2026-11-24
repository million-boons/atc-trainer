#!/bin/bash
# Tests deploy/setup_panel.sh: a real dry run on this machine, then every path with a fake root and
# stubbed amixer/pactl/udevadm/systemctl. Needs no root, changes nothing outside a temporary folder.
# As root only the real dry run is done:  sudo tests/test_setup_panel.sh --dry-run
if [ $# -gt 1 ] || { [ $# = 1 ] && [ "$1" != --dry-run ]; }; then echo "usage: $0 [--dry-run]" >&2; exit 2; fi
if [ "$EUID" = 0 ] && [ "${1:-}" != --dry-run ]; then echo "refused: this test never runs as root, except with --dry-run" >&2; exit 2; fi
export ATC_NO_INSTALL=1   # second guard: setup_panel.sh then refuses a real install or uninstall
unset ATC_TEST ATC_ROOT
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
S="$REPO/deploy/setup_panel.sh"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
N=0; BAD=0
ok() { N=$((N + 1)); if eval "$2"; then :; else BAD=$((BAD + 1)); echo "FAIL: $1   [$2]"; [ -z "${OUT:-}" ] || sed 's/^/    | /' <<<"$OUT"; fi; }

# ---- real dry run --------------------------------------------------------------------------
WHO="$(id -un)"; [ "$EUID" != 0 ] || WHO="${SUDO_USER:-root}"
WHOHOME="$(getent passwd "$WHO" | cut -d: -f6)"
state() { ls -lAR --time-style=full-iso /etc/udev/rules.d /etc/systemd/user /usr/local/libexec /etc/atc-* 2>/dev/null; ls -l --time-style=full-iso "$WHOHOME/atc-setup-report.txt" 2>/dev/null; amixer -c 1 contents 2>/dev/null; pactl list sources 2>/dev/null | grep -E 'Name|Volume:|Mute'; }
B="$(state | sha256sum)"
OUT="$("$S" --dry-run 2>&1)"; RC=$?
ok "real dry run changes nothing" '[ "$B" = "$(state | sha256sum)" ]'
if grep -q 'no USB microphone' <<<"$OUT"; then echo "note: no USB headset on this machine"; else ok "real dry run succeeds" '[ $RC = 0 ] && grep -q "DRY RUN, nothing changed" <<<"$OUT"'; fi
if [ "${1:-}" = --dry-run ]; then echo "$((N - BAD)) of $N checks passed (dry run only)"; [ $BAD = 0 ]; exit; fi
for a in "" --uninstall; do
  OUT="$("$S" $a 2>&1)"; RC=$?
  ok "ATC_NO_INSTALL refuses the real script: ${a:-install}" '[ $RC = 2 ] && grep -q "ATC_NO_INSTALL is set" <<<"$OUT"'
done

# ---- fake machine --------------------------------------------------------------------------
mkdir -p "$T/bin" "$T/proc" "$T/sysin" "$T/dev" "$T/state"
cat > "$T/bin/amixer" <<'X'
#!/bin/bash
[ "$1" = -q ] && shift; c=$2; shift 2; A="$FAKE/state/agc$c"
case "$1" in
  controls) [ -f "$A" ] && echo "numid=5,iface=MIXER,name='Auto Gain Control'"; echo "numid=4,iface=MIXER,name='Mic Capture Volume'" ;;
  cget) echo "  : values=$(cat "$A")" ;;
  cset) [ -z "${STUCK_AGC:-}" ] && echo "$3" > "$A"; exit 0 ;;
esac
X
cat > "$T/bin/pactl" <<'X'
#!/bin/bash
case "$1 ${2:-}" in
  "list sources") for f in "$FAKE"/state/vol*; do c=${f##*vol}; printf 'Source #%s\n\tName: mon%s\n\tMonitor of Sink: out\n\t\talsa.card = "%s"\nSource #1%s\n\tName: mic%s\n\tMonitor of Sink: n/a\n\t\talsa.card = "%s"\n' $c $c $c $c $c $c; done ;;
  "info ") echo "Default Source: $(cat "$FAKE/state/default")" ;;
  get-source-volume*) v=$(cat "$FAKE/state/vol${2#mic}"); echo "Volume: front-left: 1 / $v% / 0 dB, front-right: 1 / $v% / 0 dB" ;;
  set-source-volume*) echo "${3%\%}" > "$FAKE/state/vol${2#mic}" ;;
  get-source-mute*) echo "Mute: no" ;;
esac
X
cat > "$T/bin/systemctl" <<'X'
#!/bin/bash
[ "$*" = "--user restart atc-audio.service" ] || exit 0
[ -z "${FAIL_SERVICE:-}" ] || exit 1
ATC_AUDIO_CONF="$ATC_ROOT/etc/atc-audio.conf" bash "$ATC_ROOT/usr/local/libexec/atc-audio-setup" >/dev/null
X
cat > "$T/bin/udevadm" <<'X'
#!/bin/bash
echo "$*" >> "$FAKE/udev.log"
case "$*" in
  "info -q property -p "*) cat "${@: -1}/props" 2>/dev/null ;;
  "trigger --action=change "*) [ -n "${FAIL_TAG:-}" ] || echo "TAGS=:uaccess:" >> "${@: -1}/props" ;;
  "trigger --action=add "*controlC*) [ -n "${FAIL_REPLUG:-}" ] || ATC_AUDIO_CONF="$ATC_ROOT/etc/atc-audio.conf" bash "$ATC_ROOT/usr/local/libexec/atc-audio-setup" >/dev/null ;;
esac
exit 0
X
chmod +x "$T"/bin/*
export FAKE="$T" ATC_TEST=1 ATC_PROC_ASOUND="$T/proc" ATC_SYS_SOUND="$T/snd" ATC_SYS_INPUT="$T/sysin" ATC_DEV_INPUT="$T/dev" ATC_AUDIO_WAIT=0 ATC_REPLUG_WAIT=2
P="$T/bin:$PATH"
card() { mkdir -p "$T/proc/card$1/pcm0c"; echo "$2" > "$T/proc/card$1/usbid"; echo 62 > "$T/state/vol$1"; [ "${3:-agc}" = none ] || echo on > "$T/state/agc$1"; echo " $1 [X]: USB-Audio - Test Headset $1" >> "$T/proc/cards"; }
event() { mkdir -p "$T/sysin/$1/device/id"; echo "$2" > "$T/sysin/$1/device/id/vendor"; echo "$3" > "$T/sysin/$1/device/id/product"; echo "$4" > "$T/sysin/$1/props"; : > "$T/dev/$1"; chmod "$5" "$T/dev/$1"; }
fresh() {  # new fake root and devices
  rm -rf "$T/root" "$T/proc" "$T/sysin" "$T/dev" "$T/state" "$T/udev.log"; mkdir -p "$T/root/home" "$T/root/etc/udev/rules.d" "$T/proc" "$T/sysin" "$T/dev" "$T/state"
  export ATC_ROOT="$T/root"; card 3 1234:abcd; echo mic3 > "$T/state/default"
  event event7 04d8 0055 ID_INPUT_MOUSE=1 600
  event event1 04d8 0055 "ID_INPUT_KEYBOARD=1" 000
}
run() { OUT="$(env "$@" PATH="$P" bash "$S" ${ARGS:-} 2>&1)"; RC=$?; }
tree() { (cd "$T/root" && find . -path ./home -prune -o -printf '%p %y %m %l\n' | sort; cd "$T/root" && find . -path ./home -prune -o -type f -exec sha256sum {} + | sort); }
vals() { cat "$T/state/agc3" "$T/state/vol3" | paste -sd' '; }

fresh; ARGS=--dry-run; A="$(tree)"; run; ok "dry run: nothing written" '[ $RC = 0 ] && [ "$A" = "$(tree)" ] && [ "$(vals)" = "on 62" ] && [ ! -e "$T/root/home/atc-setup-report.txt" ]'

# as root through sudo: dry run allowed, still writes nothing (id and runuser stubbed)
cat > "$T/bin/id" <<'X'
#!/bin/bash
[ -n "${FAKE_ROOT:-}" ] || exec /usr/bin/id "$@"
case "$*" in -u) echo 0 ;; -un) echo root ;; *) exec /usr/bin/id "$@" ;; esac
X
cat > "$T/bin/runuser" <<'X'
#!/bin/bash
[ "$1 $3" = "-u --" ] || exit 1
echo "$2" >> "$FAKE/runuser.log"; shift 3; exec "$@"
X
chmod +x "$T/bin/id" "$T/bin/runuser"
ME="$(/usr/bin/id -un)"
fresh; ARGS=--dry-run; A="$(tree)"; run FAKE_ROOT=1 SUDO_USER="$ME"; ok "dry run under sudo: allowed, nothing written" '[ $RC = 0 ] && grep -q "DRY RUN, nothing changed" <<<"$OUT" && [ "$A" = "$(tree)" ] && [ "$(vals)" = "on 62" ] && [ ! -e "$T/root/home/atc-setup-report.txt" ] && ! grep -qE "^(trigger|control|settle)" "$T/udev.log"'
ok "dry run under sudo acts as the sudo user" '[ "$(sort -u "$T/runuser.log")" = "$ME" ]'
run FAKE_ROOT=1 SUDO_USER=; ok "dry run as plain root is refused" '[ $RC = 2 ]'
ARGS=--verify; run FAKE_ROOT=1 SUDO_USER="$ME"; ok "verify under sudo is refused" '[ $RC = 2 ]'

# one mode only, in either order; refused before anything is done
fresh; ARGS=; run; A="$(tree)"; V="$(vals)"
for m in "--dry-run --uninstall" "--uninstall --dry-run" "--verify --uninstall" "--dry-run --verify"; do
  ARGS="$m"; run; ok "two modes refused: $m" '[ $RC = 2 ] && grep -q "only one of" <<<"$OUT" && [ "$A" = "$(tree)" ] && [ "$(vals)" = "$V" ]'
done
fresh; A="$(tree)"
for m in "" --uninstall; do ARGS="$m"; run ATC_TEST_ROOT=0; ok "refused without sudo: ${m:-install}" '[ $RC = 2 ] && grep -q "run with sudo" <<<"$OUT" && [ "$A" = "$(tree)" ]'; done

fresh; ARGS=; run; ok "install passes" '[ $RC = 0 ] && grep -q "RESULT: PASS" <<<"$OUT"'
ok "AGC off, level kept" '[ "$(vals)" = "off 62" ]'
ok "report written" 'grep -q "RESULT: PASS" "$T/root/home/atc-setup-report.txt" && grep -q "^PASS  settings re-applied" "$T/root/home/atc-setup-report.txt"'
ok "section keyed by vendor:product" 'grep -qx "\[1234:abcd\]" "$T/root/etc/atc-audio.conf" && grep -qx "CAPTURE_CONTROL=Mic Capture Volume" "$T/root/etc/atc-audio.conf"'
ok "only allowed places written" '! (cd "$T/root" && find . -type f -o -type l) | grep -vE "^\./(home/atc-setup-report.txt|usr/local/libexec/atc-|etc/systemd/user/|etc/udev/rules.d/|etc/atc-)" | grep -q .'
ok "keyboard never tested or granted" '! grep -q "event1" <<<"$(grep -E "PTT device" <<<"$OUT")"'
ok "replug used remove then add" 'grep -n "action=remove" "$T/udev.log" | head -1 | grep -q . && [ "$(grep -n "action=remove" "$T/udev.log" | tail -1 | cut -d: -f1)" -lt "$(grep -n "action=add" "$T/udev.log" | head -1 | cut -d: -f1)" ]'
A="$(tree)"; run; ok "second run changes nothing" '[ $RC = 0 ] && [ "$A" = "$(tree)" ] && grep -q "unchanged  /usr/local" <<<"$OUT"'
sed -i 's/CAPTURE_PERCENT=62/CAPTURE_PERCENT=48/' "$T/root/etc/atc-audio.conf"; A="$(tree)"; run; ok "tuned configuration kept and applied" '[ $RC = 0 ] && [ "$A" = "$(tree)" ] && [ "$(vals)" = "off 48" ]'
ARGS=--verify; run; ok "verify passes" '[ $RC = 0 ] && grep -q "RESULT: PASS" <<<"$OUT"'
echo on > "$T/state/agc3"; run; ok "verify reports AGC drift" '[ $RC = 1 ] && grep -q "FAIL  DRIFT 1234:abcd AGC" <<<"$OUT"'; echo off > "$T/state/agc3"
echo x >> "$T/root/etc/udev/rules.d/92-atc-audio.rules"; run; ok "verify reports a changed file" '[ $RC = 1 ] && grep -q "missing or different: /etc/udev" <<<"$OUT"'
# update over an existing installation fails: the previous files must come back
ARGS=; A="$(tree)"; run FAIL_REPLUG=1; ok "failed update restores the previous installation" '[ $RC = 1 ] && [ "$A" = "$(tree)" ] && [ "$(vals)" = "off 48" ]'
rm -rf "$T/proc/card3" "$T/state/vol3" "$T/state/agc3"; card 4 5678:ef01; echo mic4 > "$T/state/default"; run; ok "different headset gets its own section, first kept" '[ $RC = 0 ] && grep -qx "\[5678:ef01\]" "$T/root/etc/atc-audio.conf" && grep -qx "CAPTURE_PERCENT=48" "$T/root/etc/atc-audio.conf" && [ "$(cat "$T/state/agc4")" = off ]'
ARGS=--uninstall; run; ok "uninstall removes all but the configuration" '[ $RC = 0 ] && [ "$(cd "$T/root" && find . -path ./home -prune -o \( -type f -o -type l \) -print)" = "./etc/atc-audio.conf" ]'
run; ok "uninstall twice is harmless" '[ $RC = 0 ]'

for f in FAIL_SERVICE=1 FAIL_REPLUG=1 STUCK_AGC=1; do
  fresh; ARGS=; A="$(tree)"; run $f; ok "$f: everything undone" '[ $RC = 1 ] && [ "$A" = "$(tree)" ] && [ "$(vals)" = "on 62" ] && grep -q "RESULT: FAILED" "$T/root/home/atc-setup-report.txt"'
done
fresh; ARGS=; chmod 000 "$T/dev/event7"; A="$(tree)"; run
[ "$(id -u)" = 0 ] || ok "unreadable PTT device: everything undone" '[ $RC = 1 ] && [ "$A" = "$(tree)" ] && grep -q "FAIL  PTT device event7" <<<"$OUT"'
fresh; ARGS=; A="$(tree)"; run FAIL_TAG=1; ok "PTT readable but rule not applied: everything undone" '[ $RC = 1 ] && [ "$A" = "$(tree)" ] && grep -q "does not carry" <<<"$OUT"'
fresh; ARGS=; rm -rf "$T/sysin/event7"; run; ok "no PTT device: skipped, not failed" '[ $RC = 0 ] && grep -q "SKIP  no PTT device" <<<"$OUT"'
fresh; ARGS=; rm -rf "$T/proc/card3"; A="$(tree)"; run; ok "no headset: fails, nothing changed" '[ $RC = 1 ] && [ "$A" = "$(tree)" ]'
fresh; ARGS=; card 5 9999:0001; echo other > "$T/state/default"; A="$(tree)"; run; ok "two headsets, none default: refuses" '[ $RC = 1 ] && [ "$A" = "$(tree)" ] && grep -q "use --device" <<<"$OUT"'
ARGS="--device 9999:0001"; run; ok "--device chooses" '[ $RC = 0 ] && grep -qx "\[9999:0001\]" "$T/root/etc/atc-audio.conf" && [ "$(vals)" = "on 62" ]'
fresh; ARGS=; rm -rf "$T/proc/card3" "$T/state/vol3" "$T/state/agc3"; card 6 1111:2222 none; echo mic6 > "$T/state/default"; run; ok "headset without AGC control" '[ $RC = 0 ] && grep -qx "AGC=none" "$T/root/etc/atc-audio.conf"'

echo "$((N - BAD)) of $N checks passed"
[ $BAD = 0 ]
