#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "audio/UtteranceChecks.h"
using namespace atc;

namespace {
// 16 kHz tone between [fromMs, toMs) of a clip, silence elsewhere.
std::vector<std::int16_t> clip(int lenMs, int fromMs, int toMs, double amp = 8000) {
  std::vector<std::int16_t> v(static_cast<std::size_t>(lenMs * 16), 0);
  for (int i = fromMs * 16; i < toMs * 16; ++i)
    v[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(amp * std::sin(i * 0.2));
  return v;
}
constexpr int kPre = 300 * 16;
}  // namespace

TEST(Checks, GoodUtterance) {
  auto r = checkUtterance(clip(2500, 500, 2100), kPre);
  EXPECT_FALSE(r.empty || r.overload || r.spokeBeforeKeying || r.releasedEarly);
  ASSERT_TRUE(r.keyToSpeechMs);
  EXPECT_EQ(*r.keyToSpeechMs, 200);
}
TEST(Checks, Silence) {
  auto r = checkUtterance(clip(1500, 0, 0), kPre);
  EXPECT_TRUE(r.empty);
  EXPECT_FALSE(r.keyToSpeechMs);
}
TEST(Checks, SpeechOnlyInPreRollIsEmpty) {
  auto r = checkUtterance(clip(1500, 0, 120), kPre);
  EXPECT_TRUE(r.empty);
  EXPECT_FALSE(r.spokeBeforeKeying);
}
TEST(Checks, SpokeBeforeKeying) {
  auto r = checkUtterance(clip(2500, 100, 2000), kPre);
  EXPECT_TRUE(r.spokeBeforeKeying);
  EXPECT_FALSE(r.keyToSpeechMs);
}
TEST(Checks, EarlierNoiseIsNotSpokeBeforeKeying) {
  auto v = clip(2500, 600, 2000);
  auto n = clip(2500, 20, 100);
  for (std::size_t i = 0; i < v.size(); ++i) v[i] = static_cast<std::int16_t>(v[i] + n[i]);
  auto r = checkUtterance(v, kPre);
  EXPECT_FALSE(r.spokeBeforeKeying);
  EXPECT_EQ(r.keyToSpeechMs.value_or(-1), 300);
}
TEST(Checks, ReleasedEarly) { EXPECT_TRUE(checkUtterance(clip(2000, 500, 2000), kPre).releasedEarly); }
TEST(Checks, Overload) {
  auto v = clip(2000, 500, 1500);
  for (int i = 0; i < 8; ++i) v[static_cast<std::size_t>(16000 + i)] = 32767;
  EXPECT_TRUE(checkUtterance(v, kPre).overload);
}
TEST(Checks, ShorterThanOneFrame) {
  std::vector<std::int16_t> v(10, 0);
  EXPECT_TRUE(checkUtterance(v, kPre).empty);
}
