#pragma once
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "scenario/Scenario.h"
#include "voice/VoiceBank.h"

namespace atc {

// A scenario with the voice bank that covers it.
struct PreparedExercise {
  std::shared_ptr<const Exercise> exercise;
  std::shared_ptr<const VoiceBank> bank;
  explicit operator bool() const { return exercise && bank; }
};

struct CheckResult {
  enum class State { Checking, Covered, MissingClips, Failed };
  State state = State::Checking;
  std::string scenarioFile;
  PreparedExercise ready;             // set only when Covered
  std::vector<std::string> missing;   // MissingClips: each with its clip ID
  std::vector<LoadError> errors;      // Failed: scenario or bank, with file and JSON path
};

// Runs on scenario select (D-115): loads the scenario and the voice bank and checks that every clip is there. One
// worker thread; the newest selection wins and an older result is never reported after it.
class ExerciseCheck {
 public:
  using Report = std::function<void(const CheckResult&)>;  // Checking on the caller's thread, the rest on the worker
  ExerciseCheck(std::string dataDir, std::string voicesDir, Report report);
  ~ExerciseCheck();
  void select(const std::string& scenarioFile);

  static CheckResult check(const std::string& dataDir, const std::string& voicesDir, const std::string& scenarioFile);

 private:
  void run();
  const std::string data_, voices_;
  const Report report_;
  std::mutex m_, reporting_;
  std::condition_variable cv_;
  std::optional<std::string> next_;
  std::uint64_t generation_ = 0;
  bool stop_ = false;
  std::thread thread_;
};

}  // namespace atc
