#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <algorithm>
#include <memory>

#include "app/StudioController.h"
#include "app/StudioMock.h"
using namespace atc;

namespace {
struct Studio : testing::Test {
  std::vector<Person> people;
  Ns now = ms(1000);
  std::unique_ptr<AccessController> a;
  std::unique_ptr<StudioController> s;
  StudioBackend b = mockStudio();
  int ended = 0, recorded = 0, accepted = 0, deleted = 0;
  std::string startAnswer;

  static Person person(const char* id, bool consent, bool pilot) {
    Person p;
    p.id = id;
    p.consent = consent;
    p.consentDate = "2026-11-24";
    p.consentVersion = ConsentVersions{}.recording;
    p.pilotVoice = pilot;
    p.pilotVoiceDate = pilot ? "2026-11-24" : "";
    p.pilotVoiceVersion = ConsentVersions{}.pilotVoice;
    return p;
  }
  void SetUp() override {
    people = {person("S01", true, true), person("S02", true, false), person("S03", false, true)};
    AccessBackend ab;
    ab.load = [] { return std::string(); };
    ab.people = [this] { return people; };
    ab.setPilotVoice = [this](const std::string& id, bool c, const std::string& date, const std::string&) {
      for (Person& p : people)
        if (p.id == id) {
          p.pilotVoice = c;
          p.pilotVoiceDate = date;
        }
      return std::string();
    };
    ab.pinSet = [] { return true; };
    ab.checkPin = [](const std::string& p) { return p == "482913"; };
    a = std::make_unique<AccessController>(ab, [this] { return now; });
    s = std::make_unique<StudioController>(a.get());
    const auto count = [](auto& f, int& n) { f = [inner = f, &n](auto&&... x) { ++n; return inner(std::forward<decltype(x)>(x)...); }; };
    count(b.record, recorded);
    count(b.accept, accepted);
    count(b.deleteTake, deleted);
    b.endSession = [this, inner = b.endSession] {
      ++ended;
      inner();
    };
    b.startSession = [this, inner = b.startSession](const std::string& p, StudioBackend::Done done) {
      if (!startAnswer.empty()) return done(startAnswer);
      inner(p, done);
    };
    s->setBackend(b);
  }
  void TearDown() override { s.reset(); }
  static void drain() {
    for (int i = 0; i < 6; ++i) QCoreApplication::processEvents();
  }
  template <class F>
  static bool wait(F f) {
    QElapsedTimer t;
    t.start();
    while (!f() && t.elapsed() < 3000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return f();
  }
  void login() {
    a->loginInstructor("482913");
    ASSERT_TRUE(wait([&] { return a->instructor() && s->ready() && s->hasClip(); }));
    drain();
  }
  void openTaxi() {
    s->select("READBACK_TAXI");
    drain();
  }
  void startS01() {
    s->startSession("S01");
    drain();
    ASSERT_TRUE(s->session());
  }
  QString takeState(int i) { return s->takes()[i].toMap()["state"].toString(); }
};
}  // namespace

TEST_F(Studio, NothingIsShownOrChangedWithoutTheInstructor) {
  s->enter();
  drain();
  EXPECT_FALSE(s->ready());
  EXPECT_TRUE(s->entries().isEmpty());
  EXPECT_TRUE(s->speakers().isEmpty());
  s->startSession("S01");
  s->toggleRecord();
  drain();
  EXPECT_FALSE(s->session());
  EXPECT_EQ(recorded, 0);
  EXPECT_TRUE(s->message().contains("Only the instructor"));
}

TEST_F(Studio, StripHasTypePositionAndState) {
  login();
  openTaxi();
  const auto l = s->segments();
  ASSERT_EQ(l.size(), 5);
  EXPECT_EQ(l[0].toMap()["text"].toString(), "taxi via");
  EXPECT_FALSE(l[0].toMap()["dynamic"].toBool());
  EXPECT_EQ(l[0].toMap()["position"].toString(), "INI");
  EXPECT_EQ(l[0].toMap()["state"].toString(), "P");
  EXPECT_EQ(l[1].toMap()["text"].toString(), "{taxiway}");
  EXPECT_EQ(l[1].toMap()["state"].toString(), "A");  // B2 has an alternate only
  EXPECT_EQ(l[3].toMap()["state"].toString(), "M");  // 27: only the take of somebody who withdrew
  EXPECT_TRUE(l[3].toMap()["comma"].toBool());
  EXPECT_EQ(l[4].toMap()["position"].toString(), "FIN");
  s->selectSegment(3);
  EXPECT_EQ(s->poolClips().size(), 2);
  EXPECT_TRUE(s->poolTitle().contains("SAMPLE-AF"));
}

TEST_F(Studio, EditingReturnsToDraftAndListsTheClips) {
  login();
  openTaxi();
  EXPECT_FALSE(s->dirty());
  s->setText("taxi along {taxiway} runway {runway}, {callsign}");
  drain();
  EXPECT_TRUE(s->dirty());
  EXPECT_TRUE(s->changeRows().contains("missing  PH_TAXI_ALONG@initial"));
  EXPECT_TRUE(s->canSave());
  s->select("SAY_AGAIN");  // an edit is never left silently
  EXPECT_EQ(s->entryId(), "READBACK_TAXI");
  EXPECT_TRUE(s->message().contains("Save or undo"));
  s->undo();
  drain();
  EXPECT_FALSE(s->dirty());
  EXPECT_TRUE(s->changeRows().isEmpty());
}

TEST_F(Studio, AnUnreadableOrEmptyTextCannotBeSaved) {
  login();
  openTaxi();
  s->setText("taxi via {taxiway");
  drain();
  EXPECT_FALSE(s->canSave());
  EXPECT_FALSE(s->lint().isEmpty());
  s->setText("  ");
  drain();
  EXPECT_FALSE(s->canSave());
}

TEST_F(Studio, TypedDigitsAreWarnedAbout) {
  login();
  openTaxi();
  s->setText("taxi via {taxiway} runway two seven, {callsign}");
  drain();
  ASSERT_FALSE(s->lint().isEmpty());
  EXPECT_TRUE(s->lint().first().contains("{slot}"));
}

TEST_F(Studio, ValidatedNeedsSopReferenceAndARealFile) {
  login();
  openTaxi();
  s->setSopRef("SOP 1");
  s->setValidated(true);
  EXPECT_FALSE(s->validated());
  EXPECT_TRUE(s->message().contains("PLACEHOLDER"));
}

TEST_F(Studio, AChangedTextIsSavedBeforeItIsValidated) {
  StudioBackend x = b;
  x.load = [inner = b.load](auto done) {  // the mock's files are PLACEHOLDER: here they are not
    inner([done](StudioContent c, const std::string& why) {
      for (StudioEntry& e : c.entries) e.placeholderFile = false;
      done(c, why);
    });
  };
  std::vector<bool> sent;
  x.save = [&sent, inner = b.save](StudioEntry e, const std::string& revision, StudioBackend::Done done) {
    sent.push_back(e.validated);
    e.placeholderFile = false;
    inner(e, revision, done);
  };
  s->setBackend(x);
  login();
  openTaxi();
  s->setSopRef("SOP 1");
  s->setText(s->text() + " now");
  drain();
  s->setValidated(true);
  EXPECT_FALSE(s->validated());
  EXPECT_TRUE(s->message().contains("Save the changed text first"));
  ASSERT_TRUE(s->canSave());
  s->save();
  drain();
  ASSERT_EQ(sent, std::vector<bool>{false});
  EXPECT_FALSE(s->dirty());
  s->setValidated(true);  // the text is saved: now it can be Validated
  EXPECT_TRUE(s->validated());
  s->save();
  drain();
  EXPECT_EQ(sent, (std::vector<bool>{false, true}));
  EXPECT_FALSE(s->dirty());
  EXPECT_TRUE(s->validated());
  s->setText(s->text() + " please");  // and editing is Draft again (D-25)
  EXPECT_FALSE(s->validated());
}

TEST_F(Studio, TheMockSavesAChangedTextAsDraftLikeTheLiveBackend) {
  StudioEntry e;
  std::string revision;
  const auto read = [&] {
    b.load([&](StudioContent c, const std::string&) {
      revision = c.revision;
      for (const StudioEntry& x : c.entries)
        if (x.reply && (e.id.empty() || x.id == e.id)) {
          e = x;
          break;
        }
    });
    drain();
  };
  read();
  ASSERT_FALSE(e.id.empty());
  e.texts = {"changed"};
  e.sopRef = "SOP 1";
  e.validated = true;
  e.placeholderFile = false;
  b.save(e, revision, [](const std::string& why) { EXPECT_EQ(why, ""); });
  drain();
  read();
  EXPECT_EQ(e.texts, std::vector<std::string>{"changed"});
  EXPECT_FALSE(e.validated);
}

TEST_F(Studio, ARefusedSaveKeepsTheEdit) {
  login();
  openTaxi();
  s->setSopRef("SOP 1");
  drain();
  StudioBackend x = b;
  x.save = [](StudioEntry, const std::string&, StudioBackend::Done done) { done("the file changed on disk"); };
  s->setBackend(x);
  s->save();
  drain();
  EXPECT_TRUE(s->dirty());
  EXPECT_FALSE(s->busy());
  EXPECT_TRUE(s->message().contains("changed on disk"));
}

TEST_F(Studio, AReloadDoesNotGiveAnEditTheNewRevision) {
  login();
  openTaxi();
  s->setSopRef("SOP 1");
  drain();
  std::string sent;
  StudioBackend x = b;
  x.load = [inner = b.load](auto done) {
    inner([done](StudioContent c, const std::string& why) {
      c.revision = "changed elsewhere";
      done(c, why);
    });
  };
  x.save = [&sent](StudioEntry, const std::string& r, StudioBackend::Done done) {
    sent = r;
    done("the file changed on disk");
  };
  s->setBackend(x);
  s->enter();
  drain();
  EXPECT_TRUE(s->dirty());
  s->save();
  drain();
  EXPECT_EQ(sent, "1");
}

TEST_F(Studio, OnlyASpeakerWithBothAnswersMayRecord) {  // D-132, D-133
  login();
  for (const char* id : {"S02", "S03", "S99"}) {
    s->startSession(id);
    drain();
    EXPECT_FALSE(s->session()) << id;
    EXPECT_TRUE(s->message().contains("may not record")) << id;
  }
  startS01();
  EXPECT_EQ(a->personId(), "");  // nobody's login changes
}

TEST_F(Studio, ASessionThatIsLeftWhileItStartsIsNotOpened) {
  login();
  StudioBackend::Done late;
  StudioBackend x = b;
  x.startSession = [&late](const std::string&, StudioBackend::Done done) { late = std::move(done); };
  s->setBackend(x);
  s->startSession("S01");
  s->leave();
  EXPECT_EQ(ended, 1);
  late("");
  drain();
  EXPECT_FALSE(s->session());
  EXPECT_EQ(ended, 2);  // it had started after all
  EXPECT_FALSE(s->busy());
  EXPECT_FALSE(s->message().contains("started"));
  s->setBackend(b);
  startS01();  // and can be started again
}

TEST_F(Studio, TheBackendsRefusalIsShown) {
  login();
  startAnswer = "no microphone";
  s->startSession("S01");
  drain();
  EXPECT_FALSE(s->session());
  EXPECT_EQ(s->message(), "no microphone");
}

TEST_F(Studio, ATakeIsPendingUntilItIsAccepted) {  // S-1
  login();
  openTaxi();
  s->selectSegment(3);
  s->selectClip("RUNWAY_27@medial");
  startS01();
  s->toggleRecord();
  drain();
  ASSERT_TRUE(s->recording());
  s->selectClip("RUNWAY_09@medial");  // not while recording
  EXPECT_EQ(s->clip()["id"].toString(), "RUNWAY_27@medial");
  s->toggleRecord();
  drain();
  EXPECT_FALSE(s->recording());
  EXPECT_EQ(takeState(0), "pending");
  EXPECT_EQ(s->segments()[3].toMap()["state"].toString(), "M");
  s->accept(true);
  drain();
  EXPECT_EQ(accepted, 1);
  EXPECT_EQ(s->segments()[3].toMap()["state"].toString(), "P");
}

TEST_F(Studio, RecordingIsShownFromTheBackendAndNotFromTheButton) {  // ST-1
  login();
  openTaxi();
  StudioEvents e;
  StudioBackend x = b;
  x.setEvents = [&e](StudioEvents v) { e = std::move(v); };
  x.record = [](const std::string&, StudioBackend::Done done) { done(""); };
  s->setBackend(x);
  startS01();
  s->toggleRecord();
  drain();
  EXPECT_FALSE(s->recording());  // accepted, but it has not started
  e.recording(true);
  drain();
  EXPECT_TRUE(s->recording());
  s->endSession();
  EXPECT_TRUE(s->recording());  // until the core has stopped
  e.recording(false);
  e.recordingStopped("20 s limit");
  drain();
  EXPECT_FALSE(s->recording());
  EXPECT_EQ(s->message(), "20 s limit");
  e.recording(true);  // the pedal
  drain();
  EXPECT_TRUE(s->recording());
}

TEST_F(Studio, TheRoomToneIsMeasuredBeforeTheSessionOpens) {
  login();
  StudioEvents e;
  StudioBackend::Done late;
  StudioBackend x = b;
  x.setEvents = [&e](StudioEvents v) { e = std::move(v); };
  x.startSession = [&late](const std::string&, StudioBackend::Done done) { late = std::move(done); };
  s->setBackend(x);
  s->startSession("S01");
  EXPECT_TRUE(s->starting());
  EXPECT_FALSE(s->session());
  e.roomTone(-38, "the room is loud: -38 dBFS");
  late("");
  drain();
  EXPECT_TRUE(s->session());
  EXPECT_FALSE(s->starting());
  EXPECT_EQ(s->message(), "the room is loud: -38 dBFS");
  EXPECT_EQ(s->messageLevel(), int(LabController::Caution));
  s->endSession();
  s->startSession("S01");
  s->endSession();  // cancelled during the room tone: the backend refuses
  late("cancelled");
  drain();
  EXPECT_FALSE(s->session());
  EXPECT_FALSE(s->starting());
  EXPECT_FALSE(s->busy());
}

TEST_F(Studio, TheWaveformIsAskedForTheSelectedTakeOnly) {
  QStringList asked;
  StudioBackend x = b;
  x.peaks = [&asked, inner = b.peaks](const std::string& id, auto done) {
    asked << QString::fromStdString(id);
    inner(id, done);
  };
  s->setBackend(x);
  login();
  openTaxi();
  s->selectSegment(3);
  s->selectClip("RUNWAY_09@medial");
  drain();
  EXPECT_FALSE(s->take()["peaks"].toList().isEmpty());
  const auto n = asked.size();
  s->setTrim(0.1, 0.9);
  drain();
  EXPECT_EQ(asked.size(), n);  // the raw take is the same
  s->selectTake(1);
  drain();
  EXPECT_EQ(asked.size(), n + 1);
  s->selectClip("RUNWAY_27@medial");  // withdrawn
  drain();
  EXPECT_EQ(asked.size(), n + 1);
  EXPECT_TRUE(s->take()["peaks"].toList().isEmpty());
}

TEST_F(Studio, ClipsWithOtherWordsAreListed) {  // ST-4
  login();
  EXPECT_EQ(s->differing(), QStringList{"RUNWAY_09@medial: \"zero nine\", text \"zero niner\""});
}

TEST_F(Studio, TheVoiceOfTheExerciseIsNamedIfItIsAnother) {  // ST-2
  login();
  EXPECT_EQ(s->voice(), "mock voice (exercise: mock Piper)");
}

TEST_F(Studio, AtMostOnePrimary) {
  login();
  openTaxi();
  s->selectSegment(3);
  s->selectClip("RUNWAY_09@medial");
  EXPECT_EQ(takeState(0), "pending");
  s->accept(true);
  drain();
  int primary = 0;
  for (int i = 0; i < s->takes().size(); ++i) primary += takeState(i) == "primary";
  EXPECT_EQ(primary, 1);
}

TEST_F(Studio, LogoutEndsTheSessionAndIsToldToTheBackend) {
  login();
  openTaxi();
  startS01();
  s->toggleRecord();
  drain();
  ASSERT_TRUE(s->recording());
  a->logout();
  drain();
  EXPECT_FALSE(s->session());
  EXPECT_FALSE(s->recording());
  EXPECT_EQ(ended, 1);
  EXPECT_FALSE(s->ready());
}

TEST_F(Studio, AWithdrawnAnswerEndsTheSession) {
  login();
  openTaxi();
  startS01();
  ASSERT_TRUE(a->setPilotVoice("S01", false, "2026-11-25"));
  drain();
  EXPECT_FALSE(s->session());
  EXPECT_EQ(ended, 1);
  EXPECT_TRUE(s->message().contains("S01"));
  s->selectSegment(3);
  s->selectClip("RUNWAY_09@medial");
  s->accept(true);  // the pending take of S01
  drain();
  EXPECT_EQ(accepted, 0);
}

TEST_F(Studio, AnExerciseEndsTheSession) {
  login();
  startS01();
  s->setTrainerBusy(true);
  EXPECT_FALSE(s->session());
  EXPECT_EQ(ended, 1);
  s->startSession("S01");
  drain();
  EXPECT_FALSE(s->session());
}

TEST_F(Studio, LeavingTheScreenEndsTheSessionAndKeepsAnEdit) {
  login();
  openTaxi();
  s->setSopRef("SOP 1");
  startS01();
  s->leave();
  EXPECT_FALSE(s->session());
  EXPECT_EQ(ended, 1);
  s->enter();
  drain();
  EXPECT_EQ(s->sopRef(), "SOP 1");
  EXPECT_TRUE(s->dirty());
}

TEST_F(Studio, AWithdrawnTakeIsOnlyDeleted) {  // D-133
  login();
  openTaxi();
  s->selectSegment(3);
  s->selectClip("RUNWAY_27@medial");
  ASSERT_EQ(s->takes().size(), 1);
  EXPECT_TRUE(s->take()["withdrawn"].toBool());
  s->accept(true);
  s->accept(false);
  s->reject();
  drain();
  EXPECT_EQ(accepted, 0);
  s->deleteTake();
  drain();
  EXPECT_EQ(deleted, 1);
  EXPECT_TRUE(s->takes().isEmpty());
}

TEST_F(Studio, OnlyRejectedOrWithdrawnTakesAreDeleted) {
  login();
  openTaxi();
  s->selectSegment(3);
  s->selectClip("RUNWAY_09@medial");
  s->deleteTake();  // pending
  drain();
  EXPECT_EQ(deleted, 0);
  s->reject();
  drain();
  EXPECT_EQ(takeState(0), "rejected");
  s->deleteTake();
  drain();
  EXPECT_EQ(deleted, 1);
}

TEST_F(Studio, TrimStaysInsideTheTake) {
  login();
  openTaxi();
  s->selectSegment(3);
  s->selectClip("RUNWAY_09@medial");
  s->setTrim(-1, 5);
  drain();
  EXPECT_DOUBLE_EQ(s->take()["start"].toDouble(), 0);
  EXPECT_DOUBLE_EQ(s->take()["end"].toDouble(), 1);
  s->setTrim(0.9, 0.1);
  drain();
  EXPECT_LT(s->take()["start"].toDouble(), s->take()["end"].toDouble());
}

TEST_F(Studio, NextMissingOpensTheReplyThatNeedsIt) {
  login();
  EXPECT_EQ(s->missingCount(), 2);  // hold short, and 27 whose only take is withdrawn
  s->stepMissing(1);
  drain();
  const QString first = s->clip()["id"].toString();
  s->stepMissing(1);
  drain();
  const QString second = s->clip()["id"].toString();
  EXPECT_NE(first, second);
  s->stepMissing(1);
  drain();
  EXPECT_EQ(s->clip()["id"].toString(), first);  // round again
  s->setFilter("pools");
  EXPECT_EQ(s->missingCount(), 1);
  s->setFilter("final");
  EXPECT_EQ(s->missingCount(), 0);
}

TEST_F(Studio, UnusedClipsAreListed) {
  login();
  EXPECT_EQ(s->unused(), QStringList{"PH_REQUEST_START@final"});
}

TEST_F(Studio, AtcIntentsHaveNoClipsAndAreNotRecorded) {
  login();
  s->showReplies(false);
  drain();
  s->select("TAXI_INSTRUCTION");
  drain();
  EXPECT_EQ(s->exampleCount(), 2);
  for (const QVariant& v : s->segments()) EXPECT_TRUE(v.toMap()["state"].toString().isEmpty() && v.toMap()["position"].toString().isEmpty());
  startS01();
  s->toggleRecord();
  drain();
  EXPECT_FALSE(s->recording());
  EXPECT_EQ(recorded, 0);
}

TEST_F(Studio, AddReplyRefusesAnIdThatExists) {
  login();
  s->addReply("SAY_AGAIN", "say again");
  EXPECT_TRUE(s->message().contains("exists already"));
  int added = 0;
  QObject::connect(s.get(), &StudioController::added, s.get(), [&added] { ++added; });
  s->addReply("ROGER_NEW", "roger, {callsign}");
  drain();
  EXPECT_EQ(added, 1);
  EXPECT_EQ(s->entryId(), "ROGER_NEW");
  EXPECT_FALSE(s->validated());
}

TEST_F(Studio, ASessionThatAnEventEndsIsNotEndedTwice) {
  login();
  startS01();
  StudioEvents e;
  StudioBackend x = b;
  x.setEvents = [&e](StudioEvents v) { e = std::move(v); };
  s->setBackend(x);
  e.sessionEnded("microphone lost");
  drain();
  EXPECT_FALSE(s->session());
  EXPECT_EQ(ended, 0);
  EXPECT_EQ(s->message(), "microphone lost");
}

TEST_F(Studio, ThePedalIsForASessionOnlyAndEndsWithIt) {
  login();
  std::vector<std::string> pedal;
  StudioBackend x = b;
  x.setPedal = [&pedal](bool on, const std::string& clip) { pedal.push_back(on ? clip : "off"); };
  s->setBackend(x);
  s->setPedal(true);
  EXPECT_TRUE(pedal.empty());
  startS01();
  openTaxi();
  s->selectSegment(3);
  s->selectClip("RUNWAY_09@medial");
  s->setPedal(true);
  EXPECT_TRUE(s->pedal());
  s->selectClip("RUNWAY_27@medial");  // the backend is told the new clip
  s->endSession();
  EXPECT_FALSE(s->pedal());
  EXPECT_EQ(pedal, (std::vector<std::string>{"RUNWAY_09@medial", "RUNWAY_27@medial", "off"}));
}

TEST_F(Studio, ASecondRequestWaitsForTheFirst) {
  login();
  openTaxi();
  s->selectSegment(3);
  s->selectClip("RUNWAY_09@medial");
  drain();
  StudioBackend::Done late;
  StudioBackend x = b;
  x.accept = [&late](const std::string&, bool, StudioBackend::Done done) { late = std::move(done); };
  int rejected = 0;
  x.reject = [&rejected](const std::string&, StudioBackend::Done done) {
    ++rejected;
    done("");
  };
  s->setBackend(x);
  s->accept(true);
  EXPECT_TRUE(s->busy());
  s->reject();
  EXPECT_EQ(rejected, 0);
  late("manifest changed on disk");  // 09 section 8, 5
  drain();
  EXPECT_FALSE(s->busy());
  EXPECT_EQ(s->message(), "manifest changed on disk");
  EXPECT_EQ(takeState(0), "pending");
}

TEST_F(Studio, AnswersToOlderQuestionsAreIgnored) {  // 09 section 8, 10
  login();
  std::vector<std::function<void(std::vector<StudioClip>, const std::string&)>> late;
  StudioBackend x = b;
  x.clips = [&late](const std::string&, auto done) { late.push_back(done); };
  s->setBackend(x);
  StudioEvents e;
  x.setEvents = [&e](StudioEvents v) { e = std::move(v); };
  s->setBackend(x);
  e.clipsChanged();
  e.clipsChanged();
  drain();
  ASSERT_EQ(late.size(), 2u);
  StudioClip c;
  c.id = "NEW";
  c.words = "new";
  late[1]({c}, "");
  late[0]({}, "");
  drain();
  EXPECT_EQ(s->missingCount(), 1);
}

TEST_F(Studio, AnUnreadableCopyIsSaidAndNothingIsShown) {  // 09 section 3: never a silent return to the installed data
  StudioBackend x = b;
  x.load = [](auto done) { done(StudioContent{}, "the copy is damaged: replies/placeholder.json"); };
  s->setBackend(x);
  a->loginInstructor("482913");
  ASSERT_TRUE(wait([&] { return a->instructor() && s->message().contains("damaged"); }));
  EXPECT_FALSE(s->ready());
  EXPECT_TRUE(s->entries().isEmpty());
  EXPECT_EQ(s->messageLevel(), int(LabController::Fault));
}

TEST_F(Studio, ATakeOfAnotherClipIsNotSelected) {
  login();
  openTaxi();
  s->selectSegment(3);
  s->selectClip("RUNWAY_09@medial");
  StudioEvents e;
  StudioBackend x = b;
  x.setEvents = [&e](StudioEvents v) { e = std::move(v); };
  s->setBackend(x);
  const int n = s->takes().size();
  StudioTake t;
  t.id = "other";
  t.clipId = "RUNWAY_27@medial";
  e.takeReady(t);
  drain();
  EXPECT_EQ(s->takes().size(), n);
  s->selectClip("RUNWAY_27@medial");
  EXPECT_EQ(s->takes().size(), 2);
}

TEST_F(Studio, ANewExampleIsDraftAndIsNotSavedEmpty) {
  login();
  s->showReplies(false);
  drain();
  s->select("TAXI_INSTRUCTION");
  drain();
  s->addExample();
  drain();
  EXPECT_EQ(s->exampleCount(), 3);
  EXPECT_FALSE(s->canSave());
  s->setText("{callsign} taxi");
  drain();
  EXPECT_TRUE(s->canSave());
  EXPECT_FALSE(s->validated());
}

TEST_F(Studio, ExportNeedsAFileAndShowsTheRefusal) {
  login();
  s->exportChanges("  ");
  EXPECT_TRUE(s->message().contains("file name"));
  s->exportChanges("/tmp/x.json");
  drain();
  EXPECT_TRUE(s->message().contains("nothing is written"));
}

TEST_F(Studio, NothingIsPlayedWhileItRecordsAndPlaybackStopsFirst) {
  login();
  openTaxi();
  s->selectSegment(3);
  s->selectClip("RUNWAY_09@medial");
  drain();
  int played = 0, stopped = 0;
  StudioBackend x = b;
  x.play = [&played](const std::string&, const std::string&, StudioBackend::Done done) {
    ++played;
    done("");
  };
  x.stopPlay = [&stopped] { ++stopped; };
  s->setBackend(x);
  startS01();
  stopped = 0;
  s->toggleRecord();
  drain();
  EXPECT_EQ(stopped, 1);
  ASSERT_TRUE(s->recording());
  s->play("raw");
  drain();
  EXPECT_EQ(played, 0);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  QCoreApplication app(argc, argv);
  return RUN_ALL_TESTS();
}
