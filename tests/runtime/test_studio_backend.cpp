// The Studio without a screen: wording, takes, consent and session (09, D-331). No device, no sound, no real voice.
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

#include "app/StudioService.h"
#include "audio/WavFile.h"
#include "core/PathResolver.h"
#include "phraseology/PhraseologyEdit.h"
#include "runtime/ExerciseCheck.h"
#include "voice/PhraseTemplate.h"
#include "voice/VoiceBank.h"
using namespace atc;
using nlohmann::json;
namespace fs = std::filesystem;
using SK = StudioNote::Kind;

namespace {
std::string data() {
  Paths p;
  std::string error;
  EXPECT_TRUE(resolvePaths(systemPathInputs(), p, error)) << error;
  return p.data;
}
std::string scenario() { return data() + "/scenarios/DEPARTURE-01.json"; }
struct TempDir {
  fs::path path;
  TempDir() {
    static int n = 0;
    path = fs::temp_directory_path() / ("atc_studio_" + std::to_string(getpid()) + "_" + std::to_string(n++));
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};
std::string slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}
void put(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}
int files(const fs::path& dir) {
  int n = 0;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) n += it->is_regular_file();
  return n;
}
// 0.3 s quiet, a tone, 0.3 s quiet.
std::vector<std::int16_t> tone(double seconds = 0.8, int level = 6000) {
  std::vector<std::int16_t> s(4800, 2);
  for (int i = 0; i < int(seconds * 16000); ++i) s.push_back(std::int16_t(level * std::sin(i * 0.2)));
  s.resize(s.size() + 4800, 2);
  return s;
}
const PhraseEntry& get(const PhraseSet& s, const std::string& id, bool reply = true) {
  const auto* e = s.find(id, reply);
  EXPECT_TRUE(e) << id;
  static const PhraseEntry none;
  return e ? *e : none;
}
}  // namespace

// ---- wording

TEST(Phrases, OnlyTheChangedEntryDiffersAndTheOrderStays) {
  for (const bool reply : {true, false}) {
    PhraseSet s;
    std::string error, out;
    ASSERT_TRUE(readPhraseology(data() + "/phraseology", s, error)) << error;
    PhraseEntry e = get(s, reply ? "SAY_AGAIN" : "LINE_UP", reply);
    const std::string bytes = slurp(data() + "/phraseology/" + e.file);
    e.texts[0] = reply ? "repeat please, {callsign}" : "{callsign} line up on runway {runway}";
    ASSERT_EQ(changeEntry(bytes, e, {"instructor", "2026-11-26"}, out), "");
    using oj = nlohmann::ordered_json;
    oj a = oj::parse(bytes), b = oj::parse(out);
    const oj diff = oj::diff(a, b);
    ASSERT_EQ(diff.size(), 1u) << diff.dump(1);
    EXPECT_EQ(b.dump().size() - a.dump().size(), e.texts[0].size() - get(s, e.id, reply).texts[0].size());
    std::vector<std::string> ka, kb;
    for (auto& [k, v] : a.items()) ka.push_back(k);
    for (auto& [k, v] : b.items()) kb.push_back(k);
    EXPECT_EQ(ka, kb);
    EXPECT_EQ(bytes.substr(0, 8), out.substr(0, 8));  // the indentation
    EXPECT_EQ(out.back(), '\n');
  }
}

TEST(Phrases, ValidatedNeedsReferenceAndAFileThatIsNoPlaceholderAndEditingIsDraft) {
  const std::string open = R"({"schemaVersion":1,"validation":{"status":"placeholder"},"replies":[{"id":"A","text":"one"}]})";
  const std::string good = R"({"schemaVersion":1,"validation":{"status":"validated","sopRef":"S"},"replies":[{"id":"A","text":"one"}]})";
  PhraseEntry e;
  e.id = "A";
  e.texts = {"one"};
  e.validated = true;
  std::string out;
  EXPECT_NE(changeEntry(good, e, {"instructor", "2026-11-26"}, out), "");  // no reference
  e.sopRef = "SOP 1";
  EXPECT_NE(changeEntry(open, e, {"instructor", "2026-11-26"}, out), "");
  EXPECT_TRUE(out.empty());
  ASSERT_EQ(changeEntry(good, e, {"instructor", "2026-11-26"}, out), "");
  EXPECT_EQ(json::parse(out)["replies"][0]["validation"], json::parse(R"({"status":"validated","sopRef":"SOP 1","validated_by":"instructor","validated_at":"2026-11-26"})"));
  std::string again;
  ASSERT_EQ(changeEntry(out, e, {"instructor", "2026-12-01"}, again), "");
  EXPECT_EQ(json::parse(again)["replies"][0]["validation"]["validated_at"], "2026-11-26");  // nothing was validated anew
  e.texts = {"two"};  // even if the caller still says Validated
  ASSERT_EQ(changeEntry(out, e, {"instructor", "2026-12-01"}, again), "");
  EXPECT_EQ(json::parse(again)["replies"][0]["validation"], json::parse(R"({"status":"draft","sopRef":"SOP 1"})"));
  for (const char* bad : {"", "   ", "a\nb", "\xff\xfe"}) {
    e.texts = {bad};
    EXPECT_NE(changeEntry(out, e, {"i", "2026-11-26"}, again), "") << bad;
  }
}

TEST(Phrases, NewRepliesHaveAProperNameOnce) {
  const std::string file = R"({"replies":[{"id":"A","text":"one"}]})";
  PhraseEntry e;
  e.texts = {"say {x}"};
  std::string out;
  for (const char* bad : {"", "lower", "WITH SPACE", "A", "../X"}) {
    e.id = bad;
    EXPECT_NE(appendReply(file, e, out), "") << bad;
  }
  e.id = "B_2";
  ASSERT_EQ(appendReply(file, e, out), "");
  EXPECT_EQ(json::parse(out)["replies"][1], json::parse(R"({"id":"B_2","text":"say {x}","validation":{"status":"draft"}})"));
}

TEST(DataCopyTest, TheFirstSaveMakesTheCopyAndNothingElseIsTouched) {
  TempDir t;
  const fs::path from = t.path / "installed", w = t.path / "w";
  put(from / "a.json", "A");
  put(from / "replies/r.json", "R");
  put(from / "negatives.txt", "x");
  DataCopy c(from.string(), w.string());
  EXPECT_EQ(c.dir(), from.string());
  EXPECT_FALSE(c.installedIsNewer());
  std::string seen;
  EXPECT_EQ(c.write("replies/r.json", "NEW", [&](const std::string& d) { return seen = slurp(fs::path(d) / "a.json") + slurp(fs::path(d) / "replies/r.json"), std::string("no"); }), "no");
  EXPECT_EQ(seen, "ANEW");
  EXPECT_FALSE(c.exists());
  EXPECT_EQ(files(w), 0);  // refused: nothing is left
  ASSERT_EQ(c.write("replies/r.json", "NEW", {}), "");
  EXPECT_TRUE(c.exists());
  EXPECT_EQ(slurp(fs::path(c.dir()) / "replies/r.json"), "NEW");
  EXPECT_EQ(slurp(fs::path(c.dir()) / "a.json"), "A");
  EXPECT_EQ(slurp(from / "replies/r.json"), "R");
  struct stat st {};
  ASSERT_EQ(stat((c.dir() + "/a.json").c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0600u);
  EXPECT_FALSE(c.installedIsNewer());
  put(from / "a.json", "A2");
  EXPECT_TRUE(c.installedIsNewer());
  EXPECT_EQ(slurp(fs::path(c.dir()) / "a.json"), "A");  // never merged
  EXPECT_EQ(c.write("a.json", "B", [](const std::string&) { return std::string("no"); }), "no");
  EXPECT_EQ(slurp(fs::path(c.dir()) / "a.json"), "A");
  EXPECT_NE(c.write("other.json", "B", {}), "");
  EXPECT_FALSE(fs::exists(c.dir() + ".next"));
}

// ---- takes

namespace {
struct Store {
  TempDir t;
  std::string bank = (t.path / "voices/own").string(), pending = (t.path / "pending/own").string();
  long free = 10'000;
  TakeStore s{{.bank = bank, .pending = pending, .voice = "own", .trim = {}, .minFreeMb = 200, .freeMb = [this] { return free; }}};
  TakeSpeaker who{"S01", {true, "2026-11-25", "draft-1"}, {true, "2026-11-25", "draft-1"}};
  Store() { EXPECT_EQ(s.load(), ""); }
  Take add(const std::string& clip = "PH_X@final", const std::string& words = "x", std::vector<std::int16_t> raw = tone()) {
    NewTake n;
    n.clipId = clip;
    n.words = words;
    n.speaker = who;
    n.raw = std::move(raw);
    n.noiseFloorDb = -70;
    Take out;
    EXPECT_EQ(s.add(std::move(n), out), "");
    return out;
  }
  Take::State state(const std::string& id) const {
    const Take* x = s.find(id);
    EXPECT_TRUE(x) << id;
    return x ? x->state : Take::State::Pending;
  }
  // What a program finds that starts now.
  bool loads(VoiceBank& b, bool mayUse = true) const {
    std::vector<LoadError> e;
    const bool ok = b.load(bank, e, [=](const std::string&) { return mayUse; });
    for (const auto& x : e) ADD_FAILURE() << toString(x);
    return ok;
  }
};
using TS = Take::State;
}  // namespace

TEST(Takes, APendingTakeIsInNoBankUntilItIsAccepted) {
  Store s;
  const Take a = s.add();
  EXPECT_EQ(a.state, TS::Pending);
  EXPECT_FALSE(fs::exists(s.bank));
  struct stat st {};
  ASSERT_EQ(stat((s.pending + "/" + a.id + ".wav").c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0600u);
  EXPECT_EQ(s.s.raw(a.id)->size(), tone().size());  // untouched
  EXPECT_LT(a.durationMs, 1000u);                   // the silence is cut
  EXPECT_GT(a.durationMs, 750u);
  EXPECT_NE(s.s.accept(a.id, true, "other words"), "");
  EXPECT_NE(s.s.accept(a.id, true, ""), "");
  ASSERT_EQ(s.s.accept(a.id, true, "x"), "");
  VoiceBank b;
  ASSERT_TRUE(s.loads(b));
  ASSERT_TRUE(b.find("PH_X@final"));
  EXPECT_EQ(b.find("PH_X@final")->words, "x");
  EXPECT_TRUE(b.placeholder());
  EXPECT_EQ(b.find("PH_X@final")->takes[0], *s.s.cut(a.id));
  VoiceBank no;
  ASSERT_TRUE(s.loads(no, false));  // consent withdrawn
  EXPECT_FALSE(no.find("PH_X@final"));
  EXPECT_EQ(no.withheld().size(), 1u);
  const json m = json::parse(slurp(s.bank + "/manifest.json"));
  EXPECT_EQ(m["clips"][0]["takes"][0]["speaker"], "S01");
  EXPECT_EQ(m["clips"][0]["takes"][0]["pilotVoice"]["version"], "draft-1");
}

TEST(Takes, TheCutOfTheStoreIsTheCutOfTrimClip) {
  Store s;
  const auto raw = tone(1.2, 900);
  const Take a = s.add("PH_X@final", "x", raw);
  TrimConfig c;
  c.noiseFloorDb = -70;
  TrimResult r;
  std::string error;
  ASSERT_TRUE(trimClip(raw, 16000, c, r, error));
  EXPECT_EQ(*s.s.cut(a.id), r.samples);
}

TEST(Takes, ThereIsOnePrimaryAndOneWording) {
  Store s;
  put(s.bank + "/p.wav", "");
  ASSERT_TRUE(writeWav(s.bank + "/p.wav", 16000, tone()));
  put(s.bank + "/manifest.json", R"({"schemaVersion":1,"voice":"own","sampleRate":16000,"placeholder":true,"clips":[{"id":"PH_X@final","words":"x","takes":[{"file":"p.wav","take":"primary","synthetic":true}]}]})");
  ASSERT_EQ(s.s.load(), "");
  const Take a = s.add(), b = s.add(), c = s.add();
  EXPECT_NE(a.id, b.id);
  ASSERT_EQ(s.s.accept(a.id, true, "x"), "");
  EXPECT_EQ(s.state("p"), TS::Rejected);  // synthetic (ST-3)
  ASSERT_EQ(s.s.accept(b.id, true, "x"), "");
  EXPECT_EQ(s.state(a.id), TS::Alternate);
  ASSERT_EQ(s.s.accept(c.id, false, "x"), "");
  EXPECT_EQ(s.state(b.id), TS::Primary);
  VoiceBank v;
  ASSERT_TRUE(s.loads(v));
  EXPECT_EQ(v.find("PH_X@final")->takes.size(), 3u);
  EXPECT_EQ(v.find("PH_X@final")->takes[0], *s.s.cut(b.id));
  const Take d = s.add("PH_X@final", "y");  // the data says other words now
  EXPECT_NE(s.s.accept(a.id, true, "y"), "");
  ASSERT_EQ(s.s.accept(d.id, false, "y"), "");
  EXPECT_EQ(s.s.bankWords("PH_X@final"), "y");
  for (const auto& id : {a.id, b.id, c.id}) EXPECT_EQ(s.state(id), TS::Rejected);
  ASSERT_TRUE(s.loads(v));
  EXPECT_EQ(v.find("PH_X@final")->takes.size(), 1u);
}

TEST(Takes, EveryInterruptedStepOfAcceptLeavesABankThatLoads) {
  Store s;
  const Take a = s.add("PH_A@final"), b = s.add("PH_B@final");
  ASSERT_EQ(s.s.accept(a.id, true, "x"), "");
  const std::string before = slurp(s.bank + "/manifest.json");
  fs::create_directories(s.bank + "/manifest.json.part/x");  // the manifest cannot be replaced
  EXPECT_NE(s.s.accept(b.id, true, "x"), "");
  EXPECT_EQ(slurp(s.bank + "/manifest.json"), before);
  fs::remove_all(s.bank + "/manifest.json.part");
  VoiceBank v;
  ASSERT_TRUE(s.loads(v));
  EXPECT_EQ(v.clipCount(), 1u);
  TakeStore next({.bank = s.bank, .pending = s.pending, .voice = "own", .trim = {}, .minFreeMb = 0, .freeMb = {}});  // after a restart
  ASSERT_EQ(next.load(), "");
  EXPECT_EQ(next.find(b.id)->state, TS::Pending);
  EXPECT_EQ(next.unlisted(), 1);  // the clip that was written, never read
  ASSERT_EQ(next.accept(b.id, true, "x"), "");
  ASSERT_TRUE(s.loads(v));
  EXPECT_EQ(v.clipCount(), 2u);
  EXPECT_NE(s.s.accept(a.id, false, "x"), "");  // the first store noticed that the manifest is not the one it read
}

TEST(Takes, TrimAgainFromTheRawRecording) {
  Store s;
  const Take a = s.add();
  const auto raw = *s.s.raw(a.id);
  EXPECT_NE(s.s.setTrim(a.id, 0.5, 0.5), "");
  EXPECT_NE(s.s.setTrim(a.id, -0.1, 0.5), "");
  EXPECT_NE(s.s.setTrim(a.id, 0.5, 0.501), "");
  ASSERT_EQ(s.s.setTrim(a.id, 0.25, 0.75), "");
  EXPECT_NEAR(double(s.s.cut(a.id)->size()), double(raw.size()) / 2, 2);
  ASSERT_EQ(s.s.accept(a.id, true, "x"), "");
  const std::string old = s.s.find(a.id)->file;
  ASSERT_EQ(s.s.setTrim(a.id, 0.25, 0.5), "");  // in the bank
  EXPECT_NE(s.s.find(a.id)->file, old);
  EXPECT_FALSE(fs::exists(s.bank + "/" + old));
  EXPECT_EQ(s.state(a.id), TS::Primary);
  VoiceBank v;
  ASSERT_TRUE(s.loads(v));
  EXPECT_NEAR(double(v.find("PH_X@final")->takes[0].size()), double(raw.size()) / 4, 2);
  EXPECT_EQ(*s.s.raw(a.id), raw);
  EXPECT_EQ(s.s.unlisted(), 0);
}

TEST(Takes, DeletingIsLoggedFirstAndNeverForATakeInUse) {
  Store s;
  const Take a = s.add();
  ASSERT_EQ(s.s.accept(a.id, true, "x"), "");
  EXPECT_NE(s.s.remove(a.id, false, "instructor", "2026-11-26"), "");
  fs::create_directories(s.pending + "/deletions.log");  // the log cannot be written
  const int n = files(s.t.path);
  EXPECT_NE(s.s.remove(a.id, true, "instructor", "2026-11-26"), "");
  EXPECT_EQ(files(s.t.path), n);
  EXPECT_TRUE(s.s.find(a.id));
  fs::remove_all(s.pending + "/deletions.log");
  ASSERT_EQ(s.s.remove(a.id, true, "instructor", "2026-11-26"), "");
  EXPECT_FALSE(s.s.find(a.id));
  const json line = json::parse(slurp(s.pending + "/deletions.log"));
  EXPECT_EQ(line["take"], a.id);
  EXPECT_EQ(line["speaker"], "S01");
  EXPECT_EQ(files(s.t.path), 2);  // the log and the manifest
  VoiceBank v;
  ASSERT_TRUE(s.loads(v));
  const Take b = s.add();
  EXPECT_NE(s.s.remove(b.id, false, "instructor", "2026-11-26"), s.s.remove("none", false, "", ""));
  EXPECT_FALSE(s.s.find(b.id));  // pending: allowed
}

TEST(Takes, RejectedStaysOnDiskAndMayBeAcceptedLater) {
  Store s;
  const Take a = s.add();
  ASSERT_EQ(s.s.reject(a.id), "");
  ASSERT_EQ(s.s.load(), "");
  EXPECT_EQ(s.state(a.id), TS::Rejected);
  ASSERT_EQ(s.s.accept(a.id, true, "x"), "");
  ASSERT_EQ(s.s.reject(a.id), "");
  VoiceBank v;
  ASSERT_TRUE(s.loads(v));
  EXPECT_FALSE(v.find("PH_X@final"));
  EXPECT_EQ(s.s.bankWords("PH_X@final"), "");
  ASSERT_EQ(s.s.accept(a.id, false, "x"), "");
  ASSERT_TRUE(s.loads(v));
  EXPECT_TRUE(v.find("PH_X@final"));
}

TEST(Takes, WhatMayNotBeWrittenIsNotWritten) {
  Store s;
  const auto tryAdd = [&](NewTake n) {
    Take out;
    const std::string why = s.s.add(std::move(n), out);
    EXPECT_EQ(files(s.t.path), 0) << why;
    return why;
  };
  NewTake ok;
  ok.clipId = "PH_X@final";
  ok.speaker = s.who;
  ok.raw = tone();
  NewTake n = ok;
  n.speaker.id = "";
  EXPECT_NE(tryAdd(n), "");
  n.speaker.id = "Mr. Smith";
  EXPECT_NE(tryAdd(n), "");
  n = ok;
  n.clipId = "../x";
  EXPECT_NE(tryAdd(n), "");
  n = ok;
  n.raw.clear();
  EXPECT_NE(tryAdd(n), "");
  s.free = 100;
  EXPECT_NE(tryAdd(ok), "");
  s.free = 10'000;
  fs::create_directories(s.t.path / ".git");
  EXPECT_NE(tryAdd(ok), "");  // inside a git work tree (D-31)
  fs::remove(s.t.path / ".git");
  Take out;
  EXPECT_EQ(s.s.add(ok, out), "");  // control
  put(s.bank + "/manifest.json", R"({"clips":[{"id":"A","takes":[{"file":"../../x.wav","take":"rejected","synthetic":true}]}]})");
  EXPECT_NE(s.s.load(), "");
}

TEST(Takes, SilenceIsKeptForTheInstructorToDecide) {
  Store s;
  const Take a = s.add("PH_X@final", "x", std::vector<std::int16_t>(16000, 1));
  EXPECT_FALSE(a.warnings.empty());
  EXPECT_EQ(a.trimEnd, 16000u);
  EXPECT_TRUE(s.s.cut(a.id));
}

// ---- the check before Start

TEST(StudioCheck, ConsentTodayAndWordsDecide) {
  TempDir t;
  Exercise e;
  std::vector<LoadError> errors;
  ASSERT_TRUE(loadExercise(scenarioSources(data(), scenario()), e, errors));
  SpokenForms f;
  std::string error;
  ASSERT_TRUE(loadSpokenForms(data() + "/phraseology", f, error));
  std::vector<ClipLine> lines;
  std::vector<std::string> bad;
  ASSERT_TRUE(clipList(e, f, lines, bad));
  const auto bank = [&](const std::string& person, const std::string& wrong) {
    json m = {{"schemaVersion", 1}, {"voice", "t"}, {"sampleRate", 16000}, {"placeholder", true}, {"clips", json::array()}};
    int n = 0;
    for (const auto& l : lines) {
      const std::string file = "c" + std::to_string(n++) + ".wav";
      EXPECT_TRUE(writeWav((t.path / file).string(), 16000, std::vector<std::int16_t>(1600, 500)));
      json take = {{"file", file}, {"take", "primary"}};
      if (l.id == person) {
        take["speaker"] = "S01";
        take["pilotVoice"] = {{"consent", true}, {"date", "2026-11-25"}, {"version", "draft-1"}};
      } else {
        take["synthetic"] = true;
      }
      m["clips"].push_back({{"id", l.id}, {"words", l.id == wrong ? "zero nine" : l.words}, {"takes", json::array({take})}});
    }
    std::ofstream(t.path / "manifest.json") << m.dump();
  };
  const auto state = [&](bool may, std::vector<std::string>* missing = nullptr) {
    const auto c = ExerciseCheck::check(data(), t.path.string(), scenario(), [=](const std::string&) { return may; });
    if (missing) *missing = c.missing;
    return c.state;
  };
  using S = CheckResult::State;
  bank("", "");
  EXPECT_EQ(state(false), S::Covered);
  bank("RUNWAY_09@final", "");
  EXPECT_EQ(state(true), S::Covered);
  std::vector<std::string> missing;
  EXPECT_EQ(state(false, &missing), S::MissingClips);
  ASSERT_EQ(missing.size(), 1u);
  EXPECT_NE(missing[0].find("RUNWAY_09@final"), std::string::npos);
  EXPECT_EQ(ExerciseCheck::check(data(), t.path.string(), scenario()).state, S::MissingClips);  // nobody to ask
  bank("", "RUNWAY_09@final");
  EXPECT_EQ(state(true, &missing), S::MissingClips);  // ST-4: "nine" for "niner"
  ASSERT_EQ(missing.size(), 1u);
  EXPECT_NE(missing[0].find("zero nine"), std::string::npos);
}

TEST(StudioCheck, TheWritableCopyIsWhatIsRead) {
  TempDir t;
  DataCopy c(data() + "/phraseology", t.path.string());
  const std::string bytes = slurp(data() + "/phraseology/replies/placeholder.json");
  std::string changed = bytes;
  changed.replace(changed.find("say again,"), 10, "repeat it,");
  ASSERT_EQ(c.write("replies/placeholder.json", changed, {}), "");
  ScenarioSources src = scenarioSources(data(), scenario());
  src.phraseologyDir = c.dir();
  Exercise a, b;
  std::vector<LoadError> errors;
  ASSERT_TRUE(loadExercise(src, a, errors)) << (errors.empty() ? "" : toString(errors[0]));  // ORIGIN is no data file
  ASSERT_TRUE(loadExercise(scenarioSources(data(), scenario()), b, errors));
  EXPECT_EQ(a.replies["SAY_AGAIN"].text, "repeat it, {callsign}");
  EXPECT_NE(a.hash, b.hash);  // told apart in the log
  put(fs::path(c.dir()) / "replies/placeholder.json", "{");
  EXPECT_FALSE(loadExercise(src, a, errors));  // a damaged copy is never replaced by the installed data
  EXPECT_EQ(c.dir(), src.phraseologyDir);
}

// ---- the service

namespace {
struct Studio {
  TempDir t;
  std::shared_ptr<ConsentView> consent = std::make_shared<ConsentView>();
  std::shared_ptr<DataCopy> copy = std::make_shared<DataCopy>(data() + "/phraseology", (t.path / "w").string());
  std::atomic<bool> instructor{true}, exercise{false};
  std::atomic<int> changed{0};
  std::string bank = (t.path / "voices/own").string(), pending = (t.path / "pending/own").string();
  std::vector<std::string> calls;  // of the radio
  std::vector<std::string> log;    // events
  std::vector<StudioTake> takes;
  std::mutex lm;
  std::unique_ptr<StudioService> s;
  StudioBackend b;

  Studio() {
    answers(true, true);
    StudioService::Config c;
    c.data = data();
    c.phraseology = data() + "/phraseology";
    c.writable = (t.path / "w").string();
    c.activeBank = "/x/en_US-norman-medium";
    c.bank = bank;
    c.pending = pending;
    c.voice = "own";
    s = std::make_unique<StudioService>(c, consent, copy, StudioService::Rules{[this] { return instructor.load(); }, [this] { return exercise.load(); },
                                        [] { return std::string("2026-11-26"); }, [] { return std::int64_t(5); }, [this] { ++changed; }});
    s->connect({.session = [this](bool on, const std::string& why) { call(std::string(on ? "session on" : "session off") + (why.empty() ? "" : ": " + why)); },
                .pedal = [this](bool on) { call(on ? "pedal on" : "pedal off"); },
                .record = [this] { call("record"); },
                .stop = [this] { call("stop"); },
                .stopPlay = [this] { call("stopPlay"); },
                .play = [this](std::vector<std::int16_t> p, int) { call("play " + std::to_string(p.size() > 0)); }});
    b = s->backend();
    StudioEvents e;
    e.roomTone = [this](double, const std::string& w) { note("room " + w); };
    e.recording = [this](bool on) { note(on ? "recording" : "not recording"); };
    e.recordingStopped = [this](const std::string& w) { note("stopped " + w); };
    e.takeReady = [this](StudioTake x) {
      note("take " + x.clipId);
      std::lock_guard l(lm);
      takes.push_back(std::move(x));
    };
    e.sessionEnded = [this](const std::string& w) { note("ended " + w); };
    b.setEvents(std::move(e));
  }
  void call(const std::string& c) { std::lock_guard l(lm); calls.push_back(c); }
  void note(const std::string& c) { std::lock_guard l(lm); log.push_back(c); }
  bool has(const std::string& c) { std::lock_guard l(lm); return std::find(log.begin(), log.end(), c) != log.end(); }
  bool called(const std::string& c) { std::lock_guard l(lm); return std::find(calls.begin(), calls.end(), c) != calls.end(); }
  void answers(bool recording, bool pilot) {
    Person p;
    p.id = "S01";
    p.consent = recording;
    p.consentDate = p.pilotVoiceDate = "2026-11-25";
    p.pilotVoice = pilot;
    p.consentVersion = p.pilotVoiceVersion = ConsentVersions{}.recording;
    consent->set({p}, {});
  }
  // The answer of an action; "(none)" if there is none yet.
  std::string ask(const std::function<void(StudioBackend::Done)>& f) {
    auto got = std::make_shared<std::string>("(none)");
    f([got](const std::string& w) { *got = w; });
    s->wait();
    return *got;
  }
  bool open() {
    std::string got = "(none)";
    b.startSession("S01", [&](const std::string& w) { got = w; });
    EXPECT_EQ(got, "(none)");  // only after the room tone
    s->onStudio({.kind = SK::Started});
    s->onStudio({.kind = SK::RoomTone, .dbfs = -70});
    return got.empty();
  }
  std::string record(const std::string& clip) { return ask([&](auto d) { b.record(clip, d); }); }
  void stop(const std::string& why = {}) {
    s->onStudio({.kind = SK::NotRecording, .why = why});
    s->onTake({.samples = tone(), .rate = 16000, .gap = false, .why = why});
    s->wait();
  }
  StudioTake take(const std::string& clip) {
    EXPECT_EQ(record(clip), "");
    s->onStudio({.kind = SK::Recording});
    stop();
    EXPECT_FALSE(takes.empty());
    return takes.empty() ? StudioTake{} : takes.back();
  }
  StudioContent load() {
    StudioContent c;
    std::string why = "(none)";
    b.load([&](StudioContent x, const std::string& w) { c = std::move(x); why = w; });
    s->wait();
    EXPECT_EQ(why, "");
    return c;
  }
  std::vector<StudioClip> clips() {
    std::vector<StudioClip> c;
    b.clips("DEPARTURE-01", [&](std::vector<StudioClip> x, const std::string& w) { c = std::move(x); EXPECT_EQ(w, ""); });
    s->wait();
    return c;
  }
  StudioClip clip(const std::string& id) {
    for (auto& c : clips())
      if (c.id == id) return c;
    ADD_FAILURE() << id;
    return {};
  }
  StudioEntry entry(const std::string& id, bool reply = true) {
    for (auto& e : load().entries)
      if (e.id == id && e.reply == reply) return e;
    ADD_FAILURE() << id;
    return {};
  }
};
const char* kClip = "PH_SAY_AGAIN@initial";
}  // namespace

TEST(StudioService, LoadsWordingUseAndClips) {
  Studio x;
  const auto c = x.load();
  EXPECT_FALSE(c.changedHere);
  EXPECT_EQ(c.activeVoice, "en_US-norman-medium");
  EXPECT_EQ(c.voice, "own");
  EXPECT_EQ(c.notice, "");
  ASSERT_EQ(c.scenarios.size(), 1u);
  const auto e = x.entry("READBACK_TAXI_HOLD_SHORT");
  ASSERT_EQ(e.usedBy.size(), 1u);
  EXPECT_NE(e.usedBy[0].find("CORRECT, hold_short"), std::string::npos) << e.usedBy[0];
  EXPECT_TRUE(e.placeholderFile);
  EXPECT_FALSE(x.entry("LINE_UP", false).usedBy.empty());
  const auto clips = x.clips();
  Exercise ex;
  std::vector<LoadError> errors;
  ASSERT_TRUE(loadExercise(scenarioSources(data(), scenario()), ex, errors));
  std::map<std::string, RequiredClip> need;
  std::vector<std::string> bad;
  requiredClips(ex, need, bad);
  EXPECT_EQ(clips.size(), need.size());
  for (const auto& k : clips) {
    EXPECT_TRUE(need.count(k.id)) << k.id;
    EXPECT_TRUE(k.needed);
    EXPECT_EQ(k.sayTarget, k.words) << k.id;
    EXPECT_FALSE(k.replies.empty()) << k.id;
    EXPECT_EQ(k.bankWords, "");
  }
  const auto r = x.clip("RUNWAY_09@medial");
  EXPECT_EQ(r.words, "zero niner");
  EXPECT_NE(r.sayBefore.find("alpha three"), std::string::npos) << r.sayBefore;  // values that fit together
  EXPECT_GT(r.replies.size(), 1u);
  EXPECT_EQ(files(x.t.path), 0);  // reading writes nothing
}

TEST(StudioService, ASessionNeedsInstructorConsentAndAnswersAfterTheRoomTone) {
  Studio x;
  const auto start = [&] { return x.ask([&](auto d) { x.b.startSession("S01", d); }); };
  x.instructor = false;
  EXPECT_NE(start(), "(none)");
  x.instructor = true;
  x.exercise = true;
  EXPECT_NE(start(), "(none)");
  x.exercise = false;
  x.answers(true, false);
  EXPECT_NE(start(), "(none)");
  x.answers(false, true);
  EXPECT_NE(start(), "(none)");
  EXPECT_NE(x.ask([&](auto d) { x.b.startSession("S02", d); }), "(none)");  // nobody
  x.answers(true, true);
  EXPECT_TRUE(x.calls.empty());
  EXPECT_EQ(start(), "(none)");
  EXPECT_TRUE(x.called("session on"));
  EXPECT_NE(start(), "(none)");  // one at a time
}

TEST(StudioService, ARefusedOrCancelledStartIsAnswered) {
  Studio x;
  std::string got = "(none)";
  x.b.startSession("S01", [&](const std::string& w) { got = w; });
  x.s->onStudio({.kind = SK::Refused, .why = "no microphone"});
  EXPECT_EQ(got, "no microphone");
  got = "(none)";
  x.b.startSession("S01", [&](const std::string& w) { got = w; });
  x.s->onStudio({.kind = SK::Started});
  x.b.endSession();  // during the room tone
  EXPECT_TRUE(x.called("session off"));
  x.s->onStudio({.kind = SK::Ended});
  EXPECT_NE(got, "(none)");
  EXPECT_NE(got, "");
  x.b.endSession();  // harmless
  EXPECT_EQ(std::count(x.calls.begin(), x.calls.end(), "session off"), 1);
  EXPECT_TRUE(x.open());
}

TEST(StudioService, RecordsAPendingTakeWithTheSpeakersAnswers) {
  Studio x;
  EXPECT_NE(x.record(kClip), "");  // no session
  ASSERT_TRUE(x.open());
  const auto t = x.take(kClip);
  EXPECT_EQ(x.log, (std::vector<std::string>{"room ", "recording", "not recording", "stopped ", "take " + std::string(kClip)}));
  EXPECT_EQ(t.state, StudioTake::State::Pending);
  EXPECT_EQ(t.speaker, "S01");
  EXPECT_TRUE(t.warnings.empty());
  EXPECT_FALSE(fs::exists(x.bank));
  const json j = json::parse(slurp(x.pending + "/" + t.id + ".json"));
  EXPECT_EQ(j["speaker"], "S01");
  EXPECT_EQ(j["recording"]["consent"], true);
  EXPECT_EQ(j["pilotVoice"]["version"], "draft-1");
  EXPECT_EQ(j["words"], "say again");
  EXPECT_EQ(x.record("PH_SAY_AGAIN@initial"), "");
  x.s->onStudio({.kind = SK::Recording});
  EXPECT_NE(x.record(kClip), "");  // one take at a time
  x.stop("the time limit of a take");
  ASSERT_EQ(x.takes.size(), 2u);
  EXPECT_EQ(x.takes[1].warnings.size(), 1u);
  EXPECT_EQ(x.clip(kClip).takes.size(), 2u);
}

TEST(StudioService, ThePedalRecordsTheChosenClip) {
  Studio x;
  x.b.setPedal(true, kClip);
  EXPECT_TRUE(x.calls.empty());  // no session
  ASSERT_TRUE(x.open());
  x.b.setPedal(true, "");
  EXPECT_FALSE(x.called("pedal on"));  // no clip: it records nothing
  x.b.setPedal(true, kClip);
  EXPECT_TRUE(x.called("pedal on"));
  x.s->onStudio({.kind = SK::Recording});
  x.b.setPedal(true, "PH_REQUEST_TAXI@final");  // chosen while recording: the next take
  x.stop();
  ASSERT_EQ(x.takes.size(), 1u);
  EXPECT_EQ(x.takes[0].clipId, kClip);
}

TEST(StudioService, WithoutConsentNothingIsWrittenAnywhere) {
  {  // before the take is saved
    Studio x;
    ASSERT_TRUE(x.open());
    EXPECT_EQ(x.record(kClip), "");
    x.s->onStudio({.kind = SK::Recording});
    x.answers(true, false);
    x.stop();
    EXPECT_TRUE(x.takes.empty());
    EXPECT_EQ(files(x.t.path), 0);
    EXPECT_TRUE(x.has("stopped the consent of this person has changed: the take was not kept"));
  }
  {  // before Record
    Studio x;
    ASSERT_TRUE(x.open());
    x.answers(false, true);
    EXPECT_NE(x.record(kClip), "");
    EXPECT_FALSE(x.called("record"));
  }
  {  // before Accept; control: with consent it is accepted
    Studio x;
    ASSERT_TRUE(x.open());
    const auto t = x.take(kClip);
    x.answers(true, false);
    EXPECT_NE(x.ask([&](auto d) { x.b.accept(t.id, true, d); }), "");
    EXPECT_FALSE(fs::exists(x.bank));
    EXPECT_EQ(x.changed, 0);
    x.answers(false, true);  // only the recording answer: use is still allowed (D-134)
    EXPECT_EQ(x.ask([&](auto d) { x.b.accept(t.id, true, d); }), "");
    EXPECT_EQ(x.changed, 1);
    EXPECT_EQ(x.clip(kClip).bankWords, "say again");
    x.answers(true, false);  // withdrawn: shown so, missing, and it may be deleted although it was in use
    const auto c = x.clip(kClip);
    EXPECT_EQ(c.takes[0].state, StudioTake::State::Withdrawn);
    EXPECT_EQ(c.bankWords, "");
    for (const char* mode : {"raw", "trimmed", "radio", "sentence"}) EXPECT_NE(x.ask([&](auto d) { x.b.play(t.id, mode, d); }), "") << mode;
    EXPECT_NE(x.ask([&](auto d) { x.b.setTrim(t.id, 0.1, 0.9, d); }), "");
    std::string why;
    x.b.peaks(t.id, [&](std::vector<float>, const std::string& w) { why = w; });
    x.s->wait();
    EXPECT_NE(why, "");
    EXPECT_EQ(x.ask([&](auto d) { x.b.deleteTake(t.id, d); }), "");
    EXPECT_TRUE(x.clip(kClip).takes.empty());
  }
}

TEST(StudioService, ASessionEndsWithTheConsentAndWithTheLogin) {
  for (const bool login : {false, true}) {
    Studio x;
    ASSERT_TRUE(x.open());
    x.s->consentChanged();
    EXPECT_FALSE(x.called("session off"));  // nothing that matters has changed
    if (login) x.instructor = false;
    else x.answers(true, false);
    x.s->consentChanged();
    ASSERT_FALSE(x.calls.empty());
    EXPECT_EQ(x.calls.back().rfind("session off: ", 0), 0u) << x.calls.back();
    x.s->onStudio({.kind = SK::Ended, .why = "gone"});
    EXPECT_TRUE(x.has("ended gone"));
    EXPECT_NE(x.record(kClip), "");
  }
}

TEST(StudioService, OnlyTheInstructorChangesAnythingAndNotDuringAnExercise) {
  Studio x;
  ASSERT_TRUE(x.open());
  const auto t = x.take(kClip);
  const auto rev = x.load().revision;
  const auto e = x.entry("SAY_AGAIN");
  const int n = files(x.t.path);
  for (const bool ex : {false, true}) {
    x.instructor = ex;
    x.exercise = ex;
    EXPECT_NE(x.ask([&](auto d) { x.b.save(e, rev, d); }), "");
    EXPECT_NE(x.ask([&](auto d) { x.b.addReply(e, rev, d); }), "");
    EXPECT_NE(x.ask([&](auto d) { x.b.accept(t.id, true, d); }), "");
    EXPECT_NE(x.ask([&](auto d) { x.b.reject(t.id, d); }), "");
    EXPECT_NE(x.ask([&](auto d) { x.b.deleteTake(t.id, d); }), "");
    EXPECT_NE(x.ask([&](auto d) { x.b.setTrim(t.id, 0.1, 0.9, d); }), "");
    EXPECT_NE(x.ask([&](auto d) { x.b.exportChanges((x.t.path / "e.json").string(), d); }), "");
    EXPECT_NE(x.record(kClip), "");
  }
  EXPECT_EQ(files(x.t.path), n);
}

TEST(StudioService, SavesWordingIntoTheCopyAndAsksForANewCheck) {
  Studio x;
  auto e = x.entry("SAY_AGAIN");
  const auto rev = x.load().revision;
  const std::string installed = slurp(data() + "/phraseology/replies/placeholder.json");
  e.texts = {"say again, {callsign}, {nothing}"};
  EXPECT_NE(x.ask([&](auto d) { x.b.save(e, rev, d); }), "");  // no such value
  e.texts = {"say again {callsign"};
  EXPECT_NE(x.ask([&](auto d) { x.b.save(e, rev, d); }), "");
  EXPECT_EQ(files(x.t.path), 0);
  e.texts = {"repeat your message, {callsign}"};
  StudioPreview p;
  x.b.preview(e, 0, [&](StudioPreview v) { p = std::move(v); });
  x.s->wait();
  EXPECT_EQ(p.error, "");
  ASSERT_EQ(p.segments.size(), 2u);
  EXPECT_EQ(p.segments[0].clipId, "PH_REPEAT_YOUR_MESSAGE@initial");
  EXPECT_EQ(p.segments[1].pool, "callsigns");
  EXPECT_EQ(p.missing, std::vector<std::string>{"PH_REPEAT_YOUR_MESSAGE@initial"});
  EXPECT_EQ(p.unused, std::vector<std::string>{kClip});
  EXPECT_EQ(files(x.t.path), 0);  // a preview writes nothing
  ASSERT_EQ(x.ask([&](auto d) { x.b.save(e, rev, d); }), "");
  EXPECT_EQ(x.changed, 1);
  EXPECT_NE(x.ask([&](auto d) { x.b.save(e, rev, d); }), "");  // read before the change
  EXPECT_EQ(slurp(data() + "/phraseology/replies/placeholder.json"), installed);
  EXPECT_TRUE(x.load().changedHere);
  EXPECT_EQ(x.entry("SAY_AGAIN").texts[0], e.texts[0]);
  EXPECT_EQ(x.clip("PH_REPEAT_YOUR_MESSAGE@initial").needed, true);
  const std::string out = (x.t.path / "export.json").string();
  ASSERT_EQ(x.ask([&](auto d) { x.b.exportChanges(out, d); }), "");
  const json j = json::parse(slurp(out));
  ASSERT_EQ(j["changes"].size(), 1u);
  EXPECT_EQ(j["changes"][0]["id"], "SAY_AGAIN");
  EXPECT_EQ(j["changes"][0]["installed"]["texts"][0], "say again, {callsign}");
}

TEST(StudioService, AChangeThatBreaksAScenarioIsRefused) {
  Studio x;
  auto e = x.entry("TAXI_INSTRUCTION", false);
  const auto rev = x.load().revision;
  for (auto& t : e.texts)
    for (auto i = t.find("{taxiway}"); i != std::string::npos; i = t.find("{taxiway}")) t.replace(i, 9, "any way");
  const std::string why = x.ask([&](auto d) { x.b.save(e, rev, d); });
  EXPECT_NE(why.find("would no longer load"), std::string::npos) << why;
  EXPECT_FALSE(x.copy->exists());
  EXPECT_EQ(files(x.t.path), 0);
  StudioEntry add;
  add.id = "NEW_ONE";
  add.texts = {"standing by, {callsign}"};
  ASSERT_EQ(x.ask([&](auto d) { x.b.addReply(add, rev, d); }), "");
  EXPECT_TRUE(x.entry("NEW_ONE").usedBy.empty());
  EXPECT_NE(x.ask([&](auto d) { x.b.addReply(add, x.load().revision, d); }), "");
}

TEST(StudioService, PlaysTheSentenceAndNamesWhatIsMissing) {
  Studio x;
  ASSERT_TRUE(x.open());
  const auto t = x.take(kClip);
  std::string got = "(none)";
  x.b.play(t.id, "sentence", [&](const std::string& w) { got = w; });
  x.s->wait();
  EXPECT_TRUE(x.called("play 1"));
  EXPECT_EQ(got, "(none)");
  x.s->onPlay(LocalEnd::Started);
  EXPECT_EQ(got.rfind("Missing: CALLSIGN_", 0), 0u) << got;
  got = "(none)";
  x.b.play(t.id, "raw", [&](const std::string& w) { got = w; });
  x.s->wait();
  x.s->onPlay(LocalEnd::Refused);
  EXPECT_NE(got, "(none)");
  EXPECT_NE(got, "");
  x.b.play(t.id, "radio", [&](const std::string& w) { got = w; });
  x.s->wait();
  x.s->onPlay(LocalEnd::Started);
  EXPECT_EQ(got, "");
  EXPECT_NE(x.ask([&](auto d) { x.b.play("none", "raw", d); }), "");
  std::vector<float> p;
  x.b.peaks(t.id, [&](std::vector<float> v, const std::string&) { p = std::move(v); });
  x.s->wait();
  EXPECT_LE(p.size(), 2000u);
  EXPECT_GT(*std::max_element(p.begin(), p.end()), 0.1f);
  x.s->connect({});
  EXPECT_NE(x.ask([&](auto d) { x.b.play(t.id, "raw", d); }), "");  // the radio has gone
}

TEST(StudioService, ATakeAtTheVeryEndIsStillKept) {
  Studio x;
  ASSERT_TRUE(x.open());
  EXPECT_EQ(x.record(kClip), "");
  x.s->onStudio({.kind = SK::Recording});
  x.s->onStudio({.kind = SK::NotRecording, .why = "the program ended"});
  x.s->onTake({.samples = tone(), .rate = 16000, .gap = true, .why = "the program ended"});
  x.s.reset();
  EXPECT_EQ(files(x.pending), 2);
}
