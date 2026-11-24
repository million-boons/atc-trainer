#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include "app/StatusController.h"
#include "app/StatusWords.h"
using namespace atc;
using TK = PttTest::Kind;

namespace {
void spin(int millis) {
  QEventLoop l;
  QTimer::singleShot(millis, &l, &QEventLoop::quit);
  l.exec();
}

struct Status : testing::Test {
  LabController lab;
  std::vector<bool> tests;
  int tones = 0, stops = 0, changes = 0;
  StatusConfig cfg{.pttName = "mouse", .storagePath = "/nonexistent/atc"};
  std::unique_ptr<StatusController> c;
  RadioSnapshot snap;

  void SetUp() override {
    c = std::make_unique<StatusController>(&lab, cfg, StatusController::Radio{[this](bool on) { tests.push_back(on); }, [this] { ++tones; }, [this] { ++stops; }});
    snap.mic.state = CaptureState::Running;
    snap.mic.source = "h540";
    snap.mic.description = "H540";
    snap.ptt = PttLink::Ok;
    send();
    QObject::connect(c->feed(), &StatusFeed::changed, [this] { ++changes; });
  }
  void send() { c->onSnapshot(snap); }
  const StatusInputs& in() { return c->feed()->inputs(); }
  QString chip(int i) { return lab.chips()[i].toMap()["value"].toString(); }
  // What D-103 asks for: tile and chip from one state.
  void agree() {
    EXPECT_EQ(chip(LabController::Mic), status_words::word(in().mic));
    EXPECT_EQ(chip(LabController::Speaker), status_words::word(in().speaker));
    EXPECT_EQ(chip(LabController::Ptt), status_words::word(in().ptt));
  }
  OutputStatus out(const char* n = "out", bool mute = false, int vol = 50) { return {true, n, n, mute, vol}; }
};
}  // namespace

TEST_F(Status, MicrophoneStates) {
  EXPECT_EQ(in().mic, MicReady::Ok);
  EXPECT_EQ(in().micName, "H540");
  snap.micSilent = true; send(); agree();
  EXPECT_EQ(in().mic, MicReady::NoSignal);
  snap.mic.muted = true; send(); agree();
  EXPECT_EQ(in().mic, MicReady::Muted);
  for (auto s : {CaptureState::Lost, CaptureState::Connecting, CaptureState::Stopped}) {
    snap.mic.state = s; send(); agree();
    EXPECT_EQ(in().mic, MicReady::Lost);
  }
}

TEST_F(Status, PttStates) {
  EXPECT_EQ(in().ptt, PttReady::StandIn);
  snap.pulse = true; send(); agree();
  EXPECT_EQ(in().ptt, PttReady::NoHold);
  for (auto l : {PttLink::NotSet, PttLink::Opening, PttLink::NotOpen, PttLink::Lost}) {
    snap.ptt = l; send(); agree();
    EXPECT_EQ(in().ptt, PttReady::Missing);
    EXPECT_TRUE(in().pttMissingBlocks);
  }
}

TEST_F(Status, PedalIsOkAndOnlyItsOwnPressCounts) {
  cfg.pttStandIn = false;
  SetUp();
  EXPECT_EQ(in().ptt, PttReady::Ok);
  snap.held = true; snap.owner = "SPACE"; send();
  EXPECT_FALSE(in().pttDown);
  snap.owner = "RIGHT"; send();
  EXPECT_TRUE(in().pttDown);
}

TEST_F(Status, NothingInventedAtStart) {
  LabController l2;
  StatusController fresh(&l2, {}, {[](bool) {}, [] {}, [] {}});
  const StatusInputs& i = fresh.feed()->inputs();
  EXPECT_EQ(i.mic, MicReady::Lost);
  EXPECT_EQ(i.ptt, PttReady::Missing);
  EXPECT_EQ(i.speaker, SpeakerReady::NotChecked);
  EXPECT_EQ(i.bank, BankReady::NotLoaded);
  EXPECT_FALSE(i.running);
  EXPECT_TRUE(i.scenario.isEmpty());
  EXPECT_FALSE(i.storageFree.isEmpty());
}

TEST_F(Status, SteadyStateDoesNotRedraw) {
  changes = 0;
  for (int i = 0; i < 100; ++i) send();
  EXPECT_LE(changes, 1);
}

TEST_F(Status, ToneMarksTheSpeakerOnlyWhenPlayedToTheEnd) {
  emit c->feed()->toneRequested();
  EXPECT_EQ(tones, 0);  // no output yet
  c->onOutput(out());
  for (auto e : {LocalEnd::Refused, LocalEnd::Failed, LocalEnd::Started, LocalEnd::Interrupted}) {
    c->onTone(e);
    EXPECT_EQ(in().speaker, SpeakerReady::NotChecked);
  }
  emit c->feed()->toneRequested();
  EXPECT_EQ(tones, 1);
  c->toneHeard();  // still playing
  EXPECT_EQ(in().speaker, SpeakerReady::NotChecked);
  c->onTone(LocalEnd::Completed); agree();
  EXPECT_EQ(in().speaker, SpeakerReady::NotChecked);  // playing is not hearing
  EXPECT_TRUE(c->toneAwaitingAnswer());
  c->toneHeard(); agree();
  EXPECT_EQ(in().speaker, SpeakerReady::Ok);
  EXPECT_FALSE(c->toneAwaitingAnswer());
}

TEST_F(Status, AnAnswerToAnOldToneDoesNotCount) {
  c->onOutput(out());
  const auto played = [this] { c->onTone(LocalEnd::Started); c->onTone(LocalEnd::Completed); };
  played();
  emit c->feed()->cancelTestsRequested();  // the screen was left
  c->toneHeard();
  EXPECT_EQ(in().speaker, SpeakerReady::NotChecked);
  played();
  c->onOutput(out("other"));
  c->toneHeard();
  EXPECT_EQ(in().speaker, SpeakerReady::NotChecked);
  played();
  c->onTone(LocalEnd::Started);  // played again
  c->toneHeard();
  EXPECT_EQ(in().speaker, SpeakerReady::NotChecked);
}

TEST_F(Status, SpeakerCheckIsLostWithTheOutputOrItsSound) {
  for (const auto& s : {out("other"), out("out", true), out("out", false, 0), OutputStatus{}}) {
    c->onOutput(out());
    c->onTone(LocalEnd::Completed);
    c->toneHeard();
    ASSERT_EQ(in().speaker, SpeakerReady::Ok);
    c->onOutput(s); agree();
    EXPECT_EQ(in().speaker, SpeakerReady::NotChecked);
  }
  c->onOutput(out("out", true));
  emit c->feed()->toneRequested();
  EXPECT_EQ(tones, 0);
  c->onTone(LocalEnd::Completed);  // muted while it played
  c->toneHeard();
  EXPECT_EQ(in().speaker, SpeakerReady::NotChecked);
}

TEST_F(Status, PttTestMeasuresTheHold) {
  emit c->feed()->pttTestRequested();
  EXPECT_EQ(tests, std::vector<bool>{true});
  c->onPttTest({.kind = TK::Started});
  EXPECT_TRUE(in().pttTesting);
  c->onPttTest({.kind = TK::Down, .source = "SPACE"});
  c->onPttTest({.kind = TK::Up, .source = "SPACE", .held = ms(3000)});
  EXPECT_TRUE(in().pttTesting);  // the keyboard proves nothing about the pedal
  c->onPttTest({.kind = TK::Down, .source = "RIGHT"});
  c->onPttTest({.kind = TK::Up, .source = "RIGHT", .held = ms(2100)});
  EXPECT_FALSE(in().pttTesting);
  EXPECT_EQ(tests.back(), false);
  c->onPttTest({.kind = TK::Ended});
  send();
  EXPECT_EQ(in().pttNote, "Held 2.10 s: OK");
  c->onPttTest({.kind = TK::Started});
  c->onPttTest({.kind = TK::Down, .source = "RIGHT"});
  c->onPttTest({.kind = TK::Up, .source = "RIGHT", .held = ms(1900)});
  EXPECT_EQ(in().pttNote, "Held 1.90 s: too short");
}

TEST_F(Status, CancelledOrRefusedTestGivesNoResult) {
  c->onPttTest({.kind = TK::Started});
  c->onPttTest({.kind = TK::Down, .source = "RIGHT"});
  emit c->feed()->cancelTestsRequested();
  EXPECT_EQ(tests.back(), false);
  EXPECT_EQ(stops, 1);
  c->onPttTest({.kind = TK::Ended});
  EXPECT_FALSE(in().pttTesting);
  EXPECT_TRUE(in().pttNote.isEmpty());
  c->onPttTest({.kind = TK::Up, .source = "RIGHT", .held = ms(5000)});  // still held when cancelled
  EXPECT_TRUE(in().pttNote.isEmpty());
  c->onPttTest({.kind = TK::Refused});
  EXPECT_FALSE(in().pttTesting);
  EXPECT_EQ(in().pttNote, "radio busy");
}

TEST_F(Status, MicTestJudgesTheBand) {
  lab.micLow = -20; lab.micHigh = -6;
  snap.micDbfs = -50; send();
  EXPECT_EQ(in().mic, MicReady::Ok);  // silence between transmissions is not a fault
  emit c->feed()->micTestRequested();
  EXPECT_EQ(in().micTestLeft, 5);
  spin(150); send();
  spin(5100);
  EXPECT_EQ(in().micTestLeft, 0);
  EXPECT_EQ(in().mic, MicReady::OutsideBand); agree();
  emit c->feed()->micTestRequested();
  snap.micDbfs = -12; send();
  spin(5200);
  EXPECT_EQ(in().mic, MicReady::Ok); agree();
}

TEST_F(Status, BankChipAndTileTogether) {
  c->setBank(BankReady::MissingClips, {"a", "b"});
  EXPECT_EQ(in().bank, BankReady::MissingClips);
  EXPECT_EQ(in().missingClips.size(), 2);
  EXPECT_TRUE(chip(LabController::VoiceBank).contains("2"));
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  QCoreApplication app(argc, argv);
  return RUN_ALL_TESTS();
}
