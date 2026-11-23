#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace atc {

// data/phraseology/*.json: what the trainee may say. Single source for the speech data (D-45).
// Never reads a scenario, so nothing here can know the expected value.
struct Phraseology {
  struct Intent {
    std::string id;
    std::vector<std::string> examples;
  };
  bool validated = false;
  std::map<std::string, std::vector<std::vector<std::string>>> pools;  // values -> spoken forms; includes "digit", "letter"
  std::map<std::string, std::vector<std::string>> slots;               // forms, e.g. "{digit} {digit}"
  std::vector<Intent> intents;
};

// All ten digits and all 26 letters are required. On error returns false and says where.
bool loadPhraseology(const std::string& file, Phraseology& out, std::string& error);

struct ExpandOptions {
  std::uint32_t seed = 1;
  int perExample = 200;        // random sentences per example
  int unkEvery = 0;            // every Nth sentence gets one [unk] at a random place; 0 = never
  std::vector<std::string> exclude;  // words the recogniser lacks: forms using them are not generated
  std::string boostIntent;     // expected intent: weighted, the others stay complete
  int boost = 1;
};
// Sentences in lower case, same result on every machine for the same options. Excluding a word
// removes only that spoken form, so a digit keeps its full weight. Empty, with error set, if a
// value or a sentence could then not be said at all.
std::vector<std::string> expandPhrases(const Phraseology& p, const ExpandOptions& o, std::string* error = nullptr);
// Every word that can occur, in order of first use.
std::vector<std::string> phraseologyWords(const Phraseology& p);
// Vocabulary prompt for whisper: every pool value, no example sentence.
std::string whisperPrompt(const Phraseology& p);

}  // namespace atc
