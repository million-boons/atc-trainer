#include "engine/EventJson.h"

#include <nlohmann/json.hpp>

namespace atc {
namespace {
using nlohmann::json;

const char* name(TxEnd e) { return e == TxEnd::Normal ? "normal" : e == TxEnd::Stuck ? "stuck" : "device_lost"; }
const char* name(in::PressResult r) {
  return r == in::PressResult::Blocked ? "blocked" : r == in::PressResult::Queued ? "queued" : "queue_dropped";
}
const char* name(in::PilotEnd e) {
  switch (e) {
    case in::PilotEnd::Completed: return "completed";
    case in::PilotEnd::Interrupted: return "interrupted";
    case in::PilotEnd::PressCameFirst: return "press_came_first";
    case in::PilotEnd::TimedOut: return "timed_out";
    case in::PilotEnd::Superseded: return "superseded";
  }
  return "";
}
const char* name(in::Timer t) {
  return t == in::Timer::StepTimeout ? "step_timeout" : t == in::Timer::OnEnter ? "on_enter" : "exercise_limit";
}
const char* name(out::EndReason r) {
  return r == out::EndReason::Completed ? "completed" : r == out::EndReason::InstructorStop ? "instructor_stop"
         : r == out::EndReason::ProgramClosed ? "program_closed" : "time_limit";
}
const char* name(out::ReadbackResult r) {
  switch (r) {
    case out::ReadbackResult::Caught: return "caught";
    case out::ReadbackResult::Missed: return "missed";
    case out::ReadbackResult::Unnecessary: return "unnecessary";
    case out::ReadbackResult::Open: return "open";
  }
  return "";
}
std::string str(std::string_view s) { return std::string(s); }

json j(const in::Started&) { return json::object(); }
json j(const in::TxOpened& x) { return {{"tx", x.tx}, {"press", x.press}, {"fromQueue", x.fromQueue}}; }
json j(const in::TxClosed& x) {
  json report = {{"empty", x.report.empty},
                 {"overload", x.report.overload},
                 {"spokeBeforeKeying", x.report.spokeBeforeKeying},
                 {"releasedEarly", x.report.releasedEarly},
                 {"peakDbfs", x.report.peakDbfs}};
  if (x.report.keyToSpeechMs) report["keyToSpeechMs"] = *x.report.keyToSpeechMs;
  return {{"tx", x.tx}, {"release", x.release}, {"end", name(x.end)}, {"report", report}};
}
json j(const in::TxCancelled& x) { return json{{"tx", x.tx}}; }
json j(const in::PressNotOpened& x) { return json{{"result", name(x.result)}}; }
json j(const in::Recognised& x) {
  const ParsedInstruction& p = x.instruction;
  json fields = json::object();
  for (const auto& [n, f] : p.fields)
    fields[n] = {{"value", f.value}, {"heard", f.heard}, {"uncertain", f.uncertain}, {"confidence", f.confidence}};
  return {{"tx", p.tx},
          {"class", str(toString(p.cls))},
          {"intent", p.intent},
          {"fields", fields},
          {"missing", p.missing},
          {"reasons", p.reasons},
          {"raw", p.rawText},
          {"normalised", p.normalisedText},
          {"modelId", p.modelId},
          {"modelConfig", p.modelConfig},
          {"releaseToText", x.releaseToText},
          {"sttFault", x.sttFault}};
}
json j(const in::PilotStarted& x) { return json{{"utterance", x.utterance}}; }
json j(const in::PilotEnded& x) { return {{"utterance", x.utterance}, {"end", name(x.end)}}; }
json j(const in::TimerFired& x) { return {{"timer", name(x.timer)}, {"generation", x.generation}}; }
json j(const in::PttPresence& x) { return json{{"present", x.present}}; }
json j(const in::FallbackPttUsed& x) { return {{"tx", x.tx}, {"source", x.source}}; }
json j(const in::InstructorStop&) { return json::object(); }
json j(const in::ProgramClosed&) { return json::object(); }
json j(const in::InstructorContinue&) { return json::object(); }
json j(const out::StepEntered& x) { return json{{"step", x.step}}; }
json j(const out::Judged& x) {
  return {{"step", x.step},         {"tx", x.tx},
          {"outcome", str(toString(x.outcome))}, {"problems", x.problems},
          {"advanced", x.advanced}, {"suspectedStt", x.suspectedStt}, {"waited", x.waited}};
}
json j(const out::PilotRequested& x) {
  const PilotUtterance& u = x.utterance;
  return {{"utterance", u.id}, {"reply", u.replyId},         {"text", u.text},
          {"clips", u.clipIds}, {"delay", u.delay},           {"unprompted", u.unprompted},
          {"cause", str(toString(u.cause))}, {"tx", u.tx},             {"first", u.first},
          {"owed", u.owed}};
}
json j(const out::AtcBeforePilot& x) { return json{{"tx", x.tx}}; }
json j(const out::ReadbackCheck& x) {
  return {{"step", x.step}, {"tx", x.tx}, {"utterance", x.utterance}, {"result", name(x.result)}};
}
json j(const out::NoReply& x) { return json{{"tx", x.tx}}; }
json j(const out::TimerSet& x) { return {{"timer", name(x.timer)}, {"generation", x.generation}, {"delay", x.delay}}; }
json j(const out::TimerCancelled& x) { return json{{"timer", name(x.timer)}}; }
json j(const out::InstructorAlert& x) { return json{{"why", x.why}, {"choice", x.choice}}; }
json j(const out::Ended& x) { return json{{"reason", name(x.reason)}}; }

// Recognised text may hold bytes that are not UTF-8: replaced, never an exception.
std::string line(const json& v) { return v.dump(-1, ' ', false, json::error_handler_t::replace); }

}  // namespace

std::string headerJson(const EventLogHeader& h) {
  json j{{"log", "atc-exercise"},
               {"version", h.version},
               {"session", h.sessionId},
               {"scenario", h.scenarioId},
               {"scenarioHash", h.scenarioHash},
               {"seed", h.seed},
               {"startedMs", h.startedMs},
               {"mode", str(toString(h.mode))},
               {"trainee", h.traineeId},
               {"modelId", h.modelId},
               {"modelConfig", h.modelConfig},
               {"values", h.values},
               {"replyDelayMs", {h.replyFromMs, h.replyToMs}},
               {"radio", h.radio}};
  if (!h.planId.empty()) j["plan"] = {{"id", h.planId}, {"name", h.planName}, {"preset", h.planPreset}, {"run", h.planRun}};
  return line(j);
}

std::string eventJson(const EngineEvent& e) {
  json v = std::visit([](const auto& side) { return std::visit([](const auto& x) { return j(x); }, side); }, e.body);
  v["seq"] = e.seq;
  v["t"] = e.t;
  v["event"] = str(eventName(e));
  v["input"] = isInput(e);
  return line(v);
}

}  // namespace atc
