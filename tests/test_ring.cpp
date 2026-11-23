#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "audio/PrerollRing.h"
using namespace atc;

namespace {
std::vector<std::int16_t> ramp(int start, int n) {
  std::vector<std::int16_t> v(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) v[static_cast<std::size_t>(i)] = static_cast<std::int16_t>((start + i) % 30000);
  return v;
}
// Blocks are delivered when complete: arrival = start + length (+ extra delay).
void at(PrerollRing& r, Ns start, const std::vector<std::int16_t>& b, Ns late = 0) {
  r.push(start + static_cast<Ns>(b.size()) * 1'000'000'000 / 16000 + late, b);
}
}  // namespace

TEST(Ring, CutsByTime) {
  PrerollRing r(16000, 16000);
  for (int b = 0; b < 50; ++b) at(r, ms(1000 + b * 10), ramp(b * 160, 160));
  auto c = r.extract(ms(1100), ms(1200));
  ASSERT_EQ(c.samples.size(), 1600u);
  EXPECT_EQ(c.samples.front(), 1600);
  EXPECT_EQ(c.samples.back(), 3199);
  EXPECT_EQ(c.missingHead + c.missingTail, 0u);
  EXPECT_FALSE(c.gap);
}
TEST(Ring, ReportsWhatIsMissing) {
  PrerollRing r(16000, 1600);  // 100 ms
  for (int b = 0; b < 20; ++b) at(r, ms(b * 10), ramp(b * 160, 160));  // 0..200 ms
  auto c = r.extract(ms(50), ms(250));
  EXPECT_EQ(c.samples.size(), 1600u);
  EXPECT_EQ(c.samples.front(), 1600);
  EXPECT_EQ(c.missingHead, 800u);
  EXPECT_EQ(c.missingTail, 800u);
}
TEST(Ring, NewestTime) {
  PrerollRing r(16000, 1600);
  EXPECT_EQ(r.newestTime(), 0);
  at(r, ms(1000), ramp(0, 160));
  EXPECT_EQ(r.newestTime(), ms(1010));
  at(r, ms(1010), ramp(0, 160));
  EXPECT_EQ(r.newestTime(), ms(1020));
}
TEST(Ring, EmptyRing) {
  PrerollRing r(16000, 1600);
  auto c = r.extract(0, ms(100));
  EXPECT_TRUE(c.samples.empty());
  EXPECT_EQ(c.missingTail, 1600u);
}
TEST(Ring, FutureAndPastRequests) {
  PrerollRing r(16000, 1600);
  at(r, ms(1000), ramp(0, 160));
  auto f = r.extract(ms(2000), ms(2010));
  EXPECT_TRUE(f.samples.empty());
  EXPECT_EQ(f.missingTail, 160u);
  EXPECT_EQ(f.missingHead, 0u);
  auto p = r.extract(ms(0), ms(10));
  EXPECT_TRUE(p.samples.empty());
  EXPECT_EQ(p.missingHead, 160u);
  EXPECT_EQ(p.missingTail, 0u);
}
TEST(Ring, ServerReportedGap) {
  PrerollRing r(16000, 16000);
  at(r, 0, ramp(1, 160));
  r.pushGap(1600);
  at(r, ms(110), ramp(1, 160));
  auto c = r.extract(0, ms(120));
  ASSERT_EQ(c.samples.size(), 1920u);
  EXPECT_EQ(c.samples[160], 0);
  EXPECT_EQ(c.samples[1760], 1);
  EXPECT_TRUE(c.gap);
  EXPECT_FALSE(r.extract(ms(112), ms(118)).gap);
}
TEST(Ring, ReopenedStreamKeepsTime) {
  PrerollRing r(16000, 16000 * 10);
  for (int b = 0; b < 100; ++b) at(r, ms(b * 10), ramp(b * 160, 160));
  r.restart();
  for (int b = 0; b < 100; ++b) at(r, ms(4000 + b * 10), ramp(b * 160, 160));
  auto before = r.extract(ms(500), ms(510)), after = r.extract(ms(4500), ms(4510));
  EXPECT_EQ(before.samples.front(), 8000);
  EXPECT_EQ(after.samples.front(), 8000);
  EXPECT_FALSE(before.gap || after.gap);
  EXPECT_TRUE(r.extract(ms(900), ms(4100)).gap);
}
TEST(Ring, SilentlyLostAudioIsFoundWithinTheWindow) {
  PrerollRing r(16000, 16000 * 20);
  for (int b = 0; b < 300; ++b) at(r, ms(b * 10), ramp(b * 160, 160));
  for (int b = 0; b < 400; ++b) at(r, ms(3300 + b * 10), ramp(b * 160, 160));  // 300 ms vanished
  EXPECT_EQ(r.gaps(), 1u);
  EXPECT_NEAR(double(r.newestTime()), double(ms(7300)), double(ms(1)));
  EXPECT_NEAR(r.extract(ms(6800), ms(6810)).samples.front(), (3500 * 16) % 30000, 16);
  EXPECT_TRUE(r.extract(ms(3000), ms(5000)).gap);
}
TEST(Ring, DeliveryJitterDoesNotShiftAudio) {
  PrerollRing r(16000, 32000 * 2);
  const int late[] = {0, 3, 12, 4, 30, 1, 90, 2};
  for (int b = 0; b < 300; ++b) at(r, ms(1000 + b * 10), ramp(b * 160, 160), ms(late[b % 8]));
  EXPECT_EQ(r.gaps(), 0u);
  EXPECT_NEAR(r.extract(ms(3500), ms(3510)).samples.front(), (2500 * 16) % 30000, 16);
}
TEST(Ring, StartupBacklogNeverPlacesAudioInTheFuture) {
  // Seen in test: 2 s of old audio delivered at once, then real time.
  PrerollRing r(16000, 16000 * 20);
  for (int b = 0; b < 200; ++b) {
    r.push(ms(2000), ramp(b * 160, 160));
    ASSERT_LE(r.newestTime(), ms(2000));
  }
  for (int b = 200; b < 900; ++b) {
    at(r, ms(b * 10), ramp(b * 160, 160));
    ASSERT_LE(r.newestTime(), ms(b * 10 + 10));
  }
  EXPECT_EQ(r.gaps(), 0u);
  EXPECT_NEAR(r.extract(ms(1000), ms(1010)).samples.front(), 16000, 1);
  EXPECT_NEAR(r.extract(ms(8000), ms(8010)).samples.front(), (8000 * 16) % 30000, 1);
}
TEST(Ring, FollowsSoundCardClockDrift) {
  PrerollRing r(16000, 16000 * 5);
  Ns t = 0;
  for (int b = 0; b < 60000; ++b) {  // 10 min, card 200 ppm slow
    t += 10'002'000;
    r.push(t, ramp(b * 160, 160));
  }
  EXPECT_EQ(r.gaps(), 0u);
  EXPECT_NEAR(double(r.newestTime()), double(t), double(ms(1)));
}
TEST(Ring, SurvivesMonthsOfSamples) {
  PrerollRing r(16000, 1600);
  at(r, 0, ramp(0, 160));
  r.pushGap(50'000'000'000ull);  // about 36 days
  const Ns start = 3'125'000'000'000'000 + ms(10);
  for (int b = 0; b < 5; ++b) at(r, start + ms(b * 10), ramp(b * 160, 160));
  auto c = r.extract(start + ms(20), start + ms(30));
  ASSERT_EQ(c.samples.size(), 160u);
  EXPECT_EQ(c.samples.front(), 320);
}
TEST(Ring, ConcurrentReadIsConsistent) {
  // The reader keeps asking for the oldest audio while the writer overwrites it.
  PrerollRing r(16000, 4800);  // 300 ms
  std::atomic<int> pos{0};
  std::atomic<bool> stop{false};
  std::thread w([&] {
    for (int b = 0; !stop; ++b) {
      r.push(ms(b * 10), ramp(b * 160, 160));
      pos = b;
    }
  });
  while (pos < 100) std::this_thread::yield();
  long checked = 0, bad = 0, dropped = 0;
  for (int k = 0; k < 2000; ++k) {
    const int p = pos;
    auto c = r.extract(ms(p * 10 - 290), ms(p * 10));
    dropped += c.missingHead > 0;
    for (std::size_t i = 1; i < c.samples.size(); ++i, ++checked)
      bad += c.samples[i] != (c.samples[i - 1] + 1) % 30000;
  }
  stop = true;
  w.join();
  EXPECT_EQ(bad, 0);
  EXPECT_GT(checked, 1000);
  EXPECT_GT(dropped, 0) << "test did not reach the overwrite point";
}
