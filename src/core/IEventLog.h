#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "core/EngineEvent.h"

namespace atc {

enum class ExerciseMode {
  Practice,  // the trainee sees the live verdict
  Assessed   // hidden until debrief; refused while any text is placeholder (P-11)
};

constexpr std::string_view toString(ExerciseMode m) { return m == ExerciseMode::Practice ? "practice" : "assessed"; }
constexpr std::optional<ExerciseMode> exerciseModeFromString(std::string_view s) {
  if (s == "practice") return ExerciseMode::Practice;
  if (s == "assessed") return ExerciseMode::Assessed;
  return std::nullopt;
}

// First entry of every log. Nothing in it changes during the exercise (D-39, D-416). Version 2 (D-334): the values
// that run, the difficulty and the plan; a version 1 log has only the seed.
struct EventLogHeader {
  int version = 2;
  std::string sessionId;
  std::string scenarioId, scenarioHash;
  std::uint64_t seed = 0;
  std::int64_t startedMs = 0;  // Unix time
  ExerciseMode mode = ExerciseMode::Practice;
  std::string traineeId;  // a code, not a name; empty until A-5 is answered
  std::string modelId, modelConfig;
  std::map<std::string, std::string> values;  // generator -> value: exactly what was said and expected
  int replyFromMs = 0, replyToMs = 0;
  std::string radio;
  std::string planId, planName, planPreset;  // empty: no plan
  int planRun = 0;                           // 0: unnumbered
};

// Letters, digits, - and _: the same rule as speaker IDs in ptt_record.
constexpr bool isIdCode(std::string_view s) {
  for (const char c : s)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
      return false;
  return !s.empty();
}

// Empty if the header may be written; otherwise names the field.
inline std::string headerError(const EventLogHeader& h) {
  if (h.sessionId.empty()) return "sessionId is empty";
  if (h.scenarioId.empty()) return "scenarioId is empty";
  if (h.scenarioHash.empty()) return "scenarioHash is empty";
  if (h.modelId.empty()) return "modelId is empty (D-39)";
  if (h.modelConfig.empty()) return "modelConfig is empty (D-39)";
  if (!h.traineeId.empty() && !isIdCode(h.traineeId)) return "traineeId must be a code: letters, digits, - and _";
  return {};
}

// Called from the core thread: must not block. File work happens elsewhere.
class IEventLog {
 public:
  virtual ~IEventLog() = default;
  // Once, before anything else. False, with error set, if headerError refuses it or the log
  // cannot be created; the exercise then does not start.
  virtual bool begin(const EventLogHeader& header, std::string& error) = 0;
  virtual void append(const EngineEvent& event) = 0;
  virtual void end() = 0;  // the log is complete
  // Events lost because writing fell behind or failed. Above 0 is a fault to be shown.
  virtual std::uint64_t lost() const = 0;
};

}  // namespace atc
