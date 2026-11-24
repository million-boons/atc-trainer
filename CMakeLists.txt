cmake_minimum_required(VERSION 3.22)
project(atc_trainer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
# P-03: the panel CPU has no AVX2/FMA.
add_compile_options(-march=x86-64-v2 -Wall -Wextra -Wpedantic)

option(ATC_BUILD_TESTS "Build unit tests" ON)
find_package(Threads REQUIRED)

# Qt-free, hardware-free logic.
add_library(atc_logic STATIC
  src/ptt/PttInputManager.cpp
  src/core/TransmissionController.cpp
  src/audio/PrerollRing.cpp
  src/audio/UtteranceChecks.cpp
  src/audio/WavFile.cpp
  src/audio/SideTone.cpp
  src/bench/PhraseFile.cpp
  src/bench/Scoring.cpp
  src/stt/SttService.cpp)
target_include_directories(atc_logic PUBLIC src)

# Linux input adapter and its monitor tool (needs libevdev-dev).
find_path(EVDEV_INCLUDE libevdev/libevdev.h PATH_SUFFIXES libevdev-1.0)
find_library(EVDEV_LIB evdev)
if(EVDEV_INCLUDE AND EVDEV_LIB)
  add_library(atc_evdev STATIC src/ptt/EvdevPttSource.cpp)
  target_include_directories(atc_evdev PUBLIC src PRIVATE ${EVDEV_INCLUDE})
  target_link_libraries(atc_evdev PUBLIC ${EVDEV_LIB} Threads::Threads)
  add_executable(ptt_monitor tools/ptt_monitor.cpp)
  target_link_libraries(ptt_monitor PRIVATE atc_logic atc_evdev)
else()
  message(STATUS "libevdev not found: ptt_monitor will not be built")
endif()

# Microphone capture (needs libpulse-dev).
find_path(PULSE_INCLUDE pulse/pulseaudio.h)
find_library(PULSE_LIB pulse)
if(PULSE_INCLUDE AND PULSE_LIB)
  add_library(atc_pulse STATIC src/audio/PulseCapture.cpp src/audio/PulseSideTone.cpp)
  target_include_directories(atc_pulse PUBLIC src PRIVATE ${PULSE_INCLUDE})
  target_link_libraries(atc_pulse PUBLIC atc_logic ${PULSE_LIB} Threads::Threads)
  add_executable(audio_probe tools/audio_probe.cpp)
  target_link_libraries(audio_probe PRIVATE atc_pulse)
  if(TARGET atc_evdev)
    add_executable(ptt_record tools/ptt_record.cpp)
    target_link_libraries(ptt_record PRIVATE atc_pulse atc_evdev)
  endif()
else()
  message(STATUS "libpulse not found: audio_probe and ptt_record will not be built")
endif()

# Speech benchmark (needs tools/fetch_stt.sh first).
set(TP ${CMAKE_SOURCE_DIR}/third_party)
if(EXISTS ${TP}/vosk/libvosk.so AND EXISTS ${TP}/whisper-install/lib/libwhisper.so AND EXISTS ${TP}/json/nlohmann/json.hpp)
  file(GLOB_RECURSE GGML_CPU_SO ${TP}/whisper-install/libggml-cpu-*.so ${TP}/whisper-install/*/libggml-cpu-*.so)
  list(GET GGML_CPU_SO 0 GGML_CPU_ONE)
  get_filename_component(GGML_DIR ${GGML_CPU_ONE} DIRECTORY)
  add_library(atc_stt STATIC src/stt/VoskStt.cpp src/stt/WhisperStt.cpp src/stt/SttCatalog.cpp src/stt/SttFactory.cpp)
  target_include_directories(atc_stt PUBLIC src)
  target_include_directories(atc_stt SYSTEM PUBLIC ${TP}/json PRIVATE ${TP}/vosk ${TP}/whisper-install/include)
  target_link_libraries(atc_stt PUBLIC ${TP}/vosk/libvosk.so ${TP}/whisper-install/lib/libwhisper.so
    ${TP}/whisper-install/lib/libggml.so ${TP}/whisper-install/lib/libggml-base.so)
  add_library(atc_phraseology STATIC src/phraseology/Phraseology.cpp src/phraseology/ScriptGen.cpp)
  target_include_directories(atc_phraseology PUBLIC src)
  target_include_directories(atc_phraseology SYSTEM PUBLIC ${TP}/json)
  target_link_libraries(atc_phraseology PUBLIC atc_logic)
  add_executable(phrase_gen tools/phrase_gen.cpp)
  target_link_libraries(phrase_gen PRIVATE atc_phraseology)
  add_executable(script_gen tools/script_gen.cpp)
  target_link_libraries(script_gen PRIVATE atc_phraseology)
  add_executable(stt_bench tools/stt_bench.cpp)
  target_compile_definitions(stt_bench PRIVATE ATC_GGML_DIR="${GGML_DIR}")
  target_link_libraries(stt_bench PRIVATE atc_logic atc_stt Threads::Threads)
  if(TARGET atc_pulse AND TARGET atc_evdev)
    add_executable(ptt_text tools/ptt_text.cpp)
    target_compile_definitions(ptt_text PRIVATE ATC_GGML_DIR="${GGML_DIR}" ATC_ROOT="${CMAKE_SOURCE_DIR}")
    target_link_libraries(ptt_text PRIVATE atc_pulse atc_evdev atc_logic atc_stt)
  endif()
else()
  message(STATUS "speech engines not found: run tools/fetch_stt.sh to build stt_bench")
endif()

# Qt UI (needs qt6-base-dev, qt6-declarative-dev and the qml6-module-qtquick* packages).
option(ATC_BUILD_UI "Build the Qt UI if Qt 6 is installed" ON)
if(ATC_BUILD_UI)
  find_package(Qt6 6.2 QUIET COMPONENTS Core Gui Qml Quick)
  if(Qt6_FOUND)
    add_subdirectory(ui)
  else()
    message(STATUS "Qt 6 not found: atc_trainer will not be built")
  endif()
endif()

if(ATC_BUILD_TESTS)
  enable_testing()
  add_test(NAME setup_panel COMMAND bash ${CMAKE_SOURCE_DIR}/tests/test_setup_panel.sh)
  find_package(GTest REQUIRED)
  add_executable(logic_tests
    tests/test_ptt.cpp tests/test_tx.cpp tests/test_tx_display.cpp tests/test_ring.cpp tests/test_checks.cpp
    tests/test_audio_files.cpp tests/test_bench.cpp tests/test_sidetone.cpp)
  target_link_libraries(logic_tests PRIVATE atc_logic GTest::gtest_main Threads::Threads)
  include(GoogleTest)
  gtest_discover_tests(logic_tests)
  if(TARGET atc_stt)
    add_executable(stt_tests tests/test_stt.cpp tests/test_phraseology.cpp)
    target_compile_definitions(stt_tests PRIVATE ATC_ROOT="${CMAKE_SOURCE_DIR}")
    target_link_libraries(stt_tests PRIVATE atc_phraseology atc_logic atc_stt GTest::gtest_main Threads::Threads)
    gtest_discover_tests(stt_tests)
  endif()
endif()
