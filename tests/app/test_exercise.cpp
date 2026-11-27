#include <gtest/gtest.h>

#include <QCoreApplication>

#include "app/ExerciseController.h"
#include "app/ExerciseMock.h"
#include "app/LabController.h"
using C = ExerciseController;

namespace {
QString all(C& c) {  // every text the Exercise screen can show
  QString s = c.notice() + c.alert() + c.progressText() + c.missedText() + c.clearances().join(' ') + c.clearancesNote();
  for (const QVariant& v : c.steps()) s += v.toMap()["note"].toString();
  return s;
}
}  // namespace

TEST(Exercise, PracticeShowsTheVerdict) {
  C c;
  exercise_mock::show(c, "exwrong", false, false);
  EXPECT_TRUE(c.notice().contains("INCORRECT"));
  EXPECT_TRUE(c.alert().contains("INCORRECT"));
  EXPECT_EQ(c.progressText(), "1 of 3 done");
  EXPECT_TRUE(all(c).contains("attempts"));
  EXPECT_EQ(c.clearances().size(), 1);
}

TEST(Exercise, AssessedHidesItInEveryCase) {
  for (const QVariant& v : exercise_mock::cases()) {
    const QString id = v.toMap()["id"].toString();
    if (id.startsWith("exresult")) continue;
    C c;
    exercise_mock::show(c, id, true, false);
    const QString s = all(c);
    for (const char* w : {"INCORRECT", "WRONG", "attempt", "not counted", "Not counted", "Suspected", " of 3 done", "Done", "Cleared"})
      EXPECT_FALSE(s.contains(w)) << id.toStdString() << ": " << w;
    EXPECT_EQ(c.missedText(), "-");
    EXPECT_TRUE(c.clearances().isEmpty());
    for (const QVariant& st : c.steps()) EXPECT_EQ(st.toMap()["state"].toInt(), int(C::Hidden));
  }
}

TEST(Exercise, AssessedStillShowsPttAndFaultNotices) {
  C c;
  exercise_mock::show(c, "exwd", true, false);
  EXPECT_EQ(c.noticeLevel(), int(LabController::Fault));
  EXPECT_EQ(c.alertLevel(), int(LabController::Fault));
  exercise_mock::show(c, "excut", true, false);
  EXPECT_FALSE(c.notice().isEmpty());
}

TEST(Exercise, ResultIsFullInBothModes) {
  for (bool assessed : {false, true}) {
    C c;
    exercise_mock::show(c, "exresult", assessed, false);
    EXPECT_EQ(c.result()["steps"].toList().size(), 3);
    EXPECT_EQ(c.timeline()->rowCount(), 9);
  }
}

TEST(Exercise, AssessedCannotStartOnPlaceholderText) {
  C c;
  int starts = 0;
  QObject::connect(&c, &C::startRequested, [&](bool) { ++starts; });
  exercise_mock::show(c, "exstart", false, false);
  EXPECT_FALSE(c.assessedAvailable());
  c.start(true);
  EXPECT_EQ(starts, 0);
  c.start(false);
  EXPECT_EQ(starts, 1);
  exercise_mock::show(c, "exwait", false, false);
  c.start(false);  // already running
  EXPECT_EQ(starts, 1);
}

TEST(Exercise, NoPlaybackWithoutConsentOrStoredAudio) {
  C c;
  int plays = 0;
  QObject::connect(&c, &C::playRequested, [&](int) { ++plays; });
  c.setAudioSwitch(true);
  exercise_mock::show(c, "exresult", false, false);
  c.play(0);
  EXPECT_EQ(plays, 0);
  EXPECT_TRUE(c.audioText().contains("no consent"));
  exercise_mock::show(c, "exresult", false, true);
  c.play(0);  // consent, but nothing stores the audio
  EXPECT_EQ(plays, 0);
  EXPECT_FALSE(c.audioSaved());
  EXPECT_TRUE(c.audioText().contains("not connected yet"));
  c.setAudioConnected();
  EXPECT_TRUE(c.audioSaved());
  c.play(0);
  c.play(99);
  EXPECT_EQ(plays, 1);
}

TEST(Exercise, InstructorChoiceTellsTheTraineeOnlyThatTheInstructorKnows) {  // D-125
  for (bool assessed : {false, true}) {
    C c;
    int goes = 0, stops = 0;
    QObject::connect(&c, &C::continueRequested, [&] { ++goes; });
    QObject::connect(&c, &C::stopRequested, [&] { ++stops; });
    exercise_mock::show(c, "exchoice", assessed, false);
    EXPECT_TRUE(c.choice());
    EXPECT_EQ(c.notice(), "Instructor notified");
    EXPECT_EQ(c.alert(), "Instructor notified");
    EXPECT_EQ(c.choiceText().contains("Line up"), !assessed);
    EXPECT_TRUE(c.missedText() == "none" || assessed);
    c.continueExercise();
    EXPECT_EQ(goes, 1);
    c.stop();
    EXPECT_EQ(stops, assessed ? 0 : 1);  // Assessed needs the instructor's PIN
    c.endConfirmed();  // nobody asked for the PIN
    EXPECT_EQ(stops, assessed ? 0 : 1);
    c.requestEnd();
    EXPECT_EQ(c.pinAsked(), assessed);
    if (assessed) c.endConfirmed();
    EXPECT_FALSE(c.pinAsked());
    EXPECT_EQ(stops, 1);
    c.setPhase(C::Paused);  // the PTT dialog comes first
    EXPECT_FALSE(c.choice());
    c.setPhase(C::Running);
    c.clearChoice();
    c.continueExercise();
    EXPECT_EQ(goes, 1);
    EXPECT_EQ(c.alert(), "Exercise running");
    c.endConfirmed();  // the choice is over
    EXPECT_EQ(stops, 1);
  }
}

TEST(Exercise, StopInAssessedNeedsThePinAlsoWhilePaused) {  // D-129
  for (const char* id : {"exwait", "exlost"}) {
    C c;
    int stops = 0;
    QObject::connect(&c, &C::stopRequested, [&] { ++stops; });
    exercise_mock::show(c, id, true, false);
    c.stop();
    EXPECT_EQ(stops, 0) << id;
    c.requestEnd();
    EXPECT_TRUE(c.pinAsked()) << id;
    c.cancelEnd();
    c.endConfirmed();
    EXPECT_EQ(stops, 0) << id;
    c.requestEnd();
    c.endConfirmed();
    EXPECT_EQ(stops, 1) << id;
    exercise_mock::show(c, id, false, false);
    c.requestEnd();  // Practice has no PIN
    EXPECT_FALSE(c.pinAsked()) << id;
    c.stop();
    EXPECT_EQ(stops, 2) << id;
  }
  C c;
  int stops = 0;
  QObject::connect(&c, &C::stopRequested, [&] { ++stops; });
  exercise_mock::show(c, "exchoice", true, false);
  c.requestEnd();
  c.clearChoice();  // a transmission ended the waiting while the PIN was checked
  c.endConfirmed();
  EXPECT_EQ(stops, 0);
}

TEST(Exercise, TheSaveSwitchCoversExerciseAudio) {  // EA-1
  C c;
  c.setAudioConnected();
  exercise_mock::show(c, "exresult", false, true);
  EXPECT_FALSE(c.audioSaved());  // off until told
  EXPECT_EQ(c.audioText(), "Exercise audio not saved: switched off in Settings");
  c.setAudioSwitch(true);
  EXPECT_TRUE(c.audioSaved());
  EXPECT_EQ(c.audioText(), "Saved on this machine only");
}

TEST(Exercise, AGuestHasNoAudio) {  // D-127
  C c;
  c.setAudioConnected();
  c.setAudioSwitch(true);
  c.begin({}, false, true);
  EXPECT_FALSE(c.audioSaved());
  EXPECT_TRUE(c.audioText().contains("guest"));
  EXPECT_EQ(c.who(), "Guest");
  ExerciseContext x;
  x.trainee = "S01";
  c.begin(x, false, true);
  EXPECT_TRUE(c.audioSaved());
  EXPECT_EQ(c.who(), "S01");
}

TEST(Exercise, APlanFixesTheModeAndNamesItsTrainee) {  // D-142
  C c;
  int practice = 0, assessed = 0;
  QObject::connect(&c, &C::startRequested, [&](bool a) { ++(a ? assessed : practice); });
  ExerciseContext x;
  x.placeholder = false;
  x.trainee = "S02";
  x.plan = ExercisePlan{"P", "S01", false, 3};
  c.begin(x, false, false);
  c.setPhase(C::Starting);
  EXPECT_TRUE(c.planWarning().contains("This plan is for S01. The result belongs to S02."));
  EXPECT_FALSE(c.assessedAvailable());
  c.start(true);
  c.start(false);
  EXPECT_EQ(practice, 1);
  EXPECT_EQ(assessed, 0);
  EXPECT_EQ(c.runText(), "plan, run 3");

  x.plan->assessed = true;  // somebody else: refused
  c.begin(x, false, false);
  c.setPhase(C::Starting);
  EXPECT_FALSE(c.assessedAvailable());
  EXPECT_EQ(c.assessedNote(), "This plan is for S01.");
  c.start(true);
  c.start(false);
  EXPECT_EQ(practice, 1);
  EXPECT_EQ(assessed, 0);

  x.trainee = "S01";
  c.begin(x, false, false);
  c.setPhase(C::Starting);
  EXPECT_TRUE(c.planWarning().isEmpty());
  c.start(false);
  c.start(true);
  EXPECT_EQ(practice, 1);
  EXPECT_EQ(assessed, 1);

  x.plan.reset();  // no plan: as before
  c.begin(x, false, false);
  EXPECT_TRUE(c.practiceAvailable() && c.assessedAvailable() && c.planText().isEmpty() && c.runText().isEmpty());
}

TEST(Exercise, UncertainValueIsShownAsHeard) {
  C c;
  exercise_mock::show(c, "examb", false, false);
  const auto* m = c.transcript();
  const int chips = m->roleNames().key("chips");
  const QVariantList l = m->data(m->index(1, 0), chips).toList();  // newest first: 0 is the pilot
  ASSERT_EQ(l.size(), 2);
  EXPECT_TRUE(l[1].toMap()["uncertain"].toBool());
  EXPECT_TRUE(l[1].toMap()["value"].toString().contains("0.9"));
}

TEST(Exercise, TheTimerRedrawsNothingElse) {
  C c;
  exercise_mock::show(c, "exwait", false, false);
  int changes = 0, phases = 0;
  QObject::connect(&c, &C::changed, [&] { ++changes; });
  QObject::connect(&c, &C::phaseChanged, [&] { ++phases; });
  for (int i = 0; i < 10; ++i) c.setWaiting(QString::number(i));
  EXPECT_EQ(changes + phases, 0);
}

TEST(Exercise, BeginClearsTheLastOne) {
  C c;
  exercise_mock::show(c, "exdone", false, true);
  c.begin({}, false, false);
  EXPECT_EQ(c.phase(), C::Idle);
  EXPECT_EQ(c.transcript()->rowCount(), 0);
  EXPECT_TRUE(c.clearances().isEmpty());
  EXPECT_FALSE(c.active());
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  QCoreApplication app(argc, argv);
  return RUN_ALL_TESTS();
}
