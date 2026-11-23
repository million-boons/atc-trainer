#!/bin/bash
# Runs every candidate, one at a time, in real time, on the given manifests.
#   tools/bench_all.sh OUTDIR MANIFEST [MANIFEST ...]
# Close other programs first; results count only when run on the panel.
set -euo pipefail
[ $# -ge 2 ] || { echo "usage: $0 OUTDIR MANIFEST [MANIFEST ...]" >&2; exit 2; }
R="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$1"; shift
mkdir -p "$OUT"
M=(); for m in "$@"; do M+=(--manifest "$m"); done
B="$R/build/stt_bench"; T="$R/third_party"; A="$T/archives"
V="$T/vosk-model-small-en-in-0.4"
P="$(cat "$R/data/bench/placeholder_prompt.txt")"   # vocabulary bias; placeholder, not phraseology
run() { local n="$1"; shift; echo "=== $n"; "$B" "${M[@]}" --out "$OUT/$n" "$@" | tee "$OUT/$n.txt"; }
{ date; uname -r; grep -m1 'model name' /proc/cpuinfo; cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor; uptime; } > "$OUT/machine.txt"
"$B" --lexicon "$R/data/bench/placeholder_vocabulary.txt" --model "$V" | tee "$OUT/lexicon.txt"
run vosk-free            --engine vosk --model "$V"
run vosk-grammar         --engine vosk --model "$V" --grammar "$R/data/bench/placeholder_vocabulary.txt"
run whisper-tiny         --engine whisper --threads 4 --model "$A/ggml-tiny.en-q5_1.bin"
run whisper-tiny-ctx768  --engine whisper --threads 4 --audio-ctx 768 --model "$A/ggml-tiny.en-q5_1.bin"
run whisper-base         --engine whisper --threads 4 --model "$A/ggml-base.en-q5_1.bin"
run whisper-tiny-prompt        --engine whisper --threads 4 --prompt "$P" --model "$A/ggml-tiny.en-q5_1.bin"
run whisper-tiny-ctx768-prompt --engine whisper --threads 4 --audio-ctx 768 --prompt "$P" --model "$A/ggml-tiny.en-q5_1.bin"
run whisper-base-ctx768-prompt --engine whisper --threads 4 --audio-ctx 768 --prompt "$P" --model "$A/ggml-base.en-q5_1.bin"
echo "results in $OUT"
