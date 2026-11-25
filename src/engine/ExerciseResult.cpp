#include "engine/ExerciseResult.h"

#include <algorithm>
#include <set>

namespace atc {
namespace {

struct Builder {
  ExerciseResult r;
  Ns now = 0, started = 0;
  std::optional<Ns> pilotEnd;                      // not yet answered
  std::map<std::uint64_t, Ns> response;            // per transmission
  std::map<std::uint64_t, SuspectedStt> heard;     // per transmission
  std::vector<int> keyToSpeech;
  std::set<std::uint64_t> early;                   // before the pilot's first call
  std::set<std::uint64_t> repeats;                 // pilot calls after a missed response

  StepResult* step(const std::string& id) {
    for (auto& s : r.steps)
      if (s.id == id) return &s;
    return nullptr;
  }

  void on(const in::Started&) { started = now; }
  void on(const in::TxOpened& x) {
    if (pilotEnd && x.press >= *pilotEnd) response[x.tx] = x.press - *pilotEnd;
    pilotEnd.reset();
  }
  void on(const in::TxClosed& x) {
    ++r.ptt.transmissions;
    r.ptt.spokeBeforeKeying += x.report.spokeBeforeKeying;
    r.ptt.releasedEarly += x.report.releasedEarly;
    r.ptt.noSpeech += x.report.empty;
    r.ptt.overload += x.report.overload;
    r.ptt.stuck += x.end == TxEnd::Stuck;
    r.ptt.deviceLost += x.end == TxEnd::DeviceLost;
    if (x.report.keyToSpeechMs) keyToSpeech.push_back(*x.report.keyToSpeechMs);
  }
  void on(const in::TxCancelled&) { ++r.ptt.shortTaps; }
  // A dropped queued press was already counted when it was queued.
  void on(const in::PressNotOpened& x) { r.ptt.keyedOverPilot += x.result != in::PressResult::QueueDropped; }
  void on(const in::Recognised& x) {
    SuspectedStt& s = heard[x.instruction.tx];
    s.heard = x.instruction.rawText;
    s.modelId = x.instruction.modelId;
    s.modelConfig = x.instruction.modelConfig;
    s.releaseToText = x.releaseToText;
  }
  void on(const in::PilotStarted&) {}
  void on(const in::PilotEnded& x) {
    if (x.end == in::PilotEnd::Interrupted) ++r.ptt.keyedOverPilot;
    if (x.end == in::PilotEnd::TimedOut) ++r.pilotFaults;
    // A call repeated because nobody answered does not restart the clock.
    if (x.end == in::PilotEnd::Completed && !(repeats.count(x.utterance) && pilotEnd)) pilotEnd = now;
  }
  void on(const in::TimerFired&) {}
  void on(const in::PttPresence&) {}
  void on(const in::FallbackPttUsed&) { ++r.ptt.fallback; }
  void on(const in::InstructorStop&) {}
  void on(const in::InstructorContinue&) {}

  void on(const out::StepEntered& x) {
    if (auto* s = step(x.step)) s->reached = true;
  }
  void on(const out::Judged& x) {
    auto* s = step(x.step);
    if (!s) return;
    if (x.outcome == Outcome::Timeout) {
      s->missed.push_back({now - started, x.waited});
      return;
    }
    const bool counts = x.outcome == Outcome::Correct || !early.count(x.tx);  // D-322
    if (counts && ++s->attempts == 1)
      if (const auto it = response.find(x.tx); it != response.end()) s->responseTime = it->second;
    if (x.outcome == Outcome::Correct) {
      s->completed = true;
    } else if (x.suspectedStt) {
      ++s->suspectedStt;
      SuspectedStt q = heard[x.tx];
      q.tx = x.tx;
      q.step = x.step;
      q.outcome = x.outcome;
      q.problems = x.problems;
      r.suspectedStt.push_back(std::move(q));
    } else if (counts) {
      ++s->errors[x.outcome];
    }
  }
  void on(const out::PilotRequested& x) {
    if (x.utterance.cause == Outcome::Timeout) repeats.insert(x.utterance.id);
  }
  void on(const out::AtcBeforePilot& x) {
    ++r.atcBeforePilot;
    early.insert(x.tx);
  }
  void on(const out::NoReply&) {}
  void on(const out::TimerSet&) {}
  void on(const out::TimerCancelled&) {}
  void on(const out::InstructorAlert& x) { r.alerts.push_back(x.why); }
  void on(const out::Ended& x) {
    r.end = x.reason;
    r.duration = now - started;
  }
};

}  // namespace

ExerciseResult buildResult(const Scenario& scenario, const std::vector<EngineEvent>& events) {
  Builder b;
  for (const auto& s : scenario.steps) {
    if (s.final) continue;
    StepResult sr;
    sr.id = s.id;
    sr.label = s.label;
    b.r.steps.push_back(std::move(sr));
  }
  for (const auto& e : events) {
    b.now = e.t;
    std::visit([&](const auto& side) { std::visit([&](const auto& x) { b.on(x); }, side); }, e.body);
  }
  if (!b.keyToSpeech.empty()) {
    auto& k = b.keyToSpeech;
    std::sort(k.begin(), k.end());
    const std::size_t m = k.size() / 2;
    b.r.ptt.keyToSpeechMs = k.size() % 2 ? k[m] : (k[m - 1] + k[m]) / 2;
  }
  return b.r;
}

}  // namespace atc
