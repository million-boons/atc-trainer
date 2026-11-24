# Template parser, voice bank, PhraseAssembler (2c, session 8d).
# No sources yet: turn INTERFACE into STATIC with the first .cpp. Qt-free.
add_library(atc_voice INTERFACE)
target_link_libraries(atc_voice INTERFACE atc_logic)
