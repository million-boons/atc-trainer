# RadioCore (1a) and ExerciseRuntime (3a), session b2. Qt-free; hardware behind RadioDevices.h.
add_library(atc_runtime STATIC RadioCore.cpp)
target_link_libraries(atc_runtime PUBLIC atc_logic Threads::Threads)
if(TARGET atc_pulse AND TARGET atc_evdev)
  add_library(atc_devices STATIC RealDevices.cpp)
  target_link_libraries(atc_devices PUBLIC atc_runtime atc_pulse atc_evdev)
endif()
# ExerciseRuntime (3a), its coverage check and the log file. Needs JSON, like the engine.
if(TARGET sim_cli AND TARGET parse_cli)
  add_library(atc_exercise STATIC ExerciseRuntime.cpp ExerciseCheck.cpp JsonlEventLog.cpp)
  target_link_libraries(atc_exercise PUBLIC atc_runtime atc_engine atc_parser atc_voice)
endif()
# Phraseology Studio without a screen (09, D-331). Qt-free, although the service speaks the screen's interface.
if(TARGET atc_exercise AND TARGET atc_storage)
  add_library(atc_studio STATIC ${CMAKE_SOURCE_DIR}/src/app/StudioService.cpp ${CMAKE_SOURCE_DIR}/src/voice/TakeStore.cpp
    ${CMAKE_SOURCE_DIR}/src/phraseology/PhraseologyEdit.cpp ${CMAKE_SOURCE_DIR}/src/storage/DataCopy.cpp
    ${CMAKE_SOURCE_DIR}/src/app/PlanStore.cpp ${CMAKE_SOURCE_DIR}/src/app/PlannerService.cpp)
  target_include_directories(atc_studio SYSTEM PRIVATE ${TP}/json)
  target_link_libraries(atc_studio PUBLIC atc_exercise atc_storage)
endif()
