// Clicks through the real QML with the mock backend, off-screen. Any QML warning fails the test.
#include <gtest/gtest.h>

#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <functional>
#include <memory>
#include <string>
#include <tuple>

#include "app/AccessBackends.h"
#include "app/MockBackend.h"
#include "app/SettingsController.h"
using C = ExerciseController;

namespace {
QStringList g_warnings;
void handler(QtMsgType t, const QMessageLogContext&, const QString& m) {
  if (t != QtDebugMsg && t != QtInfoMsg && m.contains(".qml")) g_warnings << m;
  fprintf(stderr, "%s\n", qPrintable(m));
}

void walk(QQuickItem* i, const std::function<void(QQuickItem*)>& f) {
  if (!i->isVisible()) return;
  f(i);
  for (QQuickItem* c : i->childItems()) walk(c, f);
}
bool isButton(const QQuickItem* i) { return i->inherits("QQuickButton"); }
// Outside the visible part of a list or pane that scrolls.
bool scrolledOut(QQuickItem* i) {
  for (QQuickItem* f = i->parentItem(); f; f = f->parentItem()) {
    if (!f->inherits("QQuickFlickable")) continue;
    const QRectF r = i->mapRectToItem(f, {0, 0, i->width(), i->height()});
    if (r.top() < -0.5 || r.bottom() > f->height() + 0.5) return true;
  }
  return false;
}
bool isText(const QQuickItem* i) { return i->inherits("QQuickText"); }

struct Ui : testing::Test {
  LabController lab;
  SttController stt;
  AccessController access{mockAccess()};
  SettingsController settings{&access, QString()};
  RecordingsController recordings{&lab, &access, &settings};
  std::unique_ptr<MockBackend> mock;
  std::unique_ptr<QQmlApplicationEngine> engine;
  QQuickWindow* win = nullptr;

  void load(QSize size = {1280, 800}, const QString& theme = "dark") {
    g_warnings.clear();
    access.setLab(&lab);
    recordings.setBackend(mockRecordings(recordings));
    atc::SttModel m;
    m.id = "whisper-tiny-genprompt";
    m.name = "Whisper tiny + generated prompt";
    m.installed = true;
    stt.start({m}, MockBackend::fakeEngine, [](const atc::SttModel& x) { return "{\"mock\":\"" + x.id + "\"}"; });
    mock = std::make_unique<MockBackend>(&lab, &stt, 2000);
    mock->attach(&access, &recordings);
    engine = std::make_unique<QQmlApplicationEngine>();
    engine->addImportPath("qrc:/");
    engine->setInitialProperties({{"lab", QVariant::fromValue(&lab)},
                                  {"mock", QVariant::fromValue(mock.get())},
                                  {"stt", QVariant::fromValue(&stt)},
                                  {"feed", QVariant::fromValue(mock->status())},
                                  {"access", QVariant::fromValue(&access)},
                                  {"settings", QVariant::fromValue(&settings)},
                                  {"recordings", QVariant::fromValue(&recordings)},
                                  {"exerciseModel", QString("whisper-tiny-genprompt")},
                                  {"themeMode", theme},
                                  {"reduceMotion", true}});
    engine->load(QUrl("qrc:/Atc/Main.qml"));
    ASSERT_FALSE(engine->rootObjects().isEmpty());
    win = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
    ASSERT_TRUE(win);
    win->resize(size);
    win->show();
    win->requestActivate();
    settle(150);
  }
  void TearDown() override {
    engine.reset();  // before the objects it shows
    mock.reset();
    EXPECT_TRUE(g_warnings.isEmpty()) << g_warnings.join('\n').toStdString();
  }
  static void settle(int ms = 60) { QTest::qWait(ms); }
  void run(const char* id) {
    mock->run(id);
    settle(120);
  }
  QString screen() const { return win->property("screen_").toString(); }

  QString allText() const {
    QStringList l;
    walk(win->contentItem(), [&](QQuickItem* i) {
      if (isText(i)) l << i->property("text").toString();
    });
    return l.join('\n');
  }
  // The last one found is the topmost: dialogs come after screens.
  QQuickItem* find(const QString& text, bool button) const {
    QQuickItem* hit = nullptr;
    walk(win->contentItem(), [&](QQuickItem* i) {
      const bool kind = button ? isButton(i) : isText(i);
      if (kind && (i->property("text").toString() == text || (button && i->property("title").toString() == text))) hit = i;
    });
    return hit;
  }
  void click(QQuickItem* i, const QString& what) {
    ASSERT_TRUE(i) << what.toStdString() << " not found";
    QTest::mouseClick(win, Qt::LeftButton, {}, i->mapToScene({i->width() / 2, i->height() / 2}).toPoint());
    settle();
  }
  void clickButton(const QString& t) { click(find(t, true), t); }
  void clickText(const QString& t) { click(find(t, false), t); }
  void pinWithKeypad(const QString& pin) {
    for (QChar ch : pin) clickButton(QString(ch));
  }
  void waitLogin() { EXPECT_TRUE(QTest::qWaitFor([this] { return !access.busy(); }, 3000)); settle(); }
};

QStringList everyCase(const MockBackend& m) {
  QStringList l;
  for (const QVariant& v : m.scripts() + m.exerciseScripts()) l << v.toMap()["id"].toString();
  return l;
}
}  // namespace

// ---- all cases

struct UiAll : Ui, testing::WithParamInterface<std::tuple<QSize, const char*>> {};
TEST_P(UiAll, EveryCaseLoadsWithoutWarningsAndNoButtonIsCutOff) {
  const auto [size, theme] = GetParam();
  load(size, theme);
  for (const QString& id : everyCase(*mock)) {
    run(qPrintable(id));
    EXPECT_TRUE(g_warnings.isEmpty()) << id.toStdString() << ": " << g_warnings.join('\n').toStdString();
    g_warnings.clear();
    walk(win->contentItem(), [&](QQuickItem* i) {
      if (!isButton(i)) return;
      // The model dropdown shortens its name on purpose.
      walk(i, [&](QQuickItem* x) {
        const QString s = isText(x) ? x->property("text").toString() : QString();
        if (!s.startsWith("Whisper") && !s.startsWith("Vosk")) {
          EXPECT_FALSE(isText(x) && x->property("truncated").toBool()) << id.toStdString() << ": " << s.toStdString();
        }
      });
      if (scrolledOut(i)) return;
      const QPointF a = i->mapToScene({0, 0}), b = i->mapToScene({i->width(), i->height()});
      EXPECT_GE(a.x(), -0.5) << id.toStdString();
      EXPECT_LE(b.x(), win->width() + 0.5) << id.toStdString() << ": " << i->property("text").toString().toStdString();
      EXPECT_LE(b.y(), win->height() + 0.5) << id.toStdString() << ": " << i->property("text").toString().toStdString();
    });
  }
}
INSTANTIATE_TEST_SUITE_P(Sizes, UiAll, testing::Values(std::make_tuple(QSize(1280, 800), "dark"), std::make_tuple(QSize(1920, 1080), "dark"),
                                                       std::make_tuple(QSize(1280, 800), "night"), std::make_tuple(QSize(1280, 800), "hc")));

TEST_F(Ui, StatusBarNeverOverlaps) {
  load();
  for (const char* id : {"mother", "vmissing", "dashfault", "savyes", "setinstr", "idle"}) {
    run(id);
    waitLogin();
    auto* chips = win->findChild<QQuickItem*>("chipRow");
    auto* right = win->findChild<QQuickItem*>("statusRight");
    ASSERT_TRUE(chips && right);
    EXPECT_LE(chips->mapToScene({chips->width(), 0}).x(), right->mapToScene({0, 0}).x() + 0.5) << id;
    EXPECT_LE(right->mapToScene({right->width(), 0}).x(), win->width()) << id;
    if (QString(id) == "setinstr") {
      auto* name = win->findChild<QQuickItem*>("scenarioName");
      ASSERT_TRUE(name);
      EXPECT_FALSE(name->property("truncated").toBool()) << name->width();
    }
  }
}

// ---- navigation

TEST_F(Ui, DashboardIsReachableAndDisabledEntriesSayWhy) {
  load();
  clickText("Dashboard");
  EXPECT_EQ(screen(), "dash");
  clickText("Scenarios");
  EXPECT_EQ(screen(), "dash");
  EXPECT_TRUE(allText().contains("No package is planned"));
}

TEST_F(Ui, NoNavigationNameIsCutOff) {
  load();
  for (const char* id : {"idle", "exwait", "exresult"}) {
    run(id);
    for (const char* n : {"Dashboard", "Scenarios", "Exercise", "Assessment", "Settings"}) {
      QQuickItem* i = nullptr;
      walk(win->contentItem(), [&](QQuickItem* x) {
        if (!i && isText(x) && x->property("text").toString() == n) i = x;
      });
      ASSERT_TRUE(i) << id << ": " << n;
      EXPECT_FALSE(i->property("truncated").toBool()) << id << ": " << n;
    }
  }
}

TEST_F(Ui, NavigationIsOffDuringAnExercise) {
  load();
  run("exwait");
  EXPECT_EQ(screen(), "ex");
  clickText("Dashboard");
  EXPECT_EQ(screen(), "ex");
  EXPECT_TRUE(allText().contains("Stop the exercise first"));
}

// ---- footer, D-110

TEST_F(Ui, TheLabFooterIsNotReadyWithoutPtt) {
  load();
  run("lost");
  EXPECT_EQ(screen(), "lab");
  EXPECT_TRUE(allText().contains("PTT device lost"));
  EXPECT_FALSE(allText().contains("PTT missing"));  // once (D-126)
}

// ---- 1c: login

TEST_F(Ui, TraineeLogsInByTouchAlone) {
  load();
  run("login");
  EXPECT_TRUE(win->property("loginOpen").toBool());
  clickButton("S01");
  clickButton("Log in");
  EXPECT_EQ(access.personId(), "S01");
  EXPECT_FALSE(win->property("loginOpen").toBool());
  EXPECT_TRUE(allText().contains("Saved on this machine only"));
  clickText("Log out");
  EXPECT_FALSE(access.trainee());
}

TEST_F(Ui, InstructorLogsInWithTheKeypad) {
  load();
  run("login");
  clickButton("Instructor");
  pinWithKeypad("111111");
  clickButton("Log in");
  waitLogin();
  EXPECT_FALSE(access.instructor());
  EXPECT_TRUE(allText().contains("4 tries left"));
  EXPECT_FALSE(allText().contains("111111"));
  pinWithKeypad("1234569");
  clickButton("Erase");
  clickButton("Log in");
  waitLogin();
  EXPECT_TRUE(access.instructor());
}

TEST_F(Ui, SpaceInTheLoginDialogNeverTransmits) {
  load();
  run("login");
  QTest::keyPress(win, Qt::Key_Space);
  settle();
  EXPECT_EQ(lab.txTone(), LabController::Neutral);
  QTest::keyRelease(win, Qt::Key_Space);
  clickButton("Cancel");
  QTest::keyPress(win, Qt::Key_Space);  // and is PTT again afterwards
  settle(400);
  EXPECT_EQ(lab.txTone(), LabController::Active);
  QTest::keyRelease(win, Qt::Key_Space);
  settle();
}

TEST_F(Ui, LoginIsRefusedDuringATransmission) {
  load();
  run("login");
  clickButton("Cancel");
  run("tx");
  EXPECT_FALSE(access.loginTrainee("S01"));
  EXPECT_FALSE(access.trainee());
}

TEST_F(Ui, SettingsAreLockedForTrainees) {
  load();
  run("setoff");
  EXPECT_EQ(screen(), "set");
  EXPECT_TRUE(allText().contains("Only the instructor may change these."));
  EXPECT_FALSE(allText().contains("Mock Name"));
  clickButton("On");  // the save switch, disabled
  EXPECT_TRUE(settings.saveRecordings());
}

TEST_F(Ui, InstructorSettingsAndRecords) {
  load();
  run("setinstr");
  waitLogin();
  EXPECT_TRUE(allText().contains("Mock Name"));
  EXPECT_TRUE(allText().contains("offline, never cloud"));
  EXPECT_TRUE(allText().contains("to be approved by the unit"));  // D-134
  walk(win->contentItem(), [&](QQuickItem* x) {
    if (x->objectName() == "consentTexts") { EXPECT_FALSE(x->property("truncated").toBool()); }
  });
  clickButton("On");
  EXPECT_FALSE(settings.saveRecordings());
  clickButton("Add");
  EXPECT_TRUE(allText().contains("Consent covers copies to project machines on the local network (offline, never cloud)."));
  clickButton("Add");  // disabled: no ID, no answer
  EXPECT_EQ(access.people().size(), 2);
  for (char ch : std::string("S03")) QTest::keyClick(win, ch);  // keyClicks() is for widgets only
  settle();
  clickButton("Yes");
  clickButton("Add");
  EXPECT_EQ(access.people().size(), 3);
  clickText("S03");
  clickButton("Change consent");
  clickButton("No");
  clickButton("Change consent");
  EXPECT_FALSE(access.people()[2].toMap()["consent"].toBool());
  EXPECT_TRUE(allText().contains("not asked"));
  clickButton("Pilot voice");  // a separate answer (D-133)
  EXPECT_TRUE(allText().contains("Pilot voice: consent of S03"));
  clickButton("Yes");
  clickButton("Change consent");
  EXPECT_TRUE(access.people()[2].toMap()["pilot"].toBool());
  EXPECT_FALSE(access.people()[2].toMap()["consent"].toBool());
}

TEST_F(Ui, RecordingsCardSaysWhyNothingIsSaved) {
  load();
  run("login");
  clickButton("Cancel");
  EXPECT_TRUE(allText().contains("Not saved: log in as a trainee"));
  run("savno");
  EXPECT_TRUE(allText().contains("Not saved: no consent"));
}

TEST_F(Ui, DeleteNeedsInstructorSelectionAndConfirmation) {
  load();
  run("savyes");
  atc::StoredRecording rec;
  rec.tx = 99;
  rec.startedMs = 1'000'000;
  recordings.offer(rec, std::vector<std::int16_t>(16000));
  run("setinstr");
  waitLogin();
  clickText("Speech Lab");
  ASSERT_EQ(screen(), "lab");
  clickButton("Stored");
  ASSERT_EQ(recordings.stored()->rowCount(), 1);
  clickButton("Delete");  // nothing selected: disabled
  EXPECT_FALSE(allText().contains("Delete this recording?"));
  clickText("TX 99");
  clickButton("Delete");
  EXPECT_TRUE(allText().contains("Delete this recording?"));
  clickButton("Cancel");
  EXPECT_EQ(recordings.stored()->rowCount(), 1);
  clickButton("Delete");
  clickButton("Delete recording");
  settle();
  EXPECT_EQ(recordings.stored()->rowCount(), 0);
}

// ---- 2g: exercise and result

TEST_F(Ui, AssessedIsDisabledWithItsReason) {
  load();
  run("exstart");
  EXPECT_TRUE(allText().contains("Scenario text is PLACEHOLDER."));
  clickButton("Assessed, not available");
  clickButton("Start exercise");
  EXPECT_EQ(mock->exercise()->phase(), C::Running);
  EXPECT_FALSE(mock->exercise()->assessed());
}

TEST_F(Ui, PracticeShowsTheVerdictAndAssessedHidesIt) {
  load();
  for (const char* id : {"exwrong", "excs", "examb"}) {
    mock->setAssessed(false);
    run(id);
    const QString shown = allText();
    EXPECT_TRUE(shown.contains("INCORRECT") || shown.contains("WRONG CALLSIGN") || shown.contains("Not counted")) << id;
    mock->setAssessed(true);
    run(id);
    const QString hidden = allText();
    for (const char* w : {"INCORRECT", "WRONG CALLSIGN", "Not counted", "Suspected", "attempts"}) EXPECT_FALSE(hidden.contains(w)) << id << ": " << w;
    EXPECT_TRUE(hidden.contains("Shown in the debrief")) << id;
  }
}

TEST_F(Ui, CriticalValuesAreNeverCutOff) {
  load();
  run("exwrong");
  for (const char* v : {"EAGLE-421", "09", "A3"}) {
    QQuickItem* i = nullptr;
    walk(win->contentItem(), [&](QQuickItem* x) {
      if (isText(x) && x->property("text").toString() == v && x->height() >= 20) i = i ? i : x;
    });
    ASSERT_TRUE(i) << v;
    EXPECT_FALSE(i->property("truncated").toBool()) << v;
  }
  walk(win->contentItem(), [&](QQuickItem* x) {
    if (isText(x) && x->property("text").toString().startsWith("eagle")) {
      EXPECT_FALSE(x->property("truncated").toBool()) << x->property("text").toString().toStdString();
    }
  });
}

TEST_F(Ui, LostPttPausesAndScreenPttContinues) {
  load();
  run("exlost");
  EXPECT_TRUE(allText().contains("PTT disconnected"));
  clickButton("Use Screen PTT");
  EXPECT_EQ(mock->exercise()->phase(), C::Running);
}

TEST_F(Ui, InstructorChoiceLeavesTheRadioBarFree) {  // D-125
  load();
  run("exchoice");
  EXPECT_TRUE(allText().contains("Instructor notified"));
  clickButton("End exercise");
  clickButton("Cancel");
  EXPECT_EQ(mock->exercise()->phase(), C::Running);
  QQuickItem* ptt = find("Screen PTT", false);
  ASSERT_TRUE(ptt);
  QTest::mousePress(win, Qt::LeftButton, {}, ptt->mapToScene({ptt->width() / 2, ptt->height() / 2}).toPoint());
  settle(200);
  EXPECT_EQ(lab.txTone(), LabController::Active);
  QTest::mouseRelease(win, Qt::LeftButton, {}, ptt->mapToScene({ptt->width() / 2, ptt->height() / 2}).toPoint());
  settle();
  EXPECT_FALSE(mock->exercise()->choice());  // the mock closes it at release; live: when the text arrives
  run("exchoice");
  EXPECT_TRUE(mock->exercise()->choice());
  clickButton("Continue");
  EXPECT_FALSE(mock->exercise()->choice());
}

TEST_F(Ui, EndInAssessedNeedsTheInstructorPin) {  // D-125
  load();
  mock->setAssessed(true);
  run("exchoice");
  clickButton("End exercise");
  EXPECT_TRUE(allText().contains("instructor PIN"));
  pinWithKeypad("111111");
  clickButton("End exercise");
  waitLogin();
  EXPECT_EQ(mock->exercise()->phase(), C::Running);
  EXPECT_TRUE(allText().contains("4 tries left"));
  pinWithKeypad("123456");
  clickButton("End exercise");
  waitLogin();
  settle(200);
  EXPECT_EQ(mock->exercise()->phase(), C::Result);
  EXPECT_FALSE(access.instructor());
}

TEST_F(Ui, StopInAssessedNeedsTheInstructorPin) {  // D-129
  load();
  mock->setAssessed(true);
  for (const char* id : {"exwait", "exlost"}) {
    run(id);
    const C::Phase before = mock->exercise()->phase();
    clickButton("Stop exercise");
    EXPECT_TRUE(allText().contains("instructor PIN")) << id;
    clickButton("Cancel");
    EXPECT_EQ(mock->exercise()->phase(), before) << id;
    clickButton("Stop exercise");
    pinWithKeypad("123456");
    clickButton("End exercise");
    waitLogin();
    settle(200);
    EXPECT_EQ(mock->exercise()->phase(), C::Result) << id;
  }
}

TEST_F(Ui, NamesAndTitlesAreWholeAt1280) {
  const auto whole = [this](const char* name) {
    int n = 0;
    walk(win->contentItem(), [&](QQuickItem* x) {
      if (x->objectName() != name) return;
      ++n;
      EXPECT_FALSE(x->property("truncated").toBool()) << name << ": " << x->property("text").toString().toStdString();
      EXPECT_LE(x->property("contentWidth").toDouble(), x->width()) << name;
      EXPECT_LE(x->property("contentHeight").toDouble(), x->height()) << name;
    });
    EXPECT_GT(n, 0) << name;
  };
  load();
  run("exresulta");
  whole("stepHead");
  EXPECT_TRUE(allText().contains("Fallback PTT used"));
  run("idle");
  whole("selectedModel");
  run("exstart");
  whole("startValues");
  EXPECT_TRUE(allText().contains("EAGLE-421   C295   RWY 09   TWY A3"));
}

TEST_F(Ui, DashboardStartOpensTheDialog) {
  load();
  run("dashready");
  clickButton("START EXERCISE");
  EXPECT_EQ(mock->exercise()->phase(), C::Starting);
  EXPECT_EQ(screen(), "ex");
  clickButton("Cancel");
  EXPECT_EQ(screen(), "dash");
}

TEST_F(Ui, StopAsksFirst) {
  load();
  run("exwait");
  clickButton("Stop exercise");
  EXPECT_EQ(mock->exercise()->phase(), C::Running);
  clickButton("Cancel");
  EXPECT_EQ(mock->exercise()->phase(), C::Running);
  clickButton("Stop exercise");
  clickButton("Stop exercise");  // in the dialog
  EXPECT_EQ(mock->exercise()->phase(), C::Result);
  EXPECT_EQ(screen(), "ass");
}

TEST_F(Ui, SpaceTransmitsInTheExerciseAndTheScreenStays) {
  load();
  run("exwait");
  QTest::keyPress(win, Qt::Key_Space);
  settle(200);
  EXPECT_EQ(lab.txTone(), LabController::Active);
  EXPECT_EQ(screen(), "ex");
  QTest::keyRelease(win, Qt::Key_Space);
  settle(1300);
  EXPECT_TRUE(lab.rxActive());
}

TEST_F(Ui, ResultHasNoPassMarkAndNoPlaybackWithoutConsent) {
  load();
  run("exresult");
  EXPECT_EQ(screen(), "ass");
  const QString s = allText();
  EXPECT_TRUE(s.contains("No pass mark"));
  EXPECT_TRUE(s.contains("no audio saved"));
  EXPECT_TRUE(s.contains("not counted against the trainee"));
  int plays = 0;
  QObject::connect(mock->exercise(), &C::playRequested, [&plays](int) { ++plays; });
  mock->exercise()->play(0);
  EXPECT_EQ(plays, 0);
  clickButton("Run again");
  EXPECT_EQ(mock->exercise()->phase(), C::Starting);
}

// ---- Phraseology Studio (04 section 5)

TEST_F(Ui, TheStudioIsForTheInstructorOnly) {
  load();
  run("phroff");
  EXPECT_EQ(screen(), "train");
  EXPECT_TRUE(allText().contains("Instructor only. Log in to see or change phrases."));
  EXPECT_FALSE(allText().contains("READBACK_TAXI"));
}

TEST_F(Ui, TheStudioShowsTheReplyItsStripAndItsCoverage) {
  load();
  run("phr");
  waitLogin();
  settle(200);
  const QString t = allText();
  for (const char* w : {"READBACK_TAXI", "Used by: DEPARTURE-01  Taxi: CORRECT", "taxi via", "{taxiway}", "RUNWAY_09@medial", "Say this:", "PLACEHOLDER VOICE", "pending"})
    EXPECT_TRUE(t.contains(w)) << w;
  walk(win->contentItem(), [&](QQuickItem* x) {
    for (const char* n : {"phraseId", "sayThis", "coverage", "takeState"})
      if (x->objectName() == n) { EXPECT_FALSE(x->property("truncated").toBool()) << n << ": " << x->property("text").toString().toStdString(); }
  });
  EXPECT_TRUE(t.contains("The clip says \"zero nine\""));
}

TEST_F(Ui, SpaceRecordsInTheStudioAndNeverTransmits) {
  load();
  run("phr");
  waitLogin();
  settle(200);
  QTest::keyClick(win, Qt::Key_Space);  // no session
  settle();
  EXPECT_FALSE(mock->studio()->recording());
  EXPECT_TRUE(allText().contains("Start a recording session first."));
  clickButton("Start session...");
  EXPECT_TRUE(allText().contains("no consent to be recorded"));  // S02
  clickButton("S01");
  settle();
  EXPECT_TRUE(mock->studio()->session());
  QTest::keyPress(win, Qt::Key_Space);
  settle(150);
  EXPECT_TRUE(mock->studio()->recording());
  EXPECT_TRUE(allText().contains("RECORDING"));
  EXPECT_EQ(lab.txTone(), LabController::Neutral);
  QTest::keyRelease(win, Qt::Key_Space);
  QTest::keyClick(win, Qt::Key_Space);
  settle();
  EXPECT_FALSE(mock->studio()->recording());
  EXPECT_FALSE(allText().contains("RECORDING"));
  QTest::keyClick(win, Qt::Key_Return);  // accept
  settle();
  EXPECT_TRUE(allText().contains("Primary take."));
  clickText("Dashboard");  // leaving ends the session
  EXPECT_FALSE(mock->studio()->session());
}

TEST_F(Ui, ChangedWordingIsListedBeforeSaving) {
  load();
  run("phr");
  waitLogin();
  settle(200);
  mock->studio()->setText("taxi along {taxiway} runway {runway}, {callsign}");
  settle();
  clickButton("Save");
  EXPECT_TRUE(allText().contains("Save changed wording?"));
  EXPECT_TRUE(allText().contains("PH_TAXI_ALONG@initial"));
  clickButton("Cancel");
  EXPECT_TRUE(mock->studio()->dirty());
  clickButton("Save");
  clickButton("Save wording");
  settle();
  EXPECT_FALSE(mock->studio()->dirty());
  EXPECT_TRUE(allText().contains("changed on this machine"));
}

TEST_F(Ui, EnterInATextFieldNeverAcceptsATake) {
  load();
  run("phrtake");
  waitLogin();
  settle(200);
  QQuickItem* label = find("SOP reference (required for Validated)", false);
  ASSERT_TRUE(label);
  QTest::mouseClick(win, Qt::LeftButton, {}, label->mapToScene({20, label->height() + 24}).toPoint());
  for (char ch : std::string("x r p ")) QTest::keyClick(win, ch);  // the shortcut keys are text here
  QTest::keyClick(win, Qt::Key_Return);
  settle();
  EXPECT_EQ(mock->studio()->sopRef(), "x r p ");
  EXPECT_EQ(mock->studio()->takes()[0].toMap()["state"].toString(), "pending");
}

TEST_F(Ui, AWithdrawnTakeCanOnlyBeDeleted) {  // D-133
  load();
  run("phrwd");
  waitLogin();
  settle(200);
  EXPECT_TRUE(allText().contains("Consent withdrawn – delete or replace"));
  clickButton("Primary");  // disabled
  EXPECT_FALSE(allText().contains("Primary take."));
  clickButton("Delete...");
  EXPECT_TRUE(allText().contains("Delete this take?"));
  clickButton("Delete take");
  settle();
  EXPECT_TRUE(mock->studio()->takes().isEmpty());
}

// ---- Exercise Planner (04 section 5g)

TEST_F(Ui, ThePlannerIsForTheInstructorOnly) {
  load();
  run("planoff");
  EXPECT_EQ(screen(), "train");
  EXPECT_TRUE(allText().contains("Instructor only. Log in to plan exercises."));
  EXPECT_FALSE(allText().contains("Departure, runway 27"));
}

TEST_F(Ui, ThePlannerShowsPlanPreviewAndChecks) {
  load();
  run("plan");
  waitLogin();
  settle(200);
  const QString t = allText();
  for (const char* w : {"Departure, runway 27", "3 runs, 25 Nov 16:20", "Ready", "Scenario changed", "Warns: preset changed", "FALCON-732  AN-32", "43 of 200",
                        "Values: fixed   Trainee S01   Practice   Normal", "Voice bank: 22 clips", "Text is PLACEHOLDER", "Trainee record S01", "Assessed: scenario not Validated"})
    EXPECT_TRUE(t.contains(w)) << w;
  walk(win->contentItem(), [&](QQuickItem* x) {
    for (const char* n : {"planState", "planCheck", "planMessage"})
      if (x->objectName() == n) { EXPECT_FALSE(x->property("truncated").toBool()) << n << ": " << x->property("text").toString().toStdString(); }
  });
}

TEST_F(Ui, TheTrainingTabsChangeBetweenPhrasesAndExercises) {
  load();
  run("plan");
  waitLogin();
  settle(200);
  clickButton("Phrases");
  EXPECT_TRUE(allText().contains("READBACK_TAXI"));
  clickButton("Exercises");
  EXPECT_TRUE(allText().contains("ON THE DASHBOARD"));
}

TEST_F(Ui, AMisfitIsMarkedInWords) {
  load();
  run("planmis");
  waitLogin();
  settle(200);
  EXPECT_TRUE(allText().contains("does not fit"));
  EXPECT_TRUE(allText().contains("The values do not fit together."));
  EXPECT_FALSE(find("Save", true)->isEnabled());
}

TEST_F(Ui, NewPresetValuesAreOfferedOnlyIfThePresetChanged) {  // D-147
  load();
  run("plan");
  waitLogin();
  settle(200);
  EXPECT_FALSE(find("Use new preset values", true));
  run("planold");
  waitLogin();
  settle(200);
  QQuickItem* b = find("Use new preset values", true);
  ASSERT_TRUE(b);
  for (QQuickItem* f = b->parentItem(); f; f = f->parentItem())  // the pane scrolls
    if (f->inherits("QQuickFlickable")) {
      f->setProperty("contentY", f->property("contentHeight").toReal() - f->height());
      break;
    }
  settle();
  clickButton("Use new preset values");
  EXPECT_FALSE(mock->planner()->plan()["presetChanged"].toBool());
}

TEST_F(Ui, TheNameFieldIsTakenOverAndEdited) {
  load();
  run("plannew");
  waitLogin();
  settle(200);
  EXPECT_TRUE(allText().contains("NEW PLAN"));
  QQuickItem* label = find("Name", false);
  ASSERT_TRUE(label);
  QTest::mouseClick(win, Qt::LeftButton, {}, label->mapToScene({20, label->height() + 24}).toPoint());
  for (char ch : std::string("M 1")) QTest::keyClick(win, ch);  // Space is text here, never PTT
  settle();
  EXPECT_EQ(mock->planner()->plan()["name"].toString(), "M 1");
  EXPECT_EQ(lab.txTone(), LabController::Neutral);
  clickButton("Save");
  settle();
  EXPECT_EQ(mock->planner()->planCount(), 5);
}

TEST_F(Ui, TheCardShowsThePlanAndTheStartDialogTheSameValues) {  // D-321
  load();
  run("dashplan");
  settle(200);
  const QString t = allText();
  for (const char* w : {"Departure, runway 27", "plan, Departure 01: Taxi, line up, takeoff", "FALCON-732", "AN-32", "27", "B2",
                        "Values: fixed   Trainee S01   Practice   Normal", "Listen for the runway. Read the card first."})
    EXPECT_TRUE(t.contains(w)) << w;
  clickButton("Briefing");
  clickButton("Close");
  clickButton("START EXERCISE");
  settle();
  EXPECT_TRUE(allText().contains("FALCON-732   AN-32   RWY 27   TWY B2"));
  EXPECT_TRUE(allText().contains("Plan: Departure, runway 27   run 4"));
}

TEST_F(Ui, AGuestChoosesAPlanForAnyone) {
  load();
  run("planoff");
  run("dashready");
  clickButton("Choose plan");
  EXPECT_FALSE(allText().contains("Departure S02"));
  EXPECT_FALSE(allText().contains("Departure, runway 27"));
  clickButton("Departure, random");
  settle();
  EXPECT_FALSE(allText().contains("Choose a plan"));
  EXPECT_TRUE(allText().contains("Values: random   Anyone   Practice   Normal"));
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  testing::InitGoogleTest(&argc, argv);
  QGuiApplication app(argc, argv);
  qInstallMessageHandler(handler);
  for (const char* n : {"Inter-Regular.otf", "Inter-SemiBold.otf", "Inter-Bold.otf", "JetBrainsMono-Regular.ttf", "JetBrainsMono-Bold.ttf"})
    QFontDatabase::addApplicationFont(QStringLiteral(":/Atc/fonts/") + n);
  return RUN_ALL_TESTS();
}
