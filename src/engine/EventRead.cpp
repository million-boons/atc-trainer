#include "engine/EventRead.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace atc {
namespace {
using nlohmann::json;

std::string text(const json& j, const char* k) {
  const auto it = j.find(k);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}
template <class T>
T num(const json& j, const char* k) {
  const auto it = j.find(k);
  return it != j.end() && it->is_number() ? it->get<T>() : T{};
}
bool flag(const json& j, const char* k) {
  const auto it = j.find(k);
  return it != j.end() && it->is_boolean() && it->get<bool>();
}
std::vector<std::string> texts(const json& j, const char* k) {
  std::vector<std::string> out;
  if (const auto it = j.find(k); it != j.end() && it->is_array())
    for (const auto& x : *it)
      if (x.is_string()) out.push_back(x.get<std::string>());
  return out;
}
in::Timer timer(const std::string& s) { return s == "on_enter" ? in::Timer::OnEnter : s == "exercise_limit" ? in::Timer::ExerciseLimit : in::Timer::StepTimeout; }
in::PilotEnd pilotEnd(const std::string& s) {
  return s == "interrupted" ? in::PilotEnd::Interrupted : s == "press_came_first" ? in::PilotEnd::PressCameFirst
       : s == "timed_out" ? in::PilotEnd::TimedOut : s == "superseded" ? in::PilotEnd::Superseded : in::PilotEnd::Completed;
}
RecognitionClass cls(const std::string& s) {
  for (const auto c : {RecognitionClass::Recognized, RecognitionClass::Partial, RecognitionClass::Ambiguous, RecognitionClass::Unrecognized})
    if (toString(c) == s) return c;
  return RecognitionClass::Unrecognized;
}

bool header(const json& j, EventLogHeader& h, std::string& error) {
  if (!j.is_object() || text(j, "log") != "atc-exercise") {
    error = "not an exercise log";
    return false;
  }
  h.version = num<int>(j, "version");
  h.sessionId = text(j, "session");
  h.scenarioId = text(j, "scenario");
  h.scenarioHash = text(j, "scenarioHash");
  h.seed = num<std::uint64_t>(j, "seed");
  h.startedMs = num<std::int64_t>(j, "startedMs");
  h.mode = exerciseModeFromString(text(j, "mode")).value_or(ExerciseMode::Practice);
  h.traineeId = text(j, "trainee");
  h.modelId = text(j, "modelId");
  h.modelConfig = text(j, "modelConfig");
  if (const auto v = j.find("values"); v != j.end() && v->is_object())
    for (const auto& [k, x] : v->items())
      if (x.is_string()) h.values[k] = x.get<std::string>();
  if (const auto d = j.find("replyDelayMs"); d != j.end() && d->is_array() && d->size() == 2) {
    h.replyFromMs = (*d)[0].is_number() ? (*d)[0].get<int>() : 0;
    h.replyToMs = (*d)[1].is_number() ? (*d)[1].get<int>() : 0;
  }
  h.radio = text(j, "radio");
  if (const auto p = j.find("plan"); p != j.end() && p->is_object()) {
    h.planId = text(*p, "id");
    h.planName = text(*p, "name");
    h.planPreset = text(*p, "preset");
    h.planRun = num<int>(*p, "run");
  }
  if (h.sessionId.empty() || h.scenarioId.empty()) {
    error = "the header has no session or scenario";
    return false;
  }
  return true;
}

// False for a line that is not one of the events.
bool event(const json& j, EngineEvent& e) {
  if (!j.is_object()) return false;
  const std::string n = text(j, "event");
  e.seq = num<std::uint64_t>(j, "seq");
  e.t = num<Ns>(j, "t");
  if (n == "started") e.body = EngineInput(in::Started{});
  else if (n == "tx_opened") e.body = EngineInput(in::TxOpened{num<std::uint64_t>(j, "tx"), num<Ns>(j, "press"), flag(j, "fromQueue")});
  else if (n == "tx_closed") {
    in::TxClosed x;
    x.tx = num<std::uint64_t>(j, "tx");
    x.release = num<Ns>(j, "release");
    const std::string end = text(j, "end");
    x.end = end == "stuck" ? TxEnd::Stuck : end == "device_lost" ? TxEnd::DeviceLost : TxEnd::Normal;
    if (const auto r = j.find("report"); r != j.end() && r->is_object()) {
      x.report.empty = flag(*r, "empty");
      x.report.overload = flag(*r, "overload");
      x.report.spokeBeforeKeying = flag(*r, "spokeBeforeKeying");
      x.report.releasedEarly = flag(*r, "releasedEarly");
      x.report.peakDbfs = num<double>(*r, "peakDbfs");
      if (r->contains("keyToSpeechMs")) x.report.keyToSpeechMs = num<int>(*r, "keyToSpeechMs");
    }
    e.body = EngineInput(x);
  } else if (n == "tx_cancelled") e.body = EngineInput(in::TxCancelled{num<std::uint64_t>(j, "tx")});
  else if (n == "press_not_opened") {
    const std::string r = text(j, "result");
    e.body = EngineInput(in::PressNotOpened{r == "queued" ? in::PressResult::Queued : r == "queue_dropped" ? in::PressResult::QueueDropped : in::PressResult::Blocked});
  } else if (n == "recognised") {
    in::Recognised x;
    ParsedInstruction& p = x.instruction;
    p.tx = num<std::uint64_t>(j, "tx");
    p.cls = cls(text(j, "class"));
    p.intent = text(j, "intent");
    if (const auto f = j.find("fields"); f != j.end() && f->is_object())
      for (const auto& [k, v] : f->items()) p.fields[k] = {text(v, "value"), text(v, "heard"), flag(v, "uncertain"), num<double>(v, "confidence")};
    p.missing = texts(j, "missing");
    p.reasons = texts(j, "reasons");
    p.rawText = text(j, "raw");
    p.normalisedText = text(j, "normalised");
    p.modelId = text(j, "modelId");
    p.modelConfig = text(j, "modelConfig");
    x.releaseToText = num<Ns>(j, "releaseToText");
    x.sttFault = text(j, "sttFault");
    e.body = EngineInput(x);
  } else if (n == "pilot_started") e.body = EngineInput(in::PilotStarted{num<std::uint64_t>(j, "utterance")});
  else if (n == "pilot_ended") e.body = EngineInput(in::PilotEnded{num<std::uint64_t>(j, "utterance"), pilotEnd(text(j, "end"))});
  else if (n == "timer_fired") e.body = EngineInput(in::TimerFired{timer(text(j, "timer")), num<std::uint64_t>(j, "generation")});
  else if (n == "ptt_presence") e.body = EngineInput(in::PttPresence{flag(j, "present")});
  else if (n == "fallback_ptt_used") e.body = EngineInput(in::FallbackPttUsed{num<std::uint64_t>(j, "tx"), text(j, "source")});
  else if (n == "instructor_stop") e.body = EngineInput(in::InstructorStop{});
  else if (n == "program_closed") e.body = EngineInput(in::ProgramClosed{});
  else if (n == "instructor_continue") e.body = EngineInput(in::InstructorContinue{});
  else if (n == "step_entered") e.body = EngineOutput(out::StepEntered{text(j, "step")});
  else if (n == "judged") {
    out::Judged x;
    x.step = text(j, "step");
    x.tx = num<std::uint64_t>(j, "tx");
    x.outcome = outcomeFromString(text(j, "outcome")).value_or(Outcome::Unintelligible);
    x.problems = texts(j, "problems");
    x.advanced = flag(j, "advanced");
    x.suspectedStt = flag(j, "suspectedStt");
    x.waited = num<Ns>(j, "waited");
    e.body = EngineOutput(x);
  } else if (n == "pilot_requested") {
    PilotUtterance u;
    u.id = num<std::uint64_t>(j, "utterance");
    u.replyId = text(j, "reply");
    u.text = text(j, "text");
    u.clipIds = texts(j, "clips");
    u.delay = num<Ns>(j, "delay");
    u.unprompted = flag(j, "unprompted");
    u.cause = outcomeFromString(text(j, "cause")).value_or(Outcome::Correct);
    u.tx = num<std::uint64_t>(j, "tx");
    u.first = flag(j, "first");
    u.owed = flag(j, "owed");
    e.body = EngineOutput(out::PilotRequested{u});
  } else if (n == "atc_before_pilot") e.body = EngineOutput(out::AtcBeforePilot{num<std::uint64_t>(j, "tx")});
  else if (n == "no_reply") e.body = EngineOutput(out::NoReply{num<std::uint64_t>(j, "tx")});
  else if (n == "timer_set") e.body = EngineOutput(out::TimerSet{timer(text(j, "timer")), num<std::uint64_t>(j, "generation"), num<Ns>(j, "delay")});
  else if (n == "timer_cancelled") e.body = EngineOutput(out::TimerCancelled{timer(text(j, "timer"))});
  else if (n == "instructor_alert") e.body = EngineOutput(out::InstructorAlert{text(j, "why"), flag(j, "choice")});
  else if (n == "ended") {
    const std::string r = text(j, "reason");
    e.body = EngineOutput(out::Ended{r == "instructor_stop" ? out::EndReason::InstructorStop : r == "time_limit" ? out::EndReason::TimeLimit
                                     : r == "program_closed" ? out::EndReason::ProgramClosed : out::EndReason::Completed});
  } else return false;
  return true;
}
}  // namespace

bool readEventLogHeader(const std::string& file, EventLogHeader& out, std::string& error) {
  std::ifstream f(file);
  std::string line;
  if (!f || !std::getline(f, line)) {
    error = file + ": cannot be read";
    return false;
  }
  const json j = json::parse(line, nullptr, false);
  return header(j, out, error);
}

ReadLog readEventLog(const std::string& file) {
  ReadLog r;
  std::ifstream f(file);
  std::string line;
  if (!f || !std::getline(f, line)) {
    r.error = file + ": cannot be read";
    return r;
  }
  if (!header(json::parse(line, nullptr, false), r.header, r.error)) return r;
  while (std::getline(f, line)) {
    EngineEvent e;
    if (!event(json::parse(line, nullptr, false), e)) {
      ++r.unreadable;
      continue;
    }
    if (const auto* o = std::get_if<EngineOutput>(&e.body); o && std::holds_alternative<out::Ended>(*o)) r.ended = true;
    r.events.push_back(std::move(e));
  }
  return r;
}

}  // namespace atc
