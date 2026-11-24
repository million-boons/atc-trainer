# RadioCore (1a) and ExerciseRuntime (3a), session b2. Qt-free; hardware behind RadioDevices.h.
add_library(atc_runtime STATIC RadioCore.cpp)
target_link_libraries(atc_runtime PUBLIC atc_logic Threads::Threads)
if(TARGET atc_pulse AND TARGET atc_evdev)
  add_library(atc_devices STATIC RealDevices.cpp)
  target_link_libraries(atc_devices PUBLIC atc_runtime atc_pulse atc_evdev)
endif()
