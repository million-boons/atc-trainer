#pragma once
#include <string>
#include <vector>

#include "bench/PhraseFile.h"
#include "phraseology/Phraseology.h"

namespace atc {

struct ScriptOptions {
  std::uint32_t seed = 1;
  int scripted = 40;
  int negatives = 20;      // a third each: off-script, near-miss, malformed (wrong digit count)
  int variants = 10;       // scripted phrases spoken with tree/fife/niner
  std::vector<std::pair<std::string, std::string>> negativeTexts;  // kind ("off"/"near"), text
};

// A balanced recording script for one speaker, in random order. Digits are balanced by slot and
// position, and kept valid (runway 01-36, heading 001-360). Same seed, same script. Every scripted
// phrase is checked: reading it perfectly must score as correct, otherwise error is set.
std::vector<Phrase> makeScript(const Phraseology& p, const ScriptOptions& o, std::string& error);
std::string phraseLine(const Phrase& p);  // PhraseFile format

}  // namespace atc
