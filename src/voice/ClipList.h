#pragma once
#include <map>
#include <string>
#include <vector>

#include "voice/PhraseTemplate.h"

namespace atc {

// How digits, letters and runway sides are spoken: the first form in data/phraseology (D-45).
struct SpokenForms {
  std::map<std::string, std::string> digits, letters, sides;
};
bool loadSpokenForms(const std::string& phraseologyDir, SpokenForms& out, std::string& error);

// "09" -> "zero nine", "27L" -> "two seven left", "A3" -> "alpha three". Callsigns use the
// airfield's own words. Empty, with error set, for anything that cannot be said.
std::string spokenValue(const Airfield& a, const SpokenForms& f, const std::string& pool, const std::string& value, std::string& error);

struct ClipLine {
  std::string id, words;
  std::string file() const { return id + ".wav"; }
  // What the dev voice is given: the punctuation asks for a running or a closing tone.
  std::string prompt() const;
};
// Every clip the exercise can ask for, with the words to record. Reply text only; nothing is invented.
bool clipList(const Exercise& e, const SpokenForms& f, std::vector<ClipLine>& out, std::vector<std::string>& errors);

}  // namespace atc
