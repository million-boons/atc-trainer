#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>

#include "audio/SideTone.h"
using namespace atc;

TEST(SideTone, LengthPeakAndCleanEdges) {
  const auto a = makeSideTone();
  ASSERT_EQ(a.size(), 960u);  // 20 ms at 48 kHz
  int peak = 0;
  for (auto v : a) peak = std::max(peak, std::abs(int(v)));
  EXPECT_NEAR(peak, 4125, 60);  // -18 dBFS
  EXPECT_EQ(a.front(), 0);
  EXPECT_EQ(a.back(), 0);
}
TEST(SideTone, SettingsAreClamped) {
  EXPECT_EQ(makeSideTone({.lengthMs = 5000}).size(), 4800u);
  EXPECT_EQ(makeSideTone({.lengthMs = 0}).size(), 240u);
  for (auto v : makeSideTone({.dbfs = 20})) ASSERT_LE(std::abs(int(v)), 32767);
}
