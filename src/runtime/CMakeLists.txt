# RadioCore (1a) and ExerciseRuntime (3a), session b2.
# No sources yet: turn INTERFACE into STATIC with the first .cpp. Qt-free.
add_library(atc_runtime INTERFACE)
target_link_libraries(atc_runtime INTERFACE atc_logic)
