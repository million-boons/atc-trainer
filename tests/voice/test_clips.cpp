#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

#include "core/PathResolver.h"
#include "voice/ClipList.h"
#include "voice/ClipTrim.h"
using namespace atc;
namespace fs = std::filesystem;

namespace {
using Samples = std::vector<std::int16_t>;

Samples tone(int rate, double hz, int milliseconds, double amplitude) {
  Samples s(static_cast<std::size_t>(rate) * static_cast<std::size_t>(milliseconds) / 1000);
  for (std::size_t i = 0; i < s.size(); ++i)
    s[i] = static_cast<std::int16_t>(std::lround(amplitude * std::sin(2 * std::numbers::pi * hz * static_cast<double>(i) / rate)));
  return s;
}
Samples join(std::initializer_list<Samples> parts) {
  Samples r;
  for (const auto& p : parts) r.insert(r.end(), p.begin(), p.end());
  return r;
}
Samples silence(int rate, int milliseconds) { return tone(rate, 1, milliseconds, 0); }
double rms(const Samples& s, std::size_t a, std::size_t b) {
  double sum = 0;
  for (std::size_t i = a; i < b; ++i) sum += static_cast<double>(s[i]) * s[i];
  return std::sqrt(sum / static_cast<double>(b - a));
}
bool has(const TrimResult& r, const std::string& w) {
  return std::any_of(r.warnings.begin(), r.warnings.end(), [&](const std::string& x) { return x.find(w) != std::string::npos; });
}

SpokenForms forms() {
  Paths p;
  std::string error;
  SpokenForms f;
  EXPECT_TRUE(resolvePaths(systemPathInputs(), p, error)) << error;
  EXPECT_TRUE(loadSpokenForms(p.data + "/phraseology", f, error)) << error;
  return f;
}
}  // namespace

TEST(ClipTrim, CutsSilenceAndKeepsThePads) {
  const Samples raw = join({silence(16000, 500), tone(16000, 400, 600, 8000), silence(16000, 700)});
  const Samples before = raw;
  TrimResult r;
  std::string error;
  ASSERT_TRUE(trimClip(raw, 16000, {}, r, error)) << error;
  EXPECT_EQ(raw, before);
  EXPECT_EQ(r.start, 16000u * 480 / 1000);  // 20 ms before
  EXPECT_EQ(r.end, 16000u * 1160 / 1000);   // 60 ms after
  EXPECT_EQ(r.samples.size(), r.end - r.start);
  EXPECT_EQ(r.samples.front(), 0);
  EXPECT_EQ(r.samples.back(), 0);
  EXPECT_TRUE(r.warnings.empty());
}
TEST(ClipTrim, LevelsToTheTargetAndNeverAboveThePeakLimit) {
  TrimResult r;
  std::string error;
  for (const double amplitude : {1000.0, 8000.0, 30000.0}) {
    ASSERT_TRUE(trimClip(join({silence(16000, 200), tone(16000, 400, 600, amplitude), silence(16000, 200)}), 16000, {}, r, error));
    EXPECT_NEAR(rms(r.samples, 3200, 6400), 32768 * std::pow(10.0, -20 / 20.0), 60) << amplitude;
  }
  // A loud spike in quiet speech limits the gain.
  Samples raw = join({silence(16000, 200), tone(16000, 400, 600, 500), silence(16000, 200)});
  raw[6400] = 30000;
  ASSERT_TRUE(trimClip(raw, 16000, {}, r, error));
  int peak = 0;
  for (const int x : r.samples) peak = std::max(peak, std::abs(x));
  EXPECT_LE(peak, static_cast<int>(32768 * std::pow(10.0, -1 / 20.0)) + 1);
}
// "Falcon": a faint f, far below the threshold, before the loud part.
TEST(ClipTrim, AFaintStartNeedsALongerPad) {
  const Samples raw = join({silence(16000, 100), tone(16000, 3000, 60, 60), tone(16000, 400, 500, 8000), silence(16000, 200)});
  TrimConfig cfg;
  TrimResult r;
  std::string error;
  ASSERT_TRUE(trimClip(raw, 16000, cfg, r, error));
  EXPECT_EQ(r.start, 16000u * 140 / 1000);  // the faint part is mostly cut
  cfg.padBeforeMs = 80;
  ASSERT_TRUE(trimClip(raw, 16000, cfg, r, error));
  EXPECT_EQ(r.start, 16000u * 80 / 1000);  // all of it kept
  cfg.padBeforeMs = 1000;
  ASSERT_TRUE(trimClip(raw, 16000, cfg, r, error));
  EXPECT_EQ(r.start, 0u);
}
TEST(ClipTrim, AClickIsNotSpeech) {
  Samples raw = silence(16000, 1000);
  for (int i = 0; i < 160; ++i) raw[static_cast<std::size_t>(1600 + i)] = 20000;  // one 10 ms frame
  TrimResult r;
  std::string error;
  EXPECT_FALSE(trimClip(raw, 16000, {}, r, error));
  EXPECT_FALSE(error.empty());
  EXPECT_FALSE(trimClip({}, 16000, {}, r, error));
  EXPECT_FALSE(trimClip(raw, 0, {}, r, error));
}
TEST(ClipTrim, APauseBetweenWordsIsKept) {
  const Samples raw = join({silence(16000, 300), tone(16000, 400, 300, 8000), silence(16000, 400), tone(16000, 500, 300, 8000), silence(16000, 300)});
  TrimResult r;
  std::string error;
  ASSERT_TRUE(trimClip(raw, 16000, {}, r, error));
  EXPECT_EQ(r.samples.size(), 16000u * 1080 / 1000);
  EXPECT_NEAR(rms(r.samples, 1600, 4800), 3277, 60);  // the pause does not raise the gain
}
TEST(ClipTrim, WarnsAboutCutQuietShortAndClippedTakes) {
  TrimResult r;
  std::string error;
  ASSERT_TRUE(trimClip(tone(16000, 400, 500, 8000), 16000, {}, r, error));
  EXPECT_TRUE(has(r, "very start"));
  EXPECT_TRUE(has(r, "very end"));
  ASSERT_TRUE(trimClip(join({silence(16000, 200), tone(16000, 400, 60, 8000), silence(16000, 200)}), 16000, {}, r, error));
  EXPECT_TRUE(has(r, "short"));
  ASSERT_TRUE(trimClip(join({silence(16000, 200), tone(16000, 400, 500, 250), silence(16000, 200)}), 16000, {}, r, error));
  EXPECT_TRUE(has(r, "quiet"));
  Samples flat = tone(16000, 400, 500, 60000.0 / 2);
  for (auto& x : flat) x = static_cast<std::int16_t>(std::clamp(static_cast<int>(x) * 4, -32768, 32767));
  ASSERT_TRUE(trimClip(join({silence(16000, 200), flat, silence(16000, 200)}), 16000, {}, r, error));
  EXPECT_TRUE(has(r, "clipped"));
}

TEST(Resample, KeepsSpeechAndRemovesWhatDoesNotFit) {
  const Samples speech = resample(tone(22050, 1000, 1000, 10000), 22050, 16000);
  EXPECT_EQ(speech.size(), 16000u);
  EXPECT_NEAR(rms(speech, 2000, 14000), 10000 / std::sqrt(2.0), 100);
  // Same tone made directly at 16 kHz.
  const Samples direct = tone(16000, 1000, 1000, 10000);
  for (std::size_t i = 2000; i < 14000; ++i) ASSERT_NEAR(speech[i], direct[i], 100) << i;
  EXPECT_LT(rms(resample(tone(22050, 10000, 1000, 10000), 22050, 16000), 2000, 14000), 100);  // would alias
  EXPECT_EQ(resample(direct, 16000, 16000), direct);
  EXPECT_TRUE(resample(direct, 0, 16000).empty());
  EXPECT_TRUE(resample({}, 22050, 16000).empty());
}

TEST(ClipList, ValuesAreSaidFromTheData) {
  Airfield a;
  Airfield::Callsign c;
  c.id = "EAGLE-421";
  c.spoken = "eagle four two one";
  a.callsigns = {c};
  const SpokenForms f = forms();
  std::string error;
  EXPECT_EQ(spokenValue(a, f, "runways", "09", error), "zero niner");
  EXPECT_EQ(spokenValue(a, f, "runways", "27L", error), "two seven left");
  EXPECT_EQ(spokenValue(a, f, "taxiways", "A3", error), "alpha three");
  EXPECT_EQ(spokenValue(a, f, "taxiways", "L", error), "lima");
  EXPECT_EQ(spokenValue(a, f, "frequencies", "121.7", error), "one two one decimal seven");  // D-430
  EXPECT_EQ(spokenValue(a, f, "callsigns", "EAGLE-421", error), "eagle four two one");
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(spokenValue(a, f, "callsigns", "HAWK-999", error), "");
  EXPECT_FALSE(error.empty());
  error.clear();
  EXPECT_EQ(spokenValue(a, f, "runways", "0#", error), "");
  EXPECT_FALSE(error.empty());
}
TEST(ClipList, EveryRequiredClipOfTheShippedScenarioHasWords) {
  Paths p;
  std::string error;
  ASSERT_TRUE(resolvePaths(systemPathInputs(), p, error)) << error;
  Exercise e;
  std::vector<LoadError> load;
  ASSERT_TRUE(loadExercise(scenarioSources(p.data, p.data + "/scenarios/DEPARTURE-01.json"), e, load));
  std::map<std::string, RequiredClip> need;
  std::vector<std::string> bad;
  ASSERT_TRUE(requiredClips(e, need, bad));
  std::vector<ClipLine> lines;
  ASSERT_TRUE(clipList(e, forms(), lines, bad));
  ASSERT_EQ(lines.size(), need.size());
  for (const auto& l : lines) {
    EXPECT_EQ(need.count(l.id), 1u) << l.id;
    EXPECT_FALSE(l.words.empty()) << l.id;
    EXPECT_EQ(l.words.find_first_of("0123456789{}"), std::string::npos) << l.words;
    EXPECT_EQ(l.prompt().back(), l.id.ends_with("@final") ? '.' : ',') << l.id;
  }
}
TEST(ClipList, IncompletePhraseologyIsRefused) {
  const fs::path dir = fs::temp_directory_path() / ("atc_clips_" + std::to_string(getpid()));
  fs::create_directories(dir);
  SpokenForms f;
  std::string error;
  EXPECT_FALSE(loadSpokenForms(dir.string(), f, error));
  std::ofstream(dir / "a.json") << R"({"digits": {"0": ["zero"]}, "letters": {}})";
  error.clear();
  EXPECT_FALSE(loadSpokenForms(dir.string(), f, error));
  EXPECT_NE(error.find("incomplete"), std::string::npos);
  std::error_code ec;
  fs::remove_all(dir, ec);
}
