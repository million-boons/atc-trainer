#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <memory>

#include "app/HistoryController.h"
#include "app/HistoryMock.h"
using namespace atc;
using L = LabController;

namespace {
constexpr std::int64_t kNow = 1'764'100'000'000;
struct History : testing::Test {
  std::vector<Person> people;
  Ns now = ms(1000);
  std::unique_ptr<AccessController> a;
  std::unique_ptr<HistoryController> h;
  HistoryBackend b = mockHistory(kNow);
  std::vector<std::string> asked;
  int played = 0, stopped = 0, reclassified = 0, removed = 0;
  std::string lastVerdict;

  void SetUp() override {
    for (const char* id : {"S01", "S02"}) {
      Person x;
      x.id = id;
      people.push_back(x);
    }
    AccessBackend ab;
    ab.load = [] { return std::string(); };
    ab.people = [this] { return people; };
    ab.pinSet = [] { return true; };
    ab.checkPin = [](const std::string& x) { return x == "482913"; };
    a = std::make_unique<AccessController>(ab, [this] { return now; });
    h = std::make_unique<HistoryController>(a.get(), [] { return kNow; });
    b.list = [this, inner = b.list](const std::string& who, auto done) {
      asked.push_back(who);
      inner(who, done);
    };
    const auto count = [](auto& f, int& n) { f = [inner = f, &n](auto&&... x) { ++n; return inner(std::forward<decltype(x)>(x)...); }; };
    count(b.play, played);
    count(b.stopPlay, stopped);
    b.reclassify = [this, inner = b.reclassify](const std::string& id, int no, const std::string& verdict, const std::string& note, HistoryBackend::Done done) {
      lastVerdict = verdict;
      inner(id, no, verdict, note, std::move(done));
    };
    count(b.reclassify, reclassified);
    count(b.remove, removed);
    h->setBackend(b);
  }
  void TearDown() override { h.reset(); }
  static void drain() {
    for (int i = 0; i < 6; ++i) QCoreApplication::processEvents();
  }
  void instructor() {
    a->loginInstructor("482913");
    QElapsedTimer t;
    t.start();
    while (!(a->instructor() && h->loaded()) && t.elapsed() < 3000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    drain();
    ASSERT_TRUE(h->loaded());
  }
  void trainee(const char* id) {
    ASSERT_TRUE(a->loginTrainee(id));
    drain();
  }
  void open(const char* id) {
    h->select(id);
    h->openSelected();
    drain();
    ASSERT_TRUE(h->isOpen());
  }
};
}  // namespace

TEST_F(History, NobodyLoggedInSeesNothing) {
  h->enter();
  drain();
  EXPECT_FALSE(h->allowed());
  EXPECT_TRUE(asked.empty());
  EXPECT_TRUE(h->rows().isEmpty());
}

TEST_F(History, ATraineeSeesOnlyTheirOwn) {  // H-1, D-124
  trainee("S02");
  EXPECT_EQ(asked, std::vector<std::string>{"S02"});
  ASSERT_EQ(h->rows().size(), 1);
  EXPECT_EQ(h->rows()[0].toMap()["trainee"].toString(), "S02");
  EXPECT_TRUE(h->trainees().isEmpty());
  h->setTrainee("S01");
  EXPECT_EQ(h->trainee(), "");
  h->select("h1");  // of S01
  h->openSelected();
  drain();
  EXPECT_FALSE(h->isOpen());
}

TEST_F(History, OtherPeoplesRowsAreDroppedEvenIfTheBackendSendsThem) {
  b.list = [](const std::string&, auto done) {
    HistoryRow mine, other, guest;
    mine.id = "a";
    mine.trainee = "S01";
    other.id = "b";
    other.trainee = "S02";
    guest.id = "c";
    mine.startedMs = other.startedMs = guest.startedMs = kNow;
    done({mine, other, guest}, "");
  };
  HistoryDetail foreign;
  foreign.row.trainee = "S02";
  b.open = [foreign](const std::string&, auto done) { done(foreign, ""); };
  h->setBackend(b);
  trainee("S01");
  ASSERT_EQ(h->rows().size(), 1);
  h->select("a");
  h->openSelected();
  drain();
  EXPECT_FALSE(h->isOpen());
  EXPECT_TRUE(h->message().contains("somebody else"));
  instructor();
  EXPECT_EQ(h->rows().size(), 2);  // the guest is in nobody's history (D-127)
}

TEST_F(History, TheInstructorSeesAllAndFilters) {
  instructor();
  EXPECT_EQ(h->trainees(), (QStringList{"S01", "S02"}));
  EXPECT_EQ(h->rows().size(), 5);  // one is older than 30 days
  h->setDays(0);
  EXPECT_EQ(h->rows().size(), 6);
  EXPECT_EQ(h->countText(), "6 exercises");
  h->setTrainee("S02");
  EXPECT_EQ(h->countText(), "1 exercise");
  h->setTrainee("");
  h->setMode("assessed");
  EXPECT_EQ(h->rows().size(), 1);
  h->setMode("practice");
  EXPECT_EQ(h->rows().size(), 5);
  h->setMode("all");
  h->setScenario("Ground 01");
  EXPECT_EQ(h->rows().size(), 1);
}

TEST_F(History, MissedResponsesAndSuspectedErrorsHaveTheirOwnColumns) {
  instructor();
  const QVariantMap r = h->rows()[1].toMap();  // h2
  EXPECT_EQ(r["errors"].toInt(), 2);
  EXPECT_EQ(r["missed"].toInt(), 1);
  EXPECT_EQ(r["suspected"].toInt(), 0);
  EXPECT_EQ(h->rows()[0].toMap()["errors"].toInt(), 1);
  EXPECT_EQ(h->rows()[0].toMap()["suspected"].toInt(), 1);  // not added
  EXPECT_EQ(h->rows()[0].toMap()["scenario"].toString(), "Departure, runway 27  run 3");
  h->setDays(0);
  EXPECT_EQ(h->rows()[5].toMap()["response"].toString(), "-");
}

TEST_F(History, TheTrendIsOfOneTraineeAndOneScenario) {  // H-5
  instructor();
  EXPECT_TRUE(h->trend().isEmpty());
  EXPECT_FALSE(h->trendNote().isEmpty());
  h->setScenario("Departure 01");
  EXPECT_TRUE(h->trend().isEmpty());
  h->setTrainee("S01");
  ASSERT_EQ(h->trend().size(), 4);
  EXPECT_EQ(h->trend()[0].toMap()["values"].toStringList(), (QStringList{"0", "2", "1"}));  // oldest first
  EXPECT_EQ(h->trend()[1].toMap()["values"].toStringList(), (QStringList{"0", "1", "0"}));
  EXPECT_EQ(h->trend()[3].toMap()["values"].toStringList(), (QStringList{"3", "0", "1"}));
}

TEST_F(History, TheTrendHasAtMostTwentyTheNewest) {
  b.list = [](const std::string&, auto done) {
    std::vector<HistoryRow> l;
    for (int i = 0; i < 25; ++i) {
      HistoryRow r;
      r.id = std::to_string(i);
      r.trainee = "S01";
      r.scenario = "D";
      r.startedMs = kNow - i * 1000;
      r.errors = i;
      l.push_back(r);
    }
    done(l, "");
  };
  h->setBackend(b);
  trainee("S01");
  h->setScenario("D");
  const QStringList v = h->trend()[0].toMap()["values"].toStringList();
  ASSERT_EQ(v.size(), 20);
  EXPECT_EQ(v.first(), "19");
  EXPECT_EQ(v.last(), "0");
}

TEST_F(History, DebriefShowsFactsAndNoScore) {
  trainee("S01");
  open("h1");
  EXPECT_EQ(h->head()["end"].toString(), "COMPLETED");
  EXPECT_TRUE(h->head()["sub"].toString().contains("fallback PTT used: no"));
  for (const QVariant& v : h->cards()) {
    EXPECT_FALSE(v.toMap()["name"].toString().contains("score", Qt::CaseInsensitive));
    EXPECT_FALSE(v.toMap()["name"].toString().contains("readback", Qt::CaseInsensitive));  // H-4
  }
  EXPECT_EQ(h->suspected().size(), 1);
  EXPECT_FALSE(h->head().contains("name"));  // ID only (H-2)
}

TEST_F(History, ChangedScenarioIsSaidInTheHeader) {  // D-340
  trainee("S01");
  open("h1");
  EXPECT_EQ(h->head()["changed"].toString(), "");
  open("h6");
  EXPECT_EQ(h->head()["changed"].toString(), "Scenario changed since this run");
}

TEST_F(History, EntryFilters) {
  trainee("S01");
  open("h1");
  EXPECT_EQ(h->entries().size(), 9);
  h->setEntryFilter("errors");
  ASSERT_EQ(h->entries().size(), 1);
  EXPECT_EQ(h->entries()[0].toMap()["outcome"].toString(), "INCORRECT runway");
  h->setEntryFilter("stt");
  ASSERT_EQ(h->entries().size(), 1);
  EXPECT_EQ(h->entries()[0].toMap()["outcome"].toString(), "AMBIGUOUS, not counted");
  h->setEntryFilter("ptt");
  EXPECT_EQ(h->entries().size(), 1);
}

TEST_F(History, AnUncertainValueIsShownAsHeard) {
  trainee("S01");
  open("h1");
  h->selectEntry(6);
  EXPECT_EQ(h->entry()["parsed"].toString(), "RWY \"0.9\" ?");
  EXPECT_EQ(h->entry()["heard"].toString(), "eagle four two one line up runway 0.9");
}

TEST_F(History, PlayOnlyIfAudioWasSaved) {
  trainee("S01");
  open("h1");
  h->selectEntry(6);  // no audio
  h->play(false);
  drain();
  EXPECT_EQ(played, 0);
  EXPECT_EQ(h->entry()["audioNote"].toString(), "no audio saved");
  h->selectEntry(1);
  h->play(true);
  drain();
  EXPECT_EQ(played, 1);
  stopped = 0;
  h->leave();
  EXPECT_EQ(stopped, 1);
}

TEST_F(History, ReclassifyIsTheInstructorsAndKeepsTheOriginal) {
  trainee("S01");
  open("h1");
  h->selectEntry(4);
  EXPECT_FALSE(h->entry()["mayReclassify"].toBool());
  h->reclassify("Not an error", "");
  drain();
  EXPECT_EQ(reclassified, 0);
  instructor();
  open("h1");
  h->selectEntry(4);
  h->reclassify("Something else", "");
  drain();
  EXPECT_EQ(reclassified, 0);
  h->reclassify("Speech recognition error", "the model heard two seven");
  drain();
  EXPECT_EQ(reclassified, 1);
  ASSERT_EQ(h->reviews().size(), 1);
  EXPECT_EQ(h->reviews()[0].toMap()["verdict"].toString(), "Speech recognition error");
  EXPECT_EQ(lastVerdict, "stt");  // the backend gets the key, as HistoryService wants it
  EXPECT_EQ(h->entry()["outcome"].toString(), "INCORRECT runway");  // unchanged
  EXPECT_TRUE(h->entries()[4].toMap()["outcome"].toString().contains("(reclassified)"));
  h->selectEntry(0);  // a pilot's call is not judged
  EXPECT_FALSE(h->entry()["mayReclassify"].toBool());
}

TEST_F(History, DeletingIsTheInstructors) {  // H-3
  trainee("S01");
  h->select("h1");
  h->removeSelected();
  drain();
  EXPECT_EQ(removed, 0);
  instructor();
  h->select("h1");
  h->removeSelected();
  drain();
  EXPECT_EQ(removed, 1);
  EXPECT_EQ(h->rows().size(), 4);
  EXPECT_EQ(h->selected(), "");
  EXPECT_TRUE(h->message().contains("in the log"));
}

TEST_F(History, AnotherLoginClearsTheScreen) {
  instructor();
  open("h4");
  h->setScenario("Departure 01");
  trainee("S01");
  EXPECT_FALSE(h->isOpen());
  EXPECT_EQ(h->scenario(), "");
  EXPECT_EQ(h->selected(), "");
  a->logout();
  EXPECT_TRUE(h->rows().isEmpty());
}

TEST_F(History, AnAnswerForSomebodyWhoHasLoggedOutIsDropped) {
  std::function<void(std::vector<HistoryRow>, const std::string&)> late;
  b.list = [&late](const std::string&, auto done) { late = done; };
  h->setBackend(b);
  trainee("S01");
  a->logout();
  HistoryRow r;
  r.trainee = "S01";
  late({r}, "");
  drain();
  EXPECT_TRUE(h->rows().isEmpty());
  EXPECT_FALSE(h->loaded());
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  QCoreApplication app(argc, argv);
  return RUN_ALL_TESTS();
}
