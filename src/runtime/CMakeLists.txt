# RadioCore (1a) and ExerciseRuntime (3a), session b2. Qt-free; hardware behind RadioDevices.h.
add_library(atc_runtime STATIC RadioCore.cpp)
target_link_libraries(atc_runtime PUBLIC atc_logic Threads::Threads)
