#include <gtest/gtest.h>

#include <QCoreApplication>
#include <algorithm>

#include "app/DashboardController.h"
using namespace atc;

namespace {
// D-110: chips, tiles and footer come from one state.
struct Dash : testing::Test {
  LabController lab;
  StatusFeed feed;
  SttController stt;
  DashboardController d;
  StatusInputs in;

  void SetUp() override {
    d.setFeed(&feed);
    d.setStt(&stt);
    d.setLab(&lab);
    in.mic = MicReady::Ok;
    in.speaker = SpeakerReady::Ok;
    in.ptt = PttReady::Ok;
    in.bank = BankReady::CoverageOk;
    model(SttState::Ready);
    send();
  }
  void send() { feed.set(in); }
  void model(SttState s, const char* id = "whisper-tiny-genprompt") {
    SttStatus st;
    st.state = s;
    st.modelId = id;
    stt.showScripted(st);
  }
  QVariantMap tile(ReadyTile t) const { return d.tiles()[int(t)].toMap(); }
  QVariantMap chip(int i) const { return d.chips()[i].toMap(); }
  void agree() const {
    ASSERT_EQ(d.tiles().size(), 6);
    ASSERT_EQ(d.chips().size(), 5);
    int worst = 0;
    for (int i = 0; i < 6; ++i) worst = std::max(worst, d.tiles()[i].toMap()["level"].toInt());
    for (int i = 0; i < 5; ++i) {
      EXPECT_EQ(chip(i)["value"].toString(), d.tiles()[i].toMap()["state"].toString()) << i;
      EXPECT_EQ(chip(i)["level"].toInt(), d.tiles()[i].toMap()["level"].toInt()) << i;
    }
    EXPECT_EQ(d.alertLevel(), worst);
  }
};
}  // namespace

TEST_F(Dash, AllReady) {
  agree();
  EXPECT_EQ(d.alertLevel(), int(LabController::Ok));
  EXPECT_EQ(d.alert(), "All systems ready");
}

TEST_F(Dash, EveryStateAgrees) {
  for (auto s : {MicReady::Ok, MicReady::NotTested, MicReady::OutsideBand, MicReady::NoSignal, MicReady::Muted, MicReady::Lost}) { in.mic = s; send(); agree(); }
  in.mic = MicReady::Ok;
  for (auto s : {SpeakerReady::NotChecked, SpeakerReady::Ok}) { in.speaker = s; send(); agree(); }
  for (bool blocks : {true, false})
    for (auto s : {PttReady::StandIn, PttReady::NoHold, PttReady::Missing, PttReady::Ok}) { in.pttMissingBlocks = blocks; in.ptt = s; send(); agree(); }
  for (auto s : {BankReady::Loaded, BankReady::Checking, BankReady::NotLoaded, BankReady::MissingClips, BankReady::Failed, BankReady::CoverageOk}) { in.bank = s; send(); agree(); }
  for (auto s : {StorageReady::Low, StorageReady::Ok}) { in.storage = s; send(); agree(); }
  for (auto s : {SttState::NotLoaded, SttState::Loading, SttState::Failed, SttState::Busy, SttState::Ready}) { model(s); agree(); }
  model(SttState::Ready, "vosk-grammar"); agree();
  EXPECT_EQ(chip(3)["value"].toString(), "OTHER MODEL");
  EXPECT_EQ(chip(3)["level"].toInt(), int(LabController::Fault));
}

TEST_F(Dash, MissingPttIsNeverAGreenFooter) {
  lab.setAlert(LabController::Ok, "Ready");  // what the Lab showed
  in.ptt = PttReady::Missing;
  send(); agree();
  EXPECT_EQ(d.alertLevel(), int(LabController::Fault));
  EXPECT_TRUE(d.alert().contains("PTT missing"));
  EXPECT_FALSE(d.alert().contains("Ready"));
  in.running = true;  // also while an exercise runs
  send(); agree();
  EXPECT_TRUE(d.alert().startsWith("Exercise running"));
}

TEST_F(Dash, MissingPttOnlyWarnsWithTheInstructorOption) {  // D-116
  in.ptt = PttReady::Missing;
  send(); agree();
  EXPECT_EQ(chip(2)["level"].toInt(), int(LabController::Fault));
  d.setPttFallback(true);
  agree();
  EXPECT_EQ(chip(2)["level"].toInt(), int(LabController::Caution));
  in.ptt = PttReady::NoHold;  // not covered by the option
  send(); agree();
  EXPECT_EQ(chip(2)["level"].toInt(), int(LabController::Fault));
}

TEST_F(Dash, LostPttIsSaidOnce) {  // D-126
  ExerciseController ex;
  d.setExercise(&ex);
  lab.setAlert(LabController::Fault, "PTT device lost", true);
  in.ptt = PttReady::Missing;
  send(); agree();
  EXPECT_EQ(d.alert(), "Not ready to start: PTT device lost");
  in.speaker = SpeakerReady::NotChecked;
  send();
  EXPECT_EQ(d.alert(), "Not ready to start: PTT device lost; 1 warn");
  in.running = true;
  ex.begin({}, false, false);
  ex.setPhase(ExerciseController::Paused);
  ex.setAlert(LabController::Fault, "PTT device lost: exercise paused");
  send();
  EXPECT_EQ(d.alert(), "PTT device lost: exercise paused; 1 warn");
  EXPECT_EQ(d.alertLevel(), int(LabController::Fault));
  in.mic = MicReady::Lost;  // another fault is not hidden by it
  send();
  EXPECT_EQ(d.alert(), "PTT device lost: exercise paused; Microphone lost; 1 warn");
  // With the instructor's option it warns, and is still said once.
  in = {};
  in.mic = MicReady::Ok;
  in.speaker = SpeakerReady::Ok;
  in.bank = BankReady::CoverageOk;
  ex.begin({}, false, false);
  d.setPttFallback(true);
  send();
  EXPECT_EQ(d.alert(), "PTT device lost, 1 warn, none block Start");
  // Another alert is shown beside it.
  lab.setAlert(LabController::Caution, "Short tap cancelled");
  EXPECT_EQ(d.alert(), "Short tap cancelled  |  1 warn, none block Start");
}

TEST_F(Dash, StartOnlyWhenNothingBlocks) {
  int starts = 0;
  QObject::connect(&feed, &StatusFeed::startExerciseRequested, [&starts] { ++starts; });
  EXPECT_TRUE(d.startEnabled());
  EXPECT_TRUE(d.reasons().isEmpty());
  d.startExercise();
  EXPECT_EQ(starts, 1);
  in.bank = BankReady::Checking;
  send();
  EXPECT_FALSE(d.startEnabled());
  d.startExercise();
  in.bank = BankReady::CoverageOk;
  in.running = true;
  send();
  d.startExercise();
  EXPECT_EQ(starts, 1);
}

TEST_F(Dash, ModelAndBankThatBlockStartAreFaultChips) {
  model(SttState::NotLoaded);
  in.bank = BankReady::NotLoaded;
  send(); agree();
  EXPECT_EQ(chip(3)["level"].toInt(), int(LabController::Fault));
  EXPECT_EQ(chip(4)["level"].toInt(), int(LabController::Fault));
}

TEST_F(Dash, StartWaitsForTheCoverageCheck) {  // D-115
  in.bank = BankReady::Checking;
  send(); agree();
  EXPECT_EQ(chip(4)["value"].toString(), "CHECKING");
  EXPECT_EQ(chip(4)["level"].toInt(), int(LabController::Fault));
  EXPECT_TRUE(d.alert().contains("Voice bank checking"));
}

TEST_F(Dash, BusyIsReady) {
  model(SttState::Busy);
  EXPECT_EQ(chip(3)["value"].toString(), "BUSY");
  EXPECT_EQ(chip(3)["level"].toInt(), int(LabController::Ok));
}

TEST_F(Dash, RadioAlertNeverHidesABlockingTile) {
  lab.setAlert(LabController::Caution, "Short tap cancelled");
  EXPECT_EQ(d.alertLevel(), int(LabController::Caution));
  EXPECT_EQ(d.alert(), "Short tap cancelled");
  in.ptt = PttReady::Missing;
  send();
  EXPECT_EQ(d.alertLevel(), int(LabController::Fault));
  EXPECT_TRUE(d.alert().startsWith("Short tap cancelled"));
  EXPECT_TRUE(d.alert().contains("PTT missing"));
}

TEST_F(Dash, HeardItOnlyAfterAToneAndNotInAnExercise) {
  int heard = 0;
  QObject::connect(&feed, &StatusFeed::toneHeardRequested, [&heard] { ++heard; });
  in.speaker = SpeakerReady::NotChecked;
  const auto enabled = [this] { return tile(ReadyTile::Speaker)["action2Enabled"].toBool(); };
  send();
  EXPECT_FALSE(enabled());
  d.toneHeard();
  EXPECT_EQ(heard, 0);
  in.toneAwaitingAnswer = true;
  in.running = true;
  send();
  EXPECT_FALSE(enabled());
  d.toneHeard();
  EXPECT_EQ(heard, 0);
  in.running = false;
  send(); agree();
  EXPECT_TRUE(enabled());
  EXPECT_EQ(chip(1)["value"].toString(), "NOT CHECKED");  // playing is not hearing
  d.toneHeard();
  EXPECT_EQ(heard, 1);
  for (int i = 0; i < 6; ++i)
    EXPECT_EQ(d.tiles()[i].toMap()["action2"].toString().isEmpty(), ReadyTile(i) != ReadyTile::Speaker) << i;
}

TEST_F(Dash, ExerciseAlertJoinsTheFooter) {
  ExerciseController ex;
  d.setExercise(&ex);
  ex.begin({}, false, false);
  ex.setPhase(ExerciseController::Running);
  ex.setAlert(LabController::Ok, "Text appears after release");
  EXPECT_EQ(d.alert(), "Text appears after release");
  ex.setAlert(LabController::Caution, "Line up: attempt 1 INCORRECT");
  EXPECT_EQ(d.alertLevel(), int(LabController::Caution));
  in.ptt = PttReady::Missing;
  in.running = true;
  send();
  EXPECT_EQ(d.alertLevel(), int(LabController::Fault));
  EXPECT_TRUE(d.alert().startsWith("Line up: attempt 1 INCORRECT"));
  EXPECT_TRUE(d.alert().contains("PTT missing"));
  EXPECT_FALSE(d.alert().contains("Exercise running"));  // it may be paused
}

TEST_F(Dash, LevelMeterDoesNotRedrawTheChips) {
  int n = 0;
  QObject::connect(&d, &DashboardController::chipsChanged, [&n] { ++n; });
  for (int i = 0; i < 20; ++i) { in.micDbfs = -40 + i; send(); }
  EXPECT_EQ(n, 0);
  in.mic = MicReady::Muted; send();
  EXPECT_EQ(n, 1);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  QCoreApplication app(argc, argv);
  return RUN_ALL_TESTS();
}
