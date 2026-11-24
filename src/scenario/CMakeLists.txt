# Scenario loader and generator (2a, session 8d).
# No sources yet: turn INTERFACE into STATIC with the first .cpp. Qt-free.
add_library(atc_scenario INTERFACE)
target_link_libraries(atc_scenario INTERFACE atc_logic)
