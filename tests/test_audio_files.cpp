#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "audio/LevelMeter.h"
#include "audio/WavFile.h"
using namespace atc;

namespace {
std::string tmp(const char* n) { return ::testing::TempDir() + n; }
}

TEST(Wav, RoundTrip) {
  const std::vector<std::int16_t> v{0, 1, -1, 32767, -32768, 1234};
  const auto p = tmp("rt.wav");
  ASSERT_TRUE(writeWav(p, 16000, v));
  EXPECT_EQ(std::filesystem::file_size(p), 44 + 12u);
  EXPECT_FALSE(std::filesystem::exists(p + ".part"));
  auto r = readWav(p);
  ASSERT_TRUE(r);
  EXPECT_EQ(r->rate, 16000);
  EXPECT_EQ(r->samples, v);
}
TEST(Wav, RejectsTruncatedAndLyingFiles) {
  const auto p = tmp("bad.wav");
  ASSERT_TRUE(writeWav(p, 16000, std::vector<std::int16_t>(100, 5)));
  std::filesystem::resize_file(p, 100);
  EXPECT_FALSE(readWav(p));
  ASSERT_TRUE(writeWav(p, 16000, std::vector<std::int16_t>(100, 5)));
  {
    std::fstream f(p, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(40);
    const char huge[4] = {'\xff', '\xff', '\xff', '\x7f'};
    f.write(huge, 4);
  }
  EXPECT_FALSE(readWav(p));
  EXPECT_FALSE(readWav(tmp("does_not_exist.wav")));
}
TEST(Wav, UnwritableFolderFails) {
  EXPECT_FALSE(writeWav("/nonexistent_dir/x.wav", 16000, std::vector<std::int16_t>(4, 0)));
}
TEST(Meter, PeakResetsAndZeroRunCounts) {
  LevelMeter m;
  m.add(std::vector<std::int16_t>{0, 16384, -100});
  EXPECT_NEAR(m.takePeakDbfs(), -6.02, 0.01);
  EXPECT_EQ(m.takePeakDbfs(), -120.0);
  m.add(std::vector<std::int16_t>(50, 0));
  EXPECT_EQ(m.zeroRun(), 50u);
  m.add(std::vector<std::int16_t>{1});
  EXPECT_EQ(m.zeroRun(), 0u);
}
