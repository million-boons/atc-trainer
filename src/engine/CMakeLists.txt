# Validator, sequence engine, pilot behaviour, result metrics (2b, session 8d).
# No sources yet: turn INTERFACE into STATIC with the first .cpp. Qt-free.
add_library(atc_engine INTERFACE)
target_link_libraries(atc_engine INTERFACE atc_scenario)
