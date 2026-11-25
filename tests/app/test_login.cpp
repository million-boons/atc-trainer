#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QTemporaryDir>
#include <memory>

#include "app/AccessController.h"
#include "app/RecordingsController.h"
#include "app/SettingsController.h"
using namespace atc;

namespace {
struct Login : testing::Test {
  LabController lab;
  std::vector<Person> people{{"S01", "Name", "Rank", true, "2026-11-24"}, {"S02", "", "", false, "2026-11-24"}};
  bool pinSet = true;
  Ns now = ms(1000);
  std::unique_ptr<AccessController> a;
  std::unique_ptr<SettingsController> settings;
  std::unique_ptr<RecordingsController> r;
  std::vector<StoredRecording> saved;
  std::vector<std::pair<std::string, std::string>> results, removals;
  std::string removeAnswer;
  std::vector<std::pair<std::size_t, int>> plays;
  bool soundThere = true;

  void SetUp() override {
    AccessBackend b;
    b.load = [] { return std::string(); };
    b.people = [this] { return people; };
    b.add = [this](Person p, const std::string&) { people.push_back(p); return std::string(); };
    b.setConsent = [this](const std::string& id, bool c, const std::string&, const std::string&) {
      for (Person& p : people)
        if (p.id == id) p.consent = c;
      return std::string();
    };
    b.pinSet = [this] { return pinSet; };
    b.checkPin = [](const std::string& p) { return p == "482913"; };
    a = std::make_unique<AccessController>(b, [this] { return now; });
    a->setLab(&lab);
    settings = std::make_unique<SettingsController>(a.get(), QString());
    r = std::make_unique<RecordingsController>(&lab, a.get(), settings.get());
    RecordingBackend rb;
    rb.makeKey = [](std::int64_t t, int tx) { return std::to_string(t) + "-" + std::to_string(tx); };
    rb.save = [this](StoredRecording x, std::vector<std::int16_t>) { saved.push_back(std::move(x)); };
    rb.addResult = [this](const std::string& k, StoredResult x) { results.emplace_back(k, x.text); };
    rb.list = [this](std::size_t, std::function<void(std::vector<StoredRecording>, int)> done) { done(saved, 0); };
    rb.loadAudio = [this](const std::string&, std::function<void(std::optional<std::vector<std::int16_t>>)> done) {
      if (soundThere) done(std::vector<std::int16_t>(5));
      else done(std::nullopt);
    };
    rb.remove = [this](const std::string& k, const std::string& role, std::function<void(const std::string&)> done) {
      removals.emplace_back(k, role);
      done(removeAnswer);
    };
    r->setBackend(rb);
    r->setConnected();
    r->setPlayer([this](std::vector<std::int16_t> a, int rate) { plays.emplace_back(a.size(), rate); });
  }
  void TearDown() override {
    r.reset();
    settings.reset();
    a.reset();
  }
  void drain() {
    QElapsedTimer t;
    t.start();
    do QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    while (a->busy() && t.elapsed() < 5000);
    QCoreApplication::processEvents();
  }
  void pin(const char* p) {
    a->loginInstructor(p);
    drain();
  }
  void offer(int tx) {
    StoredRecording x;
    x.tx = tx;
    x.startedMs = 1000 + tx;
    r->offer(x, {1, 2, 3});
  }
};
}  // namespace

TEST_F(Login, NobodyAtStart) {
  EXPECT_EQ(a->role(), Role::None);
  EXPECT_FALSE(a->consent());
  EXPECT_EQ(a->longText(), "Not logged in");
}

TEST_F(Login, OnlyARightPinMakesAnInstructor) {
  pin("111111");
  EXPECT_EQ(a->role(), Role::None);
  EXPECT_TRUE(a->message().contains("4 tries left"));
  pin("482913");
  EXPECT_EQ(a->role(), Role::Instructor);
  EXPECT_TRUE(a->message().isEmpty());
}

TEST_F(Login, FiveWrongPinsWait) {
  for (int i = 0; i < 5; ++i) pin("111111");
  pin("482913");
  EXPECT_EQ(a->role(), Role::None);
  EXPECT_TRUE(a->message().contains("Wait"));
  now += ms(60'000);
  pin("482913");
  EXPECT_EQ(a->role(), Role::Instructor);
}

TEST_F(Login, WithoutAPinThereIsNoInstructor) {
  pinSet = false;
  pin("482913");
  EXPECT_EQ(a->role(), Role::None);
}

TEST_F(Login, ConfirmingWithThePinDoesNotLogIn) {  // D-125
  int confirmed = 0, logins = 0;
  QObject::connect(a.get(), &AccessController::confirmed, [&] { ++confirmed; });
  QObject::connect(a.get(), &AccessController::loggedIn, [&] { ++logins; });
  a->loginTrainee("S01");
  lab.setTx(TxDisplay::Tx, 1);  // also during a transmission
  a->confirmInstructor("111111");
  drain();
  EXPECT_EQ(confirmed, 0);
  EXPECT_TRUE(a->message().contains("4 tries left"));
  a->confirmInstructor("482913");
  drain();
  EXPECT_EQ(confirmed, 1);
  EXPECT_EQ(logins, 1);  // the trainee
  EXPECT_TRUE(a->trainee());
  EXPECT_EQ(a->personId(), "S01");
  for (int i = 0; i < 5; ++i) { a->confirmInstructor("111111"); drain(); }  // the same guard as the login
  a->confirmInstructor("482913");
  drain();
  EXPECT_EQ(confirmed, 1);
}

TEST_F(Login, RefusedWhileTransmitting) {
  lab.setTx(TxDisplay::Tx, 1);
  EXPECT_FALSE(a->loginTrainee("S01"));
  EXPECT_FALSE(a->message().isEmpty());
  lab.setTx(TxDisplay::Idle, 1);
  EXPECT_TRUE(a->loginTrainee("S01"));
  lab.setTx(TxDisplay::Tail, 2);
  EXPECT_FALSE(a->logout());
  EXPECT_TRUE(a->trainee());
}

TEST_F(Login, OnlyListedTraineesAndOnlyTheInstructorManages) {
  EXPECT_FALSE(a->loginTrainee("S99"));
  EXPECT_FALSE(a->addPerson("S03", "", "", true, "2026-11-24"));
  EXPECT_TRUE(a->loginTrainee("S01"));
  EXPECT_FALSE(a->addPerson("S03", "", "", true, "2026-11-24"));
  EXPECT_FALSE(a->setConsent("S02", true, "2026-11-24"));
  EXPECT_FALSE(a->setPilotVoice("S02", true, "2026-11-24"));
  EXPECT_EQ(people.size(), 2u);
  EXPECT_FALSE(a->people()[0].toMap().contains("name"));  // names are for the instructor
  pin("482913");
  EXPECT_TRUE(a->people()[0].toMap().contains("name"));
  EXPECT_TRUE(a->addPerson(" S03 ", "", "", true, "2026-11-24"));
  EXPECT_EQ(people.back().id, "S03");
}

TEST_F(Login, IdleInstructorIsLoggedOut) {  // D-120
  a->setIdleLimit(ms(600'000));
  pin("482913");
  now += ms(599'000);
  a->checkIdle();
  EXPECT_TRUE(a->instructor());
  QEvent key(QEvent::KeyPress);
  a->eventFilter(&lab, &key);
  now += ms(599'000);
  a->checkIdle();
  EXPECT_TRUE(a->instructor());
  lab.setTx(TxDisplay::Tx, 1);  // never during a transmission, which counts as input
  now += ms(599'000);
  a->checkIdle();
  EXPECT_TRUE(a->instructor());
  lab.setTx(TxDisplay::Idle, 1);
  now += ms(600'000);
  a->checkIdle();
  EXPECT_EQ(a->role(), Role::None);
  EXPECT_TRUE(a->message().contains("10 min"));
}

TEST_F(Login, ATraineeIsNotLoggedOutByTime) {
  a->loginTrainee("S01");
  now += ms(7'200'000);
  a->checkIdle();
  EXPECT_TRUE(a->trainee());
}

TEST_F(Login, StoredSoundIsForTheInstructorAndDeleteNeedsThePlayer) {  // D-120
  a->loginTrainee("S01");
  offer(1);
  r->refreshStored();
  drain();
  r->playStored(0);
  drain();
  EXPECT_TRUE(plays.empty());
  pin("482913");
  r->playStored(0);
  drain();
  ASSERT_EQ(plays.size(), 1u);
  EXPECT_EQ(plays[0].second, 16000);
  soundThere = false;
  r->playStored(0);
  drain();
  EXPECT_EQ(plays.size(), 1u);
  EXPECT_TRUE(r->message().contains("Not played"));
  EXPECT_TRUE(r->canDelete());
  r->setPlayer({});
  EXPECT_FALSE(r->playerReady());
  EXPECT_FALSE(r->canDelete());
  r->deleteStored(0);
  drain();
  EXPECT_TRUE(removals.empty());
}

TEST_F(Login, ConsentTextNamesTheLocalNetworkAndNoCloud) {  // D-114
  EXPECT_TRUE(a->consentText().contains("Consent covers copies to project machines on the local network (offline, never cloud)."));
}

TEST_F(Login, PilotVoiceTextSaysWhoHearsIt) {  // D-133
  EXPECT_TRUE(a->pilotVoiceText().contains("heard by other trainees"));
  EXPECT_FALSE(a->people()[0].toMap()["pilot"].toBool());
}

TEST_F(Login, SavedOnlyForAConsentingTrainee) {
  offer(1);
  EXPECT_TRUE(r->lineText().contains("log in"));
  a->loginTrainee("S02");
  offer(2);
  EXPECT_TRUE(r->lineText().contains("no consent"));
  pin("482913");
  offer(3);
  EXPECT_TRUE(saved.empty());
  a->loginTrainee("S01");
  EXPECT_EQ(r->lineLevel(), int(LabController::Ok));
  offer(4);
  ASSERT_EQ(saved.size(), 1u);
  EXPECT_EQ(saved[0].speaker, "S01");
  EXPECT_EQ(saved[0].key, "1004-4");
  people[0].consent = false;  // withdrawn while logged in
  offer(5);
  EXPECT_EQ(saved.size(), 1u);
}

TEST_F(Login, NeverClaimsToSaveBeforeTheRadioIsConnected) {
  RecordingsController fresh(&lab, a.get(), settings.get());
  RecordingBackend rb;
  rb.save = [](StoredRecording, std::vector<std::int16_t>) {};
  fresh.setBackend(rb);
  a->loginTrainee("S01");
  EXPECT_TRUE(fresh.lineText().startsWith("Not saved"));
  EXPECT_EQ(fresh.lineLevel(), int(LabController::Caution));
  fresh.setConnected();
  EXPECT_EQ(fresh.lineText(), "Saved on this machine only");
}

TEST_F(Login, ResultsFollowOnlySavedTransmissions) {
  offer(1);
  StoredResult x;
  x.text = "eagle";
  r->addResult(1, x);
  EXPECT_TRUE(results.empty());
  a->loginTrainee("S01");
  offer(2);
  r->addResult(2, x);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].first, "1002-2");
}

TEST_F(Login, AFailedSaveIsShownAndForgotten) {
  a->loginTrainee("S01");
  offer(1);
  r->savedCallback()("1001-1", false, "disk nearly full");
  drain();
  EXPECT_TRUE(r->lineText().contains("disk nearly full"));
  EXPECT_EQ(r->lineLevel(), int(LabController::Caution));
  r->addResult(1, {});
  EXPECT_TRUE(results.empty());
}

TEST_F(Login, SettingsAreTheInstructors) {
  settings->setSaveRecordings(false);
  EXPECT_TRUE(settings->saveRecordings());
  EXPECT_FALSE(settings->message().isEmpty());
  pin("482913");
  settings->setSaveRecordings(false);
  settings->setPttFallback(true);
  EXPECT_FALSE(settings->saveRecordings());
  EXPECT_TRUE(settings->pttFallback());
  a->loginTrainee("S01");
  offer(1);
  EXPECT_TRUE(saved.empty());
  EXPECT_EQ(r->lineText(), "Saving is off");
}

TEST_F(Login, SettingsAreKept) {
  QTemporaryDir dir;
  const QString file = dir.path() + "/state/settings.json";
  pin("482913");
  {
    SettingsController s(a.get(), file);
    EXPECT_TRUE(s.saveRecordings());
    EXPECT_FALSE(s.pttFallback());
    s.setSaveRecordings(false);
    s.setPttFallback(true);
  }
  SettingsController s(a.get(), file);
  EXPECT_FALSE(s.saveRecordings());
  EXPECT_TRUE(s.pttFallback());
  QFile f(file);
  ASSERT_TRUE(f.open(QIODevice::WriteOnly));
  f.write("not json");
  f.close();
  SettingsController broken(a.get(), file);
  EXPECT_TRUE(broken.saveRecordings());
  EXPECT_FALSE(broken.pttFallback());
  EXPECT_FALSE(broken.message().isEmpty());
}

TEST_F(Login, OnlyTheInstructorDeletesAndTheMemoryCopyGoesToo) {
  a->loginTrainee("S01");
  offer(7);
  LabRecording rec;
  rec.id = 7;
  lab.addRecording(rec, "10:00:00");
  r->refreshStored();
  drain();
  ASSERT_EQ(r->stored()->rowCount(), 1);
  r->deleteStored(0);
  drain();
  EXPECT_TRUE(removals.empty());
  pin("482913");
  removeAnswer = "the deletion log cannot be written: nothing was deleted";
  r->deleteStored(0);
  drain();
  EXPECT_EQ(r->stored()->rowCount(), 1);
  EXPECT_TRUE(r->message().contains("nothing was deleted"));
  EXPECT_EQ(lab.recordings()->rowCount(), 1);
  removeAnswer.clear();
  r->deleteStored(0);
  drain();
  EXPECT_EQ(r->stored()->rowCount(), 0);
  EXPECT_EQ(lab.recordings()->rowCount(), 0);
  EXPECT_FALSE(lab.hasRecording());
  EXPECT_EQ(removals.back(), (std::pair<std::string, std::string>{"1007-7", "instructor"}));
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  QCoreApplication app(argc, argv);
  return RUN_ALL_TESTS();
}
