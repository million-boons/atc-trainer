#include <gtest/gtest.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>

#include "storage/RecordingStore.h"
using namespace atc;
namespace fs = std::filesystem;

struct Store : ::testing::Test {
  fs::path dir = fs::temp_directory_path() / ("atc_store_" + std::to_string(::getpid()) + "_" +
                                              ::testing::UnitTest::GetInstance()->current_test_info()->name());
  std::mutex m;
  std::vector<std::string> log;
  StoreConfig cfg;
  void SetUp() override { cfg.dir = dir.string() + "/rec"; }
  void TearDown() override { fs::remove_all(dir); }
  std::unique_ptr<RecordingStore> make() {
    return std::make_unique<RecordingStore>(cfg, [this](const std::string& k, bool ok, const std::string& why) {
      std::lock_guard l(m);
      log.push_back(k + (ok ? " ok" : " " + why));
    });
  }
  static StoredRecording rec(std::int64_t t, int tx) {
    StoredRecording r;
    r.key = RecordingStore::makeKey(t, tx);
    r.startedMs = t;
    r.tx = tx;
    r.speaker = "S01";
    r.pttSource = "RIGHT";
    r.end = "normal";
    r.pressSample = 4800;
    r.endSample = 40000;
    r.report.empty = false;
    r.report.peakDbfs = -11.5;
    r.report.keyToSpeechMs = 320;
    return r;
  }
  static std::vector<StoredRecording> all(RecordingStore& s, std::size_t max = 50, int* bad = nullptr) {
    std::promise<std::vector<StoredRecording>> p;
    s.list(max, [&](std::vector<StoredRecording> v, int b) {
      if (bad) *bad = b;
      p.set_value(std::move(v));
    });
    return p.get_future().get();
  }
  static std::optional<std::vector<std::int16_t>> audio(RecordingStore& s, const std::string& k) {
    std::promise<std::optional<std::vector<std::int16_t>>> p;
    s.loadAudio(k, [&](auto v) { p.set_value(std::move(v)); });
    return p.get_future().get();
  }
};

TEST_F(Store, RoundTripWithModelSettings) {
  const std::vector<std::int16_t> a{1, -2, 300, -32768, 32767};
  auto r = rec(1'763'900'000'123, 7);
  {
    auto s = make();
    s->save(r, a);
    s->addResult(r.key, {"m1", "Model one", R"({"id":"m1","modelHash":"ab"})", "eagle", {{"eagle", 0.5}}, 340});
    s->addResult(r.key, {"m2", "Model two", R"({"id":"m2"})", "legal", {}, 900});
  }  // the destructor finishes the writes
  auto s = make();
  const auto v = all(*s);
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0].key, r.key);
  EXPECT_EQ(v[0].samples, 5);
  EXPECT_EQ(v[0].speaker, "S01");
  EXPECT_EQ(v[0].pressSample, 4800);
  EXPECT_EQ(v[0].report.keyToSpeechMs, 320);
  ASSERT_EQ(v[0].results.size(), 2u) << "a second model does not replace the first result";
  EXPECT_EQ(v[0].results[0].modelConfig, R"({"id":"m1","modelHash":"ab"})");
  EXPECT_EQ(v[0].results[0].words.at(0).text, "eagle");
  EXPECT_DOUBLE_EQ(v[0].results[1].releaseToTextMs, 900);
  EXPECT_EQ(audio(*s, r.key), a);
  EXPECT_EQ(log, (std::vector<std::string>{r.key + " ok"}));
}
TEST_F(Store, OwnerOnly) {
  { make()->save(rec(1000, 1), {1}); }
  struct stat st {};
  ASSERT_EQ(stat(cfg.dir.c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0700u);
  for (const auto& e : fs::directory_iterator(cfg.dir)) {
    ASSERT_EQ(stat(e.path().c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600u) << e.path();
  }
}
TEST_F(Store, NeverOverwrites) {
  auto s = make();
  s->save(rec(1000, 1), {1, 2});
  s->save(rec(1000, 1), {9});
  EXPECT_EQ(all(*s).size(), 1u);
  EXPECT_EQ(audio(*s, rec(1000, 1).key)->size(), 2u);
  EXPECT_NE(log.at(1).find("already exists"), std::string::npos);
}
TEST_F(Store, RefusesWhenDiskIsNearlyFull) {
  cfg.freeMb = [] { return 120L; };
  auto s = make();
  s->save(rec(1000, 1), {1});
  EXPECT_TRUE(all(*s).empty());
  EXPECT_NE(log.at(0).find("disk nearly full: 120 MB"), std::string::npos);
}
TEST_F(Store, UnwritableFolderIsReported) {
  cfg.dir = "/proc/atc-no-such-place/rec";
  auto s = make();
  s->save(rec(1000, 1), {1});
  EXPECT_TRUE(all(*s).empty());
  EXPECT_NE(log.at(0).find("cannot create"), std::string::npos);
}
TEST_F(Store, NewestFirstLimitedAndBadFilesCounted) {
  auto s = make();
  for (int i = 1; i <= 4; ++i) s->save(rec(i * 60'000, i), {std::int16_t(i)});
  all(*s);
  std::ofstream(cfg.dir + "/20990101T000000Z-000-tx9.json") << "{\"format\":1}";
  std::ofstream(cfg.dir + "/notes.json") << "not json";
  int bad = 0;
  const auto v = all(*s, 3, &bad);
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0].tx, 4);
  EXPECT_EQ(v[2].tx, 2);
  EXPECT_EQ(bad, 2);
}
TEST_F(Store, NamesCannotLeaveTheFolder) {
  auto s = make();
  auto r = rec(1000, 1);
  r.key = "../outside";
  s->save(r, {1});
  EXPECT_FALSE(audio(*s, "../../etc/passwd"));
  EXPECT_NE(log.at(0).find("invalid name"), std::string::npos);
}
TEST_F(Store, FullQueueIsReportedNotBlocked) {
  cfg.maxQueue = 2;
  auto s = make();
  std::promise<void> go;
  auto f = go.get_future().share();
  s->list(1, [f](auto, int) { f.wait(); });  // holds the worker
  for (int i = 0; i < 50; ++i) s->save(rec(1000 + i, i), {1});
  go.set_value();
  all(*s);
  int refused = 0;
  for (auto& l : log) refused += l.find("too slow") != std::string::npos;
  EXPECT_GE(refused, 47);
}
TEST_F(Store, ResultForAnUnsavedRecordingIsIgnored) {
  auto s = make();
  s->addResult("20200101T000000Z-000-tx1", {"m", "M", "{}", "x", {}, 1});
  EXPECT_TRUE(all(*s).empty());
}
TEST_F(Store, InvalidTextIsStoredSafely) {
  auto r = rec(1000, 1);
  {
    auto s = make();
    s->save(r, {1});
    s->addResult(r.key, {"m", "M", "not json", "bad \xff byte", {}, 1});
  }
  auto s = make();
  ASSERT_EQ(all(*s).size(), 1u);
  EXPECT_EQ(all(*s)[0].results.at(0).modelConfig, "not json");
}
TEST_F(Store, NothingIsSavedWithoutASpeaker) {
  auto s = make();
  for (const char* id : {"", "two words", "../x"}) {
    auto r = rec(1000, 1);
    r.speaker = id;
    s->save(r, {1});
  }
  EXPECT_TRUE(all(*s).empty());
  ASSERT_EQ(log.size(), 3u);
  EXPECT_NE(log.at(0).find("no speaker ID"), std::string::npos);
}
static std::string removed(RecordingStore& s, const std::string& key) {
  std::promise<std::string> p;
  s.remove(key, "instructor", [&](const std::string& why) { p.set_value(why); });
  return p.get_future().get();
}
static std::string content(const fs::path& f) {
  std::ifstream i(f);
  return {std::istreambuf_iterator<char>(i), {}};
}
TEST_F(Store, RemoveIsLogged) {
  auto s = make();
  s->save(rec(1000, 1), {1});
  s->save(rec(2000, 2), {2});
  const std::string key = rec(1000, 1).key;
  EXPECT_EQ(removed(*s, key), "");
  ASSERT_EQ(all(*s).size(), 1u);
  EXPECT_FALSE(fs::exists(cfg.dir + "/" + key + ".wav"));
  const std::string l = content(cfg.dir + "/deletions.log");
  EXPECT_NE(l.find(key), std::string::npos);
  EXPECT_NE(l.find("\"speaker\":\"S01\""), std::string::npos);
  EXPECT_NE(l.find("\"role\":\"instructor\""), std::string::npos);
  EXPECT_EQ(std::count(l.begin(), l.end(), '\n'), 1);
  struct stat st {};
  ASSERT_EQ(stat((cfg.dir + "/deletions.log").c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0600u);
}
TEST_F(Store, NoLogNoDeletion) {
  auto s = make();
  s->save(rec(1000, 1), {1});
  all(*s);
  fs::create_directory(cfg.dir + "/deletions.log");  // cannot be opened for writing
  EXPECT_NE(removed(*s, rec(1000, 1).key).find("nothing was deleted"), std::string::npos);
  EXPECT_EQ(all(*s).size(), 1u);
}
TEST_F(Store, RemoveRefusesOtherNames) {
  auto s = make();
  s->save(rec(1000, 1), {1});
  all(*s);
  std::ofstream(dir.string() + "/outside.wav") << "x";
  EXPECT_EQ(removed(*s, "../outside"), "invalid name");
  EXPECT_EQ(removed(*s, "deletions"), "not found");
  EXPECT_TRUE(fs::exists(dir.string() + "/outside.wav"));
  EXPECT_FALSE(fs::exists(cfg.dir + "/deletions.log"));
}
TEST_F(Store, UnreadableDescriptionCanStillBeRemoved) {
  auto s = make();
  s->save(rec(1000, 1), {1});
  all(*s);
  std::ofstream(cfg.dir + "/" + rec(1000, 1).key + ".json") << "broken";
  EXPECT_EQ(removed(*s, rec(1000, 1).key), "");
  EXPECT_NE(content(cfg.dir + "/deletions.log").find("unknown"), std::string::npos);
}
