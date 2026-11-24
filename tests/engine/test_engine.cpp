#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "core/PathResolver.h"
#include "engine/EventJson.h"
#include "engine/ExerciseResult.h"
#include "engine/Simulator.h"
using namespace atc;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

struct MemoryLog : IEventLog {
  std::vector<EngineEvent> events;
  bool begin(const EventLogHeader& h, std::string& error) override {
    error = headerError(h);
    return error.empty();
  }
  void append(const EngineEvent& e) override { events.push_back(e); }
  void end() override {}
  std::uint64_t lost() const override { return 0; }
  std::vector<std::string> lines() const {
    std::vector<std::string> v;
    for (const auto& e : events) v.push_back(eventJson(e));
    return v;
  }
};

json readJson(const fs::path& f) {
  std::ifstream in(f);
  return json::parse(in, nullptr, false);
}

// The shipped departure with LINE_UP added to the intents (package 2e adds it for real).
std::shared_ptr<Exercise> departure() {
  static int n = 0;
  Paths paths;
  std::string error;
  if (!resolvePaths(systemPathInputs(), paths, error)) {
    ADD_FAILURE() << error;
    return nullptr;
  }
  const fs::path data = paths.data;
  const fs::path dir = fs::temp_directory_path() / ("atc_engine_" + std::to_string(getpid()) + "_" + std::to_string(n++));
  json ph = readJson(data / "phraseology" / "placeholder.json");
  bool have = false;
  for (const auto& i : ph["intents"]) have = have || i["id"] == "LINE_UP";
  if (!have) ph["intents"].push_back({{"id", "LINE_UP"}, {"examples", json::array({"{callsign} line up runway {runway}"})}, {"validation", {{"status", "draft"}}}});
  fs::create_directories(dir / "phraseology" / "replies");
  fs::create_directories(dir / "airfields");
  std::ofstream(dir / "phraseology" / "placeholder.json") << ph.dump();
  fs::copy_file(data / "phraseology" / "replies" / "placeholder.json", dir / "phraseology" / "replies" / "placeholder.json");
  fs::copy_file(data / "airfields" / "SAMPLE-AF.json", dir / "airfields" / "SAMPLE-AF.json");
  auto e = std::make_shared<Exercise>();
  std::vector<LoadError> errors;
  const bool ok = loadExercise(scenarioSources(dir.string(), (data / "scenarios" / "DEPARTURE-01.json").string()), *e, errors);
  for (const auto& x : errors) ADD_FAILURE() << toString(x);
  std::error_code ec;
  fs::remove_all(dir, ec);
  return ok ? e : nullptr;
}

struct Engine : ::testing::Test {
  std::shared_ptr<Exercise> ex;
  MemoryLog log;
  std::unique_ptr<Simulator> sim;

  void SetUp() override {
    ex = departure();
    ASSERT_TRUE(ex);
    make();
  }
  void make(std::uint64_t seed = 7, EngineConfig cfg = {}) {
    log.events.clear();
    sim = std::make_unique<Simulator>(SequenceEngine(ex, seed, cfg), log);
  }
  std::string param(const char* n) const { return sim->engine().context().params.at(n); }
  std::string step() const { return sim->engine().step().id; }
  std::string otherRunway() const { return param("runway") == "09" ? "27" : "09"; }
  ParsedInstruction taxi() const {
    return Simulator::instruction("TAXI_INSTRUCTION", {{"callsign", param("callsign")}, {"taxiway", param("taxiway")}, {"runway", param("runway")}});
  }
  ParsedInstruction lineUp() const { return Simulator::instruction("LINE_UP", {{"callsign", param("callsign")}, {"runway", param("runway")}}); }
  ParsedInstruction takeoff() const {
    return Simulator::instruction("TAKEOFF_CLEARANCE", {{"callsign", param("callsign")}, {"runway", param("runway")}});
  }
  template <class T>
  std::vector<T> outputs() const {
    std::vector<T> v;
    for (const auto& e : log.events)
      if (const auto* o = std::get_if<EngineOutput>(&e.body))
        if (const auto* x = std::get_if<T>(o)) v.push_back(*x);
    return v;
  }
  out::Judged lastJudged() const {
    const auto v = outputs<out::Judged>();
    return v.empty() ? out::Judged{} : v.back();
  }
  std::string lastPilot() const {
    const auto v = outputs<out::PilotRequested>();
    return v.empty() ? "" : v.back().utterance.text;
  }
  // One instruction in the TAXI step: verdict, and still in TAXI.
  void expectStays(ParsedInstruction p, Outcome want, const std::string& problem = {}) {
    make();
    sim->start();
    sim->wait(ms(6000));
    sim->transmit(std::move(p));
    EXPECT_EQ(lastJudged().outcome, want) << toString(lastJudged().outcome);
    EXPECT_FALSE(lastJudged().advanced);
    if (!problem.empty()) {
      const auto problems = lastJudged().problems;
      EXPECT_NE(std::find(problems.begin(), problems.end(), problem), problems.end()) << problem;
    }
    EXPECT_EQ(step(), "TAXI");
  }
  void run() {
    sim->start();
    sim->wait(ms(6000));
    sim->transmit(taxi());
    sim->wait(ms(8000));
    sim->transmit(lineUp());
    sim->wait(ms(4000));
    sim->transmit(takeoff());
    sim->wait(ms(5000));
  }
};

}  // namespace

TEST_F(Engine, DepartureCompletes) {
  run();
  EXPECT_EQ(sim->reason(), out::EndReason::Completed);
  std::vector<std::string> steps;
  for (const auto& s : outputs<out::StepEntered>()) steps.push_back(s.step);
  EXPECT_EQ(steps, (std::vector<std::string>{"TAXI", "LINE_UP", "TAKEOFF", "DONE"}));
  EXPECT_EQ(sim->engine().context().clearances.size(), 3u);
}

TEST_F(Engine, SameSeedAndEventsGiveTheSameLog) {
  run();
  const auto first = log.lines();
  make(7);
  run();
  EXPECT_EQ(log.lines(), first);
  make(8);
  run();
  EXPECT_NE(log.lines(), first);
}

TEST_F(Engine, ReplayingTheInputsGivesTheSameOutputs) {
  run();
  const auto recorded = log.events;
  SequenceEngine again(ex, 7);
  std::vector<std::string> got, want;
  std::uint64_t seq = 0;
  for (const auto& e : recorded) {
    if (!isInput(e)) continue;
    got.push_back(eventJson(EngineEvent{++seq, e.t, std::get<EngineInput>(e.body)}));
    for (const auto& o : again.handle(e.t, std::get<EngineInput>(e.body))) got.push_back(eventJson(EngineEvent{++seq, e.t, o}));
  }
  EXPECT_EQ(got, log.lines());
}

TEST_F(Engine, SequenceNumbersHaveNoGaps) {
  run();
  for (std::size_t i = 0; i < log.events.size(); ++i) EXPECT_EQ(log.events[i].seq, i + 1);
}

TEST_F(Engine, PilotTextHasNoSlotsLeft) {
  run();
  const auto said = outputs<out::PilotRequested>();
  EXPECT_EQ(said.size(), 5u);  // two own calls, three readbacks
  for (const auto& p : said) {
    EXPECT_FALSE(p.utterance.text.empty());
    EXPECT_EQ(p.utterance.text.find_first_of("{}"), std::string::npos) << p.utterance.text;
    EXPECT_NE(p.utterance.text.find(param("callsign")), std::string::npos) << p.utterance.text;
  }
}

TEST_F(Engine, ReplyDelayIsInTheConfiguredRange) {
  std::set<Ns> seen;
  for (std::uint64_t seed = 0; seed < 30; ++seed) {
    make(seed);
    sim->start();
    sim->wait(ms(6000));
    sim->transmit(taxi());
    const Ns d = outputs<out::PilotRequested>().back().utterance.delay;
    EXPECT_GE(d, ms(500));
    EXPECT_LE(d, ms(1500));
    seen.insert(d);
  }
  EXPECT_GT(seen.size(), 5u);
}

TEST_F(Engine, WrongValuesNeverAdvance) {
  auto p = taxi();
  p.fields["runway"].value = otherRunway();
  expectStays(p, Outcome::Incorrect, "runway");
  EXPECT_EQ(lastPilot().find("query instruction"), 0u);
  p = taxi();
  p.fields["taxiway"].value = "Z9";
  expectStays(p, Outcome::Incorrect, "taxiway");
}
TEST_F(Engine, SkippedStepNeverAdvances) { expectStays(takeoff(), Outcome::Unexpected, "TAKEOFF_CLEARANCE"); }
TEST_F(Engine, WrongCallsignGetsSilence) {
  auto p = taxi();
  p.fields["callsign"].value = "HAWK-999";
  expectStays(p, Outcome::WrongCallsign, "callsign");
  EXPECT_EQ(outputs<out::PilotRequested>().size(), 1u);  // only the pilot's own first call
  ASSERT_EQ(outputs<out::NoReply>().size(), 1u);
  EXPECT_EQ(outputs<out::NoReply>()[0].tx, 1u);
}
TEST_F(Engine, MissingCallsignOrFieldAsksForIt) {
  auto p = taxi();
  p.fields.erase("callsign");
  expectStays(p, Outcome::MissingInfo, "callsign");
  p = taxi();
  p.cls = RecognitionClass::Partial;
  p.fields.erase("taxiway");
  p.missing = {"taxiway"};
  expectStays(p, Outcome::MissingInfo, "taxiway");
}
TEST_F(Engine, UnclearSpeechGetsSayAgain) {
  expectStays(ParsedInstruction{}, Outcome::Unintelligible);
  EXPECT_EQ(lastPilot().find("say again"), 0u);
  EXPECT_TRUE(lastJudged().suspectedStt);
}
TEST_F(Engine, UncertainValueIsAmbiguousEvenIfItWouldMatch) {
  auto p = taxi();
  p.fields["runway"].uncertain = true;
  expectStays(p, Outcome::Ambiguous, "runway");
  EXPECT_TRUE(lastJudged().suspectedStt);
  p = taxi();
  p.cls = RecognitionClass::Ambiguous;
  expectStays(p, Outcome::Ambiguous);
}

TEST_F(Engine, ConflictWithAClearanceIsChallenged) {
  sim->start();
  sim->wait(ms(6000));
  sim->transmit(taxi());
  sim->wait(ms(8000));
  auto p = taxi();
  p.fields["runway"].value = otherRunway();
  sim->transmit(p);
  EXPECT_EQ(lastJudged().outcome, Outcome::Conflict);
  EXPECT_EQ(lastJudged().problems, std::vector<std::string>{"runway"});
  EXPECT_EQ(step(), "LINE_UP");
  sim->transmit(taxi());  // the same again is no conflict
  EXPECT_EQ(lastJudged().outcome, Outcome::Unexpected);
}

TEST_F(Engine, ClippedTransmissionIsNotAccepted) {
  auto r = Simulator::goodReport();
  r.releasedEarly = true;
  sim->start();
  sim->wait(ms(6000));
  sim->transmit(taxi(), r);
  EXPECT_EQ(lastJudged().outcome, Outcome::Unintelligible);
  EXPECT_FALSE(lastJudged().suspectedStt);  // PTT discipline, not the recogniser
  EXPECT_EQ(step(), "TAXI");
  EngineConfig cfg;
  cfg.rules.clippedIsUnintelligible = false;
  make(7, cfg);
  sim->start();
  sim->wait(ms(6000));
  sim->transmit(taxi(), r);
  EXPECT_EQ(lastJudged().outcome, Outcome::Correct);
}
TEST_F(Engine, WordsFromSilenceAreNotAccepted) {
  auto r = Simulator::goodReport();
  r.empty = true;
  sim->start();
  sim->transmit(taxi(), r);
  EXPECT_EQ(lastJudged().outcome, Outcome::Unintelligible);
  EXPECT_EQ(step(), "TAXI");
}
TEST_F(Engine, DeviceLossAndRecogniserFaultAreNotCounted) {
  sim->start();
  sim->transmit(taxi(), Simulator::goodReport(), TxEnd::DeviceLost);
  EXPECT_EQ(lastJudged().outcome, Outcome::Unintelligible);
  EXPECT_TRUE(lastJudged().suspectedStt);
  sim->transmit(taxi(), Simulator::goodReport(), TxEnd::Normal, "model crashed");
  EXPECT_TRUE(lastJudged().suspectedStt);
  EXPECT_EQ(step(), "TAXI");
  const auto res = buildResult(ex->scenario, log.events);
  EXPECT_EQ(res.steps[0].suspectedStt, 2);
  EXPECT_TRUE(res.steps[0].errors.empty());
}

// Closed as stuck, lost or silent: sent on as Unrecognized without using the recogniser.
TEST_F(Engine, OnlyDeviceLossAndRecogniserFaultExcuseTheTrainee) {
  sim->start();
  sim->transmit(ParsedInstruction{}, Simulator::goodReport(), TxEnd::Stuck);
  EXPECT_FALSE(lastJudged().suspectedStt);
  EXPECT_EQ(lastJudged().problems, std::vector<std::string>{"held too long"});
  auto silent = Simulator::goodReport();
  silent.empty = true;
  sim->transmit(ParsedInstruction{}, silent);
  EXPECT_FALSE(lastJudged().suspectedStt);
  EXPECT_EQ(lastJudged().problems, std::vector<std::string>{"no speech"});
  sim->transmit(ParsedInstruction{}, silent, TxEnd::DeviceLost, "device lost");
  EXPECT_TRUE(lastJudged().suspectedStt);
  EXPECT_EQ(lastJudged().problems, std::vector<std::string>{"device lost"});
}

TEST_F(Engine, NoCallMakesThePilotCallAgainThenAlerts) {
  sim->start();
  sim->wait(ms(400'000));
  int timeouts = 0;
  for (const auto& j : outputs<out::Judged>()) timeouts += j.outcome == Outcome::Timeout;
  EXPECT_EQ(timeouts, 3);  // two calls again, then the alert
  EXPECT_EQ(outputs<out::PilotRequested>().size(), 3u);
  ASSERT_EQ(outputs<out::InstructorAlert>().size(), 1u);
  EXPECT_EQ(step(), "TAXI");
  EXPECT_FALSE(sim->ended());
}
TEST_F(Engine, TooManySayAgainsAlertTheInstructorOnce) {
  sim->start();
  for (int i = 0; i < 5; ++i) sim->transmit(ParsedInstruction{});
  EXPECT_EQ(outputs<out::InstructorAlert>().size(), 1u);
}
TEST_F(Engine, TimeLimitEndsTheExercise) {
  sim->start();
  sim->wait(ms(901'000));
  EXPECT_EQ(sim->reason(), out::EndReason::TimeLimit);
}
TEST_F(Engine, InstructorStopEndsItAndLaterResultsGetNoReply) {
  SequenceEngine e(ex, 1);
  e.handle(0, in::Started{});
  e.handle(1, in::TxOpened{1, 1, false});
  const auto o = e.handle(2, in::InstructorStop{});
  ASSERT_FALSE(o.empty());
  EXPECT_TRUE(std::holds_alternative<out::Ended>(o.back()));
  in::Recognised r;
  r.instruction.tx = 1;
  const auto late = e.handle(3, r);
  ASSERT_EQ(late.size(), 1u);
  EXPECT_TRUE(std::holds_alternative<out::NoReply>(late[0]));
}

TEST_F(Engine, TraineeWhoIsQuickerSkipsThePilotsOwnCall) {
  sim->start();
  sim->transmit(taxi());  // before the pilot's first call at 2 s
  EXPECT_EQ(lastJudged().outcome, Outcome::Correct);
  for (const auto& p : outputs<out::PilotRequested>()) EXPECT_NE(p.utterance.replyId, "REQUEST_TAXI");
}

namespace {
std::optional<out::TimerSet> timerIn(const SequenceEngine::Outputs& o, in::Timer k) {
  for (const auto& x : o)
    if (const auto* t = std::get_if<out::TimerSet>(&x))
      if (t->timer == k) return *t;
  return std::nullopt;
}
bool cancelled(const SequenceEngine::Outputs& o, in::Timer k) {
  for (const auto& x : o)
    if (const auto* t = std::get_if<out::TimerCancelled>(&x))
      if (t->timer == k) return true;
  return false;
}
std::uint64_t utteranceIn(const SequenceEngine::Outputs& o) {
  for (const auto& x : o)
    if (const auto* p = std::get_if<out::PilotRequested>(&x)) return p->utterance.id;
  return 0;
}
}  // namespace

TEST_F(Engine, StaleTimerIsIgnored) {
  SequenceEngine e(ex, 1);
  const auto first = timerIn(e.handle(0, in::Started{}), in::Timer::OnEnter);
  ASSERT_TRUE(first);
  EXPECT_TRUE(cancelled(e.handle(ms(100), in::TxOpened{1, ms(100), false}), in::Timer::OnEnter));
  EXPECT_TRUE(e.handle(ms(2000), in::TimerFired{in::Timer::OnEnter, first->generation}).empty());
  const auto second = timerIn(e.handle(ms(2100), in::TxCancelled{1}), in::Timer::OnEnter);
  ASSERT_TRUE(second);
  EXPECT_NE(second->generation, first->generation);
  EXPECT_TRUE(e.handle(ms(4000), in::TimerFired{in::Timer::OnEnter, first->generation}).empty());
  EXPECT_NE(utteranceIn(e.handle(ms(4100), in::TimerFired{in::Timer::OnEnter, second->generation})), 0u);
}

TEST_F(Engine, PttLossPausesTimers) {
  SequenceEngine e(ex, 1);
  ASSERT_TRUE(timerIn(e.handle(0, in::Started{}), in::Timer::OnEnter));
  EXPECT_TRUE(cancelled(e.handle(ms(500), in::PttPresence{false}), in::Timer::OnEnter));
  const auto o = e.handle(ms(60'000), in::PttPresence{true});
  const auto t = timerIn(o, in::Timer::OnEnter);
  ASSERT_TRUE(t);
  EXPECT_EQ(t->delay, ms(1500));
  const auto limit = timerIn(o, in::Timer::ExerciseLimit);
  ASSERT_TRUE(limit);
  EXPECT_EQ(limit->delay, ms(899'500));
}

TEST_F(Engine, InterruptedReplyStaysOutstandingOnlyWhenItIsRepeated) {
  for (const bool repeat : {true, false}) {
    ex->scenario.repeatInterruptedReply = repeat;
    SequenceEngine e(ex, 1);
    const auto t = timerIn(e.handle(0, in::Started{}), in::Timer::OnEnter);
    ASSERT_TRUE(t);
    const auto id = utteranceIn(e.handle(ms(2000), in::TimerFired{in::Timer::OnEnter, t->generation}));
    e.handle(ms(2000), in::PilotStarted{id});
    const auto o = e.handle(ms(2500), in::PilotEnded{id, in::PilotEnd::Interrupted});
    EXPECT_EQ(timerIn(o, in::Timer::StepTimeout).has_value(), !repeat);
    const auto kept = e.handle(ms(2600), in::PilotEnded{id, in::PilotEnd::PressCameFirst});
    EXPECT_FALSE(timerIn(kept, in::Timer::StepTimeout).has_value());
  }
}

TEST_F(Engine, ASecondResultForATransmissionIsNotJudged) {
  SequenceEngine e(ex, 7);  // the seed taxi() takes its values from
  e.handle(0, in::Started{});
  e.handle(1, in::TxOpened{1, 1, false});
  in::TxClosed c;
  c.tx = 1;
  c.report = Simulator::goodReport();
  e.handle(2, c);
  in::Recognised r;
  r.instruction = taxi();
  r.instruction.tx = 1;
  EXPECT_NE(utteranceIn(e.handle(3, r)), 0u);
  const auto o = e.handle(4, r);
  ASSERT_EQ(o.size(), 1u);
  EXPECT_TRUE(std::holds_alternative<out::NoReply>(o[0]));
}

TEST_F(Engine, ErrorBranchMovesWithoutAdvancing) {
  ex->scenario.steps[0].transitions.push_back({Outcome::Unexpected, "TAKEOFF", true});
  make();
  sim->start();
  sim->wait(ms(6000));
  sim->transmit(takeoff());
  EXPECT_FALSE(lastJudged().advanced);
  EXPECT_EQ(step(), "TAKEOFF");
  EXPECT_FALSE(buildResult(ex->scenario, log.events).steps[0].completed);
}

// Random instructions, reports and waits: the rules hold whatever comes in.
TEST_F(Engine, RandomInputsNeverBreakTheRules) {
  std::mt19937 g(1234);
  const auto chance = [&](unsigned percent) { return g() % 100 < percent; };
  for (int round = 0; round < 300; ++round) {
    make(static_cast<std::uint64_t>(round));
    sim->start();
    for (int i = 0; i < 25 && !sim->ended(); ++i) {
      if (chance(40)) sim->wait(ms(static_cast<std::int64_t>(g() % 40'000)));
      ParsedInstruction p = chance(34) ? taxi() : chance(50) ? lineUp() : takeoff();
      if (chance(20)) p.fields["runway"].value = otherRunway();
      if (chance(10)) p.fields["callsign"].value = "HAWK-999";
      if (chance(10)) p.fields.erase("runway");
      if (chance(10)) p.fields["runway"].uncertain = true;
      if (chance(10)) p.cls = static_cast<RecognitionClass>(g() % 4);
      if (chance(5)) p.intent = "NONSENSE";
      auto r = Simulator::goodReport();
      r.releasedEarly = chance(8);
      r.empty = chance(4);
      sim->transmit(p, r, chance(4) ? TxEnd::DeviceLost : TxEnd::Normal, chance(3) ? "fault" : "");
    }
    std::string current;
    std::uint64_t recognised = 0;
    std::map<std::uint64_t, int> answers;
    bool mayMove = true;
    for (const auto& e : log.events) {
      if (const auto* i = std::get_if<EngineInput>(&e.body)) {
        mayMove = std::holds_alternative<in::Started>(*i);
        if (const auto* rec = std::get_if<in::Recognised>(i)) {
          ++recognised;
          answers[rec->instruction.tx] += 0;
        }
        continue;
      }
      const auto& o = std::get<EngineOutput>(e.body);
      if (const auto* j = std::get_if<out::Judged>(&o)) {
        EXPECT_EQ(j->step, current);
        EXPECT_EQ(j->advanced, j->outcome == Outcome::Correct);
        mayMove = j->advanced;
      } else if (const auto* s = std::get_if<out::StepEntered>(&o)) {
        EXPECT_TRUE(mayMove) << "step " << s->step << " entered without CORRECT";
        current = s->step;
      } else if (const auto* u = std::get_if<out::PilotRequested>(&o)) {
        if (u->utterance.tx) ++answers[u->utterance.tx];
      } else if (const auto* n = std::get_if<out::NoReply>(&o)) {
        ++answers[n->tx];
      }
    }
    EXPECT_EQ(answers.size(), recognised);
    for (const auto& [tx, n] : answers) EXPECT_EQ(n, 1) << "transmission " << tx;
    for (const auto& c : sim->engine().context().clearances) {
      EXPECT_EQ(c.fields.at("callsign"), param("callsign"));
      EXPECT_EQ(c.fields.at("runway"), param("runway"));
    }
  }
}

TEST_F(Engine, ResultSeparatesTraineeErrorsFromSuspectedRecognitionErrors) {
  sim->start();
  sim->wait(ms(6000));
  auto wrong = taxi();
  wrong.fields["runway"].value = otherRunway();
  sim->transmit(wrong);
  sim->transmit(ParsedInstruction{});
  auto r = Simulator::goodReport();
  r.spokeBeforeKeying = true;
  sim->transmit(taxi(), r);
  sim->transmit(taxi());
  sim->waitForPilot();
  sim->stop();
  const auto res = buildResult(ex->scenario, log.events);
  ASSERT_EQ(res.steps.size(), 3u);  // the final step is left out
  EXPECT_TRUE(res.steps[0].completed);
  EXPECT_EQ(res.steps[0].attempts, 4);
  EXPECT_EQ(res.steps[0].errors.at(Outcome::Incorrect), 1);
  EXPECT_EQ(res.steps[0].errors.at(Outcome::Unintelligible), 1);  // the clipped one
  EXPECT_EQ(res.steps[0].suspectedStt, 1);
  ASSERT_EQ(res.suspectedStt.size(), 1u);
  EXPECT_EQ(res.suspectedStt[0].step, "TAXI");
  EXPECT_EQ(res.steps[0].responseTime, std::optional<Ns>(ms(1500)));  // pilot ends at 4.5 s, press at 6 s
  EXPECT_FALSE(res.steps[2].reached);
  EXPECT_EQ(res.ptt.transmissions, 4);
  EXPECT_EQ(res.ptt.spokeBeforeKeying, 1);
  EXPECT_EQ(res.ptt.keyToSpeechMs, std::optional<int>(300));
  EXPECT_EQ(res.end, out::EndReason::InstructorStop);
}

TEST(EventJson, HeaderHoldsModeTraineeAndModel) {
  EventLogHeader h;
  h.modelId = "m";
  h.modelConfig = "{}";
  h.traineeId = "T1";
  const json j = json::parse(headerJson(h));
  EXPECT_EQ(j["mode"].get<std::string>(), "practice");
  EXPECT_EQ(j["trainee"].get<std::string>(), "T1");
  EXPECT_EQ(j["modelId"].get<std::string>(), "m");
  EXPECT_EQ(j["modelConfig"].get<std::string>(), "{}");
}
TEST(EventJson, BrokenTextDoesNotThrow) {
  in::Recognised r;
  r.instruction.rawText = "\xff\xfe runway";
  EXPECT_NO_THROW({
    const json j = json::parse(eventJson(EngineEvent{1, 2, EngineInput(r)}));
    EXPECT_EQ(j["event"].get<std::string>(), "recognised");
    EXPECT_EQ(j["seq"].get<int>(), 1);
  });
}
TEST(FillSlots, ReplacesKnownSlots) {
  EXPECT_EQ(fillSlots("runway {runway}, {callsign}", {{"runway", "09"}, {"callsign", "EAGLE-421"}}), "runway 09, EAGLE-421");
  EXPECT_EQ(fillSlots("plain", {}), "plain");
}
