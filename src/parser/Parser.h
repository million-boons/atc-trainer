#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/ParsedInstruction.h"
#include "stt/SpeechRecognizer.h"

namespace atc {

// Parsing rules from data/phraseology (02 section 4, D-45). Holds every pool value, never only
// the expected ones: nothing here knows the scenario.
struct ParserRules {
  using Words = std::vector<std::string>;  // normalised words
  struct Value {
    std::string id;
    std::vector<Words> spoken;
  };
  struct Slot {
    // Pool names, or "=word" for a fixed word of the form ("=decimal" writes the decimal point).
    std::vector<std::vector<std::string>> forms;
    std::vector<Words> markers;  // empty: the slot starts with a value of its first pool
    int lo = 0, hi = 0;          // range of the number formed by the digits; hi 0 = no check
  };
  struct Intent {
    std::string id;
    std::vector<Words> cues;
    std::vector<std::string> required, optional;  // slots or flags
  };
  bool placeholder = true;
  std::map<std::string, std::vector<Value>> pools;
  std::map<std::string, Slot> slots;
  std::map<std::string, std::vector<Words>> flags;
  std::vector<Intent> intents;
};
bool loadParserRules(const std::string& phraseologyFile, ParserRules& out, std::string& error);

struct ParseOptions {
  double minConfidence = -1;  // a field below this is uncertain. Off: not usable on the engines so far
};

// Pure function. A value is taken exactly as said or not at all; every word of the transmission
// must be explained, otherwise the result is AMBIGUOUS ("cancel", "not", a second value, a number
// without its marker). The longest cue wins ("line up and wait" is not "line up" plus two words);
// an intent whose only cue is a flag of another intent found ("hold short" with "taxi") is that
// flag, not a second instruction. tx, modelId and modelConfig are the caller's.
ParsedInstruction parseInstruction(const SttResult& heard, const ParserRules& rules, const ParseOptions& o = {});

}  // namespace atc
