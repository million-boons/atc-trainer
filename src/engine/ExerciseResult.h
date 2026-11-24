#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/EngineEvent.h"
#include "scenario/Scenario.h"

namespace atc {

struct MissedResponse {
  Ns at = 0;      // since the start of the exercise
  Ns waited = 0;  // silence before it counted as missed
};

struct StepResult {
  std::string id, label;
  bool reached = false, completed = false;
  int attempts = 0;                // transmissions judged in this step
  std::map<Outcome, int> errors;   // counted against the trainee
  int suspectedStt = 0;            // not counted
  std::vector<MissedResponse> missed;  // one per timeout
  std::optional<Ns> responseTime;  // pilot end to key-down, first attempt; from the first call left unanswered
};

struct PttResult {
  int transmissions = 0, spokeBeforeKeying = 0, releasedEarly = 0, shortTaps = 0, noSpeech = 0, keyedOverPilot = 0,
      stuck = 0, deviceLost = 0, overload = 0;
  int fallback = 0;  // transmissions keyed without the missing PTT device (D-311)
  std::optional<int> keyToSpeechMs;  // median
};

struct SuspectedStt {
  std::uint64_t tx = 0;
  std::string step;
  Outcome outcome = Outcome::Unintelligible;
  std::vector<std::string> problems;
  std::string heard, modelId, modelConfig;
  Ns releaseToText = 0;
};

// Facts only: no score and no pass mark (no approved phraseology yet).
struct ExerciseResult {
  std::optional<out::EndReason> end;  // empty: the log stops before the end
  Ns duration = 0;
  std::vector<StepResult> steps;      // final steps left out
  PttResult ptt;
  std::vector<SuspectedStt> suspectedStt;
  int pilotFaults = 0;                // watchdog stops (D-301)
  std::vector<std::string> alerts;
  bool fallbackPttUsed() const { return ptt.fallback > 0; }
};

// Built from the log alone, so it can be rebuilt at debrief.
ExerciseResult buildResult(const Scenario& scenario, const std::vector<EngineEvent>& events);

}  // namespace atc
