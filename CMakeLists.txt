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

# D-410: no compiled-in absolute paths. The build tree has the installed layout (bin, lib, share),
# so the rule "<prefix>/bin -> <prefix>/share/atc-trainer" serves both.
add_compile_options(-ffile-prefix-map=${CMAKE_SOURCE_DIR}=. -ffile-prefix-map=${CMAKE_BINARY_DIR}=build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_INSTALL_RPATH "\$ORIGIN/../lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/share ${CMAKE_BINARY_DIR}/lib/ggml)
file(CREATE_LINK ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/share/atc-trainer SYMBOLIC)

option(ATC_BUILD_TESTS "Build unit tests" ON)
find_package(Threads REQUIRED)

# Qt-free, hardware-free logic.
add_library(atc_logic STATIC
  src/ptt/PttInputManager.cpp
  src/core/TransmissionController.cpp
  src/core/PathResolver.cpp
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
  add_library(atc_pulse STATIC src/audio/PulseCapture.cpp src/audio/PulseSideTone.cpp src/audio/PulsePlayback.cpp)
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
# Speech Lab recordings on disk.
if(EXISTS ${TP}/json/nlohmann/json.hpp)
  add_library(atc_storage STATIC src/storage/RecordingStore.cpp)
  target_include_directories(atc_storage PUBLIC src)
  target_include_directories(atc_storage SYSTEM PRIVATE ${TP}/json)
  target_link_libraries(atc_storage PUBLIC atc_logic Threads::Threads)
endif()
if(EXISTS ${TP}/vosk/libvosk.so AND EXISTS ${TP}/whisper-install/lib/libwhisper.so AND EXISTS ${TP}/json/nlohmann/json.hpp)
  file(GLOB_RECURSE GGML_CPU_SO ${TP}/whisper-install/libggml-cpu-*.so ${TP}/whisper-install/*/libggml-cpu-*.so)
  list(GET GGML_CPU_SO 0 GGML_CPU_ONE)
  get_filename_component(GGML_DIR ${GGML_CPU_ONE} DIRECTORY)
  file(GLOB GGML_PLUGINS ${GGML_DIR}/libggml-cpu-*.so)
  file(GLOB ENGINE_LIBS ${TP}/whisper-install/lib/libwhisper.so* ${TP}/whisper-install/lib/libggml*.so*)
  list(APPEND ENGINE_LIBS ${TP}/vosk/libvosk.so)
  foreach(L ${ENGINE_LIBS})
    get_filename_component(N ${L} NAME)
    file(CREATE_LINK ${L} ${CMAKE_BINARY_DIR}/lib/${N} SYMBOLIC)
  endforeach()
  foreach(L ${GGML_PLUGINS})
    get_filename_component(N ${L} NAME)
    file(CREATE_LINK ${L} ${CMAKE_BINARY_DIR}/lib/ggml/${N} SYMBOLIC)
  endforeach()
  install(FILES ${ENGINE_LIBS} DESTINATION lib)
  install(FILES ${GGML_PLUGINS} DESTINATION lib/ggml)
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
  target_link_libraries(stt_bench PRIVATE atc_logic atc_stt Threads::Threads)
  if(TARGET atc_pulse AND TARGET atc_evdev)
    add_executable(ptt_text tools/ptt_text.cpp)
    target_link_libraries(ptt_text PRIVATE atc_pulse atc_evdev atc_logic atc_stt)
  endif()
else()
  message(STATUS "speech engines not found: run tools/fetch_stt.sh to build stt_bench")
endif()

add_subdirectory(src/parser)
add_subdirectory(src/scenario)
add_subdirectory(src/engine)
add_subdirectory(src/voice)
add_subdirectory(src/runtime)

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
    tests/test_audio_files.cpp tests/test_bench.cpp tests/test_sidetone.cpp tests/test_paths.cpp)
  target_link_libraries(logic_tests PRIVATE atc_logic GTest::gtest_main Threads::Threads)
  include(GoogleTest)
  gtest_discover_tests(logic_tests)
  add_subdirectory(tests/contracts)
  if(TARGET atc_storage)
    add_executable(storage_tests tests/test_storage.cpp)
    target_link_libraries(storage_tests PRIVATE atc_storage GTest::gtest_main)
    gtest_discover_tests(storage_tests)
  endif()
  if(TARGET atc_stt)
    add_executable(stt_tests tests/test_stt.cpp tests/test_phraseology.cpp)
    target_link_libraries(stt_tests PRIVATE atc_phraseology atc_logic atc_stt GTest::gtest_main Threads::Threads)
    gtest_discover_tests(stt_tests)
  endif()
endif()

# cmake --install build --prefix DIR --strip. Models and audio are copied by rsync, never from here (D-31).
foreach(T atc_trainer ptt_monitor audio_probe ptt_record ptt_text stt_bench phrase_gen script_gen
        logic_tests contract_tests storage_tests stt_tests)
  if(TARGET ${T})
    install(TARGETS ${T} RUNTIME DESTINATION bin)
  endif()
endforeach()
install(DIRECTORY config data DESTINATION share/atc-trainer
        PATTERN "*.wav" EXCLUDE PATTERN "*.flac" EXCLUDE PATTERN "*.bin" EXCLUDE)
