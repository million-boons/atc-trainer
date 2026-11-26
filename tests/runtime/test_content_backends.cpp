// Scenario Selection (5d) and Assessment (5e) without a screen, and the log reader they use (D-335).
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "app/HistoryService.h"
#include "app/ScenarioService.h"
#include "app/SetupService.h"
#include "core/PathResolver.h"
#include "engine/EventJson.h"
#include "engine/EventRead.h"
#include "engine/ExerciseResult.h"
#include "engine/Simulator.h"
#include "runtime/JsonlEventLog.h"
using namespace atc;
namespace fs = std::filesystem;

namespace {
Paths paths() {
  Paths p;
  std::string error;
  EXPECT_TRUE(resolvePaths(systemPathInputs(), p, error)) << error;
  return p;
}
struct TempDir {
  fs::path path;
  TempDir() {
    static int n = 0;
    path = fs::temp_directory_path() / ("atc_content_" + std::to_string(getpid()) + "_" + std::to_string(n++));
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};
std::vector<std::string> lines(const fs::path& p) {
  std::ifstream f(p);
  std::vector<std::string> out;
  for (std::string l; std::getline(f, l);) out.push_back(l);
  return out;
}

// One departure through the real engine, written as the program writes it. trainee empty: a guest.
std::string exercise(const fs::path& dir, const std::string& session, const std::string& trainee, bool complete, bool ended = true) {
  auto e = std::make_shared<Exercise>();
  std::vector<LoadError> errors;
  EXPECT_TRUE(loadExercise(scenarioSources(paths().data, paths().data + "/scenarios/DEPARTURE-01.json"), *e, errors));
  ParamValues v;
  generateParams(e->scenario, e->airfield, 5, v);
  EventLogHeader h;
  h.sessionId = session;
  h.scenarioId = e->scenario.id;
  h.scenarioHash = e->hash;
  h.seed = 5;
  h.startedMs = 1'795'600'000'000;
  h.traineeId = trainee;
  h.modelId = "m";
  h.modelConfig = "{}";
  h.values = v;
  {
    JsonlEventLog log(dir.string());
    std::string error;
    EXPECT_TRUE(log.begin(h, error)) << error;
    Simulator sim(SequenceEngine(e, 5), log);
    sim.start();
    sim.wait(ms(1500));  // the pilot calls first (D-322)
    sim.waitForPilot();
    sim.transmit(Simulator::instruction("TAXI_INSTRUCTION", {{"callsign", v["callsign"]}, {"taxiway", v["taxiway"]}, {"runway", v["runway"] == "09" ? "27" : "09"}}));
    sim.waitForPilot();
    if (complete) {
      sim.transmit(Simulator::instruction("TAXI_INSTRUCTION", {{"callsign", v["callsign"]}, {"taxiway", v["taxiway"]}, {"runway", v["runway"]}}));
      sim.waitForPilot();
      sim.transmit(Simulator::instruction("LINE_UP", {{"callsign", v["callsign"]}, {"runway", v["runway"]}}));
      sim.waitForPilot();
      sim.transmit(Simulator::instruction("TAKEOFF_CLEARANCE", {{"callsign", v["callsign"]}, {"runway", v["runway"]}}));
      sim.waitForPilot();
    } else if (ended) {
      sim.stop();
    }
    if (ended || complete) log.end();
  }
  return session;
}

struct History {
  TempDir t;
  Role role = Role::Instructor;
  std::string person;
  std::unique_ptr<HistoryService> s;
  HistoryBackend b;
  History() {
    fs::create_directories(t.path / "exercises");
    s = std::make_unique<HistoryService>(HistoryService::Config{paths().data, (t.path / "exercises").string()},
                                         HistoryService::Rules{[this] { return std::pair{role, person}; }, [] { return std::string("2026-11-26 11:00"); }});
    b = s->backend();
  }
  std::pair<std::vector<HistoryRow>, std::string> list(const std::string& who) {
    std::pair<std::vector<HistoryRow>, std::string> r{{}, "(none)"};
    b.list(who, [&](std::vector<HistoryRow> x, const std::string& w) { r = {std::move(x), w}; });
    s->wait();
    return r;
  }
  std::pair<HistoryDetail, std::string> open(const std::string& id) {
    std::pair<HistoryDetail, std::string> r{{}, "(none)"};
    b.open(id, [&](HistoryDetail x, const std::string& w) { r = {std::move(x), w}; });
    s->wait();
    return r;
  }
  std::string ask(const std::function<void(HistoryBackend::Done)>& f) {
    std::string r = "(none)";
    f([&](const std::string& w) { r = w; });
    s->wait();
    return r;
  }
};
}  // namespace

// Every line that is read back is written again byte for byte: nothing of a log is lost on the way.
TEST(EventRead, AWholeExerciseComesBackUnchanged) {
  TempDir t;
  const std::string id = exercise(t.path, "20261126T100000Z-aaaa", "S01", true);
  const auto file = t.path / (id + ".jsonl");
  const ReadLog r = readEventLog(file.string());
  ASSERT_EQ(r.error, "");
  EXPECT_TRUE(r.ended);
  EXPECT_EQ(r.unreadable, 0);
  const auto original = lines(file);
  ASSERT_EQ(original.size(), r.events.size() + 1);
  EXPECT_EQ(headerJson(r.header), original[0]);
  std::set<std::string> kinds;
  for (std::size_t i = 0; i < r.events.size(); ++i) {
    EXPECT_EQ(eventJson(r.events[i]), original[i + 1]);
    kinds.insert(std::string(eventName(r.events[i])));
  }
  EXPECT_GE(kinds.size(), 10u);
}

TEST(EventRead, EveryKindOfEventComesBackUnchanged) {
  // The kinds that a simulated departure does not produce.
  in::TxClosed c;
  c.tx = 3;
  c.end = TxEnd::Stuck;
  c.report.keyToSpeechMs = 420;
  c.report.peakDbfs = -7.5;
  in::Recognised rec;
  rec.instruction.tx = 3;
  rec.instruction.cls = RecognitionClass::Ambiguous;
  rec.instruction.fields["runway"] = {"", "zero two 7", true, 0.37};
  rec.sttFault = "no result in time";
  const std::vector<EngineEvent> all{
      {1, 10, EngineInput(in::TxCancelled{2})}, {2, 11, EngineInput(in::PressNotOpened{in::PressResult::QueueDropped})},
      {3, 12, EngineInput(c)}, {4, 13, EngineInput(rec)}, {5, 14, EngineInput(in::PilotEnded{4, in::PilotEnd::Superseded})},
      {6, 15, EngineInput(in::PttPresence{false})}, {7, 16, EngineInput(in::FallbackPttUsed{3, "SPACE"})},
      {8, 17, EngineInput(in::InstructorContinue{})}, {9, 18, EngineOutput(out::AtcBeforePilot{3})},
      {10, 19, EngineOutput(out::NoReply{3})}, {11, 20, EngineOutput(out::InstructorAlert{"why", true})},
      {12, 21, EngineInput(in::ProgramClosed{})}, {13, 22, EngineOutput(out::Ended{out::EndReason::ProgramClosed})}};
  TempDir t;
  EventLogHeader h;
  h.sessionId = "x";
  h.scenarioId = "S";
  h.planId = "p1";
  h.planName = "Plan";
  h.planRun = 2;
  h.replyFromMs = 300;
  {
    std::ofstream f(t.path / "x.jsonl");
    f << headerJson(h) << "\n";
    for (const auto& e : all) f << eventJson(e) << "\n";
    f << "{\"event\":";  // a power cut in the last line
  }
  const ReadLog r = readEventLog((t.path / "x.jsonl").string());
  ASSERT_EQ(r.events.size(), all.size());
  EXPECT_EQ(r.unreadable, 1);
  EXPECT_EQ(headerJson(r.header), headerJson(h));
  for (std::size_t i = 0; i < all.size(); ++i) EXPECT_EQ(eventJson(r.events[i]), eventJson(all[i]));
}

TEST(HistoryServiceTest, TraineesSeeTheirOwnTheInstructorAllAndGuestsNobody) {
  History x;
  const auto dir = x.t.path / "exercises";
  exercise(dir, "20261126T100000Z-aaaa", "S01", true);
  exercise(dir, "20261126T100100Z-bbbb", "S02", false);
  exercise(dir, "20261126T100200Z-cccc", "", true);  // a guest
  auto [all, why] = x.list("");
  ASSERT_EQ(why, "");
  ASSERT_EQ(all.size(), 2u);
  EXPECT_EQ(all[0].trainee, "S02");  // newest first
  EXPECT_EQ(all[0].end, HistoryRow::End::Stopped);
  EXPECT_EQ(all[1].end, HistoryRow::End::Completed);
  EXPECT_EQ(all[1].stepsDone, 3);
  EXPECT_EQ(all[1].errors, 1);  // the wrong runway
  x.role = Role::Trainee;
  x.person = "S01";
  EXPECT_EQ(x.list("").first.size(), 1u);
  EXPECT_NE(x.list("S02").second, "");
  EXPECT_NE(x.open("20261126T100100Z-bbbb").second, "");
  x.role = Role::None;
  x.person.clear();
  EXPECT_NE(x.list("").second, "");
  x.role = Role::Instructor;
  EXPECT_NE(x.open("20261126T100200Z-cccc").second, "");  // a guest: nobody's
  EXPECT_NE(x.open("../../etc/passwd").second, "");
}

TEST(HistoryServiceTest, TheDebriefIsTheLogAndReviewsStandBesideIt) {
  History x;
  const auto dir = x.t.path / "exercises";
  const std::string id = exercise(dir, "20261126T100000Z-aaaa", "S01", true);
  const std::string before = lines(dir / (id + ".jsonl")).back();
  auto [d, why] = x.open(id);
  ASSERT_EQ(why, "");
  int atc = -1, pilot = -1;
  for (std::size_t i = 0; i < d.entries.size(); ++i) {
    if (d.entries[i].kind == HistoryEntry::Kind::Atc && atc < 0) atc = int(i);
    if (d.entries[i].kind == HistoryEntry::Kind::Pilot && pilot < 0) pilot = int(i);
  }
  ASSERT_GE(atc, 0);
  ASSERT_GE(pilot, 0);
  EXPECT_TRUE(d.entries[std::size_t(atc)].error);  // the wrong runway, as the engine judged it
  EXPECT_EQ(d.entries[std::size_t(atc)].outcome, "INCORRECT");
  EXPECT_FALSE(d.entries[std::size_t(atc)].audio);
  EXPECT_NE(x.ask([&](auto done) { x.b.reclassify(id, pilot, "none", "", done); }), "");  // only an ATC entry
  x.role = Role::Trainee;
  x.person = "S01";
  EXPECT_NE(x.ask([&](auto done) { x.b.reclassify(id, atc, "stt", "", done); }), "");
  x.role = Role::Instructor;
  EXPECT_EQ(x.ask([&](auto done) { x.b.reclassify(id, atc, "stt", "misheard runway", done); }), "");
  EXPECT_EQ(lines(dir / (id + ".jsonl")).back(), before);  // the log never changes
  std::tie(d, why) = x.open(id);
  ASSERT_EQ(d.entries[std::size_t(atc)].reviews.size(), 1u);
  EXPECT_EQ(d.entries[std::size_t(atc)].reviews[0].verdict, "stt");
  EXPECT_EQ(d.row.reclassified, 1);
  EXPECT_NE(x.ask([&](auto done) { x.b.play(id, atc, false, done); }), "");  // no exercise audio (D-314)
}

TEST(HistoryServiceTest, ALogThatStopsEarlyIsAFault) {
  History x;
  const auto dir = x.t.path / "exercises";
  const std::string id = exercise(dir, "20261126T100000Z-aaaa", "S01", false, false);
  const auto [rows, why] = x.list("");
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].end, HistoryRow::End::Fault);
  EXPECT_EQ(x.open(id).first.entries.back().text, "The log stops here: fault");
}

TEST(HistoryServiceTest, DeletingIsForTheInstructorAndLoggedFirst) {
  History x;
  const auto dir = x.t.path / "exercises";
  const std::string id = exercise(dir, "20261126T100000Z-aaaa", "S01", true);
  exercise(dir, "20261126T100100Z-bbbb", "", true);
  exercise(dir, "20261126T100200Z-cccc", "", true);
  x.role = Role::Trainee;
  x.person = "S01";
  EXPECT_NE(x.ask([&](auto done) { x.b.remove(id, done); }), "");
  x.role = Role::Instructor;
  fs::create_directories(dir / "deletions.log");  // cannot be written
  EXPECT_NE(x.ask([&](auto done) { x.b.remove(id, done); }), "");
  EXPECT_TRUE(fs::exists(dir / (id + ".jsonl")));
  fs::remove_all(dir / "deletions.log");
  EXPECT_EQ(x.ask([&](auto done) { x.b.remove(id, done); }), "");
  EXPECT_FALSE(fs::exists(dir / (id + ".jsonl")));
  EXPECT_EQ(x.s->guests(), 2);
  int count = 0;
  EXPECT_NE(x.s->deleteGuests(3, count), "");  // the count changed meanwhile: nothing is deleted
  EXPECT_EQ(count, 2);
  EXPECT_EQ(x.s->guests(), 2);
  EXPECT_EQ(x.s->deleteGuests(2, count), "");
  EXPECT_EQ(count, 0);
  EXPECT_EQ(lines(dir / "deletions.log").size(), 3u);
}

// A file whose name is not its scenario's id can still be checked and chosen: the file name is the key.
TEST(ScenarioServiceTest, TheFileNameIsTheKey) {
  TempDir t;
  fs::create_directories(t.path / "scenarios");
  fs::copy(paths().data + "/airfields", t.path / "airfields", fs::copy_options::recursive);
  fs::copy(paths().data + "/phraseology", t.path / "phraseology", fs::copy_options::recursive);
  fs::copy_file(paths().data + "/scenarios/DEPARTURE-01.json", t.path / "scenarios/OTHER-NAME.json");
  std::vector<std::string> selected;
  ScenarioService s({t.path.string(), (t.path / "none").string()}, {{}, {}, {}, [](std::function<void()> f) { f(); }, [&](const std::string& f) { selected.push_back(f); }});
  auto b = s.backend();
  std::vector<ScenarioInfo> list;
  b.load([&](std::vector<ScenarioInfo> l, const std::string&) { list = std::move(l); });
  s.wait();
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(list[0].id, "OTHER-NAME");
  std::string why = "(none)";
  b.select(list[0].id, [&](const std::string& w) { why = w; });
  s.wait();
  EXPECT_EQ(why, "");
  EXPECT_EQ(selected.size(), 1u);
}

TEST(ScenarioServiceTest, ListsChecksAndSelects) {
  bool active = false;
  std::vector<std::string> selected;
  ScenarioService s({paths().data, (fs::temp_directory_path() / "atc_no_bank").string()},
                    {[&] { return active; }, {}, {}, [](std::function<void()> f) { f(); }, [&](const std::string& f) { selected.push_back(f); }});
  auto b = s.backend();
  std::vector<ScenarioInfo> list;
  b.load([&](std::vector<ScenarioInfo> l, const std::string& why) { list = std::move(l); EXPECT_EQ(why, ""); });
  s.wait();
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(list[0].id, "DEPARTURE-01");
  EXPECT_EQ(list[0].category, "departure");
  EXPECT_EQ(list[0].difficulty, "basic");
  EXPECT_FALSE(list[0].objectives.empty());
  EXPECT_EQ(list[0].steps.size(), 3u);
  EXPECT_EQ(list[0].runways, 2);
  EXPECT_TRUE(list[0].errors.empty());
  for (const auto& st : list[0].steps) EXPECT_FALSE(st.intent.empty());
  std::vector<ScenarioCoverage> seen;
  b.coverage("DEPARTURE-01", [&](ScenarioCoverage c) { seen.push_back(c); });
  s.wait();
  ASSERT_EQ(seen.size(), 2u);
  EXPECT_EQ(seen[0].state, ScenarioCoverage::State::Checking);
  EXPECT_EQ(seen[1].state, ScenarioCoverage::State::Failed);  // no bank there
  std::string why = "(none)";
  active = true;
  b.select("DEPARTURE-01", [&](const std::string& w) { why = w; });
  s.wait();
  EXPECT_NE(why, "");
  active = false;
  b.select("NONE", [&](const std::string& w) { why = w; });
  s.wait();
  EXPECT_NE(why, "");
  EXPECT_TRUE(selected.empty());
  b.select("DEPARTURE-01", [&](const std::string& w) { why = w; });
  s.wait();
  EXPECT_EQ(why, "");
  ASSERT_EQ(selected.size(), 1u);
  EXPECT_NE(selected[0].find("DEPARTURE-01.json"), std::string::npos);
}

// ---- Settings (5f) and the bank choice (S-5)

namespace {
struct SetupRig {
  TempDir t;
  Role role = Role::Instructor;
  bool busy = false;
  int guests = 2;
  std::unique_ptr<SetupService> s;
  SetupBackend b;
  SetupRig() {
    fs::create_directories(t.path / "voices/studio-1");
    std::ofstream(t.path / "voices/studio-1/manifest.json") << "{}";
    fs::create_directories(t.path / "voices/studio-2");  // nothing accepted yet
    make();
  }
  void make() {
    s.reset();
    s = std::make_unique<SetupService>(
        SetupService::Config{(t.path / "setup.json").string(), "/x/en_US-norman-medium", (t.path / "voices").string(), {{"cut", "1500"}}, {{"about", "Program", "test"}}, {"whisper-tiny-genprompt"}},
        SetupService::Rules{[this] { return role; }, [this] { return busy; },
                            [](const std::string& o, const std::string&) { return o == "123456" ? std::string() : std::string("wrong PIN"); },
                            {}, [this] { return guests; },
                            [this](int expected, int& count) { count = guests; if (expected != guests) return std::string("changed"); count = guests = 0; return std::string(); }});
    b = s->backend();
  }
  SetupContent load() {
    SetupContent c;
    b.load([&](SetupContent x, const std::string&) { c = std::move(x); });
    return c;
  }
  std::string ask(const std::function<void(SetupBackend::Done)>& f) {
    std::atomic<bool> got{false};
    std::string r;
    f([&](const std::string& w) { r = w; got = true; });
    for (int i = 0; i < 500 && !got; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return got ? r : "(none)";
  }
};
}  // namespace

TEST(SetupServiceTest, ValuesAreCheckedStoredAndRestored) {
  SetupRig x;
  auto c = x.load();
  const auto num = [&](const SetupContent& k, const std::string& key) {
    for (const auto& n : k.numbers)
      if (n.key == key) return n;
    ADD_FAILURE() << key;
    return SetupNumber{};
  };
  EXPECT_EQ(num(c, "cut").starting, 1500);  // from ui.json
  EXPECT_FALSE(num(c, "cut").byUser);
  EXPECT_EQ(c.guests, 2);
  EXPECT_NE(x.ask([&](auto d) { x.b.setNumber("cut", 1600, d); }), "");  // not a step
  EXPECT_NE(x.ask([&](auto d) { x.b.setNumber("cut", 9000, d); }), "");
  EXPECT_NE(x.ask([&](auto d) { x.b.setNumber("nothing", 1, d); }), "");
  EXPECT_EQ(x.ask([&](auto d) { x.b.setNumber("tail", 500, d); }), "");  // always below the shortest "longest" (10 s)
  EXPECT_EQ(x.ask([&](auto d) { x.b.setNumber("longest", 10, d); }), "");
  x.role = Role::Trainee;
  EXPECT_NE(x.ask([&](auto d) { x.b.setNumber("cut", 1000, d); }), "");
  EXPECT_EQ(x.ask([&](auto d) { x.b.setChoice("theme", "night", d); }), "");  // everybody
  x.role = Role::Instructor;
  x.busy = true;
  EXPECT_NE(x.ask([&](auto d) { x.b.setNumber("cut", 1000, d); }), "");
  x.busy = false;
  EXPECT_EQ(x.ask([&](auto d) { x.b.setNumber("cut", 1000, d); }), "");
  x.make();  // kept for the next start
  c = x.load();
  EXPECT_EQ(num(c, "cut").value, 1000);
  EXPECT_TRUE(num(c, "cut").byUser);
  EXPECT_EQ(SetupService::stored((x.t.path / "setup.json").string()).at("theme"), "night");
  struct stat st {};
  ASSERT_EQ(stat((x.t.path / "setup.json").c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0600u);
  EXPECT_EQ(x.ask([&](auto d) { x.b.restore("ptt", d); }), "");
  EXPECT_FALSE(num(x.load(), "cut").byUser);
  EXPECT_EQ(SetupService::stored((x.t.path / "setup.json").string()).count("theme"), 1u);
}

TEST(SetupServiceTest, TheBankIsChosenAmongTheStudioBanks) {
  SetupRig x;
  const auto c = x.load();
  const auto bank = std::find_if(c.choices.begin(), c.choices.end(), [](const SetupChoice& k) { return k.key == "bank"; });
  ASSERT_NE(bank, c.choices.end());
  ASSERT_EQ(bank->options.size(), 3u);
  EXPECT_EQ(bank->value, "/x/en_US-norman-medium");
  const std::string ready = (x.t.path / "voices/studio-1").string(), empty = (x.t.path / "voices/studio-2").string();
  EXPECT_NE(x.ask([&](auto d) { x.b.setChoice("bank", empty, d); }), "");  // no accepted clip yet
  EXPECT_NE(x.ask([&](auto d) { x.b.setChoice("bank", "/etc", d); }), "");
  EXPECT_EQ(x.ask([&](auto d) { x.b.setChoice("bank", ready, d); }), "");
  EXPECT_EQ(SetupService::stored((x.t.path / "setup.json").string()).at("bank"), ready);
  const auto mic = std::find_if(c.choices.begin(), c.choices.end(), [](const SetupChoice& k) { return k.key == "mic"; });
  EXPECT_NE(x.ask([&](auto d) { x.b.setChoice("mic", mic->options.back().id, d); }), "");  // device lists: not built
  EXPECT_NE(x.ask([&](auto d) { x.b.startLearn([](std::vector<SetupPress>) {}, d); }), "");
}

TEST(SetupServiceTest, PinAndGuests) {
  SetupRig x;
  EXPECT_NE(x.ask([&](auto d) { x.b.changePin("000000", "654321", d); }), "");
  EXPECT_EQ(x.ask([&](auto d) { x.b.changePin("123456", "654321", d); }), "");
  x.role = Role::Trainee;
  EXPECT_NE(x.ask([&](auto d) { x.b.changePin("123456", "654321", d); }), "");
  x.role = Role::Instructor;
  int count = -1;
  std::atomic<bool> got{false};
  std::string why;
  x.b.deleteGuests(3, [&](int n, const std::string& w) { count = n; why = w; got = true; });
  for (int i = 0; i < 500 && !got; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_NE(why, "");
  EXPECT_EQ(count, 2);
  got = false;
  x.b.deleteGuests(2, [&](int n, const std::string& w) { count = n; why = w; got = true; });
  for (int i = 0; i < 500 && !got; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_EQ(why, "");
  EXPECT_EQ(count, 0);
}
