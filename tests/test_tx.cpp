#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "core/TransmissionController.h"
using namespace atc;
using S = TransmissionController::State;

struct Tx : ::testing::Test {
  std::vector<std::string> log;
  bool playing = false;
  PlaybackId pid = 0;  // latest playback
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
        [this](const Reply& r, PlaybackId id) {
          pid = id;
          ASSERT_FALSE(playing);
          if (self) { ASSERT_FALSE(self->pedalDown()) << "pilot started while pedal held"; }
          playing = true;
          log.push_back("pilot " + r.text);
        },
        [this](const Reply& r, PlaybackId id, PilotStop why) {
          EXPECT_EQ(id, pid);
          playing = false;
          log.push_back((why == PilotStop::Interrupted ? "cut " : why == PilotStop::Timeout ? "TIMEOUT " : "withdrawn ") + r.text);
        },
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
  c.onPlaybackDone(pid, ms(4000));
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
  c.onPlaybackDone(pid, ms(2000));
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
  c.onPlaybackDone(pid, ms(2000));
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
  c.onPlaybackDone(pid, ms(2000));
  c.tick(ms(2024));
  EXPECT_NE(c.state(), S::TxOpen);
  c.tick(ms(2025));
  EXPECT_EQ(c.state(), S::TxOpen) << "held press goes before the next reply";
  EXPECT_FALSE(c.pressQueued());
  c.onRelease(ms(4000), ReleaseReason::Normal);
  c.tick(ms(4200));
  EXPECT_EQ(log, (V{"pilot call", "queued@100", "click@2025", "begin1@2025", "end1 2025-4200"}));
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
    c.onPlaybackDone(pid, ms(2000));
    EXPECT_EQ(c.state(), S::Idle);
    EXPECT_EQ(log, (V{"pilot call", "queued@100", "dropped@1500"}));
  }
}
TEST_F(Tx, QueuedTransmissionLimitsCountFromOpening) {
  cfg.pressDuringPilot = PressRule::Queue;
  auto c = make();
  c.requestPilot({"call", 0}, 0);
  c.onPress(ms(100));
  c.tick(ms(19'000));  // waiting is not transmitting: not stuck
  EXPECT_TRUE(c.pressQueued());
  playing = false;
  c.onPlaybackDone(pid, ms(19'500));
  c.tick(ms(49'524));
  EXPECT_EQ(c.state(), S::TxOpen);
  c.tick(ms(49'525));
  EXPECT_EQ(log.back(), "end1 19525-49525 stuck");
}
TEST_F(Tx, QueuedTransmissionReleasedAtOnceIsAShortTap) {
  cfg.pressDuringPilot = PressRule::Queue;
  auto c = make();
  c.requestPilot({"call", 0}, 0);
  c.onPress(ms(100));
  playing = false;
  c.onPlaybackDone(pid, ms(2000));
  c.tick(ms(2030));
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
// ---- review findings (D-300 to D-303)

TEST_F(Tx, LateDoneOfACutReplyDoesNotEndTheNext) {
  auto c = make();
  c.requestPilot({"call", 0}, 0);
  const PlaybackId first = pid;
  c.onPress(ms(100));
  c.onRelease(ms(101), ReleaseReason::Normal);
  c.tick(ms(301));  // short tap: the reply starts again
  ASSERT_TRUE(playing);
  c.onPlaybackDone(first, ms(320));
  EXPECT_EQ(c.state(), S::PilotSpeaking);
  c.onPress(ms(400));
  EXPECT_EQ(log, (V{"pilot call", "cut call", "begin1@-200", "cancel1", "pilot call", "cut call", "begin2@100"}));
}
TEST_F(Tx, DuplicateAndUnknownDoneAreIgnored) {
  auto c = make();
  c.requestPilot({"one", 0}, 0);
  c.requestPilot({"two", 0}, 0);
  const PlaybackId first = pid;
  playing = false;
  c.onPlaybackDone(first, ms(1000));
  ASSERT_EQ(log.back(), "pilot two");
  c.onPlaybackDone(first, ms(1100));
  c.onPlaybackDone(0, ms(1200));
  c.onPlaybackDone(pid + 1, ms(1300));
  EXPECT_EQ(c.state(), S::PilotSpeaking);
}
TEST_F(Tx, StaleDoneDoesNotOpenAQueuedPress) {
  cfg.pressDuringPilot = PressRule::Queue;
  auto c = make();
  c.requestPilot({"one", 0}, 0);
  c.requestPilot({"two", 0}, 0);
  const PlaybackId first = pid;
  playing = false;
  c.onPlaybackDone(first, ms(1000));
  c.onPress(ms(1500));
  c.onPlaybackDone(first, ms(1600));
  c.tick(ms(1700));
  EXPECT_TRUE(c.pressQueued());
  EXPECT_EQ(c.state(), S::PilotSpeaking);
}
TEST_F(Tx, WatchdogUsesExpectedLengthPlusMargin) {
  auto c = make();
  c.requestPilot({"call", 0, ms(3000)}, 0);
  c.tick(ms(4499));
  EXPECT_TRUE(playing);
  c.tick(ms(4500));
  EXPECT_EQ(log.back(), "TIMEOUT call");
  EXPECT_EQ(c.state(), S::Idle) << "a timed-out reply is not repeated";
  c.onPlaybackDone(pid, ms(4600));  // the late end changes nothing
  EXPECT_EQ(c.state(), S::Idle);
}
TEST_F(Tx, WatchdogCap) {
  for (Ns expected : {Ns(0), ms(19'000), ms(60'000)}) {
    log.clear();
    playing = false;
    auto c = make();
    c.requestPilot({"call", 0, expected}, 0);
    c.tick(ms(19'999));
    EXPECT_TRUE(playing) << expected;
    c.tick(ms(20'000));
    EXPECT_EQ(log.back(), "TIMEOUT call");
  }
}
TEST_F(Tx, WatchdogIsConfigurable) {
  cfg.pilotCap = ms(5000);
  cfg.pilotMargin = ms(100);
  auto c = make();
  c.requestPilot({"a", 0, ms(1000)}, 0);
  c.requestPilot({"b", 0}, 0);
  c.tick(ms(1100));
  EXPECT_EQ(log, (V{"pilot a", "TIMEOUT a", "pilot b"}));
  c.tick(ms(6100));
  EXPECT_EQ(log.back(), "TIMEOUT b");
}
TEST_F(Tx, NoEndReportedStillLeavesTheRadioUsable) {
  for (auto rule : {PressRule::Block, PressRule::Queue, PressRule::Interrupt}) {
    log.clear();
    playing = false;
    cfg.pressDuringPilot = rule;
    auto c = make();
    c.requestPilot({"call", 0}, 0);
    c.tick(ms(20'000));
    c.onPress(ms(21'000));
    EXPECT_EQ(c.state(), S::TxOpen);
  }
}
TEST_F(Tx, QueuedPressOpensAfterATimeout) {
  cfg.pressDuringPilot = PressRule::Queue;
  auto c = make();
  c.requestPilot({"call", 0, ms(1000)}, 0);
  c.onPress(ms(100));
  c.tick(ms(2500));
  c.tick(ms(2525));
  EXPECT_EQ(log, (V{"pilot call", "queued@100", "TIMEOUT call", "click@2525", "begin1@2525"}));
}
TEST_F(Tx, BlockedPressStaysBlockedAfterATimeout) {
  cfg.pressDuringPilot = PressRule::Block;
  auto c = make();
  c.requestPilot({"call", 0, ms(1000)}, 0);
  c.onPress(ms(100));
  c.tick(ms(9000));
  EXPECT_TRUE(c.pressBlocked());
  EXPECT_EQ(log, (V{"pilot call", "blocked@100", "TIMEOUT call"}));
}
TEST_F(Tx, PressAtTheInstantThePilotIsDueComesFirst) {
  for (auto rule : {PressRule::Interrupt, PressRule::Block, PressRule::Queue}) {
    log.clear();
    cfg.pressDuringPilot = rule;
    auto c = make();
    c.requestPilot({"call", ms(500)}, 0);
    c.onPress(ms(500));
    EXPECT_EQ(log, (V{"begin1@200"}));
  }
}
TEST_F(Tx, PressStampedBeforePilotStartButDeliveredAfterComesFirst) {
  for (auto rule : {PressRule::Interrupt, PressRule::Block, PressRule::Queue}) {
    log.clear();
    playing = false;
    cfg.pressDuringPilot = rule;
    cfg.repeatInterruptedReply = false;  // it was never really interrupted: kept anyway
    auto c = make();
    c.requestPilot({"call", ms(500)}, 0);
    c.tick(ms(505));
    c.onPress(ms(500));
    EXPECT_EQ(log, (V{"pilot call", "withdrawn call", "begin1@200"}));
    c.onRelease(ms(2000), ReleaseReason::Normal);
    c.tick(ms(2200));
    c.onProcessed(1, std::nullopt, ms(2300));
    c.tick(ms(2800));
    EXPECT_EQ(log.back(), "pilot call");
  }
}
TEST_F(Tx, PressStampedDuringSpeechButDeliveredAfterItsEndFollowsTheRule) {
  for (auto rule : {PressRule::Block, PressRule::Queue}) {
    log.clear();
    cfg.pressDuringPilot = rule;
    auto c = make();
    c.requestPilot({"call", 0}, 0);
    playing = false;
    c.onPlaybackDone(pid, ms(2000));
    c.onPress(ms(1995));
    c.tick(ms(2025));
    EXPECT_EQ(log, rule == PressRule::Block ? (V{"pilot call", "blocked@1995"})
                                            : (V{"pilot call", "queued@1995", "click@2025", "begin1@2025"}));
  }
}
TEST_F(Tx, PressAfterTheEndIsNormalEvenBeforeTheSettleTime) {
  cfg.pressDuringPilot = PressRule::Block;
  auto c = make();
  c.requestPilot({"call", 0}, 0);
  playing = false;
  c.onPlaybackDone(pid, ms(2000));
  c.onPress(ms(2000));
  EXPECT_EQ(log.back(), "begin1@1700");
}
TEST_F(Tx, ReleaseBeforeTheEndButConfirmedAfterDropsWithoutAClick) {
  cfg.pressDuringPilot = PressRule::Queue;
  auto c = make();
  PttInputManager p({}, {[&](Ns t, const std::string&) { c.onPress(t); },
                         [&](Ns t, const std::string&, ReleaseReason why) { c.onRelease(t, why); }, nullptr, nullptr});
  c.requestPilot({"call", 0}, 0);
  p.rawEdge(ms(100), "p", true);
  p.rawEdge(ms(1990), "p", false);
  playing = false;
  c.onPlaybackDone(pid, ms(2000));
  for (Ns t = ms(2000); t <= ms(2400); t += ms(5)) {
    p.tick(t);
    c.tick(t);
  }
  EXPECT_EQ(log, (V{"pilot call", "queued@100", "dropped@1990"}));
}
TEST_F(Tx, ReleaseReportedVeryLateUndoesTheOpening) {
  cfg.pressDuringPilot = PressRule::Queue;
  auto c = make();
  c.requestPilot({"call", 0}, 0);
  c.onPress(ms(100));
  playing = false;
  c.onPlaybackDone(pid, ms(2000));
  c.tick(ms(2300));
  c.onRelease(ms(1990), ReleaseReason::Normal);
  EXPECT_EQ(log, (V{"pilot call", "queued@100", "click@2025", "begin1@2025", "cancel1", "dropped@1990"}));
  c.onPress(ms(3000));
  EXPECT_EQ(log.back(), "begin2@2700") << "next press is normal, with pre-roll";
}
TEST_F(Tx, PlaybackThatEndsAtOnceIsSafe) {
  std::vector<std::string> seen;
  TransmissionController* c = nullptr;
  TransmissionController ctl(cfg, {nullptr, nullptr, nullptr,
                                   [&](const Reply& r, PlaybackId id) {
                                     c->onPlaybackDone(id, ms(1));
                                     seen.push_back(r.text);
                                   },
                                   nullptr});
  c = &ctl;
  ctl.requestPilot({"a first reply that is long enough to live on the heap", 0}, ms(1));
  ctl.requestPilot({"a second reply that is long enough to live on the heap", 0}, ms(1));
  EXPECT_EQ(seen, (V{"a first reply that is long enough to live on the heap", "a second reply that is long enough to live on the heap"}));
  EXPECT_EQ(ctl.state(), S::Idle);
}

// The sound player as it really behaves: it sounds until it ends or is stopped, and its
// end reports arrive late, twice, out of order or never. Pedal edges carry kernel time
// stamps and are delivered late, through the debouncer.
TEST(TxFaults, RandomisedPlaybackAndPedalFaults) {
  int timeouts = 0, opened = 0, withdrawn = 0, queuedOpens = 0;
  for (auto rule : {PressRule::Interrupt, PressRule::Block, PressRule::Queue})
    for (unsigned seed = 1; seed <= 400; ++seed) {
      std::mt19937 g(seed);
      const auto rnd = [&](int n) { return static_cast<int>(g() % static_cast<unsigned>(n)); };
      TxConfig cfg;
      cfg.pressDuringPilot = rule;
      std::multimap<Ns, std::function<void()>> events;  // by delivery time
      Ns now = 0;
      bool audible = false, faults = true;
      PlaybackId sounding = 0;
      TransmissionController* cp = nullptr;
      std::vector<std::uint64_t> toProcess;
      const std::string at = "rule " + std::to_string(int(rule)) + " seed " + std::to_string(seed);
      const auto check = [&] {
        const auto st = cp->state();
        ASSERT_FALSE(audible && (st == S::TxOpen || st == S::TxTail)) << at << " t " << now / ms(1);
      };
      TransmissionController c(cfg, {
          [&](std::uint64_t, Ns) {
            ++opened;
            ASSERT_FALSE(audible) << "TX opened while the pilot is audible, " << at << " t " << now / ms(1);
          },
          [&](std::uint64_t id, Ns from, Ns to, TxEnd) {
            ASSERT_LT(from, to) << at;
            toProcess.push_back(id);
          },
          nullptr,
          [&](const Reply& r, PlaybackId id) {
            ASSERT_FALSE(cp->pedalDown()) << "pilot started while PTT down, " << at;
            ASSERT_FALSE(audible) << at;
            audible = true;
            sounding = id;
            const Ns length = r.expected > 0 ? r.expected : ms(1500);
            const bool hangs = faults && rnd(6) == 0;  // sounds on until it is stopped
            if (!hangs) events.emplace(now + length, [&, id] { if (sounding == id) audible = false; });
            const int kind = faults ? rnd(8) : 7;
            const auto done = [&, id] { cp->onPlaybackDone(id, now); };
            if (hangs || kind == 0) return;                                        // never
            events.emplace(now + length + ms(kind == 1 ? rnd(30'000) : rnd(20)), done);  // late
            if (kind == 2) events.emplace(now + length + ms(rnd(25'000)), done);   // twice
            if (kind == 3) events.emplace(now + ms(rnd(5000)), [&] { cp->onPlaybackDone(1'000'000'000, now); });  // unknown
            if (kind == 4 && id > 1) events.emplace(now + ms(rnd(2000)), [&, id] { cp->onPlaybackDone(id - 1, now); });  // out of order
          },
          [&](const Reply&, PlaybackId id, PilotStop why) {
            ASSERT_EQ(id, sounding) << at;
            audible = false;
            timeouts += why == PilotStop::Timeout;
            withdrawn += why == PilotStop::PressCameFirst;
          },
          nullptr, nullptr, [&](Ns) { ++queuedOpens; }, nullptr});
      cp = &c;
      PttInputManager p({}, {[&](Ns t, const std::string&) { c.onPress(t); },
                             [&](Ns t, const std::string&, ReleaseReason why) { c.onRelease(t, why); }, nullptr, nullptr});
      Ns edge = ms(500);
      bool down = false;
      const auto step = [&] {
        now += ms(5);
        while (!events.empty() && events.begin()->first <= now) {
          auto f = std::move(events.begin()->second);
          events.erase(events.begin());
          f();
          check();
        }
        p.tick(now);
        c.tick(now);
        check();
      };
      while (now < ms(90'000)) {
        if (now >= edge) {
          down = !down;
          const Ns stamp = now;
          const Ns delivery = now + ms(rnd(15));
          const bool lost = !down && rnd(10) == 0;
          events.emplace(delivery, [&, stamp, d = down, lost] {
            if (lost) p.deviceLost(stamp, "p");
            else p.rawEdge(stamp, "p", d);
          });
          edge = now + ms(down && rnd(4) == 0 ? 1 + rnd(40) : 50 + rnd(6000));
        }
        if (rnd(400) == 0) c.requestPilot({"call", ms(rnd(800)), rnd(3) ? ms(500 + rnd(4000)) : 0}, now);
        if (!toProcess.empty() && rnd(60) == 0) {
          const auto id = toProcess.front();
          toProcess.erase(toProcess.begin());
          c.onProcessed(id, rnd(2) ? std::optional<Reply>({"rb", ms(rnd(800)), ms(500 + rnd(3000))}) : std::nullopt, now);
        }
        step();
        if (HasFatalFailure()) return;
      }
      // Recovery: after the faults stop, the radio must become usable again.
      faults = false;
      if (down) events.emplace(now, [&, stamp = now] { p.rawEdge(stamp, "p", false); });
      for (Ns end = now + ms(25'000); now < end;) {
        for (auto id : std::exchange(toProcess, {})) c.onProcessed(id, std::nullopt, now);
        step();
        if (HasFatalFailure()) return;
      }
      for (int i = 0; i < 400'000 && c.state() != S::Idle; ++i) {  // replies still waiting
        for (auto id : std::exchange(toProcess, {})) c.onProcessed(id, std::nullopt, now);
        step();
        if (HasFatalFailure()) return;
      }
      ASSERT_EQ(c.state(), S::Idle) << at;
      p.rawEdge(now, "p", true);
      ASSERT_EQ(c.state(), S::TxOpen) << "radio unusable after faults, " << at;
    }
  // The test must really have exercised the paths it claims to.
  EXPECT_GT(timeouts, 500);
  EXPECT_GT(opened, 1000);
  EXPECT_GT(withdrawn, 10);
  EXPECT_GT(queuedOpens, 100);
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
        case 3: if (playing) { playing = false; c.onPlaybackDone(pid, t); } break;
        case 4: if (g() % 5 == 0) c.requestPilot({"q", 0}, t); break;
        default: c.tick(t);
      }
      begun = 0;
      for (auto& l : log) begun += l.rfind("begin", 0) == 0;
      ASSERT_FALSE(playing && c.pedalDown() && !c.pressBlocked() && !c.pressQueued()) << "seed " << seed << " step " << i;
      ASSERT_FALSE(playing && (c.state() == S::TxOpen || c.state() == S::TxTail)) << "seed " << seed << " step " << i;
      ASSERT_FALSE(c.pressQueued() && rule != PressRule::Queue) << "seed " << seed << " step " << i;
      ASSERT_FALSE((c.pressBlocked() || c.pressQueued()) && !c.pedalDown()) << "seed " << seed << " step " << i;
      if (rule == PressRule::Interrupt) { ASSERT_FALSE(c.pressBlocked() || c.pressQueued()); }
      ASSERT_EQ(playing, c.state() == S::PilotSpeaking) << "seed " << seed << " step " << i;
    }
    self = nullptr;
  }
}
