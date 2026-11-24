#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace atc {

// 02 section 4.
enum class RecognitionClass { Recognized, Partial, Ambiguous, Unrecognized };

constexpr std::string_view toString(RecognitionClass c) {
  switch (c) {
    case RecognitionClass::Recognized: return "RECOGNIZED";
    case RecognitionClass::Partial: return "PARTIAL";
    case RecognitionClass::Ambiguous: return "AMBIGUOUS";
    case RecognitionClass::Unrecognized: return "UNRECOGNIZED";
  }
  return {};
}

// A value exactly as it was said. Never corrected, snapped to a pool or filled from the
// expected step: a wrong runway is the trainee's instruction and goes to the validator.
struct ParsedField {
  std::string value;        // canonical, e.g. "09", "EAGLE-421", "A3"; empty if uncertain
  std::string heard;        // the words it came from, e.g. "zero nine", "0.9"
  bool uncertain = false;   // shown as heard with "?"; makes the instruction AMBIGUOUS
  double confidence = -1;   // lowest word confidence in the span, engine's scale; -1 = none
};

// What the parser makes of one transmission. It never sees the scenario.
struct ParsedInstruction {
  std::uint64_t tx = 0;                       // transmission it came from
  RecognitionClass cls = RecognitionClass::Unrecognized;
  std::string intent;                         // data/phraseology id; empty if none
  std::map<std::string, ParsedField> fields;  // by slot name: callsign, runway, taxiway, ...
  std::vector<std::string> missing;           // slots the intent needs and did not get (PARTIAL)
  std::vector<std::string> reasons;           // why not RECOGNIZED, for the log and the Lab
  std::string rawText, normalisedText;
  // Stored with every result (D-39).
  std::string modelId, modelConfig;
};

}  // namespace atc
