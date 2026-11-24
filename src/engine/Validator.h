#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/EngineEvent.h"
#include "scenario/Scenario.h"

namespace atc {

struct Clearance {  // an instruction judged CORRECT
  std::string step, intent;
  std::map<std::string, std::string> fields;
};
struct Turn {  // one entry of the conversation
  Ns t = 0;
  bool pilot = false;
  std::uint64_t id = 0;  // transmission, or utterance of the pilot
  std::string step, text;
  std::optional<Outcome> outcome;  // of a transmission
};
// Everything known about the running exercise besides the step.
struct ScenarioContext {
  ParamValues params;
  std::vector<Clearance> clearances;
  std::vector<Turn> history;
};

struct TxFacts {  // what TxClosed said; closed = false: nothing known about the audio
  bool closed = false;
  TxEnd end = TxEnd::Normal;
  UtteranceReport report{};
};

struct Verdict {
  Outcome outcome = Outcome::Unintelligible;
  std::vector<std::string> problems;
  bool suspectedStt = false;  // not counted against the trainee
};

struct ValidatorRules {
  // 03 section 3: a clipped transmission gets "say again", whatever was recognised.
  bool clippedIsUnintelligible = true;
};

// The checks of 03 section 2, in that order. No value is ever corrected: fields are compared
// as they are. There are no safety rules yet, so UNSAFE does not occur.
Verdict validate(const ScenarioStep& step, const ScenarioContext& ctx, const ParsedInstruction& p, const TxFacts& facts,
                 const std::string& sttFault, const ValidatorRules& rules);

}  // namespace atc
