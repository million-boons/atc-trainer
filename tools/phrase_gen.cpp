// Speech data from data/phraseology (D-45). Never reads a scenario.
//   phrase_gen FILE.json sentences [--per N] [--unk-every N] [--boost INTENT:N] [--seed N]
//              [--exclude a,b,c]  words the model lacks (stt_bench --lexicon lists them)
//   phrase_gen FILE.json words | prompt
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#include "phraseology/Phraseology.h"
using namespace atc;

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: phrase_gen FILE.json sentences|words|prompt [--per N] [--unk-every N] [--boost INTENT:N] [--seed N]\n");
    return 2;
  }
  Phraseology p;
  std::string error;
  if (!loadPhraseology(argv[1], p, error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  if (!p.validated) std::fprintf(stderr, "note: PLACEHOLDER phraseology, not approved\n");
  ExpandOptions o;
  for (int i = 3; i + 1 < argc; i += 2) {
    const std::string k = argv[i], v = argv[i + 1];
    if (k == "--per") o.perExample = std::atoi(v.c_str());
    else if (k == "--unk-every") o.unkEvery = std::atoi(v.c_str());
    else if (k == "--seed") o.seed = static_cast<std::uint32_t>(std::atoi(v.c_str()));
    else if (k == "--exclude") {
      std::istringstream in(v);
      for (std::string w; std::getline(in, w, ',');) o.exclude.push_back(w);
    } else if (k == "--boost" && v.find(':') != std::string::npos) {
      o.boostIntent = v.substr(0, v.find(':'));
      o.boost = std::atoi(v.c_str() + v.find(':') + 1);
    } else {
      std::fprintf(stderr, "bad option %s\n", k.c_str());
      return 2;
    }
  }
  const std::string what = argv[2];
  if (what == "sentences") {
    const auto v = expandPhrases(p, o, &error);
    if (v.empty()) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return 1;
    }
    for (const auto& s : v) std::puts(s.c_str());
  } else if (what == "words")
    for (const auto& s : phraseologyWords(p)) std::puts(s.c_str());
  else if (what == "prompt")
    std::puts(whisperPrompt(p).c_str());
  else
    return 2;
  return 0;
}
