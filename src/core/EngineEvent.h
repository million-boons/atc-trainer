#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "audio/UtteranceChecks.h"
#include "core/Outcome.h"
#include "core/ParsedInstruction.h"
#include "core/PilotUtterance.h"
#include "core/Time.h"
#include "core/TransmissionController.h"

namespace atc {

// Everything that goes into and comes out of the sequence engine: (state, input) -> (state',
// outputs). The log holds both, in order; replaying the inputs with the header's seed must
// give the same outputs. kName is the name in the log: never rename one.
namespace in {

struct Started { static constexpr std::string_view kName = "started"; };
struct TxOpened {
  static constexpr std::string_view kName = "tx_opened";
  std::uint64_t tx = 0;
  Ns press = 0;             // for a queued press, the moment it opened (D-24)
  bool fromQueue = false;
};
struct TxClosed {
  static constexpr std::string_view kName = "tx_closed";
  std::uint64_t tx = 0;
  Ns release = 0;
  TxEnd end = TxEnd::Normal;  // DeviceLost is never scored against the trainee
  UtteranceReport report{};
};
struct TxCancelled {  // short tap
  static constexpr std::string_view kName = "tx_cancelled";
  std::uint64_t tx = 0;
};
enum class PressResult { Blocked, Queued, QueueDropped };
struct PressNotOpened {  // a press during pilot speech that did not open a transmission
  static constexpr std::string_view kName = "press_not_opened";
  PressResult result = PressResult::Blocked;
};
struct Recognised {  // in transmission order
  static constexpr std::string_view kName = "recognised";
  ParsedInstruction instruction{};  // .tx names the transmission, already closed
  Ns releaseToText = 0;
  std::string sttFault;  // not empty: the recogniser failed, so this is no trainee error
};
struct PilotStarted {
  static constexpr std::string_view kName = "pilot_started";
  std::uint64_t utterance = 0;
  Ns sinceRelease = 0;  // D-342: from the release of the transmission it answers; 0 for the pilot's own calls
};
enum class PilotEnd { Completed, Interrupted, PressCameFirst, TimedOut, Superseded };  // Superseded: never spoken (D-333)
struct PilotEnded {
  static constexpr std::string_view kName = "pilot_ended";
  std::uint64_t utterance = 0;
  PilotEnd end = PilotEnd::Completed;
};
enum class Timer { StepTimeout, OnEnter, ExerciseLimit };
struct TimerFired {
  static constexpr std::string_view kName = "timer_fired";
  Timer timer = Timer::StepTimeout;
  std::uint64_t generation = 0;  // not the latest for this timer: ignored
};
struct PttPresence {  // lost: timers pause until it is seen released
  static constexpr std::string_view kName = "ptt_presence";
  bool present = true;
};
struct FallbackPttUsed {  // D-311: this transmission was keyed on screen or keyboard because the PTT device is missing
  static constexpr std::string_view kName = "fallback_ptt_used";
  std::uint64_t tx = 0;
  std::string source;  // SCREEN or SPACE
};
struct InstructorStop { static constexpr std::string_view kName = "instructor_stop"; };
// The program was closed during the exercise (D-333): the log still ends.
struct ProgramClosed { static constexpr std::string_view kName = "program_closed"; };
struct InstructorContinue {  // answer to an alert that asks for a choice: the pilot calls again
  static constexpr std::string_view kName = "instructor_continue";
};

}  // namespace in

namespace out {

struct StepEntered {
  static constexpr std::string_view kName = "step_entered";
  std::string step;
};
struct Judged {  // the validator's verdict on one transmission, or on a missing one
  static constexpr std::string_view kName = "judged";
  std::string step;             // step it was judged in
  std::uint64_t tx = 0;         // 0 for Timeout
  Outcome outcome = Outcome::Unintelligible;
  std::vector<std::string> problems;  // field names or rule ids, for the result screen
  bool advanced = false;        // true only with Outcome::Correct
  bool suspectedStt = false;    // shown apart, not counted against the trainee
  Ns waited = 0;                // Timeout: silence since the channel was free
};
struct AtcBeforePilot {  // D-322: transmitted before the pilot's first call began; no step failure
  static constexpr std::string_view kName = "atc_before_pilot";
  std::uint64_t tx = 0;
};
struct PilotRequested {
  static constexpr std::string_view kName = "pilot_requested";
  PilotUtterance utterance{};
};
struct NoReply {  // the transmission is finished and the pilot stays silent
  static constexpr std::string_view kName = "no_reply";
  std::uint64_t tx = 0;
};
struct TimerSet {  // replaces any earlier one of the same kind
  static constexpr std::string_view kName = "timer_set";
  in::Timer timer = in::Timer::StepTimeout;
  std::uint64_t generation = 0;
  Ns delay = 0;
};
struct TimerCancelled {
  static constexpr std::string_view kName = "timer_cancelled";
  in::Timer timer = in::Timer::StepTimeout;
};
struct InstructorAlert {  // e.g. too many "say again" in one step
  static constexpr std::string_view kName = "instructor_alert";
  std::string why;
  bool choice = false;  // the instructor decides: in::InstructorContinue or in::InstructorStop (D-424)
};
// D-430: an incomplete readback was challenged in the next transmission (Caught), or the next
// instruction came without a challenge (Missed: a trainee error); a challenge after a complete
// readback is Unnecessary (a warning only); Open: the exercise ended while one was due.
enum class ReadbackResult { Caught, Missed, Unnecessary, Open };
struct ReadbackCheck {
  static constexpr std::string_view kName = "readback_check";
  std::string step;             // the step whose readback it is
  std::uint64_t tx = 0;         // the challenge or the instruction; 0 for Open
  std::uint64_t utterance = 0;  // the readback; 0 if there was none to repeat
  ReadbackResult result = ReadbackResult::Caught;
};
enum class EndReason { Completed, InstructorStop, TimeLimit, ProgramClosed };  // ProgramClosed: D-333
struct Ended {
  static constexpr std::string_view kName = "ended";
  EndReason reason = EndReason::Completed;
};

}  // namespace out

using EngineInput = std::variant<in::Started, in::TxOpened, in::TxClosed, in::TxCancelled, in::PressNotOpened,
                                 in::Recognised, in::PilotStarted, in::PilotEnded, in::TimerFired, in::PttPresence,
                                 in::InstructorStop, in::InstructorContinue, in::FallbackPttUsed, in::ProgramClosed>;
using EngineOutput = std::variant<out::StepEntered, out::Judged, out::PilotRequested, out::NoReply, out::TimerSet,
                                  out::TimerCancelled, out::InstructorAlert, out::Ended, out::AtcBeforePilot, out::ReadbackCheck>;

struct EngineEvent {
  std::uint64_t seq = 0;  // from 1, no gaps
  Ns t = 0;               // CLOCK_MONOTONIC; an output has the time of the input that caused it
  std::variant<EngineInput, EngineOutput> body{};
};

inline bool isInput(const EngineEvent& e) { return std::holds_alternative<EngineInput>(e.body); }

inline std::string_view eventName(const EngineEvent& e) {
  return std::visit(
      [](const auto& side) {
        return std::visit([](const auto& payload) { return std::string_view(payload.kName); }, side);
      },
      e.body);
}

}  // namespace atc
