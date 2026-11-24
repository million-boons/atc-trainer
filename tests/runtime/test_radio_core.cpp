#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <random>
#include <thread>

#include "runtime/RadioCore.h"
using namespace atc;
using S = TransmissionController::State;
using TK = PttTest::Kind;

namespace {

struct Rig;
struct FakeMic : IRadioMic {
  explicit FakeMic(Rig& r) : rig(r) {}
  bool start() override;
  Rig& rig;
};
struct FakePtt : IRadioPtt {
  explicit FakePtt(bool& s) : started(s) {}
  void start() override { started = true; }
  bool& started;
};
struct FakePlayer : IRadioPlayer {
  explicit FakePlayer(Rig& r) : rig(r) {}
  bool start() override { return true; }
  bool play(PlayId id, std::vector<std::int16_t> samples, int rate) override;
  bool stop() override;
  Rig& rig;
};
struct FakeSpeech : ISpeechFeed {
  bool begin(std::uint64_t tx) override { if (model) open = tx; return model; }
  void accept(std::vector<std::int16_t> a) override { samples += a.size(); }
  void finish(Ns) override { finished.push_back(std::exchange(open, 0)); }
  void cancel() override { cancelled.push_back(std::exchange(open, 0)); }
  bool model = true;
  std::uint64_t open = 0;
  std::size_t samples = 0;
  std::vector<std::uint64_t> finished, cancelled;
};

// A radio run by hand: time moves in 10 ms steps and the microphone hears `level`.
struct Rig {
  Ns now = ms(1000);
  PrerollRing* ring = nullptr;
  LevelMeter* meter = nullptr;
  std::function<void(const CaptureStatus&)> micStatus;
  PttCallbacks ptt;
  PlayerCallbacks out;
  FakeSpeech speech;
  bool pttStarted = false, micOk = true, playOk = true, stopOk = true, answer = true;
  int level = 3000;
  bool delivering = true;
  PlayId sounding = 0, lastPlay = 0;
  int plays = 0, stops = 0, sideTones = 0, silentAtOpen = 0, pressed = 0;
  std::optional<Reply> reply;  // given to the next closed transmission
  RadioSnapshot snap;
  std::vector<PttNote> notes;
  std::vector<std::uint64_t> opened, cancelled;
  std::vector<RadioTx> closed;
  std::vector<std::uint64_t> started, ended, heard;
  std::vector<PilotStop> stopped;
  std::vector<std::string> faults;
  std::vector<LocalEnd> local;
  std::vector<PttTest> tests;
  bool haveAudio = true;
  std::function<void()> onPilotStarted;
  Ns startedAt = 0;
  std::unique_ptr<RadioCore> core;

  explicit Rig(RadioConfig cfg = {}, bool withPtt = true) {
    RadioDevices d;
    d.mic = [this](PrerollRing& r, LevelMeter& m, std::function<void(const CaptureStatus&)> cb) {
      ring = &r;
      meter = &m;
      micStatus = std::move(cb);
      return std::make_unique<FakeMic>(*this);
    };
    d.player = [this](PlayerCallbacks cb) { out = std::move(cb); return std::make_unique<FakePlayer>(*this); };
    if (withPtt) d.ptt = [this](PttCallbacks cb) { ptt = std::move(cb); return std::make_unique<FakePtt>(pttStarted); };
    d.speech = &speech;
    RadioCore::Events e;
    e.snapshot = [this](const RadioSnapshot& s) { snap = s; };
    e.note = [this](const PttNote& n) { notes.push_back(n); };
    e.pressed = [this] { ++pressed; };
    e.txOpened = [this](std::uint64_t id, Ns, bool) {
      opened.push_back(id);
      silentAtOpen += sounding == 0;
    };
    e.txClosed = [this](RadioTx&& t) {
      if (answer && !t.recognising) core->processed(t.id, std::exchange(reply, {}));
      closed.push_back(std::move(t));
    };
    e.txCancelled = [this](std::uint64_t id, bool) { cancelled.push_back(id); };
    e.sideTone = [this] { ++sideTones; };
    e.pilotStarted = [this](const Reply& r) { started.push_back(r.id); startedAt = now; if (onPilotStarted) onPilotStarted(); };
    e.pilotEnded = [this](const Reply& r, bool ok) { ended.push_back(r.id); if (ok) heard.push_back(r.id); };
    e.pilotStopped = [this](const Reply&, PilotStop w) { stopped.push_back(w); };
    e.outputFault = [this](const std::string& s) { faults.push_back(s); };
    e.local = [this](LocalSound, LocalEnd l) { local.push_back(l); };
    e.pttTest = [this](const PttTest& t) { tests.push_back(t); };
    e.pilotSound = [this](const Reply&) { return haveAudio ? PilotSound{std::vector<std::int16_t>(160, 1000), 16000} : PilotSound{}; };
    cfg.publishEvery = ms(10);
    core = std::make_unique<RadioCore>(cfg, std::move(d), std::move(e), [this] { return now; });
    core->open();
    if (withPtt) ptt.opened(false);
    run(1000);  // pre-roll
  }
  void run(int millis) {
    for (int i = 0; i < millis / 10; ++i) {
      now += ms(10);
      std::vector<std::int16_t> b(160);
      for (int k = 0; k < 160; ++k) b[std::size_t(k)] = std::int16_t(level * std::sin(k * 0.4));
      if (delivering) {
        ring->push(now, b);
        meter->add(b);
      }
      core->step();
    }
  }
  void down() { ptt.edge(now, true); core->step(); }
  void up() { ptt.edge(now, false); core->step(); }
  void finish(bool ok = true) { out.finished(std::exchange(sounding, 0), ok); core->step(); }
  void speak(int millis = 1000) { down(); run(millis); up(); run(400); }
  Reply pilot(std::uint64_t id) { return {.text = "x", .expected = ms(2000), .id = id}; }
  void pilotNow(std::uint64_t id) { core->requestPilot(pilot(id)); core->step(); }
  bool has(PttNote::Kind k) const { return std::any_of(notes.begin(), notes.end(), [k](auto& n) { return n.kind == k; }); }
};
bool FakeMic::start() { return rig.micOk; }
bool FakePlayer::play(PlayId id, std::vector<std::int16_t> s, int rate) {
  ++rig.plays;
  EXPECT_FALSE(s.empty());
  EXPECT_GT(rate, 0);
  rig.sounding = rig.playOk ? id : 0;
  if (rig.playOk) rig.lastPlay = id;
  return rig.playOk;
}
bool FakePlayer::stop() {
  ++rig.stops;
  rig.sounding = 0;
  return rig.stopOk;
}

}  // namespace

TEST(RadioCore, TransmissionGivesAudioAndText) {
  Rig r;
  r.speak();
  ASSERT_EQ(r.closed.size(), 1u);
  const RadioTx& t = r.closed[0];
  EXPECT_TRUE(t.recognising);
  EXPECT_FALSE(t.gap);
  EXPECT_FALSE(t.noSpeech);
  EXPECT_NEAR(double(t.audio.size()), 1.5 * 16000, 320);  // 300 ms + 1 s + 200 ms
  EXPECT_NEAR(t.preRoll, 4800, 1);
  EXPECT_EQ(r.speech.finished, std::vector<std::uint64_t>{1});
  EXPECT_NEAR(double(r.speech.samples), double(t.audio.size()), 320);
  EXPECT_EQ(r.snap.state, S::Recognising);
  EXPECT_TRUE(r.snap.active);
  r.core->processed(1);
  r.core->step();
  EXPECT_EQ(r.snap.state, S::Idle);
  EXPECT_FALSE(r.snap.active);
}

TEST(RadioCore, NoModelStillRecords) {
  Rig r;
  r.speech.model = false;
  r.speak();
  ASSERT_EQ(r.closed.size(), 1u);
  EXPECT_FALSE(r.closed[0].hadModel);
  EXPECT_FALSE(r.closed[0].recognising);
  EXPECT_FALSE(r.closed[0].audio.empty());
  EXPECT_EQ(r.speech.samples, 0u);
}

TEST(RadioCore, SilenceIsNotRecognised) {
  Rig r;
  r.level = 0;
  r.run(2000);
  r.speak();
  ASSERT_EQ(r.closed.size(), 1u);
  EXPECT_TRUE(r.closed[0].noSpeech);
  EXPECT_FALSE(r.closed[0].recognising);
  EXPECT_EQ(r.speech.cancelled, std::vector<std::uint64_t>{1});
  EXPECT_TRUE(r.snap.micSilent);
}

TEST(RadioCore, RunningMicrophoneThatDeliversNothingIsSilent) {
  Rig r;
  CaptureStatus c;
  c.state = CaptureState::Running;
  r.micStatus(c);
  r.run(2000);
  EXPECT_FALSE(r.snap.micSilent);
  r.delivering = false;
  r.run(900);
  EXPECT_FALSE(r.snap.micSilent);
  r.run(300);
  EXPECT_TRUE(r.snap.micSilent);
  r.delivering = true;
  r.run(50);
  EXPECT_FALSE(r.snap.micSilent);
}

// The tail that never arrives must not hold the radio.
TEST(RadioCore, TransmissionClosesWithoutItsLastAudio) {
  Rig r;
  r.down(); r.run(600);
  r.delivering = false;
  r.up(); r.run(1500);
  ASSERT_EQ(r.closed.size(), 1u);
  EXPECT_TRUE(r.closed[0].gap);
}

TEST(RadioCore, ShortTapIsCancelled) {
  Rig r;
  r.down(); r.run(100); r.up(); r.run(400);
  EXPECT_TRUE(r.closed.empty());
  EXPECT_EQ(r.cancelled, std::vector<std::uint64_t>{1});
  EXPECT_EQ(r.speech.cancelled, std::vector<std::uint64_t>{1});
  EXPECT_EQ(r.snap.state, S::Idle);
}

TEST(RadioCore, StuckPedalClosesAtTheLimitAndIsNotRecognised) {
  Rig r;
  r.down();
  r.run(31'000);
  ASSERT_EQ(r.closed.size(), 1u);
  EXPECT_EQ(r.closed[0].end, TxEnd::Stuck);
  EXPECT_FALSE(r.closed[0].recognising);
  EXPECT_EQ(r.snap.display, TxDisplay::Stuck);
  EXPECT_TRUE(r.has(PttNote::Kind::Stuck));
  r.up(); r.run(200);
  EXPECT_EQ(r.opened.size(), 1u);
  EXPECT_EQ(r.snap.display, TxDisplay::Idle);
}

TEST(RadioCore, LostPttClosesTheTransmission) {
  Rig r;
  r.down(); r.run(600);
  r.ptt.lost(r.now);
  r.run(400);
  ASSERT_EQ(r.closed.size(), 1u);
  EXPECT_EQ(r.closed[0].end, TxEnd::DeviceLost);
  EXPECT_EQ(r.snap.ptt, PttLink::Lost);
  EXPECT_EQ(r.snap.display, TxDisplay::NoPtt);
  r.ptt.error("no such device");
  r.core->step();
  EXPECT_EQ(r.snap.ptt, PttLink::Lost);
  r.ptt.opened(false);
  r.core->step();
  EXPECT_EQ(r.snap.ptt, PttLink::Ok);
  EXPECT_TRUE(r.snap.pttError.empty());
}

TEST(RadioCore, HeldAtOpenNeedsAReleaseFirst) {
  Rig r;
  r.ptt.opened(true);
  r.run(500);
  EXPECT_TRUE(r.has(PttNote::Kind::HeldAtOpen));
  r.up(); r.run(100);
  EXPECT_TRUE(r.opened.empty());
  r.speak();
  EXPECT_EQ(r.opened.size(), 1u);
}

TEST(RadioCore, PulseDeviceIsSuspectedUntilAProperHold) {
  Rig r;
  r.down(); r.run(20); r.up(); r.run(400);
  EXPECT_TRUE(r.snap.pulse);
  EXPECT_EQ(r.snap.display, TxDisplay::NoHold);
  r.speak();
  r.core->processed(2);
  r.run(20);
  EXPECT_FALSE(r.snap.pulse);
  // A short tap on the screen says nothing about the pedal.
  r.core->press("SCREEN"); r.run(20); r.core->release("SCREEN"); r.run(400);
  EXPECT_FALSE(r.snap.pulse);
}

TEST(RadioCore, NoDeviceIsNotSetAndTheScreenStillWorks) {
  Rig r({}, false);
  EXPECT_EQ(r.snap.ptt, PttLink::NotSet);
  r.core->press("SCREEN"); r.run(600); r.core->release("SCREEN"); r.run(400);
  EXPECT_EQ(r.closed.size(), 1u);
}

TEST(RadioCore, ReplyIsPlayedAfterTheTransmission) {
  Rig r;
  r.speech.model = false;
  r.reply = r.pilot(7);
  r.speak();
  EXPECT_EQ(r.started, std::vector<std::uint64_t>{7});
  EXPECT_EQ(r.snap.state, S::PilotSpeaking);
  EXPECT_NE(r.sounding, 0u);
  r.finish();
  EXPECT_EQ(r.heard, std::vector<std::uint64_t>{7});
  EXPECT_EQ(r.snap.state, S::Idle);
}

TEST(RadioCore, InterruptingPressSilencesThePilotBeforeOpening) {
  Rig r;
  r.pilotNow(1);
  r.run(100);
  r.down();
  EXPECT_EQ(r.stopped, std::vector<PilotStop>{PilotStop::Interrupted});
  EXPECT_EQ(r.opened.size(), 1u);
  EXPECT_EQ(r.silentAtOpen, 1);
  EXPECT_TRUE(r.snap.pilotCut);
  r.run(2100);
  EXPECT_FALSE(r.snap.pilotCut);
}

TEST(RadioCore, UnconfirmedSilenceIsAFault) {
  Rig r;
  r.stopOk = false;
  r.pilotNow(1);
  r.run(100);
  r.down();
  ASSERT_EQ(r.faults.size(), 1u);
  EXPECT_EQ(r.opened.size(), 1u);
}

TEST(RadioCore, BlockAndQueueRules) {
  RadioConfig b;
  b.tx.pressDuringPilot = PressRule::Block;
  Rig r(b);
  r.pilotNow(1);
  r.run(50); r.down(); r.run(50);
  EXPECT_EQ(r.snap.display, TxDisplay::Blocked);
  EXPECT_TRUE(r.opened.empty());
  EXPECT_NE(r.sounding, 0u);

  RadioConfig q;
  q.tx.pressDuringPilot = PressRule::Queue;
  Rig k(q);
  k.pilotNow(1);
  k.run(50); k.down(); k.run(50);
  EXPECT_EQ(k.snap.display, TxDisplay::Queued);
  k.finish();
  k.run(100);
  EXPECT_EQ(k.opened.size(), 1u);
  EXPECT_EQ(k.silentAtOpen, 1);
  EXPECT_EQ(k.sideTones, 1);
}

TEST(RadioCore, MissingAudioOrFailedPlaybackNeverHangsTheRadio) {
  Rig r;
  r.haveAudio = false;
  r.pilotNow(1);
  EXPECT_EQ(r.faults.size(), 1u);
  EXPECT_EQ(r.ended, std::vector<std::uint64_t>{1});
  EXPECT_TRUE(r.started.empty());
  r.haveAudio = true;
  r.playOk = false;
  r.pilotNow(2);
  EXPECT_EQ(r.faults.size(), 2u);
  r.playOk = true;
  r.pilotNow(3);
  r.finish(false);
  EXPECT_EQ(r.faults.size(), 3u);
  EXPECT_EQ(r.snap.state, S::Idle);
  EXPECT_TRUE(r.heard.empty());
}

TEST(RadioCore, PlaybackThatNeverEndsIsStoppedByTheWatchdog) {
  Rig r;
  r.pilotNow(1);
  r.run(3600);
  EXPECT_EQ(r.stopped, std::vector<PilotStop>{PilotStop::Timeout});
  EXPECT_EQ(r.sounding, 0u);
  // The late report must not touch the next reply.
  const PlayId old = r.lastPlay;
  r.pilotNow(2);
  r.out.finished(old, true);
  r.core->step();
  EXPECT_EQ(r.snap.state, S::PilotSpeaking);
  EXPECT_TRUE(r.ended.empty());
}

TEST(RadioCore, RepliesEndInOrderEvenIfPlaybackFailsAtOnce) {
  Rig r;
  r.haveAudio = false;
  r.core->requestPilot(r.pilot(1));
  r.core->requestPilot(r.pilot(2));
  r.run(50);
  EXPECT_EQ(r.ended, (std::vector<std::uint64_t>{1, 2}));
}

TEST(RadioCore, LocalSoundIsSilencedByAPress) {
  Rig r;
  r.core->playLocal(LocalSound::Recording, std::vector<std::int16_t>(100, 1), 16000);
  r.core->step();
  EXPECT_NE(r.sounding, 0u);
  EXPECT_FALSE(r.snap.pilotSpeaking());
  r.down();
  EXPECT_EQ(r.silentAtOpen, 1);
  EXPECT_EQ(r.local, (std::vector<LocalEnd>{LocalEnd::Started, LocalEnd::Interrupted}));
  // Refused while transmitting, in the tail and while recognising.
  r.core->playLocal(LocalSound::Tone, std::vector<std::int16_t>(100, 1), 16000);
  r.run(500);
  EXPECT_EQ(r.local.back(), LocalEnd::Refused);
  r.up();
  r.run(50);
  r.core->playLocal(LocalSound::Tone, std::vector<std::int16_t>(100, 1), 16000);
  r.run(350);
  r.core->playLocal(LocalSound::Tone, std::vector<std::int16_t>(100, 1), 16000);
  r.core->step();
  EXPECT_EQ(r.plays, 1);
}

TEST(RadioCore, LocalSoundEndsFailsAndIsReplaced) {
  Rig r;
  const auto play = [&] { r.core->playLocal(LocalSound::Tone, std::vector<std::int16_t>(100, 1), 16000); r.core->step(); };
  play();
  r.finish();
  play();
  play();
  r.core->stopLocal();
  r.core->step();
  r.playOk = false;
  play();
  EXPECT_EQ(r.local, (std::vector<LocalEnd>{LocalEnd::Started, LocalEnd::Completed, LocalEnd::Started, LocalEnd::Interrupted,
                                            LocalEnd::Started, LocalEnd::Interrupted, LocalEnd::Failed}));
}

TEST(RadioCore, ThePilotReplacesLocalSound) {
  Rig r;
  r.core->playLocal(LocalSound::Recording, std::vector<std::int16_t>(100, 1), 16000);
  r.core->step();
  const PlayId local = r.lastPlay;
  r.pilotNow(1);
  EXPECT_EQ(r.local.back(), LocalEnd::Interrupted);
  r.out.finished(local, true);  // late
  r.core->step();
  EXPECT_EQ(r.snap.state, S::PilotSpeaking);
  r.core->stopLocal();  // must not stop the pilot
  r.core->step();
  EXPECT_NE(r.sounding, 0u);
}

TEST(RadioCore, PttTestMeasuresAndNeverTransmits) {
  Rig r;
  r.core->setPttTest(true);
  r.down();
  r.run(2500);
  EXPECT_EQ(r.snap.display, TxDisplay::Idle);
  EXPECT_TRUE(r.snap.testing);
  r.up(); r.run(400);
  EXPECT_TRUE(r.opened.empty());
  EXPECT_EQ(r.pressed, 0);
  EXPECT_TRUE(r.closed.empty());
  ASSERT_EQ(r.tests.size(), 3u);
  EXPECT_EQ(r.tests[1].kind, TK::Down);
  EXPECT_EQ(r.tests[2].kind, TK::Up);
  EXPECT_EQ(r.tests[2].held, ms(2500));
  r.core->setPttTest(false);
  r.speak();
  EXPECT_EQ(r.opened.size(), 1u);
}

TEST(RadioCore, EndingTheTestWhileHeldOpensNothingUntilTheNextPress) {
  Rig r;
  r.core->setPttTest(true);
  r.down();
  r.run(200);
  r.core->setPttTest(false);
  r.run(2000);
  EXPECT_TRUE(r.opened.empty());
  r.up(); r.run(100);
  EXPECT_TRUE(r.opened.empty());
  r.down();
  EXPECT_EQ(r.opened.size(), 1u);
}

TEST(RadioCore, ThePilotWaitsForTheTestAndForTheHeldPress) {
  Rig r;
  r.core->setPttTest(true);
  r.down();
  r.pilotNow(1);
  r.run(200);
  r.core->setPttTest(false);
  r.run(200);
  EXPECT_TRUE(r.started.empty());  // PTT is still down
  r.up(); r.run(100);
  EXPECT_EQ(r.started, std::vector<std::uint64_t>{1});
}

TEST(RadioCore, PttTestIsRefusedUnlessIdle) {
  Rig r;
  r.down();
  r.core->setPttTest(true);
  r.run(500);
  ASSERT_EQ(r.tests.size(), 1u);
  EXPECT_EQ(r.tests[0].kind, TK::Refused);
  EXPECT_FALSE(r.snap.testing);
  r.up(); r.run(400);
  EXPECT_EQ(r.closed.size(), 1u);
}

TEST(RadioCore, TestEndsCleanlyIfTheDeviceIsLost) {
  Rig r;
  r.core->setPttTest(true);
  r.down(); r.run(100);
  r.ptt.lost(r.now);
  r.run(50);
  EXPECT_EQ(r.tests.back().reason, ReleaseReason::DeviceLost);
  EXPECT_TRUE(r.opened.empty());
  r.core->setPttTest(false);
  r.core->step();
  r.ptt.opened(false);
  r.speak();
  EXPECT_EQ(r.opened.size(), 1u);
}

// D-300, with the whole radio: whatever happens, nothing sounds while a transmission is
// open and the pilot never starts while PTT is down.
TEST(RadioCore, RandomFaultsNeverBreakHalfDuplex) {
  for (unsigned seed = 1; seed <= 60; ++seed) {
    std::mt19937 rng(seed);
    RadioConfig cfg;
    cfg.tx.pressDuringPilot = static_cast<PressRule>(seed % 3);
    Rig r(cfg);
    bool down = false;
    Ns downAt = 0;
    // A press queued at the very same instant is handled next and withdraws the pilot (D-303).
    r.onPilotStarted = [&] { EXPECT_TRUE(!down || downAt == r.now) << "seed " << seed; };
    std::uint64_t id = 0;
    for (int i = 0; i < 400; ++i) {
      switch (rng() % 12) {
        case 0: case 1: if (!down) { down = true; downAt = r.now; r.ptt.edge(r.now, true); } break;
        case 2: case 3: if (down) { down = false; r.ptt.edge(r.now, false); } break;
        case 4: r.core->requestPilot(r.pilot(++id)); break;
        case 5: if (r.sounding) r.out.finished(std::exchange(r.sounding, 0), rng() % 2); break;  // sound is over when reported
        case 6: if (const PlayId p = rng() % (r.lastPlay + 2); p != r.lastPlay) r.out.finished(p, true); break;  // late or unknown
        case 7: r.core->playLocal(LocalSound::Recording, std::vector<std::int16_t>(100, 1), 16000); break;
        case 8: r.core->setPttTest(rng() % 2); break;
        case 9: for (const auto& t : r.closed) r.core->processed(t.id); break;
        case 10: r.playOk = rng() % 4; break;
        case 11: r.haveAudio = rng() % 4; break;
      }
      r.run(int(rng() % 30) * 10);
      r.core->step();
      EXPECT_TRUE(!(down && r.sounding && r.snap.state == S::PilotSpeaking && downAt <= r.startedAt)) << "seed " << seed;
      if (r.snap.state == S::TxOpen) {
        EXPECT_EQ(r.sounding, 0u) << "seed " << seed << " step " << i;
      }
    }
    EXPECT_EQ(r.silentAtOpen, int(r.opened.size())) << "seed " << seed;
  }
}

TEST(RadioCore, RealThreadStartsAndStops) {
  std::atomic<int> snaps{0};
  RadioDevices d;
  RadioCore::Events e;
  e.snapshot = [&](const RadioSnapshot&) { ++snaps; };
  RadioCore core({}, std::move(d), std::move(e));
  EXPECT_FALSE(core.start());  // no microphone
  core.press("SCREEN");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  core.release("SCREEN");
  core.stop();
  EXPECT_GT(snaps, 0);
}
