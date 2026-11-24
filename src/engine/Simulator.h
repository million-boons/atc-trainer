#pragma once
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "engine/SequenceEngine.h"

namespace atc {

// Runs an exercise in virtual time without audio, pedal or recogniser: for sim_cli and tests.
// Half-duplex is simplified: the simulated trainee always waits for the pilot to finish.
class Simulator {
 public:
  struct Options {
    Ns talk = ms(3000), recognise = ms(2000), pilotTalk = ms(2500);
  };
  Simulator(SequenceEngine engine, IEventLog& log) : Simulator(std::move(engine), log, Options()) {}
  Simulator(SequenceEngine engine, IEventLog& log, Options opt);

  void start();
  void wait(Ns duration);
  void transmit(ParsedInstruction p, const UtteranceReport& report = goodReport(), TxEnd end = TxEnd::Normal,
                const std::string& sttFault = {});
  void stop();
  void waitForPilot();  // until nothing is being or about to be said

  bool ended() const { return reason_.has_value(); }
  std::optional<out::EndReason> reason() const { return reason_; }
  Ns now() const { return now_; }
  const SequenceEngine& engine() const { return logged_.engine(); }
  static UtteranceReport goodReport();
  // A correct, complete instruction for this intent: fields as name -> value.
  static ParsedInstruction instruction(const std::string& intent, const std::map<std::string, std::string>& fields);

 private:
  struct Due { std::uint64_t generation = 0; Ns at = 0; };
  void apply(const EngineInput& input);
  void advanceTo(Ns t);
  std::optional<Ns> nextPilot() const;
  void pilotStep();

  LoggedEngine logged_;
  Options opt_;
  Ns now_ = 0;
  std::map<in::Timer, Due> timers_;
  std::deque<PilotUtterance> queue_;
  std::optional<PilotUtterance> speaking_;
  Ns eligibleSince_ = 0, speechEnd_ = 0;
  bool txBusy_ = false;
  std::uint64_t tx_ = 0;
  std::optional<out::EndReason> reason_;
};

}  // namespace atc
