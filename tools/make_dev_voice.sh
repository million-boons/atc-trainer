#!/bin/bash
# Offline step (D-418): PLACEHOLDER pilot voice from Piper, en_US-norman-medium (D-413). Nothing here
# is shipped or used at run time. Run it only when no other session is building.
#   tools/make_dev_voice.sh fetch              download Piper and the voice, pinned checksums (network)
#   tools/make_dev_voice.sh make BIN [FOLDER]  BIN: folder with voice_tool; FOLDER: the voices folder,
#                                              default ~/.local/share/atc-trainer/voices
# Raw Piper files are kept in FOLDER/../voices-raw; the trimmed clips and manifest go to FOLDER.
set -euo pipefail
cd "$(dirname "$0")/.."
A=third_party/archives
VOICE=en_US-norman-medium
REPO=https://huggingface.co/rhasspy/piper-voices/resolve/e5349433ed883a32f893a434dc43a6c66b95ef7c/en/en_US/norman/medium
get() {  # sha256 name url
  if [ ! -f "$A/$2" ]; then
    echo "downloading $2"
    curl -fsSL --retry 2 -o "$A/$2.part" "$3"
    mv "$A/$2.part" "$A/$2"
  fi
  echo "$1  $A/$2" | sha256sum -c --quiet || { echo "CHECKSUM MISMATCH: $2" >&2; exit 1; }
}
case "${1:-}" in
fetch)
  mkdir -p "$A"
  get a50cb45f355b7af1f6d758c1b360717877ba0a398cc8cbe6d2a7a3a26e225992 piper_linux_x86_64.tar.gz \
    https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_linux_x86_64.tar.gz
  get b9739443232a80a59c7d18810dd856899bf16a7964725f5ab81ea49b1351cb71 $VOICE.onnx $REPO/$VOICE.onnx
  get 6c2db7f558a4a8deb9fe822583c1c5105f6c4e834dd0f9de8ad17a888ee9fe1d $VOICE.onnx.json $REPO/$VOICE.onnx.json
  if [ ! -d third_party/piper ]; then
    rm -rf third_party/piper.part
    mkdir third_party/piper.part
    tar -xzf $A/piper_linux_x86_64.tar.gz -C third_party/piper.part --strip-components=1
    mv third_party/piper.part third_party/piper
  fi
  echo "Piper ready"
  ;;
make)
  BIN=${2:?folder with voice_tool}
  OUT=${3:-$HOME/.local/share/atc-trainer/voices}
  RAW=$(dirname "$OUT")/voices-raw/$VOICE
  for x in "$BIN/voice_tool" third_party/piper/piper "$A/$VOICE.onnx"; do
    [ -e "$x" ] || { echo "missing: $x (run fetch, or build voice_tool)" >&2; exit 1; }
  done
  # voice_tool checks this too, but only after Piper has run.
  if git -C "$(dirname "$(realpath -m "$OUT")")" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "$OUT is inside a git working copy" >&2; exit 1
  fi
  rm -rf "$RAW.part"
  mkdir -p "$RAW.part"
  "$BIN/voice_tool" list --out "$RAW.part/clips.jsonl"
  # One process for all clips. Low priority: the panel stays usable.
  LIST=$(realpath "$RAW.part/clips.jsonl"); PIPER=$(realpath third_party/piper/piper); MODEL=$(realpath "$A/$VOICE.onnx")
  (cd "$RAW.part" && nice -n 10 "$PIPER" --model "$MODEL" --json-input --quiet < "$LIST" > piper.out)
  [ ! -d "$RAW" ] || mv "$RAW" "$RAW.$(date +%Y%m%d-%H%M%S)"   # raw takes are never deleted
  mv "$RAW.part" "$RAW"
  "$BIN/voice_tool" build --raw "$RAW" --out "$OUT/$VOICE" --voice $VOICE
  ;;
*)
  sed -n 2,7p "$0"; exit 2 ;;
esac
