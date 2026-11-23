#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ptt/PttInputManager.h"
using namespace atc;

struct Ptt : ::testing::Test {
  std::vector<std::string> log;
  PttInputManager m{{}, {
      [this](Ns t, const std::string& s) { log.push_back("P " + s + " " + std::to_string(t / ms(1))); },
      [this](Ns t, const std::string& s, ReleaseReason r) {
        log.push_back(std::string(r == ReleaseReason::DeviceLost ? "L " : "R ") + s + " " + std::to_string(t / ms(1)));
      },
      [this](const std::string& s) { log.push_back("I " + s); },
      [this](Ns, const std::string& s) { log.push_back("PULSE " + s); }}};
  using V = std::vector<std::string>;
};

TEST_F(Ptt, PressIsImmediateReleaseKeepsRawTime) {
  m.rawEdge(ms(100), "pedal", true);
  EXPECT_EQ(log, V{"P pedal 100"});
  m.rawEdge(ms(2000), "pedal", false);
  m.tick(ms(2010));
  EXPECT_EQ(log.size(), 1u);
  m.tick(ms(2020));
  EXPECT_EQ(log, (V{"P pedal 100", "R pedal 2000"}));
}
TEST_F(Ptt, BounceIsIgnored) {
  m.rawEdge(ms(100), "pedal", true);
  m.rawEdge(ms(105), "pedal", false);
  m.rawEdge(ms(110), "pedal", true);
  m.tick(ms(500));
  EXPECT_EQ(log, V{"P pedal 100"});
  EXPECT_TRUE(m.isDown());
}
TEST_F(Ptt, CurrentPedalPulseIsDetected) {
  m.rawEdge(ms(100), "pedal", true);
  m.rawEdge(ms(101), "pedal", false);
  m.tick(ms(200));
  EXPECT_EQ(log, (V{"P pedal 100", "PULSE pedal", "R pedal 101"}));
}
TEST_F(Ptt, FirstSourceOwnsAndSecondCannotTakeOver) {
  m.rawEdge(ms(0), "pedal", true);
  m.rawEdge(ms(100), "screen", true);
  m.rawEdge(ms(1000), "pedal", false);
  m.tick(ms(1100));
  EXPECT_EQ(log, (V{"P pedal 0", "I screen", "R pedal 1000"}));
  EXPECT_FALSE(m.isDown());
  m.rawEdge(ms(1200), "screen", false);
  m.rawEdge(ms(1300), "screen", true);
  EXPECT_EQ(log.back(), "P screen 1300");
}
TEST_F(Ptt, HeldAtStartupNeedsUpFirst) {
  m.setInitialState("pedal", true);
  m.rawEdge(ms(10), "pedal", true);
  EXPECT_TRUE(log.empty());
  m.rawEdge(ms(500), "pedal", false);
  m.rawEdge(ms(900), "pedal", true);
  EXPECT_EQ(log, V{"P pedal 900"});
}
TEST_F(Ptt, DeviceLostForcesRelease) {
  m.rawEdge(ms(0), "pedal", true);
  m.deviceLost(ms(700), "pedal");
  EXPECT_EQ(log, (V{"P pedal 0", "L pedal 700"}));
}
TEST_F(Ptt, LateEdgeDoesNotHideAnExpiredRelease) {
  m.rawEdge(ms(0), "pedal", true);
  m.rawEdge(ms(1000), "pedal", false);
  m.rawEdge(ms(1500), "pedal", true);  // no tick in between
  EXPECT_EQ(log, (V{"P pedal 0", "R pedal 1000", "P pedal 1500"}));
}
