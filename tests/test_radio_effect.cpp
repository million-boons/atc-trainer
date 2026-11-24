#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "audio/RadioAudioProcessor.h"
using namespace atc;

namespace {
constexpr int kRate = 16000;
using Pcm = std::vector<std::int16_t>;

Pcm sine(double hz, double dbfs, int millis) {
  Pcm v(std::size_t(kRate * millis / 1000));
  for (std::size_t i = 0; i < v.size(); ++i)
    v[i] = std::int16_t(std::lround(32767 * std::pow(10, dbfs / 20) * std::sin(2 * M_PI * hz * double(i) / kRate)));
  return v;
}
double rmsDb(std::span<const std::int16_t> v) {
  double p = 0;
  for (const double x : v) p += x * x;
  return 10 * std::log10(p / double(v.size()) / (32768.0 * 32768.0) + 1e-12);
}
int peak(const Pcm& v) {
  int p = 0;
  for (const int x : v) p = std::max(p, std::abs(x));
  return p;
}
std::size_t ms16(int m) { return std::size_t(m * 16); }
// Band and levels only: no noise, clip or squelch.
RadioEffectConfig plain() {
  RadioEffectConfig c;
  c.matchLevel = false;
  c.ratio = 1; c.makeupDb = 0; c.drive = 0; c.hissDbfs = -200; c.clickDbfs = -200; c.clickMs = c.tailMs = 0;
  return c;
}
double gainDb(double hz, const RadioEffectConfig& c) {
  const Pcm in = sine(hz, -12, 1000), out = applyRadioEffect(in, kRate, c, 1);
  const std::span<const std::int16_t> a(in), b(out);
  return rmsDb(b.subspan(4000, 8000)) - rmsDb(a.subspan(4000, 8000));
}
const RadioIntensity kAll[] = {RadioIntensity::Off, RadioIntensity::Light, RadioIntensity::Medium, RadioIntensity::Heavy};
}  // namespace

TEST(RadioEffect, NamesRoundTrip) {
  for (auto i : kAll) EXPECT_EQ(radioIntensityFromString(toString(i)), i);
  EXPECT_FALSE(radioIntensityFromString("loud"));
  EXPECT_FALSE(radioIntensityFromString(""));
}

TEST(RadioEffect, OffChangesNothing) {
  const Pcm in = sine(700, -6, 300);
  EXPECT_EQ(applyRadioEffect(in, kRate, RadioIntensity::Off, 1), in);
}

TEST(RadioEffect, LightIsTheTableInThePlan) {
  const auto c = radioEffect(RadioIntensity::Light);
  EXPECT_EQ(c.lowHz, 300);
  EXPECT_EQ(c.highHz, 3000);
  EXPECT_EQ(c.ratio, 3);
  EXPECT_EQ(c.hissDbfs, -40);
  EXPECT_EQ(c.tailMs, 60);
}

TEST(RadioEffect, PassesTheVoiceBandAndNothingElse) {
  const auto c = plain();
  for (double hz : {600.0, 1000.0, 1800.0}) EXPECT_NEAR(gainDb(hz, c), 0, 1) << hz;
  for (double hz : {300.0, 3000.0}) EXPECT_NEAR(gainDb(hz, c), -3, 0.7) << hz;  // the corners
  EXPECT_LT(gainDb(100, c), -30);
  EXPECT_LT(gainDb(7000, c), -30);
}

TEST(RadioEffect, CompressorNarrowsLoudAndQuiet) {
  auto c = plain();
  c.ratio = 3;
  const auto level = [&](double db) {
    const Pcm out = applyRadioEffect(sine(1000, db, 1000), kRate, c, 1);
    return rmsDb(std::span<const std::int16_t>(out).subspan(8000, 6000));
  };
  EXPECT_NEAR(level(-30) - level(-40), 10, 0.5);  // below the threshold: untouched
  const double loud = level(-4) - level(-14);
  EXPECT_LT(loud, 6);
  EXPECT_GT(loud, 2);
}

TEST(RadioEffect, WholeSpeechBetweenClickAndTail) {
  const Pcm in = sine(1000, -12, 500);
  for (auto i : {RadioIntensity::Light, RadioIntensity::Medium, RadioIntensity::Heavy}) {
    const auto c = radioEffect(i);
    const Pcm out = applyRadioEffect(in, kRate, i, 1);
    ASSERT_EQ(out.size(), in.size() + ms16(c.clickMs) + ms16(c.tailMs)) << toString(i);
    const std::span<const std::int16_t> o(out);
    // Speech from its first to its last 20 ms, and only hiss level after it.
    EXPECT_GT(rmsDb(o.subspan(ms16(c.clickMs) + ms16(10), ms16(20))), -25);
    EXPECT_GT(rmsDb(o.subspan(ms16(c.clickMs) + in.size() - ms16(20), ms16(20))), -25);
    EXPECT_LT(rmsDb(o.subspan(ms16(c.clickMs) + in.size() + ms16(15), ms16(25))), -25);
    EXPECT_EQ(out.front(), 0);
    EXPECT_EQ(out.back(), 0);
  }
}

TEST(RadioEffect, EveryIntensityIsAsLoudAsTheDryVoice) {
  for (double db : {-21.0, -18.0, -12.0}) {
    const Pcm in = sine(1000, db, 1000);
    for (auto i : {RadioIntensity::Light, RadioIntensity::Medium, RadioIntensity::Heavy}) {
      const Pcm out = applyRadioEffect(in, kRate, i, 1);
      EXPECT_NEAR(rmsDb(std::span<const std::int16_t>(out).subspan(4000, 8000)), rmsDb(in), 1.5) << toString(i) << " " << db;
    }
  }
}

TEST(RadioEffect, HissLevelAndOrder) {
  const Pcm quiet(std::size_t(kRate), 0);  // a pause inside the transmission
  double last = -100;
  for (auto i : {RadioIntensity::Light, RadioIntensity::Medium, RadioIntensity::Heavy}) {
    const Pcm out = applyRadioEffect(quiet, kRate, i, 1);
    const double h = rmsDb(std::span<const std::int16_t>(out).subspan(4000, 8000));
    EXPECT_NEAR(h, radioEffect(i).hissDbfs, 1) << toString(i);
    EXPECT_GT(h, last);
    last = h;
  }
}

TEST(RadioEffect, NeverAboveTheCeilingAndNeverWraps) {
  Pcm square(std::size_t(kRate / 2));
  for (std::size_t i = 0; i < square.size(); ++i) square[i] = (i / 8) % 2 ? 32767 : -32768;
  for (auto i : {RadioIntensity::Light, RadioIntensity::Medium, RadioIntensity::Heavy})
    for (const Pcm& in : {square, sine(1000, 0, 500), Pcm(8000, 32767), Pcm(8000, -32768)})
      EXPECT_LE(peak(applyRadioEffect(in, kRate, i, 1)), int(std::ceil(32767 * std::pow(10, -1 / 20.0)))) << toString(i);
}

TEST(RadioEffect, RepeatableBySeed) {
  const Pcm in = sine(900, -10, 400);
  EXPECT_EQ(applyRadioEffect(in, kRate, RadioIntensity::Medium, 7), applyRadioEffect(in, kRate, RadioIntensity::Medium, 7));
  EXPECT_NE(applyRadioEffect(in, kRate, RadioIntensity::Medium, 7), applyRadioEffect(in, kRate, RadioIntensity::Medium, 8));
}

TEST(RadioEffect, OddInputs) {
  EXPECT_TRUE(applyRadioEffect({}, kRate, RadioIntensity::Heavy, 1).empty());
  const Pcm one{1000};
  EXPECT_EQ(applyRadioEffect(one, kRate, RadioIntensity::Light, 1).size(), 1 + ms16(12) + ms16(60));
  EXPECT_EQ(applyRadioEffect(one, 0, RadioIntensity::Light, 1), one);
  for (int rate : {8000, 22050, 48000}) EXPECT_GT(applyRadioEffect(Pcm(std::size_t(rate / 4), 900), rate, RadioIntensity::Heavy, 1).size(), std::size_t(rate / 4));
}

// Very quiet speech is not drowned by making it louder than it was, and hiss keeps its own level.
TEST(RadioEffect, HissDoesNotFollowTheSpeechLevel) {
  Pcm in = sine(1000, -12, 500);
  in.resize(in.size() + 8000, 0);  // then a pause
  const Pcm out = applyRadioEffect(in, kRate, RadioIntensity::Light, 1);
  EXPECT_NEAR(rmsDb(std::span<const std::int16_t>(out).subspan(8000 + ms16(150), ms16(300))), -40, 1.5);
}
