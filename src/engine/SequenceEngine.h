#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "core/IEventLog.h"
#include "engine/Validator.h"

namespace atc {

struct EngineConfig {
  Ns replyDelayMin = ms(500), replyDelayMax = ms(1500);  // 03 section 3
  ValidatorRules rules{};
};

// The exercise as a function of its inputs: handle() changes the state and returns what is to
// be done. No clock, no audio, no threads; a copy of the object is a snapshot.
// Only Outcome::Correct advances; every other outcome may take its configured error branch.
// Every Recognised returns exactly one PilotRequested with its tx, or one NoReply.
class SequenceEngine {
 public:
  enum class Status { Ready, Running, Ended };
  using Outputs = std::vector<EngineOutput>;

  SequenceEngine(std::shared_ptr<const Exercise> exercise, std::uint64_t seed, EngineConfig cfg = {});

  Outputs handle(Ns t, const EngineInput& input);

  Status status() const { return status_; }
  const ScenarioStep& step() const { return exercise_->scenario.steps[step_]; }
  const ScenarioContext& context() const { return ctx_; }
  const Exercise& exercise() const { return *exercise_; }

 private:
  struct TimerState {
    bool active = false, paused = false;
    std::uint64_t generation = 0;
    Ns deadline = 0, remaining = 0;
  };
  void on(Ns t, const in::Started&, Outputs& o);
  void on(Ns t, const in::TxOpened&, Outputs& o);
  void on(Ns t, const in::TxClosed&, Outputs& o);
  void on(Ns t, const in::TxCancelled&, Outputs& o);
  void on(Ns t, const in::PressNotOpened&, Outputs& o);
  void on(Ns t, const in::Recognised&, Outputs& o);
  void on(Ns t, const in::PilotStarted&, Outputs& o);
  void on(Ns t, const in::PilotEnded&, Outputs& o);
  void on(Ns t, const in::TimerFired&, Outputs& o);
  void on(Ns t, const in::PttPresence&, Outputs& o);
  void on(Ns t, const in::FallbackPttUsed&, Outputs& o);
  void on(Ns t, const in::InstructorStop&, Outputs& o);
  void on(Ns t, const in::InstructorContinue&, Outputs& o);

  bool running() const { return status_ == Status::Running; }
  TimerState& timer(in::Timer k) { return timers_[static_cast<std::size_t>(k)]; }
  void setTimer(in::Timer k, Ns delay, Ns t, Outputs& o);
  void cancelTimer(in::Timer k, Outputs& o);
  void enter(const std::string& id, Outputs& o);
  void takeBranch(Outcome outcome, Outputs& o);
  PilotUtterance& say(const std::string& replyId, Outcome cause, std::uint64_t tx, bool unprompted, Outputs& o);
  void stepTimedOut(Outputs& o);
  void finish(out::EndReason why, Outputs& o);
  void settle(Ns t, Outputs& o);

  std::shared_ptr<const Exercise> exercise_;
  EngineConfig cfg_;
  std::mt19937_64 rng_;
  ScenarioContext ctx_;
  Status status_ = Status::Ready;
  std::size_t step_ = 0;
  bool onEnterPending_ = false;
  // D-322: the pilot's first call is due at a fixed time after Start, whatever is transmitted.
  enum class First { None, Waiting, Requested, Begun } first_ = First::None;
  std::string firstReply_;
  std::uint64_t firstCall_ = 0;       // its utterance
  std::set<std::uint64_t> early_;     // transmissions opened before it began, not yet judged
  bool callsExhausted_ = false;
  int pilotCalls_ = 0, sayAgain_ = 0;
  bool pttPresent_ = true;
  std::map<std::uint64_t, TxFacts> openTx_;            // opened and not yet recognised or cancelled
  struct Said { std::string text; bool owed = false; };
  std::map<std::uint64_t, Said> outstanding_;          // pilot utterances not finished
  std::map<std::uint64_t, std::uint64_t> cut_;         // D-325: cut and not owed -> latest transmission before the cut
  std::uint64_t lastTx_ = 0;
  std::uint64_t nextUtterance_ = 1;
  std::array<TimerState, 3> timers_{};
};

// The engine with its log: the only way the runtime and the simulator drive it.
class LoggedEngine {
 public:
  LoggedEngine(SequenceEngine engine, IEventLog& log) : engine_(std::move(engine)), log_(log) {}
  SequenceEngine::Outputs apply(Ns t, const EngineInput& input);
  const SequenceEngine& engine() const { return engine_; }

 private:
  SequenceEngine engine_;
  IEventLog& log_;
  std::uint64_t seq_ = 0;
};

// {slot}s replaced by the exercise's values.
std::string fillSlots(const std::string& text, const ParamValues& values);

}  // namespace atc
