// The live exercise (3b): the real scenario and engine in simulated time, without a radio.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <memory>

#include "app/LiveExercise.h"
#include "core/PathResolver.h"
#include "engine/Simulator.h"
using namespace atc;
using C = ExerciseController;

namespace {
std::shared_ptr<const Exercise> departure() {
  Paths p;
  std::string error;
  EXPECT_TRUE(resolvePaths(systemPathInputs(), p, error)) << error;
  auto e = std::make_shared<Exercise>();
  std::vector<LoadError> errors;
  EXPECT_TRUE(loadExercise(scenarioSources(p.data, p.data + "/scenarios/DEPARTURE-01.json"), *e, errors));
  return e;
}

struct MemoryLog : IEventLog {
  std::vector<EngineEvent> events;
  std::function<void(const EngineEvent&)> seen;
  bool begin(const EventLogHeader&, std::string&) override { return true; }
  void append(const EngineEvent& e) override {
    events.push_back(e);
    if (seen) seen(e);
  }
  void end() override {}
  std::uint64_t lost() const override { return 0; }
};

// Every event of the Simulator goes to the view, in order, as ExerciseRuntime reports them.
struct View : testing::Test {
  std::shared_ptr<const Exercise> ex = departure();
  C c;
  ExerciseView v{&c};
  MemoryLog log;
  std::unique_ptr<Simulator> sim;
  ParamValues p;
  static constexpr std::uint64_t seed = 7;

  void SetUp() override {
    ASSERT_TRUE(generateParams(ex->scenario, ex->airfield, seed, p));
    v.prepare({.exercise = ex, .seed = seed, .model = "Whisper tiny + generated prompt", .consent = true});
    EXPECT_EQ(c.phase(), C::Starting);
  }
  void go(bool assessed = false) {
    v.started(assessed, QDateTime(QDate(2026, 11, 25), QTime(10, 41, 0)));
    log.seen = [this](const EngineEvent& e) {
      if (const auto* i = std::get_if<EngineInput>(&e.body)) v.input(e.t, *i);
      else v.output(e.t, std::get<EngineOutput>(e.body));
    };
    sim = std::make_unique<Simulator>(SequenceEngine(ex, seed), log);
    sim->start();
    sim->wait(ms(8000));  // the pilot's first call
  }
  ParsedInstruction right(const std::string& step) {
    std::map<std::string, std::string> f{{"callsign", p["callsign"]}, {"runway", p["runway"]}};
    if (step == "TAXI") f["taxiway"] = p["taxiway"];
    return Simulator::instruction(step == "TAXI" ? "TAXI_INSTRUCTION" : step == "LINE_UP" ? "LINE_UP" : "TAKEOFF_CLEARANCE", f);
  }
  void say(ParsedInstruction i) {
    sim->transmit(std::move(i));
    sim->waitForPilot();
  }
  void finish() { v.ended(buildResult(ex->scenario, log.events), QDateTime(QDate(2026, 11, 25), QTime(10, 44, 0))); }
  QVariant cell(QAbstractItemModel* m, int row, int col) const { return m->data(m->index(row, 0), Qt::UserRole + col); }
  QString otherRunway() const {
    for (const auto& r : ex->airfield.runways)
      if (r != p.at("runway")) return QString::fromStdString(r);
    return "36";
  }
};
}  // namespace

TEST_F(View, CardAndContextShowTheValuesOfTheSeed) {
  const QVariantMap card = ExerciseView::card(*ex, seed);
  EXPECT_EQ(card["callsign"].toString().toStdString(), p["callsign"]);
  EXPECT_EQ(card["runway"].toString().toStdString(), p["runway"]);
  EXPECT_EQ(card["steps"].toInt(), 3);
  EXPECT_TRUE(card["placeholder"].toBool());
  EXPECT_EQ(c.context()["callsign"].toString(), card["callsign"].toString());
  EXPECT_EQ(c.context()["taxiway"].toString(), card["taxiway"].toString());
}

TEST_F(View, ACorrectDeparture) {
  go();
  EXPECT_EQ(c.phase(), C::Running);
  EXPECT_EQ(c.transcript()->rowCount(), 1);  // the pilot's call
  EXPECT_EQ(cell(c.transcript(), 0, 0).toInt(), int(C::Pilot));
  for (const char* s : {"TAXI", "LINE_UP", "TAKEOFF"}) say(right(s));
  EXPECT_TRUE(sim->ended());
  EXPECT_EQ(c.progressText(), "3 of 3 done");
  EXPECT_EQ(c.clearances().size(), 3);
  EXPECT_TRUE(c.clearances()[0].contains(QString::fromStdString("RWY " + p["runway"])));
  finish();
  EXPECT_EQ(c.phase(), C::Complete);
  v.showResult();
  EXPECT_EQ(c.phase(), C::Result);
  const QVariantMap r = c.result();
  EXPECT_EQ(r["title"].toString(), "COMPLETED");
  EXPECT_EQ(r["stepsDone"].toString(), "3 of 3");
  EXPECT_EQ(r["suspectedTitle"].toString(), "None");
  for (const QVariant& s : r["steps"].toList()) EXPECT_EQ(s.toMap()["errors"].toString(), "none");
  EXPECT_EQ(r["ptt"].toList().last().toMap()["value"].toString(), "no");  // fallback PTT
  int ok = 0;
  for (int i = 0; i < c.timeline()->rowCount(); ++i) ok += cell(c.timeline(), i, 3).toString() == "CORRECT";
  EXPECT_EQ(ok, 3);
}

TEST_F(View, AWrongRunwayIsShownAsSaidAndNotAdvanced) {
  go();
  auto i = right("TAXI");
  i.fields["runway"].value = otherRunway().toStdString();
  say(i);
  EXPECT_EQ(c.progressText(), "0 of 3 done");
  EXPECT_TRUE(c.notice().startsWith("Step not advanced")) << c.notice().toStdString();
  EXPECT_TRUE(c.alert().contains("attempt 1"));
  EXPECT_TRUE(c.clearances().isEmpty());
  bool said = false;
  for (const QVariant& ch : cell(c.transcript(), 1, 4).toList()) said = said || ch.toMap()["value"].toString() == "RWY " + otherRunway();
  EXPECT_TRUE(said);
  say(right("TAXI"));
  EXPECT_EQ(c.progressText(), "1 of 3 done");
}

TEST_F(View, AssessedHidesTheVerdictOfTheRealEngine) {
  go(true);
  auto i = right("TAXI");
  i.fields["runway"].value = otherRunway().toStdString();
  say(i);
  EXPECT_TRUE(c.notice().isEmpty());
  EXPECT_FALSE(c.alert().contains("attempt"));
  say(right("TAXI"));
  EXPECT_TRUE(c.clearances().isEmpty());
  EXPECT_FALSE(c.progressText().contains("of 3"));
}

TEST_F(View, AnUncertainValueIsNotCountedAsAnError) {
  go();
  auto i = right("TAXI");
  i.cls = RecognitionClass::Ambiguous;
  i.fields["runway"] = {"", "0.9", true, -1};
  i.rawText = "eagle taxi runway 0.9";
  say(i);
  bool shown = false;
  for (int row = 0; row < c.transcript()->rowCount(); ++row)
    for (const QVariant& ch : cell(c.transcript(), row, 4).toList())
      shown = shown || (ch.toMap()["uncertain"].toBool() && ch.toMap()["value"].toString() == "RWY \"0.9\" ?");
  EXPECT_TRUE(shown);
  EXPECT_TRUE(c.notice().contains("Not counted as your error"));
  sim->stop();
  finish();
  EXPECT_EQ(c.phase(), C::Result);  // a stopped exercise goes to its result
  EXPECT_EQ(c.result()["title"].toString(), "STOPPED BY INSTRUCTOR");
  EXPECT_TRUE(c.result()["suspectedTitle"].toString().startsWith("1, not counted"));
  EXPECT_EQ(c.result()["steps"].toList()[0].toMap()["errors"].toString(), "none");
  EXPECT_EQ(c.result()["steps"].toList()[0].toMap()["sayAgain"].toString(), "1");
}

TEST_F(View, MissedCallsAskTheInstructorAndContinueCallsAgain) {  // D-125
  go();
  for (int n = 0; n < 12 && !c.choice(); ++n) sim->wait(ms(15'000));
  ASSERT_TRUE(c.choice());
  EXPECT_EQ(c.notice(), "Instructor notified");
  EXPECT_EQ(c.choiceText(), "Taxi: 2 pilot calls without an ATC call.");
  EXPECT_EQ(c.missedText(), "none");
  EXPECT_EQ(c.waitingText(), "-");
  const int rows = c.transcript()->rowCount();
  sim->wait(ms(60'000));  // never moves on by itself
  EXPECT_TRUE(c.choice());
  EXPECT_EQ(c.transcript()->rowCount(), rows);
  sim->resume();
  EXPECT_FALSE(c.choice());
  sim->wait(ms(40'000));
  EXPECT_GT(c.transcript()->rowCount(), rows);
  EXPECT_TRUE(c.notice().contains("1 of 2")) << c.notice().toStdString();
  for (int n = 0; n < 12 && !c.choice(); ++n) sim->wait(ms(15'000));
  ASSERT_TRUE(c.choice());
  say(right("TAXI"));  // a transmission ends the waiting too
  EXPECT_FALSE(c.choice());
  EXPECT_EQ(c.progressText(), "1 of 3 done");
}

TEST_F(View, WaitingCountsFromTheQuietChannel) {
  go();
  EXPECT_EQ(c.waitingText(), "00:00");
  Ns quiet = 0;
  for (const auto& e : log.events)
    if (const auto* o = std::get_if<EngineOutput>(&e.body))
      if (std::holds_alternative<out::TimerSet>(*o)) quiet = e.t;
  v.tick(quiet + ms(14'200));
  EXPECT_EQ(c.waitingText(), "00:14");
  sim->transmit(right("TAXI"));
  EXPECT_EQ(c.waitingText(), "-");
}

TEST_F(View, LostPttPausesAndAnActiveChoiceComesBack) {
  go();
  for (int n = 0; n < 12 && !c.choice(); ++n) sim->wait(ms(15'000));
  ASSERT_TRUE(c.choice());
  v.input(sim->now(), in::PttPresence{false});
  EXPECT_EQ(c.phase(), C::Paused);
  EXPECT_EQ(c.alert(), "PTT device lost: exercise paused");
  EXPECT_FALSE(c.choice());
  v.input(sim->now(), in::PttPresence{true});
  EXPECT_TRUE(c.choice());
  EXPECT_EQ(c.alert(), "Instructor notified");
}

TEST_F(View, ARecogniserFaultIsASystemFaultCountedOnce) {
  go();
  v.fault("no text from the speech model for transmission 1");  // the runtime reports it, then the input
  in::Recognised r;
  r.instruction.tx = 1;
  r.sttFault = "no result in time";
  v.input(sim->now(), r);
  EXPECT_EQ(c.noticeLevel(), int(LabController::Fault));
  out::Judged j;
  j.step = "TAXI";
  j.tx = 1;
  j.suspectedStt = true;
  v.output(sim->now(), j);
  EXPECT_TRUE(c.notice().contains("Not your error"));
  EXPECT_FALSE(c.notice().contains("Step not advanced"));
  sim->stop();
  finish();
  EXPECT_EQ(c.result()["speech"].toList()[2].toMap()["value"].toString(), "1");
}

TEST_F(View, AKeyedOverReplyIsRepeatedAndShownAgain) {
  go();
  const int rows = c.transcript()->rowCount();
  v.input(sim->now(), in::PilotEnded{1, in::PilotEnd::Interrupted});
  EXPECT_TRUE(c.notice().contains("will be repeated"));
  v.input(sim->now(), in::PilotStarted{1});
  EXPECT_EQ(c.transcript()->rowCount(), rows + 1);
}

// ---- LiveExercise

namespace {
AccessBackend people() {
  AccessBackend b;
  b.people = [] { return std::vector<Person>{}; };
  return b;
}
struct Live : testing::Test {
  std::shared_ptr<const Exercise> ex = departure();
  LabController lab;
  SttController stt;
  AccessController access{people()};
  SettingsController settings{&access, QString()};
  StatusFeed feed;
  CheckResult::State state = CheckResult::State::Covered;
  std::unique_ptr<LiveExercise> live;
  ExerciseRuntime::Callbacks cb;
  std::vector<ExerciseOptions> starts;
  std::string refusal;
  int stops = 0, resumes = 0, fallbacks = 0;
  BankReady bank = BankReady::NotLoaded;
  QStringList lines, screens;
  QVariantMap scenario;
  bool running = false;

  void make() {
    LiveExerciseConfig cfg;
    cfg.scenarioFile = "/x/DEPARTURE-01.json";
    cfg.makeLog = [] { return std::make_unique<MemoryLog>(); };
    cfg.check = [this](const std::string& f) {
      CheckResult r;
      r.state = state;
      r.scenarioFile = f;
      if (state == CheckResult::State::Covered) r.ready = {ex, std::make_shared<VoiceBank>()};
      if (state == CheckResult::State::MissingClips) r.missing = {"RUNWAY_09@final"};
      if (state == CheckResult::State::Failed) r.errors = {{"a.json", "/steps/0", "bad"}};
      return r;
    };
    live = std::make_unique<LiveExercise>(cfg, &stt, &lab, &access, &settings);
    cb = live->callbacks();
    QObject::connect(c(), &C::screenRequested, c(), [this](const QString& s) { screens << s; });
    live->connectRadio({[this](PreparedExercise r, ExerciseOptions o, std::unique_ptr<IEventLog> l) {
                          EXPECT_TRUE(bool(r));
                          EXPECT_TRUE(l);
                          starts.push_back(o);
                          return refusal;
                        },
                        [this] { ++stops; }, [this] { ++resumes; }, [this] { ++fallbacks; }},
                       {[this](BankReady b, const QStringList& l) { bank = b; lines = l; },
                        [this](bool on) { running = on; }, [this](const QVariantMap& m) { scenario = m; }},
                       &feed);
  }
  C* c() { return live->controller(); }
  static void queued() { QCoreApplication::processEvents(); }
  void run() {
    emit feed.startExerciseRequested();
    c()->start(false);
    cb.started(true, {});
    queued();
  }
};
}  // namespace

TEST_F(Live, CoveredShowsTheScenarioAndMissingClipsAreListed) {
  make();
  EXPECT_EQ(bank, BankReady::CoverageOk);
  EXPECT_EQ(scenario["steps"].toInt(), 3);
  state = CheckResult::State::MissingClips;
  make();
  EXPECT_EQ(bank, BankReady::MissingClips);
  EXPECT_EQ(lines, QStringList{"RUNWAY_09@final"});
  emit feed.startExerciseRequested();
  EXPECT_EQ(c()->phase(), C::Idle);  // nothing to start
  state = CheckResult::State::Failed;
  make();
  EXPECT_EQ(bank, BankReady::Failed);
  ASSERT_EQ(lines.size(), 1);
  EXPECT_TRUE(lines[0].contains("a.json"));
}

TEST_F(Live, StartGoesThroughTheDialogAndRunsWithTheShownValues) {
  make();
  emit feed.startExerciseRequested();
  EXPECT_EQ(c()->phase(), C::Starting);
  EXPECT_EQ(screens.last(), "ex");
  EXPECT_EQ(c()->context()["callsign"].toString(), scenario["callsign"].toString());
  c()->start(true);  // placeholder text
  EXPECT_TRUE(starts.empty());
  c()->start(false);
  c()->start(false);  // pressed twice
  ASSERT_EQ(starts.size(), 1u);
  EXPECT_EQ(starts[0].model, "whisper-tiny-genprompt");
  EXPECT_EQ(starts[0].mode, ExerciseMode::Practice);
  EXPECT_FALSE(starts[0].pttFallback);
  EXPECT_TRUE(isIdCode(starts[0].sessionId)) << starts[0].sessionId;
  ParamValues p;
  ASSERT_TRUE(generateParams(ex->scenario, ex->airfield, starts[0].seed, p));
  EXPECT_EQ(scenario["callsign"].toString().toStdString(), p["callsign"]);
  EXPECT_FALSE(running);
  cb.started(true, {});
  EXPECT_FALSE(running);  // queued, not on the radio's thread
  queued();
  EXPECT_TRUE(running);
  EXPECT_EQ(c()->phase(), C::Running);
  EXPECT_FALSE(c()->audioSaved());
}

TEST_F(Live, ARefusalSaysWhyAndCanBeTriedAgain) {
  make();
  refusal = "the PTT is missing";
  emit feed.startExerciseRequested();
  c()->start(false);
  EXPECT_EQ(c()->phase(), C::Starting);
  EXPECT_TRUE(c()->notice().contains("the PTT is missing"));
  refusal.clear();
  c()->start(false);
  EXPECT_EQ(starts.size(), 2u);
  cb.started(false, "the radio is in use");
  queued();
  EXPECT_FALSE(running);
  EXPECT_TRUE(c()->notice().contains("the radio is in use"));
  c()->cancelStart();
  EXPECT_EQ(c()->phase(), C::Idle);
  EXPECT_EQ(screens.last(), "dash");
}

TEST_F(Live, StopContinueAndScreenPttReachTheRadio) {
  make();
  run();
  c()->stop();
  EXPECT_EQ(stops, 1);
  emit feed.stopExerciseRequested();
  EXPECT_EQ(stops, 2);
  cb.output(ms(1000), out::StepEntered{"TAXI"});
  cb.output(ms(1000), out::InstructorAlert{"TAXI: no ATC call", true});
  queued();
  ASSERT_TRUE(c()->choice());
  c()->continueExercise();
  EXPECT_EQ(resumes, 1);
  cb.input(ms(2000), in::PttPresence{false});
  queued();
  c()->useScreenPtt();
  EXPECT_EQ(fallbacks, 1);
}

TEST_F(Live, AfterTheEndTheNextExerciseHasNewValuesAndTheLabIsWaiting) {
  make();
  run();
  lab.setText("", "Listening", true, {}, false);
  const auto first = starts[0].seed;
  ExerciseResult r = buildResult(ex->scenario, {});
  r.end = out::EndReason::InstructorStop;
  cb.ended(r);
  queued();
  EXPECT_FALSE(running);
  EXPECT_EQ(lab.textState(), "Waiting");
  EXPECT_EQ(c()->phase(), C::Result);
  EXPECT_EQ(screens.last(), "ass");
  c()->runAgain();
  EXPECT_EQ(c()->phase(), C::Starting);
  c()->start(false);
  ASSERT_EQ(starts.size(), 2u);
  EXPECT_NE(starts[1].seed, first);
  EXPECT_NE(starts[1].sessionId, starts[0].sessionId);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  QCoreApplication app(argc, argv);
  return RUN_ALL_TESTS();
}
