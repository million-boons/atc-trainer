#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "audio/PrerollRing.h"
#include "stt/SttCatalog.h"
#include "stt/SttService.h"
using namespace atc;
using namespace std::chrono_literals;

namespace {
std::atomic<int> alive{0}, maxAlive{0};
std::atomic<int> permits{1000};  // each finish() uses one, and waits for it

struct Fake : ISpeechRecognizer {
  std::string id;
  std::size_t n = 0;
  explicit Fake(std::string i) : id(std::move(i)) { maxAlive = std::max(maxAlive.load(), ++alive); }
  ~Fake() override { --alive; }
  std::string name() const override { return id; }
  void begin() override { n = 0; }
  void accept(std::span<const std::int16_t> a) override { n += a.size(); }
  SttResult finish() override {
    while (permits <= 0) std::this_thread::sleep_for(1ms);
    --permits;
    return {id + ":" + std::to_string(n), {}};
  }
};
SttModel model(const std::string& id, bool installed = true, int peak = 0) {
  SttModel m;
  m.id = m.name = id;
  m.engine = "vosk";
  m.installed = installed;
  m.peakMb = peak;
  return m;
}
struct Svc : ::testing::Test {
  std::mutex m;
  std::condition_variable cv;
  std::vector<std::string> results, configs;
  std::vector<SttState> states;
  long freeMb = 4000;
  std::unique_ptr<SttService> s;
  void SetUp() override {
    alive = maxAlive = 0;
    permits = 1000;
    s = std::make_unique<SttService>(
        std::vector<SttModel>{model("a"), model("b"), model("bad"), model("missing", false), model("big", true, 500), model("nodesc")},
        [](const SttModel& x, std::string& e) -> std::unique_ptr<ISpeechRecognizer> {
          if (x.id == "bad") return e = "broken file", nullptr;
          return std::make_unique<Fake>(x.id);
        },
        [](const SttModel& x) { return x.id == "nodesc" ? std::string() : "cfg-" + x.id; },
        SttService::Callbacks{[this](const SttStatus& st) {
                                std::lock_guard l(m);
                                states.push_back(st.state);
                                cv.notify_all();
                              },
                              [this](std::uint64_t tx, const std::string& id, const std::string& cfg, const SttResult& r, Ns d) {
                                std::lock_guard l(m);
                                configs.push_back(cfg);
                                results.push_back(std::to_string(tx) + "/" + id + "/" + r.text + (d > 0 ? "" : " BAD DELAY"));
                                cv.notify_all();
                              }},
        [this] { return freeMb; });
  }
  bool waitState(SttState st) {
    for (int i = 0; i < 400; ++i, std::this_thread::sleep_for(5ms))
      if (s->status().state == st) return true;
    return false;
  }
  bool waitResults(std::size_t n) {
    for (int i = 0; i < 400; ++i, std::this_thread::sleep_for(5ms)) {
      std::lock_guard l(m);
      if (results.size() >= n) return true;
    }
    return false;
  }
};
std::string writeFile(const std::string& name, const std::string& text) {
  const auto p = (std::filesystem::temp_directory_path() / ("atc_stt_" + name)).string();
  std::ofstream(p) << text;
  return p;
}
bool catalog(const std::string& json, std::vector<SttModel>& v, std::string& e) {
  return loadSttCatalog(writeFile("cat.json", json), "/root_dir", v, e);
}
}  // namespace

TEST_F(Svc, NothingLoadedRefusesTransmission) {
  EXPECT_FALSE(s->begin(1));
  s->accept({1, 2});
  s->finish(0);
  EXPECT_EQ(s->status().state, SttState::NotLoaded);
  s->load("a");  // the worker is still alive: nothing was passed to a missing engine
  EXPECT_TRUE(waitState(SttState::Ready));
}

TEST_F(Svc, LoadRunResult) {
  ASSERT_TRUE(s->load("a"));
  ASSERT_TRUE(waitState(SttState::Ready));
  ASSERT_TRUE(s->begin(7));
  s->accept({1, 2, 3});
  s->accept({4});
  s->finish(monotonicNow());
  ASSERT_TRUE(waitResults(1));
  EXPECT_EQ(results[0], "7/a/a:4");
  EXPECT_EQ(configs[0], "cfg-a");
  EXPECT_TRUE(waitState(SttState::Ready));
}

TEST_F(Svc, SwitchFreesTheOldModelFirst) {
  s->load("a");
  ASSERT_TRUE(waitState(SttState::Ready));
  ASSERT_TRUE(s->load("b"));
  for (int i = 0; i < 400 && !(s->status().state == SttState::Ready && s->status().modelId == "b"); ++i)
    std::this_thread::sleep_for(5ms);
  EXPECT_EQ(s->status().modelId, "b");
  EXPECT_EQ(maxAlive, 1);
  EXPECT_EQ(alive, 1);
}

TEST_F(Svc, FailureIsReportedAndRecoverable) {
  s->load("bad");
  ASSERT_TRUE(waitState(SttState::Failed));
  EXPECT_EQ(s->status().message, "broken file");
  EXPECT_FALSE(s->begin(1));
  s->load("a");
  EXPECT_TRUE(waitState(SttState::Ready));
}

TEST_F(Svc, RefusesUnknownAndNotInstalled) {
  EXPECT_FALSE(s->load("nope"));
  EXPECT_FALSE(s->load("missing"));
  EXPECT_NE(s->status().message.find("not installed"), std::string::npos);
}

TEST_F(Svc, RefusesWhenMemoryIsShort) {
  freeMb = 600;  // 500 + 200 margin needed
  s->load("big");
  ASSERT_TRUE(waitState(SttState::Failed));
  EXPECT_NE(s->status().message.find("not enough memory"), std::string::npos);
  EXPECT_EQ(alive, 0);
  freeMb = 700;
  s->load("big");
  EXPECT_TRUE(waitState(SttState::Ready));
}

TEST_F(Svc, NoSwitchDuringTransmission) {
  s->load("a");
  ASSERT_TRUE(waitState(SttState::Ready));
  s->begin(1);
  EXPECT_FALSE(s->load("b"));
  EXPECT_FALSE(s->unload());
  EXPECT_EQ(s->status().message, "finish the transmission first");
  s->cancel();
  EXPECT_TRUE(s->load("b"));
}

TEST_F(Svc, NoSwitchWhileRecognisingAndNextTransmissionIsKept) {
  s->load("a");
  ASSERT_TRUE(waitState(SttState::Ready));
  permits = 0;
  s->begin(1);
  s->accept({1});
  s->finish(monotonicNow());
  EXPECT_EQ(s->status().state, SttState::Busy);
  EXPECT_FALSE(s->load("b"));
  EXPECT_FALSE(s->unload());
  EXPECT_TRUE(s->begin(2)) << "a second call must not be lost while the first is recognised";
  s->accept({1, 2});
  permits = 1;
  ASSERT_TRUE(waitResults(1));
  permits = 1;
  s->finish(monotonicNow());
  ASSERT_TRUE(waitResults(2));
  EXPECT_EQ(results[0], "1/a/a:1");
  EXPECT_EQ(results[1], "2/a/a:2");
}

TEST_F(Svc, TwoQueuedTransmissionsBothHoldTheModel) {
  s->load("a");
  ASSERT_TRUE(waitState(SttState::Ready));
  permits = 0;
  for (std::uint64_t tx : {1, 2}) {
    ASSERT_TRUE(s->begin(tx));
    s->finish(monotonicNow());
  }
  permits = 1;
  ASSERT_TRUE(waitResults(1));
  std::this_thread::sleep_for(50ms);
  EXPECT_EQ(s->status().state, SttState::Busy);
  EXPECT_FALSE(s->load("b"));
  permits = 1;
  ASSERT_TRUE(waitResults(2));
  EXPECT_TRUE(waitState(SttState::Ready));
}

TEST_F(Svc, CancelledAudioDoesNotLeakIntoTheNext) {
  s->load("a");
  ASSERT_TRUE(waitState(SttState::Ready));
  s->begin(1);
  s->accept({1, 2, 3});
  s->cancel();
  s->accept({9});  // late block
  s->begin(2);
  s->accept({1});
  s->finish(monotonicNow());
  ASSERT_TRUE(waitResults(1));
  EXPECT_EQ(results[0], "2/a/a:1");
}

TEST_F(Svc, LockNeedsALoadedModel) {
  EXPECT_FALSE(s->lock());
  s->load("a");
  ASSERT_TRUE(waitState(SttState::Ready));
  s->begin(1);
  EXPECT_FALSE(s->lock());
  s->cancel();
  EXPECT_TRUE(s->lock());
}

TEST_F(Svc, LockedModelCannotChangeButStillRecognises) {
  s->load("a");
  ASSERT_TRUE(waitState(SttState::Ready));
  ASSERT_TRUE(s->lock());
  EXPECT_TRUE(s->status().locked);
  EXPECT_FALSE(s->load("b"));
  EXPECT_FALSE(s->load("a"));
  EXPECT_FALSE(s->unload());
  EXPECT_EQ(s->status().message, "model is locked for the exercise");
  ASSERT_TRUE(s->begin(9));
  EXPECT_FALSE(s->lock());  // a refused second lock must not release the first
  EXPECT_TRUE(s->status().locked);
  s->cancel();
  for (std::uint64_t tx : {1, 2}) {
    ASSERT_TRUE(s->begin(tx));
    s->finish(monotonicNow());
  }
  ASSERT_TRUE(waitResults(2));
  EXPECT_EQ(configs, (std::vector<std::string>{"cfg-a", "cfg-a"}));
  ASSERT_TRUE(waitState(SttState::Ready));
  EXPECT_TRUE(s->status().locked);
  s->unlock();
  EXPECT_TRUE(s->load("b"));
}

TEST_F(Svc, NoRecordNoLoad) {
  s->load("nodesc");
  ASSERT_TRUE(waitState(SttState::Failed));
  EXPECT_EQ(alive, 0);
}

TEST_F(Svc, FailedLoadLeavesNoOldConfig) {
  s->load("a");
  ASSERT_TRUE(waitState(SttState::Ready));
  s->load("bad");
  ASSERT_TRUE(waitState(SttState::Failed));
  EXPECT_EQ(s->status().modelConfig, "");
}

TEST(Describe, ChangesWhenAFileChanges) {
  SttModel m;
  m.id = "x";
  m.engine = "whisper";
  m.path = writeFile("m.bin", "one");
  m.promptFile = writeFile("p.txt", "prompt");
  const auto a = describeModel(m);
  EXPECT_NE(a.find("modelHash"), std::string::npos);
  EXPECT_EQ(describeModel(m), a);
  writeFile("p.txt", "prompt2");
  const auto b = describeModel(m);
  EXPECT_NE(b, a);
  m.audioCtx = 768;
  EXPECT_NE(describeModel(m), b);
  m.path = "/nope";
  EXPECT_EQ(describeModel(m), "");
}

TEST_F(Svc, UnloadFreesMemory) {
  s->load("a");
  ASSERT_TRUE(waitState(SttState::Ready));
  ASSERT_TRUE(s->unload());
  ASSERT_TRUE(waitState(SttState::NotLoaded));
  EXPECT_EQ(alive, 0);
  EXPECT_FALSE(s->begin(1));
}

TEST(SttCatalog, ReadsAndChecksFiles) {
  std::vector<SttModel> v;
  std::string e;
  ASSERT_TRUE(catalog(R"({"models":[{"id":"x","name":"X","engine":"vosk","path":"/"},
    {"id":"y","name":"Y","engine":"whisper","path":"gone.bin","threads":2}]})", v, e)) << e;
  EXPECT_TRUE(v[0].installed);
  EXPECT_FALSE(v[1].installed);
  EXPECT_EQ(v[1].path, "/root_dir/gone.bin");
  EXPECT_EQ(v[1].threads, 2);
}

TEST(SttCatalog, NamesTheProblem) {
  const std::string head = R"({"models":[{"id":"x","name":"X","engine":"vosk","path":"/"},)";
  for (const std::string bad : {R"({"id":"x","name":"X","engine":"vosk","path":"/"}]})", R"({"id":"y","name":"Y","engine":"cloud","path":"/"}]})",
                                R"({"id":"y","engine":"vosk","path":"/"}]})", R"({"id":"y","name":"Y","engine":"vosk","path":"/","threads":0}]})",
                                R"({"id":"y","name":"Y","engine":"vosk","path":"/","audioCtx":9999}]})", R"({"id":"y","name":"Y","engine":"vosk","path":""}]})",
                                R"({"id":"y","name":"Y","engine":"vosk","path":"/","threads":"two"}]})"}) {
    std::vector<SttModel> v;
    std::string e;
    EXPECT_FALSE(catalog(head + bad, v, e)) << bad;
    EXPECT_FALSE(e.empty());
    EXPECT_TRUE(v.empty());
  }
  std::vector<SttModel> v;
  std::string e;
  EXPECT_FALSE(catalog("not json", v, e));
  EXPECT_FALSE(catalog(R"({"models":[]})", v, e));
  EXPECT_FALSE(loadSttCatalog("/nope.json", "/", v, e));
}

TEST(SttCatalog, ShippedFileIsValid) {
  std::vector<SttModel> v;
  std::string e;
  ASSERT_TRUE(loadSttCatalog(ATC_ROOT "/config/stt_models.json", ATC_ROOT, v, e)) << e;
  EXPECT_GE(v.size(), 2u);
}

TEST(Ring, IndexReadsJoinWithoutRepeatOrSkip) {
  PrerollRing r(16000, 16000 * 4);
  std::vector<std::int16_t> b(160);
  std::int16_t v = 0;
  Ns t = ms(1000);
  std::int64_t from = -1;
  std::vector<std::int16_t> got;
  for (int i = 0; i < 100; ++i) {
    for (auto& x : b) x = v++;
    // jitter moves the time mapping between reads
    r.push(t + ms(i % 7 == 3 ? 0 : 4), b);
    t += ms(10);
    if (i == 20) from = r.indexAt(r.newestTime() - ms(50));
    if (from >= 0) {
      const auto c = r.extractIndex(from + std::int64_t(got.size()), r.newestIndex());
      got.insert(got.end(), c.samples.begin(), c.samples.end());
    }
  }
  ASSERT_GT(got.size(), 12000u);
  for (std::size_t i = 1; i < got.size(); ++i) ASSERT_EQ(std::int16_t(got[i] - got[i - 1]), 1) << i;
}
