#!/bin/bash
# Downloads and unpacks the speech engines for the benchmark. Build machine only: the
# panel needs no network once third_party/archives is copied to it.
#   tools/fetch_stt.sh            download what is missing, verify, unpack, build whisper.cpp
set -euo pipefail
cd "$(dirname "$0")/.."
A=third_party/archives
mkdir -p "$A"
WV=1.9.5
WC=5359861c739e955e79d9a303bcbc70fb988958b1   # model repository commit
get() {  # sha256 name url
  if [ ! -f "$A/$2" ]; then
    echo "downloading $2"
    curl -fsSL --retry 2 -o "$A/$2.part" "$3"
    mv "$A/$2.part" "$A/$2"
  fi
  echo "$1  $A/$2" | sha256sum -c --quiet || { echo "CHECKSUM MISMATCH: $2" >&2; exit 1; }
}
get bbdc8ed85c43979f6443142889770ea95cbfbc56cffb5c5dcd73afa875c5fbb2 vosk-linux-x86_64-0.3.45.zip \
  https://github.com/alphacep/vosk-api/releases/download/v0.3.45/vosk-linux-x86_64-0.3.45.zip
get 20663dcac4d5cb783a579c54d98339344a688e4ec6e1b4a4b059fd1235454cc7 vosk-model-small-en-in-0.4.zip \
  https://alphacephei.com/vosk/models/vosk-model-small-en-in-0.4.zip
get 9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6 json-3.11.3.hpp \
  https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
get c77c5766f1cef09b6b7d47f21b546cbddd4157886b3b5d6d4f709e91e66c7c2b ggml-tiny.en-q5_1.bin \
  https://huggingface.co/ggerganov/whisper.cpp/resolve/$WC/ggml-tiny.en-q5_1.bin
get 4baf70dd0d7c4247ba2b81fafd9c01005ac77c2f9ef064e00dcf195d0e2fdd2f ggml-base.en-q5_1.bin \
  https://huggingface.co/ggerganov/whisper.cpp/resolve/$WC/ggml-base.en-q5_1.bin
WHISPER_SHA=ff1a9053feb509ff9d7729703355541ae9690073a6b1c40eb692c962e0dc1720
get "$WHISPER_SHA" whisper.cpp-$WV.tar.gz \
  https://github.com/ggml-org/whisper.cpp/archive/refs/tags/v$WV.tar.gz

unz() { [ -d "third_party/$1" ] || { mkdir "third_party/$1.part"; unzip -q "$A/$2" -d "third_party/$1.part"; mv "third_party/$1.part/$3" "third_party/$1"; rmdir "third_party/$1.part"; }; }
unz vosk vosk-linux-x86_64-0.3.45.zip vosk-linux-x86_64-0.3.45
unz vosk-model-small-en-in-0.4 vosk-model-small-en-in-0.4.zip vosk-model-small-en-in-0.4
mkdir -p third_party/json/nlohmann
cp "$A/json-3.11.3.hpp" third_party/json/nlohmann/json.hpp
if [ ! -d third_party/whisper.cpp ]; then
  mkdir third_party/whisper.part
  tar -xzf "$A/whisper.cpp-$WV.tar.gz" -C third_party/whisper.part --strip-components=1
  mv third_party/whisper.part third_party/whisper.cpp
fi
# P-03: baseline plus separate CPU variants chosen at run time. Nothing needs AVX2.
if [ ! -f third_party/whisper-install/lib/libwhisper.so ]; then
  cmake -S third_party/whisper.cpp -B third_party/whisper-build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PWD/third_party/whisper-install" -DCMAKE_INSTALL_LIBDIR=lib \
    -DBUILD_SHARED_LIBS=ON -DGGML_NATIVE=OFF -DGGML_BACKEND_DL=ON -DGGML_CPU_ALL_VARIANTS=ON \
    -DGGML_OPENMP=OFF -DWHISPER_BUILD_TESTS=OFF -DWHISPER_BUILD_EXAMPLES=OFF -DWHISPER_BUILD_SERVER=OFF
  cmake --build third_party/whisper-build -j"${JOBS:-2}"
  cmake --install third_party/whisper-build
fi
echo "speech engines ready"
