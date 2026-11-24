#pragma once
#include <set>
#include <string>
#include <vector>

#include "bench/PhraseFile.h"
#include "stt/SpeechRecognizer.h"

namespace atc {

// Benchmark scoring only. This is NOT the trainer's parser (Phase 3).

// Same words whatever the engine's spelling: "Runway 09," -> runway zero nine; niner -> nine.
// Never changes one value into another.
std::vector<SttWord> normaliseWords(const std::vector<SttWord>& words);
std::vector<std::string> normaliseText(const std::string& text);

struct EditCounts {
  int sub = 0, del = 0, ins = 0, ref = 0;
  int errors() const { return sub + del + ins; }
};
EditCounts editCounts(const std::vector<std::string>& ref, const std::vector<std::string>& hyp);

struct Extracted {
  Fields fields;
  double minConf = 1.0;   // lowest word confidence over all field words
  bool conflict = false;  // same field heard twice with different values
};
// callsignNames: lower-case spoken names, e.g. {"eagle", "falcon"}.
Extracted extractFields(const std::vector<SttWord>& normalised, const std::set<std::string>& callsignNames);

enum class Outcome {
  Correct,      // accepted, every field right
  Incomplete,   // accepted, nothing wrong, a field other than hold short missing
  Wrong,        // accepted with a wrong or unexpected value, or hold short lost
  Rejected,     // not accepted: the pilot would say again
  FalseAccept,  // negative accepted
  NegativeOk
};
// Field lengths and ranges: callsign 3 digits, runway 01-36, taxiway letter + 1 or 2 digits, heading
// 3 digits 001-360, level FL + 3 digits, frequency 3 digits + decimals. Only ever rejects; a value
// is never changed. PLACEHOLDER shapes, to be validated with the phraseology.
bool shapeOk(const Extracted& e);
// Instruction words that must never be swapped: takeoff, land, turn_left, turn_right, climb, descend.
// "" if none, "conflict" if two. Kept apart from extractFields so existing totals stay comparable.
std::string actionOf(const std::vector<SttWord>& normalised);
// Accepted = callsign and at least one other field, no conflict, confidence >= threshold.
bool accepted(const Extracted& e, double threshold);
// useShape: a result failing shapeOk is not accepted.
Outcome judge(const Phrase& p, const Extracted& e, double threshold, bool useShape = false);

// Nearest-rank percentile, p in (0, 100]. 0 if empty.
double percentile(std::vector<double> v, double p);

}  // namespace atc
