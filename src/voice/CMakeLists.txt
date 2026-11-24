# Template parser, voice bank, PhraseAssembler (2c, session 8d). Qt-free.
if(EXISTS ${TP}/json/nlohmann/json.hpp)
  add_library(atc_voice STATIC PhraseTemplate.cpp VoiceBank.cpp PhraseAssembler.cpp)
  target_include_directories(atc_voice SYSTEM PRIVATE ${TP}/json)
  target_link_libraries(atc_voice PUBLIC atc_scenario)
else()
  add_library(atc_voice INTERFACE)
  target_link_libraries(atc_voice INTERFACE atc_logic)
endif()
