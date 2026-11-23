#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

#include "core/TransmissionController.h"
using namespace atc;
using S = TransmissionController::State;

struct Tx : ::testing::Test {
  std::vector<std::string> log;
  bool playing = false;
  TransmissionController* self = nullptr;
  TxConfig cfg;
  TransmissionController make() {
    return TransmissionController(cfg, {
        [this](std::uint64_t id, Ns a) { log.push_back("begin" + std::to_string(id) + "@" + std::to_string(a / ms(1))); },
        [this](std::uint64_t id, Ns a, Ns b, TxEnd e) {
          log.push_back("end" + std::to_string(id) + " " + std::to_string(a / ms(1)) + "-" + std::to_string(b / ms(1)) +
                        (e == TxEnd::Stuck ? " stuck" : e == TxEnd::DeviceLost ? " lost" : ""));
        },
        [this](std::uint64_t id) { log.push_back("cancel" + std::to_string(id)); },
        [this](const Reply& r) {
          ASSERT_FALSE(playing);
          if (self) { ASSERT_FALSE(self->pedalDown()) << "pilot started while pedal held"; }
          playing = true;
          log.push_back("pilot " + r.text);
        },
        [this](const Reply& r) { playing = false; log.push_back("cut " + r.text); },
        [this](Ns t) { log.push_back("blocked@" + std::to_string(t / ms(1))); },
        [this](Ns t) { log.push_back("queued@" + std::to_string(t / ms(1))); },
        [this](Ns t) { log.push_back("click@" + std::to_string(t / ms(1))); },
        [this](Ns t) { log.push_back("dropped@" + std::to_string(t / ms(1))); }});
  }
  using V = std::vector<std::string>;
};

TEST_F(Tx, AudioIncludesPreRollAndTail) {
  auto c = make();
  c.onPress(ms(1000));
  c.onRelease(ms(3000), ReleaseReason::Normal);
  EXPECT_EQ(c.state(), S::TxTail);
  c.tick(ms(3200));
  EXPECT_EQ(log, (V{"begin1@700", "end1 700-3200"}));
  EXPECT_EQ(c.state(), S::Recognising);
}
TEST_F(Tx, ShortTapIsCancelled) {
  auto c = make();
  c.onPress(ms(1000));
  c.onRelease(ms(1001), ReleaseReason::Normal);
  c.tick(ms(2000));
  EXPECT_EQ(log.back(), "cancel1");
  EXPECT_EQ(c.state(), S::Idle);
}
TEST_F(Tx, PressWithinTailContinuesSameTransmission) {
  auto c = make();
  c.onPress(ms(1000));
  c.onRelease(ms(2000), ReleaseReason::Normal);
  c.onPress(ms(2100));
  c.onRelease(ms(3000), ReleaseReason::Normal);
  c.tick(ms(3500));
  EXPECT_EQ(log, (V{"begin1@700", "end1 700-3200"}));
}
TEST_F(Tx, StuckPedalClosesAndDoesNotReopen) {
  auto c = make();
  c.onPress(0);
  c.tick(ms(30'000));
  EXPECT_EQ(log.back(), "end1 -300-30000 stuck");
  c.onProcessed(1, Reply{"x", 0}, ms(31'000));
  c.tick(ms(40'000));
  EXPECT_EQ(log.size(), 2u) << "pilot must stay silent while the pedal is stuck down";
  c.onRelease(ms(41'000), ReleaseReason::Normal);
  EXPECT_EQ(log.back(), "pilot x");
}
TEST_F(Tx, DeviceLostIsFlaggedEvenIfShort) {
  auto c = make();
  c.onPress(ms(1000));
  c.onRelease(ms(1100), ReleaseReason::DeviceLost);
  c.tick(ms(1300));
  EXPECT_EQ(log.back(), "end1 700-1300 lost");
}
TEST_F(Tx, PilotWaitsForDelayAndPedal) {
  auto c = make();
  c.onPress(ms(1000));
  c.onRelease(ms(2000), ReleaseReason::Normal);
  c.tick(ms(2200));
  c.onProcessed(1, Reply{"rb", ms(500)}, ms(2300));
  EXPECT_EQ(c.state(), S::PilotDelay);
  c.onPress(ms(2500));  // trainee keys again before the pilot starts
  c.tick(ms(4000));
  EXPECT_FALSE(playing);
  c.onRelease(ms(4000), ReleaseReason::Normal);
  c.tick(ms(4200));
  c.onProcessed(2, std::nullopt, ms(4300));
  c.tick(ms(4799));
  EXPECT_FALSE(playing);
  c.tick(ms(4800));
  EXPECT_TRUE(playing);
}
TEST_F(Tx, PressInterruptsPilotBeforeOpeningTx) {
  auto c = make();
  c.requestPilot({"call", 0}, 0);
  ASSERT_TRUE(playing);
  c.onPress(ms(100));
  EXPECT_EQ(log, (V{"pilot call", "cut call", "begin1@-200"}));
}
TEST_F(Tx, InterruptedReplyRepeatsAfterNewInstruction) {
  auto c = make();
  c.requestPilot({"call", 0}, 0);
  c.onPress(ms(100));
  c.onRelease(ms(1100), ReleaseReason::Normal);
  c.tick(ms(1300));
  EXPECT_FALSE(playing);
  c.onProcessed(1, std::nullopt, ms(1400));
  EXPECT_EQ(log.back(), "pilot call");
}
TEST_F(Tx, InterruptedReplyCanBeDiscarded) {
  cfg.repeatInterruptedReply = false;
  auto c = make();
  c.requestPilot({"call", 0}, 0);
  c.onPress(ms(100));
  c.onRelease(ms(1100), ReleaseReason::Normal);
  c.tick(ms(1300));
  c.onProcessed(1, std::nullopt, ms(1400));
  EXPECT_EQ(c.state(), S::Idle);
}
TEST_F(Tx, LateResultCannotBeOvertaken) {
  auto c = make();
  c.onPress(0);
  c.onRelease(ms(1000), ReleaseReason::Normal);
  c.tick(ms(1200));
  c.onPress(ms(1300));
  c.onRelease(ms(2300), ReleaseReason::Normal);
  c.tick(ms(2500));
  c.onProcessed(2, Reply{"second", 0}, ms(2600));
  EXPECT_FALSE(playing);
  c.onProcessed(1, Reply{"first", 0}, ms(2700));
  EXPECT_EQ(log.back(), "pilot first");
  playing = false;
  c.onPlaybackDone(ms(4000));
  EXPECT_EQ(log.back(), "pilot second");
}
TEST_F(Tx, RuleNamesMatchScenarioSchema) {
  EXPECT_EQ(pressRuleFromString("interrupt"), PressRule::Interrupt);
  EXPECT_EQ(pressRuleFromString("block"), PressRule::Block);
  EXPECT_EQ(pressRuleFromString("queue"), PressRule::Queue);
  EXPECT_EQ(pressRuleFromString("Block"), std::nullopt);
  EXPECT_EQ(pressRuleFromString(""), std::nullopt);
  EXPECT_EQ(TxConfig{}.pressDuringPilot, PressRule::Interrupt);
}
TEST_F(Tx, BlockDiscardsThePress) {
  cfg.pressDuringPilot = PressRule::Block;
  auto c = make();
  c.requestPilot({"call", 0}, 0);
  c.onPress(ms(100));
  EXPECT_TRUE(c.pressBlocked());
  EXPECT_TRUE(playing);
  playing = false;
  c.onPlaybackDone(ms(2000));
  c.tick(ms(9000));
  EXPECT_TRUE(c.pressBlocked()) << "still held: must not turn into a transmission";
  c.onRelease(ms(9100), ReleaseReason::Normal);
  c.tick(ms(9500));
  EXPECT_FALSE(c.pressBlocked());
  EXPECT_EQ(log, (V{"pilot call", "blocked@100"}));
  c.onPress(ms(10'000));
  EXPECT_EQ(log.back(), "begin1@9700") << "next press is a normal one";
}
TEST_F(Tx, BlockedReleaseDuringSpeechLeavesPilotSpeaking) {
  cfg.pressDuringPilot = PressRule::Block;
  auto c = make();
  c.requestPilot({"call", 0}, 0);
  c.onPress(ms(100));
  c.onRelease(ms(900), ReleaseReason::Normal);
  EXPECT_EQ(c.state(), S::PilotSpeaking);
  EXPECT_EQ(log, (V{"pilot call", "blocked@100"}));
}
TEST_F(Tx, HeldBlockedPressDelaysTheNextReply) {
  cfg.pressDuringPilot = PressRule::Block;
  auto c = make();
  self = &c;
  c.requestPilot({"one", 0}, 0);
  c.requestPilot({"two", 0}, 0);
  c.onPress(ms(100));
  playing = false;
  c.onPlaybackDone(ms(2000));
  c.tick(ms(5000));
  EXPECT_FALSE(playing);
  c.onRelease(ms(6000), ReleaseReason::Normal);
  EXPECT_EQ(log.back(), "pilot two");
  self = nullptr;
}
TEST_F(Tx, QueueOpensWhenPilotFinishes) {
  cfg.pressDuringPilot = PressRule::Queue;
  auto c = make();
  c.requestPilot({"call", 0}, 0);
  c.requestPilot({"next", 0}, 0);
  c.onPress(ms(100));
  EXPECT_TRUE(c.pressQueued());
  EXPECT_TRUE(playing);
  playing = false;
  c.onPlaybackDone(ms(2000));
  EXPECT_EQ(c.state(), S::TxOpen) << "held press goes before the next reply";
  EXPECT_FALSE(c.pressQueued());
  c.onRelease(ms(4000), ReleaseReason::Normal);
  c.tick(ms(4200));
  EXPECT_EQ(log, (V{"pilot call", "queued@100", "click@2000", "begin1@1700", "end1 1700-4200"}));
}
TEST_F(Tx, QueuedPressReleasedEarlyIsDropped) {
  cfg.pressDuringPilot = PressRule::Queue;
  for (auto why : {ReleaseReason::Normal, ReleaseReason::DeviceLost}) {
    log.clear();
    auto c = make();
    c.requestPilot({"call", 0}, 0);
    c.onPress(ms(100));
    c.onRelease(ms(1500), why);
    EXPECT_FALSE(c.pressQueued());
    playing = false;
    c.onPlaybackDone(ms(2000));
    EXPECT_EQ(c.state(), S::Idle);
    EXPECT_EQ(log, (V{"pilot call", "queued@100", "dropped@1500"}));
  }
}
TEST_F(Tx, QueuedTransmissionLimitsCountFromOpening) {
  cfg.pressDuringPilot = PressRule::Queue;
  auto c = make();
  c.requestPilot({"call", 0}, 0);
  c.onPress(ms(100));
  c.tick(ms(35'000));  // waiting is not transmitting: not stuck
  EXPECT_TRUE(c.pressQueued());
  playing = false;
  c.onPlaybackDone(ms(40'000));
  c.tick(ms(69'999));
  EXPECT_EQ(c.state(), S::TxOpen);
  c.tick(ms(70'000));
  EXPECT_EQ(log.back(), "end1 39700-70000 stuck");
}
TEST_F(Tx, QueuedTransmissionReleasedAtOnceIsAShortTap) {
  cfg.pressDuringPilot = PressRule::Queue;
  auto c = make();
  c.requestPilot({"call", 0}, 0);
  c.onPress(ms(100));
  playing = false;
  c.onPlaybackDone(ms(2000));
  c.onRelease(ms(2100), ReleaseReason::Normal);
  c.tick(ms(2400));
  EXPECT_EQ(log.back(), "cancel1");
}
TEST_F(Tx, RulesDoNotApplyWhenPilotIsSilent) {
  for (auto rule : {PressRule::Block, PressRule::Queue}) {
    log.clear();
    cfg.pressDuringPilot = rule;
    auto c = make();
    c.requestPilot({"call", ms(500)}, 0);  // pilot delay, not speaking yet
    c.onPress(ms(100));
    EXPECT_EQ(log, (V{"begin1@-200"}));
  }
}
TEST_F(Tx, RandomisedInvariants) {
  for (auto rule : {PressRule::Interrupt, PressRule::Block, PressRule::Queue})
  for (unsigned seed = 1; seed <= 300; ++seed) {
    cfg.pressDuringPilot = rule;
    log.clear();
    playing = false;
    auto c = make();
    self = &c;
    std::mt19937 g(seed);
    Ns t = 0;
    std::uint64_t begun = 0;
    for (int i = 0; i < 400; ++i) {
      t += ms(static_cast<int>(g() % 400));
      switch (g() % 6) {
        case 0: c.onPress(t); break;
        case 1: if (c.pedalDown()) c.onRelease(t, g() % 8 ? ReleaseReason::Normal : ReleaseReason::DeviceLost); break;
        case 2: c.onProcessed(g() % (begun + 2), g() % 2 ? std::optional<Reply>({"r", ms(static_cast<int>(g() % 600))}) : std::nullopt, t); break;
        case 3: if (playing) { playing = false; c.onPlaybackDone(t); } break;
        case 4: if (g() % 5 == 0) c.requestPilot({"q", 0}, t); break;
        default: c.tick(t);
      }
      begun = 0;
      for (auto& l : log) begun += l.rfind("begin", 0) == 0;
      ASSERT_FALSE(playing && c.pedalDown() && !c.pressBlocked() && !c.pressQueued()) << "seed " << seed << " step " << i;
      ASSERT_FALSE(playing && (c.state() == S::TxOpen || c.state() == S::TxTail)) << "seed " << seed << " step " << i;
      ASSERT_FALSE(c.pressQueued() && !playing) << "seed " << seed << " step " << i;
      ASSERT_FALSE((c.pressBlocked() || c.pressQueued()) && !c.pedalDown()) << "seed " << seed << " step " << i;
      if (rule == PressRule::Interrupt) { ASSERT_FALSE(c.pressBlocked() || c.pressQueued()); }
      ASSERT_EQ(playing, c.state() == S::PilotSpeaking) << "seed " << seed << " step " << i;
    }
    self = nullptr;
  }
}
