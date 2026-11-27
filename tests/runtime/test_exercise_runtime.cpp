#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <set>
#include <sstream>

#include "audio/WavFile.h"
#include "core/PathResolver.h"
#include "engine/EventJson.h"
#include "runtime/ExerciseCheck.h"
#include "runtime/ExerciseRuntime.h"
#include "runtime/JsonlEventLog.h"
#include "voice/PhraseTemplate.h"
using namespace atc;
using nlohmann::json;
namespace fs = std::filesystem;
using S = TransmissionController::State;

namespace {
Paths paths() {
  Paths p;
  std::string error;
  EXPECT_TRUE(resolvePaths(systemPathInputs(), p, error)) << error;
  return p;
}
std::string scenarioFile() { return paths().data + "/scenarios/DEPARTURE-01.json"; }

struct TempDir {
  fs::path path;
  TempDir() {
    static int n = 0;
    path = fs::temp_directory_path() / ("atc_run_" + std::to_string(getpid()) + "_" + std::to_string(n++));
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

// A voice bank with a short tone for every clip the departure needs, except `without`.
void makeBank(const fs::path& dir, const std::string& without = {}) {
  Exercise e;
  std::vector<LoadError> errors;
  ASSERT_TRUE(loadExercise(scenarioSources(paths().data, scenarioFile()), e, errors));
  std::map<std::string, RequiredClip> need;
  std::vector<std::string> bad;
  ASSERT_TRUE(requiredClips(e, need, bad));
  json m = {{"schemaVersion", 1}, {"voice", "test"}, {"sampleRate", 16000}, {"placeholder", true}, {"clips", json::array()}};
  int n = 0;
  for (const auto& [id, c] : need) {
    if (id == without) continue;
    const std::string file = "c" + std::to_string(n++) + ".wav";
    ASSERT_TRUE(writeWav((dir / file).string(), 16000, std::vector<std::int16_t>(3200, 1000)));
    json take = {{"file", file}, {"take", "primary"}};
    m["clips"].push_back({{"id", id}, {"takes", json::array({take})}});
  }
  std::ofstream(dir / "manifest.json") << m.dump();
}

struct FakeModel : ISpeechModel {
  SttStatus st;
  int locks = 0, unlocks = 0;
  bool allow = true;
  FakeModel() {
    st.state = SttState::Ready;
    st.modelId = "whisper-tiny-genprompt";
    st.modelConfig = "{\"file\":\"x\"}";
  }
  SttStatus status() const override { return st; }
  bool lock() override {
    if (!allow) return false;
    ++locks;
    return st.locked = true;
  }
  void unlock() override {
    ++unlocks;
    st.locked = false;
  }
};

struct MemoryLog : IEventLog {
  struct Store {
    EventLogHeader header;
    std::vector<EngineEvent> events;
    bool ended = false, refuse = false;
    std::uint64_t lost = 0;
  };
  explicit MemoryLog(std::shared_ptr<Store> s) : store(std::move(s)) {}
  bool begin(const EventLogHeader& h, std::string& error) override {
    error = store->refuse ? "disk" : headerError(h);
    store->header = h;
    return error.empty();
  }
  void append(const EngineEvent& e) override {
    EXPECT_FALSE(store->ended);
    store->events.push_back(e);
  }
  void end() override { store->ended = true; }
  std::uint64_t lost() const override { return store->lost; }
  std::shared_ptr<Store> store;
};

struct Rig;
struct FakeMic : IRadioMic { bool start() override { return true; } };
struct FakePtt : IRadioPtt { void start() override {} };
struct FakePlayer : IRadioPlayer {
  explicit FakePlayer(Rig& r) : rig(r) {}
  bool start() override { return true; }
  bool play(PlayId id, std::vector<std::int16_t> samples, int rate) override;
  bool stop() override;
  Rig& rig;
};
struct FakeSpeech : ISpeechFeed {
  bool model = true;
  std::uint64_t open = 0;
  std::vector<std::uint64_t> finished;
  bool begin(std::uint64_t tx) override { if (model) open = tx; return model; }
  void accept(std::vector<std::int16_t>) override {}
  void finish(Ns) override { finished.push_back(std::exchange(open, 0)); }
  void cancel() override { open = 0; }
};

// The whole exercise on fakes, run by hand in 10 ms steps: pedal, microphone, player and recogniser are simulated,
// everything between them is real.
struct Rig {
  Ns now = ms(1000);
  TempDir dir;
  PrerollRing* ring = nullptr;
  LevelMeter* meter = nullptr;
  PttCallbacks ptt;
  PlayerCallbacks out;
  FakeSpeech speech;
  FakeModel model;
  int level = 4000;
  PlayId sounding = 0;
  std::size_t soundSamples = 0;
  Ns soundEnd = 0;
  bool playOk = true;
  int plays = 0;
  RadioSnapshot snap;
  std::vector<std::uint64_t> labOpened, labClosed;
  std::vector<std::string> faults, refusals;
  std::vector<std::string> order;  // "fault" and the names of the inputs, as the screen gets them
  std::vector<EngineOutput> outputs;
  std::optional<ExerciseResult> result;
  int startedOk = 0;
  std::vector<std::pair<std::uint64_t, std::size_t>> audio;  // tx, samples handed over for keeping (11)
  std::shared_ptr<MemoryLog::Store> log = std::make_shared<MemoryLog::Store>();
  PreparedExercise ready;
  ExerciseOptions opt;
  std::unique_ptr<ExerciseRuntime> run;
  std::unique_ptr<RadioCore> core;

  explicit Rig(bool withPtt = true, RuntimeConfig rc = {}) {
    makeBank(dir.path);
    auto c = ExerciseCheck::check(paths().data, dir.path.string(), scenarioFile());
    EXPECT_EQ(c.state, CheckResult::State::Covered);
    ready = c.ready;
    ParserRules rules;
    std::string error;
    EXPECT_TRUE(loadParserRules(paths().data + "/phraseology/placeholder.json", rules, error)) << error;
    ExerciseRuntime::Callbacks cb;
    cb.started = [this](bool ok, const std::string& why) { ok ? void(++startedOk) : refusals.push_back(why); };
    cb.output = [this](Ns, const EngineOutput& o) { outputs.push_back(o); };
    cb.ended = [this](const ExerciseResult& r) { result = r; };
    cb.audio = [this](const RadioTx& t) { audio.emplace_back(t.id, t.audio.size()); };
    cb.fault = [this](const std::string& s) { faults.push_back(s); order.push_back("fault"); };
    cb.input = [this](Ns, const EngineInput& i) { order.push_back(std::visit([](const auto& x) { return std::string(x.kName); }, i)); };
    run = std::make_unique<ExerciseRuntime>(rc, rules, model, cb);
    RadioDevices d;
    d.mic = [this](PrerollRing& r, LevelMeter& m, std::function<void(const CaptureStatus&)> f) {
      ring = &r;
      meter = &m;
      CaptureStatus s;
      s.state = CaptureState::Running;
      f(s);
      return std::make_unique<FakeMic>();
    };
    d.player = [this](PlayerCallbacks p) { out = std::move(p); return std::make_unique<FakePlayer>(*this); };
    if (withPtt) d.ptt = [this](PttCallbacks p) { ptt = std::move(p); return std::make_unique<FakePtt>(); };
    d.speech = &speech;
    RadioCore::Events lab;
    lab.snapshot = [this](const RadioSnapshot& s) { snap = s; };
    lab.txOpened = [this](std::uint64_t id, Ns, bool) { labOpened.push_back(id); };
    lab.txClosed = [this](RadioTx&& t) { labClosed.push_back(t.id); core->processed(t.id); };
    lab.outputFault = [this](const std::string& s) { faults.push_back(s); };
    RadioConfig cfg;
    cfg.publishEvery = ms(10);
    core = std::make_unique<RadioCore>(cfg, std::move(d), run->wrap(std::move(lab)), [this] { return now; });
    run->attach(*core);
    core->open();
    if (withPtt) ptt.opened(false);
    opt.sessionId = "test";
    opt.model = "whisper-tiny-genprompt";
    opt.seed = 7;
    pass(1000);
  }
  ~Rig() { core->stop(); }

  void pass(int millis) {
    for (int i = 0; i < millis / 10; ++i) {
      now += ms(10);
      std::vector<std::int16_t> b(160);
      for (int k = 0; k < 160; ++k) b[std::size_t(k)] = std::int16_t(level * std::sin(k * 0.4));
      ring->push(now, b);
      meter->add(b);
      if (sounding && now >= soundEnd) out.finished(std::exchange(sounding, 0), true);
      core->step();
      half();
    }
  }
  // The invariant, after every step.
  void half() {
    if (!sounding) return;
    EXPECT_NE(snap.state, S::TxOpen);
    EXPECT_NE(snap.state, S::TxTail);
  }
  std::string start() {
    const std::string why = run->start(ready, opt, std::make_unique<MemoryLog>(log));
    core->step();
    return why;
  }
  void down() { ptt.edge(now, true); core->step(); }
  void up() { ptt.edge(now, false); core->step(); }
  // Until the pilot has finished what is asked for.
  void hearPilot(int limit = 8000) {
    int t = 0;
    for (; t < limit && !sounding; t += 10) pass(10);
    EXPECT_TRUE(sounding) << "the pilot said nothing";
    for (; t < limit && sounding; t += 10) pass(10);
    pass(50);
  }
  std::uint64_t transmit(int millis = 1500) {
    down();
    pass(millis);
    up();
    pass(400);
    return speech.finished.empty() ? 0 : speech.finished.back();
  }
  void text(std::uint64_t tx, const std::string& words) {
    SttResult r;
    r.text = words;
    EXPECT_TRUE(run->recognised(tx, model.st.modelId, model.st.modelConfig, r, ms(2000)));
    core->step();
  }
  void say(const std::string& words) { text(transmit(), words); }
  // The exercise's own value, as it is said.
  std::string v(const std::string& name) const {
    ParamValues p;
    generateParams(ready.exercise->scenario, ready.exercise->airfield, opt.seed, p);
    static const std::map<std::string, std::string> spoken{{"09", "zero nine"}, {"27", "two seven"}, {"A3", "alpha three"}, {"B2", "bravo two"}};
    const auto& x = p.at(name);
    if (name == "callsign")
      for (const auto& c : ready.exercise->airfield.callsigns)
        if (c.id == x) return c.spoken;
    return spoken.at(x);
  }
  std::string taxi(const std::string& runway = {}) const { return v("callsign") + " taxi via " + v("taxiway") + " runway " + (runway.empty() ? v("runway") : runway); }
  std::string lineUp() const { return v("callsign") + " line up runway " + v("runway"); }
  std::string takeoff() const { return v("callsign") + " runway " + v("runway") + " cleared for takeoff"; }
  std::string otherRunway() const { return v("runway") == "zero nine" ? "two seven" : "zero nine"; }

  template <class T> std::vector<T> logged() const {
    std::vector<T> r;
    for (const auto& e : log->events)
      std::visit([&](const auto& side) {
        std::visit([&](const auto& x) { if constexpr (std::is_same_v<std::decay_t<decltype(x)>, T>) r.push_back(x); }, side);
      }, e.body);
    return r;
  }
  std::vector<Outcome> outcomes() const {
    std::vector<Outcome> o;
    for (const auto& j : logged<out::Judged>()) o.push_back(j.outcome);
    return o;
  }
};
bool FakePlayer::play(PlayId id, std::vector<std::int16_t> s, int rate) {
  EXPECT_NE(rig.snap.state, S::TxOpen);
  ++rig.plays;
  if (!rig.playOk) return false;
  rig.sounding = id;
  rig.soundSamples = s.size();
  rig.soundEnd = rig.now + Ns(s.size()) * 1'000'000'000 / rate;
  return true;
}
bool FakePlayer::stop() {
  rig.sounding = 0;
  return true;
}
}  // namespace

TEST(ExerciseRuntime, DepartureFromPedalToResult) {
  Rig r;
  ASSERT_EQ(r.start(), "");
  ASSERT_EQ(r.startedOk, 1);
  EXPECT_TRUE(r.model.st.locked);
  r.hearPilot();  // the pilot calls first
  r.say(r.taxi());
  r.hearPilot();
  r.say(r.lineUp());
  r.hearPilot();
  r.say(r.takeoff());
  r.hearPilot();
  ASSERT_TRUE(r.result);
  EXPECT_EQ(r.result->end, out::EndReason::Completed);
  EXPECT_EQ(r.outcomes(), (std::vector<Outcome>{Outcome::Correct, Outcome::Correct, Outcome::Correct}));
  EXPECT_EQ(r.result->ptt.transmissions, 3);
  EXPECT_FALSE(r.result->fallbackPttUsed());
  EXPECT_EQ(r.result->pilotFaults, 0);
  EXPECT_TRUE(r.faults.empty()) << r.faults.front();
  EXPECT_TRUE(r.log->ended);
  EXPECT_FALSE(r.model.st.locked);
  EXPECT_EQ(r.run->phase(), ExercisePhase::Ended);
  EXPECT_TRUE(r.labClosed.empty());
  EXPECT_EQ(r.plays, 4);
  // Afterwards the radio is the Lab's again.
  r.pass(500);
  r.transmit();
  EXPECT_EQ(r.labClosed.size(), 1u);
}

TEST(ExerciseRuntime, LogHoldsEverythingOfD427) {
  Rig r;
  r.opt.traineeId = "S01";
  ASSERT_EQ(r.start(), "");
  r.hearPilot();
  r.say(r.taxi());
  r.hearPilot();
  r.down(); r.pass(100); r.up(); r.pass(400);  // short tap
  EXPECT_EQ(r.log->header.modelId, "whisper-tiny-genprompt");
  EXPECT_EQ(r.log->header.modelConfig, r.model.st.modelConfig);
  EXPECT_EQ(r.log->header.seed, 7u);
  EXPECT_EQ(r.log->header.traineeId, "S01");
  EXPECT_EQ(r.log->header.scenarioHash, r.ready.exercise->hash);
  const auto rec = r.logged<in::Recognised>();
  ASSERT_EQ(rec.size(), 1u);
  EXPECT_EQ(rec[0].instruction.modelId, r.model.st.modelId);
  EXPECT_EQ(rec[0].instruction.modelConfig, r.model.st.modelConfig);
  EXPECT_EQ(rec[0].instruction.intent, "TAXI_INSTRUCTION");
  EXPECT_EQ(rec[0].releaseToText, ms(2000));
  const auto asked = r.logged<out::PilotRequested>();
  ASSERT_EQ(asked.size(), 2u);
  for (const auto& a : asked) {
    EXPECT_FALSE(a.utterance.replyId.empty());
    EXPECT_FALSE(a.utterance.clipIds.empty());
  }
  EXPECT_EQ(r.logged<in::PilotStarted>().size(), 2u);
  EXPECT_EQ(r.logged<in::PilotEnded>().size(), 2u);
  EXPECT_EQ(r.logged<in::TxOpened>().size(), 2u);
  EXPECT_EQ(r.logged<in::TxClosed>().size(), 1u);
  EXPECT_EQ(r.logged<in::TxCancelled>().size(), 1u);
  // In order, without gaps, and a replay of the inputs gives the same outputs.
  SequenceEngine again(r.ready.exercise, 7);
  std::vector<EngineOutput> expect;
  std::uint64_t seq = 0;
  std::size_t next = 0;
  for (const auto& e : r.log->events) {
    EXPECT_EQ(e.seq, ++seq);
    if (isInput(e)) {
      EXPECT_EQ(next, expect.size());
      expect = again.handle(e.t, std::get<EngineInput>(e.body));
      next = 0;
    } else {
      ASSERT_LT(next, expect.size());
      EngineEvent mine{e.seq, e.t, expect[next++]};
      EXPECT_EQ(eventJson(mine), eventJson(e));
    }
  }
}

TEST(ExerciseRuntime, WrongRunwayNeverAdvances) {
  Rig r;
  ASSERT_EQ(r.start(), "");
  r.hearPilot();
  r.say(r.taxi(r.otherRunway()));
  EXPECT_EQ(r.outcomes(), std::vector<Outcome>{Outcome::Incorrect});
  EXPECT_FALSE(r.logged<out::Judged>()[0].advanced);
  EXPECT_EQ(r.logged<out::StepEntered>().size(), 1u);
  r.hearPilot();
  r.say(r.taxi());
  EXPECT_EQ(r.logged<out::StepEntered>().size(), 2u);
}

TEST(ExerciseRuntime, UnclearSpeechGetsSayAgainAndWrongCallsignSilence) {
  Rig r;
  ASSERT_EQ(r.start(), "");
  r.hearPilot();
  r.say("mumble mumble");
  EXPECT_EQ(r.outcomes().back(), Outcome::Unintelligible);
  r.hearPilot();
  std::string other;
  for (const auto& c : r.ready.exercise->airfield.callsigns)
    if (c.spoken != r.v("callsign")) other = c.spoken;
  ASSERT_FALSE(other.empty());
  const int before = r.plays;
  r.say(other + " taxi via " + r.v("taxiway") + " runway " + r.v("runway"));
  EXPECT_EQ(r.outcomes().back(), Outcome::WrongCallsign);
  r.pass(4000);
  EXPECT_EQ(r.plays, before);
  EXPECT_EQ(r.snap.state, S::Idle);  // answered all the same
}

TEST(ExerciseRuntime, NoCallMakesThePilotCallAgainThenAsksTheInstructor) {
  Rig r;
  ASSERT_EQ(r.start(), "");
  r.hearPilot();
  for (int call = 0; call < 2; ++call) r.hearPilot(40'000);
  r.pass(40'000);
  EXPECT_EQ(r.logged<out::InstructorAlert>().size(), 1u);
  EXPECT_TRUE(r.logged<out::InstructorAlert>()[0].choice);
  EXPECT_EQ(r.logged<out::StepEntered>().size(), 1u);
  r.run->resume();
  r.hearPilot(40'000);
  EXPECT_EQ(r.logged<in::InstructorContinue>().size(), 1u);
}

TEST(ExerciseRuntime, PressDuringPilotSpeechSilencesBeforeTxOpens) {
  Rig r;
  ASSERT_EQ(r.start(), "");
  for (int t = 0; t < 5000 && !r.sounding; t += 10) r.pass(10);
  ASSERT_TRUE(r.sounding);
  r.pass(100);
  r.down();
  EXPECT_FALSE(r.sounding);
  EXPECT_EQ(r.snap.state, S::TxOpen);
  ASSERT_EQ(r.logged<in::PilotEnded>().size(), 1u);
  EXPECT_EQ(r.logged<in::PilotEnded>()[0].end, in::PilotEnd::Interrupted);
  r.pass(1500);
  r.up();
  r.pass(400);
  r.text(r.speech.finished.back(), r.taxi());
  r.hearPilot();
  EXPECT_TRUE(r.faults.empty());
  // D-325: the answer, and not the cut call again.
  const auto started = r.logged<in::PilotStarted>();
  ASSERT_EQ(started.size(), 2u);
  EXPECT_EQ(r.logged<out::PilotRequested>()[1].utterance.replyId, "READBACK_TAXI");
  EXPECT_EQ(started[1].utterance, r.logged<out::PilotRequested>()[1].utterance.id);
  EXPECT_TRUE(r.logged<out::AtcBeforePilot>().empty());  // the call had begun
}

// D-325
TEST(ExerciseRuntime, ACutReadbackIsSaidAgainAfterTheNewAnswer) {
  Rig r;
  ASSERT_EQ(r.start(), "");
  r.hearPilot();
  r.say(r.taxi());
  for (int t = 0; t < 5000 && !r.sounding; t += 10) r.pass(10);
  ASSERT_TRUE(r.sounding);
  r.pass(100);
  r.down();
  EXPECT_FALSE(r.sounding);
  r.pass(1500);
  r.up();
  r.pass(400);
  r.text(r.speech.finished.back(), "mumble");
  r.hearPilot();
  r.hearPilot();
  const auto said = r.logged<out::PilotRequested>();
  const auto started = r.logged<in::PilotStarted>();
  ASSERT_GE(said.size(), 3u);
  EXPECT_EQ(said[1].utterance.replyId, "READBACK_TAXI");
  EXPECT_EQ(said[2].utterance.replyId, "SAY_AGAIN");
  ASSERT_GE(started.size(), 4u);
  EXPECT_EQ(started[2].utterance, said[2].utterance.id);  // the new one first
  EXPECT_EQ(started[3].utterance, said[1].utterance.id);
  EXPECT_TRUE(r.faults.empty());
}

// D-322: 3c run 1 pressed before the call was due.
TEST(ExerciseRuntime, ThePilotCallsFirstThoughTheTraineeIsQuicker) {
  Rig r;
  ASSERT_EQ(r.start(), "");
  r.pass(500);
  const auto tx = r.transmit(2000);  // the call is due meanwhile
  EXPECT_TRUE(r.sounding);           // after the release, without waiting for the text
  r.text(tx, "mumble");
  for (int t = 0; t < 8000 && r.sounding; t += 10) r.pass(10);
  r.hearPilot();
  const auto said = r.logged<out::PilotRequested>();
  const auto started = r.logged<in::PilotStarted>();
  ASSERT_GE(said.size(), 2u);
  EXPECT_EQ(said[0].utterance.replyId, "REQUEST_TAXI");
  EXPECT_EQ(said[1].utterance.replyId, "SAY_AGAIN");
  ASSERT_GE(started.size(), 2u);
  EXPECT_EQ(started[0].utterance, said[0].utterance.id);
  EXPECT_EQ(started[1].utterance, said[1].utterance.id);
  ASSERT_EQ(r.logged<out::AtcBeforePilot>().size(), 1u);
  EXPECT_TRUE(r.faults.empty());
}

TEST(ExerciseRuntime, StopSilencesThePilotAtOnceAndEndsEverything) {
  Rig r;
  ASSERT_EQ(r.start(), "");
  for (int t = 0; t < 5000 && !r.sounding; t += 10) r.pass(10);
  ASSERT_TRUE(r.sounding);
  r.run->stop();
  r.core->step();
  EXPECT_FALSE(r.sounding);
  ASSERT_TRUE(r.result);
  EXPECT_EQ(r.result->end, out::EndReason::InstructorStop);
  EXPECT_FALSE(r.model.st.locked);
  const int plays = r.plays;
  r.pass(60'000);
  EXPECT_EQ(r.plays, plays);
  EXPECT_EQ(r.snap.state, S::Idle);
  EXPECT_TRUE(r.faults.empty());
}

TEST(ExerciseRuntime, StopDuringTransmissionOrRecognitionLeavesTheRadioFree) {
  for (const bool whileOpen : {true, false}) {
    Rig r;
    ASSERT_EQ(r.start(), "");
    r.hearPilot();
    std::uint64_t tx = 0;
    if (whileOpen) {
      r.down();
      r.pass(800);
    } else {
      tx = r.transmit();
    }
    r.run->stop();
    r.core->step();
    ASSERT_TRUE(r.result);
    if (whileOpen) {
      r.pass(500);
      r.up();
      r.pass(400);
      tx = r.speech.finished.back();
    }
    r.pass(20);
    EXPECT_EQ(r.snap.state, S::Idle);
    SttResult late;
    late.text = r.taxi();
    EXPECT_TRUE(r.run->recognised(tx, "m", "c", late, 0));  // still not the Lab's
    r.pass(3000);
    EXPECT_EQ(r.snap.state, S::Idle);
    EXPECT_TRUE(r.labClosed.empty());
    EXPECT_TRUE(r.outcomes().empty());
  }
}

TEST(ExerciseRuntime, SilentOrBrokenRecogniserIsNoTraineeError) {
  RuntimeConfig rc;
  rc.recogniseLimit = ms(5000);
  Rig r(true, rc);
  ASSERT_EQ(r.start(), "");
  r.hearPilot();
  const auto tx = r.transmit();
  r.pass(6000);
  ASSERT_EQ(r.logged<in::Recognised>().size(), 1u);
  EXPECT_FALSE(r.logged<in::Recognised>()[0].sttFault.empty());
  EXPECT_TRUE(r.logged<out::Judged>()[0].suspectedStt);
  EXPECT_EQ(r.faults.size(), 1u);
  SttResult late;
  late.text = r.taxi();
  r.run->recognised(tx, "m", "c", late, 0);
  r.pass(10);
  EXPECT_EQ(r.logged<in::Recognised>().size(), 1u);  // exactly one per transmission
  EXPECT_EQ(r.logged<out::StepEntered>().size(), 1u);
  r.hearPilot();
  // The model vanishes: recorded as a fault too.
  r.speech.model = false;
  r.down(); r.pass(1500); r.up(); r.pass(400);
  EXPECT_EQ(r.logged<in::Recognised>().back().sttFault, "no speech model");
}

// D-314: 10 s unless configured, a system fault that is not the trainee's, and the pilot says again.
TEST(ExerciseRuntime, RecogniserHasTenSecondsThenThePilotSaysAgain) {
  EXPECT_EQ(RuntimeConfig{}.recogniseLimit, ms(10'000));
  Rig r;
  ASSERT_EQ(r.start(), "");
  r.hearPilot();
  r.transmit();
  r.pass(9500);  // transmit() has waited for the tail
  EXPECT_TRUE(r.logged<in::Recognised>().empty());
  r.pass(600);
  ASSERT_EQ(r.logged<in::Recognised>().size(), 1u);
  EXPECT_EQ(r.logged<in::Recognised>()[0].sttFault, "no result in time");
  const auto f = std::find(r.order.begin(), r.order.end(), "fault");
  ASSERT_NE(f, r.order.end());
  EXPECT_EQ(*std::next(f), "recognised");  // directly after it: ExerciseView counts the two as one
  ASSERT_FALSE(r.logged<out::PilotRequested>().empty());
  EXPECT_EQ(r.logged<out::PilotRequested>().back().utterance.replyId, "SAY_AGAIN");
  const ExerciseResult res = buildResult(r.ready.exercise->scenario, r.log->events);
  ASSERT_EQ(res.suspectedStt.size(), 1u);
  for (const auto& s : res.steps) {
    EXPECT_TRUE(s.errors.empty());
    EXPECT_FALSE(s.completed);
  }
}

TEST(ExerciseRuntime, SilenceIsUnintelligibleWithoutFault) {
  Rig r;
  ASSERT_EQ(r.start(), "");
  r.hearPilot();
  r.level = 0;
  r.pass(1000);
  r.down(); r.pass(1500); r.up(); r.pass(400);
  ASSERT_EQ(r.logged<in::Recognised>().size(), 1u);
  EXPECT_TRUE(r.logged<in::Recognised>()[0].sttFault.empty());
  EXPECT_TRUE(r.logged<in::TxClosed>()[0].report.empty);
}

// D-333, the 3c case: the trainee speaks again before the first answer; only the newer one is answered, and the
// engine does not wait for the dropped one.
TEST(ExerciseRuntime, OnlyTheAnswerToTheNewerTransmissionIsSpoken) {
  Rig r;
  ASSERT_EQ(r.start(), "");
  r.hearPilot();
  const auto first = r.transmit();
  const auto second = r.transmit();  // before the first result
  r.text(first, "mumble");
  r.text(second, "mumble");
  const int before = r.plays;
  r.hearPilot();
  r.pass(3000);
  EXPECT_EQ(r.plays, before + 1);
  int superseded = 0;
  for (const auto& e : r.logged<in::PilotEnded>()) superseded += e.end == in::PilotEnd::Superseded;
  EXPECT_EQ(superseded, 1);
  r.say(r.taxi());  // the exercise goes on: nothing is waited for
  EXPECT_EQ(r.outcomes().back(), Outcome::Correct);
}

// D-334: a plan's values are the ones run and logged; values that do not fit are refused before the model is locked.
TEST(ExerciseRuntime, APlansValuesRunAndAreLogged) {
  Rig r;
  bool tooMany = false;
  const auto all = combinations(r.ready.exercise->scenario, r.ready.exercise->airfield, 10, tooMany);
  ParamValues seeds;
  generateParams(r.ready.exercise->scenario, r.ready.exercise->airfield, r.opt.seed, seeds);
  ParamValues other = all[0] == seeds ? all[1] : all[0];
  ParamValues wrong = other;
  wrong["taxiway"] = wrong["runway"] == "09" ? "B2" : "A3";
  r.opt.values = wrong;
  const int locks = r.model.locks;
  EXPECT_NE(r.start().find("does not serve"), std::string::npos);
  EXPECT_EQ(r.model.locks, locks);
  r.opt.values = other;
  r.opt.replyDelay = std::pair{ms(300), ms(400)};
  r.opt.plan = {"p1", "Departure, runway", "custom", 3};
  ASSERT_EQ(r.start(), "");
  EXPECT_EQ(r.log->header.values, other);
  EXPECT_EQ(r.log->header.replyFromMs, 300);
  EXPECT_EQ(r.log->header.replyToMs, 400);
  EXPECT_EQ(r.log->header.planRun, 3);
  const json h = json::parse(headerJson(r.log->header));
  EXPECT_EQ(h["version"], 2);
  EXPECT_EQ(h["values"]["runway"], other["runway"]);
  EXPECT_EQ(h["plan"]["name"], "Departure, runway");
  r.hearPilot();
  const auto said = r.logged<out::PilotRequested>().front().utterance.text;
  for (const auto& c : r.ready.exercise->airfield.callsigns)
    if (c.id == other["callsign"]) {
      EXPECT_NE(said.find(c.id), std::string::npos) << said;
    }
}

// 11: each closed transmission of the exercise is handed over with its audio; nothing else (Lab, short tap).
TEST(ExerciseRuntime, TheAudioOfEachTransmissionIsHandedOver) {
  Rig r;
  r.down(); r.pass(10); r.up(); r.pass(400);  // a short tap before the start: not the exercise's
  ASSERT_EQ(r.start(), "");
  r.hearPilot();
  const auto tx = r.transmit();
  r.down(); r.pass(50); r.up(); r.pass(400);  // a short tap: cancelled, no audio
  ASSERT_EQ(r.audio.size(), 1u);
  EXPECT_EQ(r.audio[0].first, tx);
  EXPECT_GT(r.audio[0].second, 16000u);  // pre-roll, 1.5 s, tail
}

// D-342: the reply delay is the whole response time from the release; a slow recogniser does not add to it.
TEST(ExerciseRuntime, RecognitionTimeIsNotAddedToTheReplyDelay) {
  Rig r;
  ASSERT_EQ(r.start(), "");
  r.hearPilot();
  const auto pilotStarts = [&] { return r.logged<in::PilotStarted>(); };
  // A slow result: 2.5 s after the release, more than any configured delay (0.5 to 1.5 s).
  const auto tx = r.transmit();  // includes 0.4 s after the release
  r.pass(2100);
  const std::size_t before = pilotStarts().size();
  r.text(tx, r.taxi());
  r.pass(60);
  ASSERT_EQ(pilotStarts().size(), before + 1);  // at once, no further delay
  const Ns since = pilotStarts().back().sinceRelease;
  EXPECT_GE(since, ms(2500));
  EXPECT_LT(since, ms(2700));
  r.hearPilot();
  // A quick result: the pilot still waits for the configured delay, counted from the release.
  const auto tx2 = r.transmit();
  r.text(tx2, r.lineUp());
  r.hearPilot();
  const Ns since2 = pilotStarts().back().sinceRelease;
  EXPECT_GE(since2, ms(500));
  EXPECT_LT(since2, ms(1600));
  const json j = json::parse(eventJson(EngineEvent{1, 0, EngineInput(pilotStarts().back())}));
  EXPECT_EQ(j["sinceRelease"].get<Ns>(), since2);  // in the log
}

TEST(ExerciseRuntime, StartIsRefusedWithAReason) {
  Rig r;
  r.model.allow = false;
  EXPECT_NE(r.start(), "");
  r.model.allow = true;
  r.model.st.modelId = "vosk-grammar";
  EXPECT_NE(r.start().find("vosk-grammar"), std::string::npos);
  EXPECT_FALSE(r.model.st.locked);
  r.model.st.modelId = r.opt.model;
  r.log->refuse = true;
  EXPECT_NE(r.start().find("disk"), std::string::npos);
  EXPECT_FALSE(r.model.st.locked);
  r.log->refuse = false;
  r.opt.mode = ExerciseMode::Assessed;  // placeholder text (P-11)
  EXPECT_NE(r.start(), "");
  r.opt.mode = ExerciseMode::Practice;
  r.opt.traineeId = "Not A Code";
  EXPECT_NE(r.start(), "");
  r.opt.traineeId.clear();
  EXPECT_NE(r.run->start({}, r.opt, std::make_unique<MemoryLog>(r.log)), "");
  EXPECT_NE(r.run->start(r.ready, r.opt, nullptr), "");
  EXPECT_FALSE(r.model.st.locked);
  // The radio is in use: reported afterwards, and the model is free again.
  r.down();
  EXPECT_EQ(r.start(), "");
  ASSERT_EQ(r.refusals.size(), 1u);
  EXPECT_FALSE(r.model.st.locked);
  EXPECT_EQ(r.run->phase(), ExercisePhase::Idle);
}

TEST(ExerciseRuntime, OneExerciseAtATime) {
  Rig r;
  ASSERT_EQ(r.start(), "");
  EXPECT_NE(r.start(), "");
  EXPECT_TRUE(r.model.st.locked);
  EXPECT_EQ(r.startedOk, 1);
}

TEST(ExerciseRuntime, SecondExerciseStartsClean) {
  Rig r;
  ASSERT_EQ(r.start(), "");
  r.hearPilot();
  r.say(r.taxi());
  r.run->stop();
  r.core->step();
  r.log = std::make_shared<MemoryLog::Store>();
  r.result.reset();
  ASSERT_EQ(r.start(), "");
  EXPECT_EQ(r.startedOk, 2);
  EXPECT_EQ(r.log->events.front().seq, 1u);
  r.hearPilot();
  r.say(r.taxi());
  EXPECT_EQ(r.outcomes(), std::vector<Outcome>{Outcome::Correct});
}

// D-116, D-311
TEST(ExerciseRuntime, MissingPttBlocksUnlessTheFallbackIsAllowed) {
  Rig r(false);
  EXPECT_EQ(r.start(), "");
  ASSERT_EQ(r.refusals.size(), 1u);
  EXPECT_NE(r.refusals[0].find("PTT"), std::string::npos);
  EXPECT_FALSE(r.model.st.locked);
  r.opt.pttFallback = true;
  r.log = std::make_shared<MemoryLog::Store>();
  ASSERT_EQ(r.start(), "");
  ASSERT_EQ(r.startedOk, 1);
  r.hearPilot();
  r.core->press("SCREEN"); r.core->step();
  r.pass(1500);
  r.core->release("SCREEN"); r.core->step();
  r.pass(400);
  r.text(r.speech.finished.back(), r.taxi());
  const auto used = r.logged<in::FallbackPttUsed>();
  ASSERT_EQ(used.size(), 1u);
  EXPECT_EQ(used[0].source, "SCREEN");
  EXPECT_EQ(used[0].tx, r.logged<in::TxOpened>()[0].tx);
  r.run->stop();
  r.core->step();
  EXPECT_TRUE(r.result->fallbackPttUsed());
  EXPECT_EQ(r.result->ptt.fallback, 1);
}

TEST(ExerciseRuntime, ScreenPttBesideAWorkingPedalIsNoFallback) {
  Rig r;
  ASSERT_EQ(r.start(), "");
  r.hearPilot();
  r.core->press("SPACE"); r.core->step();
  r.pass(1500);
  r.core->release("SPACE"); r.core->step();
  r.pass(400);
  r.text(r.speech.finished.back(), r.taxi());
  EXPECT_TRUE(r.logged<in::FallbackPttUsed>().empty());
}

TEST(ExerciseRuntime, LostPttPausesUntilSeenReleasedOrFallback) {
  Rig r;
  ASSERT_EQ(r.start(), "");
  r.hearPilot();
  r.ptt.lost(r.now);
  r.core->step();
  ASSERT_EQ(r.logged<in::PttPresence>().size(), 1u);
  r.pass(120'000);
  EXPECT_TRUE(r.outcomes().empty());  // nothing times out
  r.ptt.opened(true);                 // back, but held: not yet
  r.core->step();
  EXPECT_EQ(r.logged<in::PttPresence>().size(), 1u);
  r.ptt.edge(r.now, false);
  r.core->step();
  ASSERT_EQ(r.logged<in::PttPresence>().size(), 2u);
  EXPECT_TRUE(r.logged<in::PttPresence>()[1].present);
  r.ptt.lost(r.now);
  r.core->step();
  r.run->useFallback();
  r.core->step();
  EXPECT_TRUE(r.logged<in::PttPresence>().back().present);
  r.hearPilot(40'000);  // time runs again
}

TEST(ExerciseRuntime, LostPttDuringATransmissionIsNotScored) {
  Rig r;
  ASSERT_EQ(r.start(), "");
  r.hearPilot();
  r.down();
  r.pass(1000);
  r.ptt.lost(r.now);
  r.core->step();
  r.pass(500);
  ASSERT_EQ(r.logged<in::TxClosed>().size(), 1u);
  EXPECT_EQ(r.logged<in::TxClosed>()[0].end, TxEnd::DeviceLost);
  EXPECT_EQ(r.logged<in::Recognised>().size(), 1u);
  EXPECT_NE(r.snap.state, S::Recognising);
}

TEST(ExerciseRuntime, PressRulesComeFromTheScenarioAndAreLogged) {
  for (const auto rule : {PressRule::Block, PressRule::Queue}) {
    Rig r;
    auto e = std::make_shared<Exercise>(*r.ready.exercise);
    e->scenario.pressDuringPilot = rule;
    r.ready.exercise = e;
    ASSERT_EQ(r.start(), "");
    for (int t = 0; t < 5000 && !r.sounding; t += 10) r.pass(10);
    r.pass(50);
    r.down();
    EXPECT_TRUE(r.sounding);  // not interrupted
    const auto p = r.logged<in::PressNotOpened>();
    ASSERT_EQ(p.size(), 1u);
    EXPECT_EQ(p[0].result, rule == PressRule::Block ? in::PressResult::Blocked : in::PressResult::Queued);
    if (rule == PressRule::Queue) {
      for (int t = 0; t < 5000 && r.sounding; t += 10) r.pass(10);
      r.pass(100);
      ASSERT_EQ(r.logged<in::TxOpened>().size(), 1u);
      EXPECT_TRUE(r.logged<in::TxOpened>()[0].fromQueue);
    } else {
      r.up();
    }
  }
}

TEST(ExerciseRuntime, AQueuedPressLetGoIsLogged) {
  Rig r;
  auto e = std::make_shared<Exercise>(*r.ready.exercise);
  e->scenario.pressDuringPilot = PressRule::Queue;
  r.ready.exercise = e;
  ASSERT_EQ(r.start(), "");
  for (int t = 0; t < 5000 && !r.sounding; t += 10) r.pass(10);
  r.pass(50);
  r.down(); r.pass(50); r.up(); r.pass(50);
  ASSERT_EQ(r.logged<in::PressNotOpened>().size(), 2u);
  EXPECT_EQ(r.logged<in::PressNotOpened>()[1].result, in::PressResult::QueueDropped);
}

TEST(ExerciseRuntime, SpeakerFailureIsAFaultAndTheExerciseGoesOn) {
  Rig r;
  r.playOk = false;
  ASSERT_EQ(r.start(), "");
  r.pass(5000);
  EXPECT_FALSE(r.faults.empty());
  EXPECT_FALSE(r.logged<in::PilotEnded>().empty());
  EXPECT_EQ(r.logged<in::PilotEnded>()[0].end, in::PilotEnd::TimedOut);
  r.playOk = true;
  r.say(r.taxi());
  EXPECT_EQ(r.outcomes(), std::vector<Outcome>{Outcome::Correct});
  r.hearPilot();
}

TEST(ExerciseRuntime, LostLogEntriesAreAFaultOnce) {
  Rig r;
  ASSERT_EQ(r.start(), "");
  r.log->lost = 3;
  r.hearPilot();
  r.say(r.taxi());
  r.hearPilot();
  EXPECT_EQ(std::count_if(r.faults.begin(), r.faults.end(), [](const auto& f) { return f.find("log") != std::string::npos; }), 1);
}

TEST(ExerciseRuntime, PilotSoundHasTheRadioEffectAndItsWholeLength) {
  std::size_t lengths[2];
  int k = 0;
  for (const auto i : {RadioIntensity::Off, RadioIntensity::Light}) {
    Rig r;
    r.opt.radio = i;
    ASSERT_EQ(r.start(), "");
    r.hearPilot();
    lengths[k++] = r.soundSamples;
  }
  const auto c = radioEffect(RadioIntensity::Light);
  EXPECT_EQ(lengths[1], lengths[0] + std::size_t(16 * (c.clickMs + c.tailMs)));
}

// Random pedal, text, faults and stops: the radio is never both ways, every transmission is answered, and the log
// stays replayable.
TEST(ExerciseRuntime, RandomExercisesKeepTheRules) {
  for (unsigned seed = 1; seed <= 25; ++seed) {
    std::mt19937 rng(seed);
    const auto one = [&](unsigned n) { return rng() % n == 0; };
    RuntimeConfig rc;
    rc.recogniseLimit = ms(4000);
    Rig r(true, rc);
    auto e = std::make_shared<Exercise>(*r.ready.exercise);
    e->scenario.pressDuringPilot = PressRule(rng() % 3);
    r.ready.exercise = e;
    r.opt.seed = seed;
    r.opt.pttFallback = true;
    ASSERT_EQ(r.start(), "");
    bool held = false, gone = false;
    std::size_t answered = 0;
    for (int i = 0; i < 400; ++i) {
      r.pass(int(rng() % 40) * 10);
      if (gone ? one(5) : one(60)) {
        gone = !gone;
        held = false;
        if (gone) r.ptt.lost(r.now);
        else r.ptt.opened(false);
      } else if (!gone && one(3)) {
        held = !held;
        r.ptt.edge(r.now, held);
      }
      r.core->step();
      while (answered < r.speech.finished.size()) {
        const auto tx = r.speech.finished[answered++];
        if (one(6)) continue;  // no answer ever
        const std::string words[] = {r.taxi(), r.lineUp(), r.takeoff(), "", "mumble", r.taxi(r.otherRunway())};
        SttResult s;
        s.text = words[rng() % 6];
        r.run->recognised(tx, "m", "c", s, ms(100));
      }
      r.playOk = !one(10);
      if (one(200)) r.run->stop();
      if (one(30)) r.run->resume();
      if (one(30)) r.run->useFallback();
    }
    if (!gone && held) r.ptt.edge(r.now, false);
    r.pass(6000);
    r.run->stop();
    r.pass(40'000);
    EXPECT_EQ(r.snap.state, S::Idle) << "seed " << seed;
    EXPECT_TRUE(r.result) << "seed " << seed;
    EXPECT_FALSE(r.model.st.locked);
    const auto closed = r.logged<in::TxClosed>();
    const auto rec = r.logged<in::Recognised>();
    EXPECT_LE(rec.size(), closed.size()) << "seed " << seed;
    std::set<std::uint64_t> seen;
    for (const auto& x : rec) EXPECT_TRUE(seen.insert(x.instruction.tx).second) << "seed " << seed;
    for (const auto& j : r.logged<out::Judged>()) EXPECT_EQ(j.advanced, j.outcome == Outcome::Correct);
    SequenceEngine again(r.ready.exercise, seed);
    std::vector<std::string> a, b;
    for (const auto& ev : r.log->events) {
      if (!isInput(ev)) {
        a.push_back(eventJson({1, ev.t, ev.body}));
        continue;
      }
      for (const auto& o : again.handle(ev.t, std::get<EngineInput>(ev.body))) b.push_back(eventJson({1, ev.t, o}));
    }
    EXPECT_EQ(a, b) << "seed " << seed;
    if (HasFailure()) break;
  }
}

// ---- coverage check (D-115)

TEST(ExerciseCheck, MissingClipIsNamedAndBlocks) {
  TempDir d;
  Exercise e;
  std::vector<LoadError> errors;
  ASSERT_TRUE(loadExercise(scenarioSources(paths().data, scenarioFile()), e, errors));
  std::map<std::string, RequiredClip> need;
  std::vector<std::string> bad;
  requiredClips(e, need, bad);
  for (const auto& [id, c] : need) {  // any one of them
    for (const auto& f : fs::directory_iterator(d.path)) fs::remove(f);
    makeBank(d.path, id);
    const auto r = ExerciseCheck::check(paths().data, d.path.string(), scenarioFile());
    EXPECT_EQ(r.state, CheckResult::State::MissingClips) << id;
    ASSERT_EQ(r.missing.size(), 1u);
    EXPECT_NE(r.missing[0].find(id), std::string::npos);
    EXPECT_FALSE(r.ready);
  }
}

TEST(ExerciseCheck, BrokenScenarioOrBankFails) {
  TempDir d;
  auto r = ExerciseCheck::check(paths().data, d.path.string(), scenarioFile());  // no manifest
  EXPECT_EQ(r.state, CheckResult::State::Failed);
  EXPECT_FALSE(r.errors.empty());
  makeBank(d.path);
  r = ExerciseCheck::check(paths().data, d.path.string(), paths().data + "/scenarios/NONE.json");
  EXPECT_EQ(r.state, CheckResult::State::Failed);
  EXPECT_FALSE(r.errors.empty());
  EXPECT_FALSE(r.ready);
  EXPECT_EQ(ExerciseCheck::check(paths().data, d.path.string(), scenarioFile()).state, CheckResult::State::Covered);
}

TEST(ExerciseCheck, NewestSelectionWins) {
  TempDir d;
  makeBank(d.path);
  std::mutex m;
  std::vector<CheckResult> seen;
  const std::string none = paths().data + "/scenarios/NONE.json";
  {
    ExerciseCheck c(paths().data, d.path.string(), [&](const CheckResult& r) { std::lock_guard l(m); seen.push_back(r); });
    for (int i = 0; i < 20; ++i) c.select(i % 2 ? scenarioFile() : none);  // ends with the real one
    for (int t = 0; t < 500; ++t) {
      {
        std::lock_guard l(m);
        if (seen.back().state != CheckResult::State::Checking) break;
      }
      usleep(10'000);
    }
  }
  ASSERT_FALSE(seen.empty());
  EXPECT_EQ(seen.back().state, CheckResult::State::Covered);
  EXPECT_EQ(seen.back().scenarioFile, scenarioFile());
  // A result never belongs to anything but the selection before it.
  std::string selected;
  for (const auto& s : seen) {
    if (s.state == CheckResult::State::Checking) selected = s.scenarioFile;
    else EXPECT_EQ(s.scenarioFile, selected);
  }
}

// ---- log file (D-401)

namespace {
EventLogHeader header(const std::string& session = "s1") {
  EventLogHeader h;
  h.sessionId = session;
  h.scenarioId = "X";
  h.scenarioHash = "h";
  h.modelId = "m";
  h.modelConfig = "{}";
  return h;
}
std::vector<std::string> lines(const std::string& file) {
  std::ifstream in(file);
  std::vector<std::string> v;
  for (std::string l; std::getline(in, l);) v.push_back(l);
  return v;
}
}  // namespace

TEST(JsonlEventLog, WritesOneLinePerEntryAndOnlyForItsOwner) {
  TempDir d;
  std::string path;
  {
    JsonlEventLog log((d.path / "logs").string());
    std::string error;
    ASSERT_TRUE(log.begin(header(), error)) << error;
    for (std::uint64_t i = 1; i <= 500; ++i) log.append({i, Ns(i), EngineInput(in::FallbackPttUsed{i, "SCREEN"})});
    log.end();
    log.append({501, 0, EngineInput(in::Started{})});  // after the end: not written, counted
    EXPECT_EQ(log.lost(), 1u);
    path = log.path();
  }
  const auto l = lines(path);
  ASSERT_EQ(l.size(), 501u);
  EXPECT_EQ(l[0], headerJson(header()));
  for (const auto& x : l) EXPECT_FALSE(json::parse(x, nullptr, false).is_discarded());
  EXPECT_NE(l[500].find("fallback_ptt_used"), std::string::npos);
  EXPECT_NE(l[500].find("SCREEN"), std::string::npos);
  struct stat st {};
  ASSERT_EQ(stat(path.c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0600u);
}

TEST(JsonlEventLog, RefusesBadHeadersAndNeverOverwrites) {
  TempDir d;
  std::string error;
  for (const char* id : {"", "../x", "a/b", "a b"}) EXPECT_FALSE(JsonlEventLog(d.path.string()).begin(header(id), error)) << id;
  auto h = header();
  h.modelConfig.clear();
  EXPECT_FALSE(JsonlEventLog(d.path.string()).begin(h, error));
  std::ofstream(d.path / "s1.jsonl") << "old";
  EXPECT_FALSE(JsonlEventLog(d.path.string()).begin(header(), error));
  EXPECT_FALSE(error.empty());
  EXPECT_EQ(lines((d.path / "s1.jsonl").string()), std::vector<std::string>{"old"});
  EXPECT_FALSE(JsonlEventLog("/proc/none").begin(header(), error));
}

TEST(JsonlEventLog, FullQueueIsCountedNotWaitedFor) {
  TempDir d;
  JsonlEventLog log(d.path.string(), 0);
  std::string error;
  ASSERT_TRUE(log.begin(header(), error));
  log.append({1, 0, EngineInput(in::Started{})});
  EXPECT_EQ(log.lost(), 1u);
}

// ---- the whole chain with its real threads: RadioCore's, SttService's and a device thread; only the devices and
// the recogniser's words are simulated.
namespace {
struct Scripted : ISpeechRecognizer {
  explicit Scripted(std::shared_ptr<std::vector<std::string>> w) : words(std::move(w)) {}
  std::string name() const override { return "scripted"; }
  void begin() override {}
  void accept(std::span<const std::int16_t>) override {}
  SttResult finish() override {
    SttResult r;
    if (!words->empty()) {
      r.text = words->front();
      words->erase(words->begin());
    }
    return r;
  }
  std::shared_ptr<std::vector<std::string>> words;
};
struct Feed : ISpeechFeed {
  SttService* stt = nullptr;
  bool begin(std::uint64_t tx) override { return stt->begin(tx); }
  void accept(std::vector<std::int16_t> a) override { stt->accept(std::move(a)); }
  void finish(Ns t) override { stt->finish(t); }
  void cancel() override { stt->cancel(); }
};
}  // namespace

TEST(ExerciseRuntimeThreads, DepartureWithRealThreadsLockedModelAndLogFile) {
  TempDir d;
  makeBank(d.path);
  const auto ready = ExerciseCheck::check(paths().data, d.path.string(), scenarioFile()).ready;
  ASSERT_TRUE(ready);
  ParamValues p;
  ASSERT_TRUE(generateParams(ready.exercise->scenario, ready.exercise->airfield, 7, p));
  std::string call;
  for (const auto& c : ready.exercise->airfield.callsigns)
    if (c.id == p["callsign"]) call = c.spoken;
  const std::string rwy = p["runway"] == "09" ? "zero nine" : "two seven", twy = p["taxiway"] == "A3" ? "alpha three" : "bravo two";
  auto words = std::make_shared<std::vector<std::string>>(std::vector<std::string>{
      call + " taxi via " + twy + " runway " + rwy, call + " line up runway " + rwy, call + " runway " + rwy + " cleared for takeoff"});

  ExerciseRuntime* runtime = nullptr;
  std::atomic<int> labResults{0}, faults{0}, ended{0};
  std::vector<SttModel> models(2);
  models[0].id = "whisper-tiny-genprompt";
  models[1].id = "other";
  for (auto& m : models) m.installed = true;
  SttService::Callbacks scb;
  scb.result = [&](std::uint64_t tx, const std::string& id, const std::string& config, const SttResult& r, Ns t) {
    if (!runtime || !runtime->recognised(tx, id, config, r, t)) ++labResults;
  };
  SttService stt(models, [&](const SttModel&, std::string&) { return std::make_unique<Scripted>(words); }, [](const SttModel& m) { return "{\"id\":\"" + m.id + "\"}"; }, scb);
  SpeechModel model(stt);
  Feed feed;
  feed.stt = &stt;
  ASSERT_TRUE(stt.load("whisper-tiny-genprompt"));
  for (int i = 0; i < 300 && stt.status().state != SttState::Ready; ++i) usleep(10'000);

  ParserRules rules;
  std::string error;
  ASSERT_TRUE(loadParserRules(paths().data + "/phraseology/placeholder.json", rules, error));
  std::mutex m;
  std::optional<ExerciseResult> result;
  ExerciseRuntime::Callbacks cb;
  cb.fault = [&](const std::string&) { ++faults; };
  cb.ended = [&](const ExerciseResult& r) { std::lock_guard l(m); result = r; ++ended; };
  RuntimeConfig rc;
  rc.engine.replyDelayMin = rc.engine.replyDelayMax = ms(100);
  ExerciseRuntime run(rc, rules, model, cb);
  runtime = &run;

  std::atomic<bool> quit{false};
  std::atomic<PlayId> playing{0};
  std::atomic<int> heard{0}, bothWays{0};
  std::atomic<bool> txOpen{false};
  PrerollRing* ring = nullptr;
  LevelMeter* meter = nullptr;
  PlayerCallbacks out;
  PttCallbacks ptt;
  struct Player : IRadioPlayer {
    std::atomic<PlayId>& playing;
    std::atomic<bool>& txOpen;
    std::atomic<int>& bothWays;
    Player(std::atomic<PlayId>& p, std::atomic<bool>& t, std::atomic<int>& b) : playing(p), txOpen(t), bothWays(b) {}
    bool start() override { return true; }
    bool play(PlayId id, std::vector<std::int16_t>, int) override { bothWays += txOpen; playing = id; return true; }
    bool stop() override { playing = 0; return true; }
  };
  RadioDevices dev;
  dev.mic = [&](PrerollRing& r, LevelMeter& l, std::function<void(const CaptureStatus&)>) { ring = &r; meter = &l; return std::make_unique<FakeMic>(); };
  dev.player = [&](PlayerCallbacks c) { out = std::move(c); return std::make_unique<Player>(playing, txOpen, bothWays); };
  dev.ptt = [&](PttCallbacks c) { ptt = std::move(c); return std::make_unique<FakePtt>(); };
  dev.speech = &feed;
  RadioCore::Events lab;
  lab.snapshot = [&](const RadioSnapshot& s) { txOpen = s.state == S::TxOpen; };
  RadioCore core(RadioConfig{}, std::move(dev), run.wrap(std::move(lab)));
  run.attach(core);
  ASSERT_TRUE(core.start());
  ptt.opened(false);
  std::thread devices([&] {  // speech into the microphone, and each reply heard for 300 ms
    Ns t0 = monotonicNow();
    for (int k = 0; !quit; ++k) {
      std::vector<std::int16_t> b(160);
      for (int i = 0; i < 160; ++i) b[std::size_t(i)] = std::int16_t(4000 * std::sin(i * 0.4));
      ring->push(monotonicNow(), b);
      meter->add(b);
      if (k % 30 == 0)
        if (const auto id = playing.exchange(0)) {
          ++heard;
          out.finished(id, true);
        }
      t0 += ms(10);
      const Ns wait = t0 - monotonicNow();
      if (wait > 0) usleep(useconds_t(wait / 1000));
    }
  });
  const auto until = [](int millis, auto f) {
    for (int t = 0; t < millis && !f(); t += 10) usleep(10'000);
    return f();
  };
  usleep(500'000);
  ExerciseOptions o;
  o.sessionId = "threads";
  o.model = "whisper-tiny-genprompt";
  o.seed = 7;
  auto log = std::make_unique<JsonlEventLog>((d.path / "logs").string());
  ASSERT_EQ(run.start(ready, o, std::move(log)), "");
  for (int turn = 1; turn <= 3; ++turn) {
    EXPECT_TRUE(until(12'000, [&] { return heard >= turn; })) << turn;
    usleep(100'000);
    EXPECT_FALSE(stt.load("other"));  // locked (D-39)
    EXPECT_FALSE(stt.unload());
    ptt.edge(monotonicNow(), true);
    usleep(700'000);
    ptt.edge(monotonicNow(), false);
  }
  EXPECT_TRUE(until(12'000, [&] { return ended > 0; }));
  quit = true;
  devices.join();
  core.stop();
  ASSERT_TRUE(result);
  EXPECT_EQ(result->end, out::EndReason::Completed);
  EXPECT_EQ(result->ptt.transmissions, 3);
  EXPECT_EQ(faults, 0);
  EXPECT_EQ(labResults, 0);
  EXPECT_EQ(bothWays, 0);
  EXPECT_TRUE(stt.load("other"));  // unlocked again
  usleep(200'000);
  const auto l = lines((d.path / "logs" / "threads.jsonl").string());
  ASSERT_GT(l.size(), 20u);
  EXPECT_NE(l[0].find("whisper-tiny-genprompt"), std::string::npos);
  EXPECT_NE(l.back().find("\"ended\""), std::string::npos) << l.back();
}
