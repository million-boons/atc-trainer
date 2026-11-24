#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <random>
#include <string>
#include <vector>

#include "audio/WavFile.h"
#include "engine/SequenceEngine.h"
#include "voice/PhraseAssembler.h"
#include "voice/PhraseTemplate.h"
#include "voice/VoiceBank.h"
using namespace atc;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

Exercise exercise() {
  Exercise e;
  e.scenario.id = "TEST";
  e.scenario.generators["callsign"] = {"callsigns", ""};
  e.scenario.generators["runway"] = {"runways", ""};
  e.airfield.runways = {"09", "27"};
  Airfield::Callsign c;
  c.id = "EAGLE-421";
  c.spoken = "eagle four two one";
  e.airfield.callsigns = {c};
  PilotReply r;
  r.id = "LINE_UP";
  r.text = "line up runway {runway}, {callsign}";
  e.replies[r.id] = r;
  r.id = "AGAIN";
  r.text = "say again, {callsign}";
  e.replies[r.id] = r;
  ScenarioStep s;
  s.id = "S";
  s.onEnter = StepOnEnter{"LINE_UP", 0};
  e.scenario.steps = {s};
  e.scenario.initialStep = "S";
  return e;
}

// A voice folder in which every required clip is a short constant tone.
struct Voice : ::testing::Test {
  fs::path dir;
  json manifest;
  std::vector<LoadError> errors;
  VoiceBank bank;

  void SetUp() override {
    static int n = 0;
    dir = fs::temp_directory_path() / ("atc_voice_" + std::to_string(getpid()) + "_" + std::to_string(n++));
    fs::create_directories(dir);
    manifest = {{"schemaVersion", 1}, {"voice", "test"}, {"sampleRate", 16000}, {"placeholder", true}, {"clips", json::array()}};
    std::map<std::string, RequiredClip> need;
    std::vector<std::string> bad;
    ASSERT_TRUE(requiredClips(exercise(), need, bad));
    for (const auto& [id, c] : need) add(id, 1600);
  }
  void TearDown() override {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  void add(const std::string& id, std::size_t samples) {
    const std::string file = "c" + std::to_string(manifest["clips"].size()) + ".wav";
    ASSERT_TRUE(writeWav((dir / file).string(), 16000, std::vector<std::int16_t>(samples, 1000)));
    json take = {{"file", file}, {"take", "primary"}};
    json clip = {{"id", id}, {"takes", json::array({take})}};
    manifest["clips"].push_back(clip);
  }
  bool load() {
    std::ofstream(dir / "manifest.json") << manifest.dump();
    return bank.load(dir.string(), errors);
  }
  std::string all() const {
    std::string s;
    for (const auto& e : errors) s += toString(e) + "\n";
    return s;
  }
};

std::vector<PhraseSegment> parse(const std::string& t) {
  std::vector<PhraseSegment> s;
  std::string error;
  EXPECT_TRUE(parsePhraseTemplate(t, s, error)) << error;
  return s;
}
}  // namespace

TEST(PhraseTemplate, SplitsWordsSlotsAndCommas) {
  const auto s = parse("line up runway {runway}, {callsign}");
  ASSERT_EQ(s.size(), 3u);
  EXPECT_FALSE(s[0].dynamic);
  EXPECT_EQ(s[0].text, "line up runway");
  EXPECT_EQ(s[0].position, ClipPosition::Initial);
  EXPECT_TRUE(s[1].dynamic);
  EXPECT_EQ(s[1].text, "runway");
  EXPECT_EQ(s[1].position, ClipPosition::Medial);
  EXPECT_TRUE(s[1].commaAfter);
  EXPECT_EQ(s[2].position, ClipPosition::Final);
  EXPECT_FALSE(s[2].commaAfter);
}
TEST(PhraseTemplate, WordsAreSplitAtPunctuation) {
  const auto s = parse("roger, standing by.");
  ASSERT_EQ(s.size(), 2u);
  EXPECT_EQ(s[0].text, "roger");
  EXPECT_TRUE(s[0].commaAfter);
  EXPECT_EQ(s[1].text, "standing by");
  EXPECT_FALSE(s[1].commaAfter);
}
TEST(PhraseTemplate, ASingleSegmentIsFinal) { EXPECT_EQ(parse("say again")[0].position, ClipPosition::Final); }
TEST(PhraseTemplate, BadTextIsRefused) {
  std::vector<PhraseSegment> s;
  std::string error;
  for (const char* bad : {"", "  ", ",", ", roger", "{", "{}", "roger {run way}", "- -"}) {
    error.clear();
    EXPECT_FALSE(parsePhraseTemplate(bad, s, error)) << bad;
    EXPECT_FALSE(error.empty()) << bad;
  }
}
TEST(PhraseTemplate, ClipIdsComeFromTheText) {
  EXPECT_EQ(phraseClipId("line up  runway", ClipPosition::Initial), "PH_LINE_UP_RUNWAY@initial");
  EXPECT_EQ(valueClipId("runways", "09", ClipPosition::Final), "RUNWAY_09@final");
  EXPECT_EQ(valueClipId("callsigns", "EAGLE-421", ClipPosition::Initial), "CALLSIGN_EAGLE_421@initial");
}
TEST(PhraseTemplate, LintFindsValuesTypedAsWords) {
  const Exercise e = exercise();
  EXPECT_TRUE(lintPhrase(parse("line up runway {runway}, {callsign}"), e.airfield).empty());
  EXPECT_EQ(lintPhrase(parse("line up runway 09"), e.airfield).size(), 1u);
  EXPECT_EQ(lintPhrase(parse("roger, Eagle Four Two One"), e.airfield).size(), 1u);
}
TEST(PhraseTemplate, RequiredClipsCoverEveryPoolValueAndShareEqualOnes) {
  std::map<std::string, RequiredClip> need;
  std::vector<std::string> bad;
  ASSERT_TRUE(requiredClips(exercise(), need, bad));
  std::vector<std::string> ids;
  for (const auto& [id, c] : need) ids.push_back(id);
  EXPECT_EQ(ids, (std::vector<std::string>{"CALLSIGN_EAGLE_421@final", "PH_LINE_UP_RUNWAY@initial", "PH_SAY_AGAIN@initial",
                                           "RUNWAY_09@medial", "RUNWAY_27@medial"}));
}
TEST(PhraseTemplate, EngineFillsTheClipsOfEveryUtterance) {
  auto e = std::make_shared<Exercise>(exercise());
  SequenceEngine fresh(e, 1);
  std::uint64_t generation = 0;
  for (const auto& o : fresh.handle(0, in::Started{}))
    if (const auto* t = std::get_if<out::TimerSet>(&o))
      if (t->timer == in::Timer::OnEnter) generation = t->generation;
  std::vector<std::string> clips;
  for (const auto& o : fresh.handle(1, in::TimerFired{in::Timer::OnEnter, generation}))
    if (const auto* p = std::get_if<out::PilotRequested>(&o)) clips = p->utterance.clipIds;
  ASSERT_EQ(clips.size(), 4u);
  EXPECT_EQ(clips[0], "PH_LINE_UP_RUNWAY@initial");
  EXPECT_EQ(clips[2], kCommaPause);
  EXPECT_EQ(clips[3], "CALLSIGN_EAGLE_421@final");
}

TEST_F(Voice, LoadsEveryClip) {
  ASSERT_TRUE(load()) << all();
  EXPECT_EQ(bank.clipCount(), 5u);
  EXPECT_EQ(bank.bytes(), 5u * 1600 * 2);
  EXPECT_TRUE(bank.placeholder());
  EXPECT_TRUE(checkCoverage(exercise(), bank, errors)) << all();
}
TEST_F(Voice, RemovingAnyOneClipIsFoundAndNamed) {
  const json full = manifest;
  for (std::size_t i = 0; i < full["clips"].size(); ++i) {
    manifest = full;
    const std::string id = full["clips"][i]["id"].get<std::string>();
    manifest["clips"].erase(i);
    ASSERT_TRUE(load()) << all();
    EXPECT_FALSE(checkCoverage(exercise(), bank, errors));
    ASSERT_EQ(errors.size(), 1u) << all();
    EXPECT_NE(errors[0].what.find(id), std::string::npos) << errors[0].what;
  }
}
TEST_F(Voice, AClipWithOnlyRejectedTakesIsMissing) {
  manifest["clips"][0]["takes"][0]["take"] = "rejected";
  ASSERT_TRUE(load()) << all();
  EXPECT_FALSE(checkCoverage(exercise(), bank, errors));
}
TEST_F(Voice, MissingFileIsRefused) {
  fs::remove(dir / "c0.wav");
  EXPECT_FALSE(load());
  EXPECT_NE(all().find("/clips/0/takes/0"), std::string::npos) << all();
}
TEST_F(Voice, FileOutsideTheFolderIsRefused) {
  for (const char* f : {"../x.wav", "/etc/x.wav", "a/../../x.wav", ""}) {
    manifest["clips"][0]["takes"][0]["file"] = f;
    EXPECT_FALSE(load()) << f;
    EXPECT_NE(all().find("inside the voice folder"), std::string::npos) << all();
  }
}
TEST_F(Voice, WrongRateIsRefused) {
  ASSERT_TRUE(writeWav((dir / "c0.wav").string(), 22050, std::vector<std::int16_t>(100, 1)));
  EXPECT_FALSE(load());
  EXPECT_NE(all().find("16 kHz"), std::string::npos) << all();
}
TEST_F(Voice, AFailedLoadKeepsTheOldBank) {
  ASSERT_TRUE(load()) << all();
  manifest["sampleRate"] = 8000;
  EXPECT_FALSE(load());
  EXPECT_EQ(bank.clipCount(), 5u);
}
TEST_F(Voice, NoManifestIsAnError) {
  EXPECT_FALSE(bank.load((dir / "none").string(), errors));
  EXPECT_FALSE(checkCoverage(exercise(), bank, errors));  // an empty bank covers nothing
}

TEST_F(Voice, AssemblesWithGapsOfTheRightLength) {
  ASSERT_TRUE(load()) << all();
  PhraseAssembler a(bank);
  std::mt19937_64 rng(1);
  std::vector<std::int16_t> pcm;
  std::string error;
  ASSERT_TRUE(a.assemble({"PH_LINE_UP_RUNWAY@initial", "RUNWAY_09@medial", std::string(kCommaPause), "CALLSIGN_EAGLE_421@final"}, rng, pcm, error)) << error;
  EXPECT_EQ(pcm.size(), 3u * 1600 + 960 + 2880);  // 60 ms and 180 ms
  EXPECT_EQ(speechLength(pcm.size()), ms(540));
  // Silent between the clips, full level inside them, and no click at any edge.
  EXPECT_EQ(pcm[1600 + 480], 0);
  EXPECT_EQ(pcm[800], 1000);
  EXPECT_EQ(pcm.front(), 0);
  EXPECT_EQ(pcm.back(), 0);
  for (std::size_t i = 1; i < pcm.size(); ++i) EXPECT_LE(std::abs(pcm[i] - pcm[i - 1]), 16) << i;
}
TEST_F(Voice, NoGapMeansACrossfade) {
  ASSERT_TRUE(load()) << all();
  AssemblerConfig cfg;
  cfg.wordGapMs = 0;
  PhraseAssembler a(bank, cfg);
  std::mt19937_64 rng(1);
  std::vector<std::int16_t> pcm;
  std::string error;
  ASSERT_TRUE(a.assemble({"PH_SAY_AGAIN@initial", "CALLSIGN_EAGLE_421@final"}, rng, pcm, error));
  EXPECT_EQ(pcm.size(), 2u * 1600 - 128);
  for (std::size_t i = 1500; i < 1700; ++i) EXPECT_GE(pcm[i], 900) << i;  // no hole
}
TEST_F(Voice, AMissingClipIsNamedAndNothingIsSaid) {
  ASSERT_TRUE(load()) << all();
  PhraseAssembler a(bank);
  std::mt19937_64 rng(1);
  std::vector<std::int16_t> pcm{1, 2, 3};
  std::string error;
  EXPECT_FALSE(a.assemble({"PH_SAY_AGAIN@initial", "RUNWAY_36@final"}, rng, pcm, error));
  EXPECT_EQ(error, "missing clip RUNWAY_36@final");
  EXPECT_EQ(pcm.size(), 3u);
  EXPECT_FALSE(a.assemble({}, rng, pcm, error));
}
TEST_F(Voice, TakesArePickedByTheSeed) {
  json alternate = {{"file", "alt.wav"}, {"take", "alternate"}};
  ASSERT_TRUE(writeWav((dir / "alt.wav").string(), 16000, std::vector<std::int16_t>(3200, 1000)));
  for (auto& c : manifest["clips"])
    if (c["id"].get<std::string>() == "PH_SAY_AGAIN@initial") c["takes"].push_back(alternate);
  ASSERT_TRUE(load()) << all();
  PhraseAssembler a(bank);
  const auto lengths = [&](std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<std::size_t> v;
    for (int i = 0; i < 20; ++i) {
      std::vector<std::int16_t> pcm;
      std::string error;
      EXPECT_TRUE(a.assemble({"PH_SAY_AGAIN@initial"}, rng, pcm, error)) << error;
      v.push_back(pcm.size());
    }
    return v;
  };
  const auto v = lengths(5);
  EXPECT_EQ(v, lengths(5));
  EXPECT_NE(std::count(v.begin(), v.end(), 1600u), 0);
  EXPECT_NE(std::count(v.begin(), v.end(), 3200u), 0);
}
TEST_F(Voice, VeryShortClipsAreSafe) {
  add("TINY@final", 1);
  ASSERT_TRUE(load()) << all();
  PhraseAssembler a(bank);
  std::mt19937_64 rng(1);
  std::vector<std::int16_t> pcm;
  std::string error;
  EXPECT_TRUE(a.assemble({"TINY@final", "TINY@final"}, rng, pcm, error));
}
