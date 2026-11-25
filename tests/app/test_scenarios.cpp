#include <gtest/gtest.h>

#include <QCoreApplication>
#include <memory>

#include "app/ScenarioController.h"
#include "app/ScenarioMock.h"
using L = LabController;

namespace {
struct Scenarios : testing::Test {
  std::unique_ptr<ScenarioController> s;
  ScenarioBackend b;
  std::vector<std::string> selected, checked;

  void SetUp() override {
    s = std::make_unique<ScenarioController>();
    b = mockScenarios([this](const ScenarioInfo& x) { selected.push_back(x.id); });
    b.coverage = [this, inner = b.coverage](const std::string& id, auto done) {
      checked.push_back(id);
      inner(id, done);
    };
    s->setBackend(b);
  }
  void TearDown() override { s.reset(); }
  static void drain() {
    for (int i = 0; i < 6; ++i) QCoreApplication::processEvents();
  }
  void enter() {
    s->enter();
    drain();
    ASSERT_TRUE(s->ready());
  }
  QVariantMap row(const char* id) const {
    for (const QVariant& v : s->rows())
      if (v.toMap()["id"].toString() == id) return v.toMap();
    return {};
  }
};
}  // namespace

TEST_F(Scenarios, ListFilterAndCount) {
  enter();
  EXPECT_EQ(s->countText(), "9 of 9");
  s->setCategory("departure");
  EXPECT_EQ(s->countText(), "3 of 9");
  s->setDifficulty("advanced");
  EXPECT_EQ(s->rows().size(), 1);
  s->setCategory("all");
  EXPECT_EQ(s->rows().size(), 2);
  s->search("go-around");
  EXPECT_EQ(s->rows().size(), 1);
  s->search("nothing");
  EXPECT_TRUE(s->rows().isEmpty());
}

TEST_F(Scenarios, ACategoryWithoutScenariosIsDisabled) {
  enter();
  ASSERT_EQ(s->categories().size(), 9);  // all and the eight of the schema
  for (const QVariant& v : s->categories()) {
    const QString id = v.toMap()["id"].toString();
    EXPECT_EQ(v.toMap()["off"].toBool(), id == "emergency" || id == "rcf" || id == "abnormal") << id.toStdString();
  }
}

TEST_F(Scenarios, OpeningACardChecksItsClipsOnce) {  // D-115
  enter();
  EXPECT_EQ(checked, std::vector<std::string>{"DEPARTURE-01"});
  EXPECT_EQ(row("ARRIVAL-01")["state"].toString(), "Clips not checked");
  s->open("ARRIVAL-01");
  drain();
  EXPECT_EQ(row("ARRIVAL-01")["state"].toString(), "7 clips missing");
  EXPECT_EQ(row("DEPARTURE-01")["state"].toString(), "Clips complete");
  EXPECT_EQ(s->missing().size(), 7);
  s->open("DEPARTURE-01");
  s->open("ARRIVAL-01");
  drain();
  EXPECT_EQ(checked.size(), 2u);
}

TEST_F(Scenarios, SelectIsAllowedWhileCheckingAndWithMissingClips) {
  enter();
  s->open("DEPARTURE-03");
  drain();
  EXPECT_TRUE(s->detail()["bank"].toString().contains("CHECKING"));
  EXPECT_TRUE(s->canSelect());
  s->open("ARRIVAL-01");
  drain();
  EXPECT_TRUE(s->canSelect());
  EXPECT_TRUE(s->detail()["bank"].toString().contains("block Start"));
}

TEST_F(Scenarios, AFileWithErrorsIsListedAndCannotBeSelected) {  // D-408
  enter();
  EXPECT_EQ(row("GROUND-02")["state"].toString(), "Cannot be loaded: 2 errors");
  s->open("GROUND-02");
  drain();
  EXPECT_EQ(s->errors().size(), 2);
  EXPECT_FALSE(s->canSelect());
  s->select();
  drain();
  EXPECT_TRUE(selected.empty());
  EXPECT_EQ(checked.size(), 1u);  // its clips are not checked
}

TEST_F(Scenarios, StepsShowTheIntentAndNeverTheValues) {
  enter();
  ASSERT_EQ(s->steps().size(), 3);
  EXPECT_EQ(s->steps()[0].toMap()["intent"].toString(), "TAXI_INSTRUCTION");
  EXPECT_EQ(s->steps()[0].toMap()["wait"].toString(), "30 s, 2 calls");
  for (const QVariant& v : s->steps()) EXPECT_EQ(v.toMap().size(), 4);
}

TEST_F(Scenarios, PlaceholderAndValidatedAreWords) {
  enter();
  EXPECT_EQ(row("DEPARTURE-01")["tag"].toString(), "PLACEHOLDER");
  EXPECT_EQ(row("GROUND-01")["tag"].toString(), "VALIDATED");
  EXPECT_TRUE(s->detail()["assessed"].toString().contains("not available"));
  s->open("GROUND-01");
  EXPECT_TRUE(s->detail()["assessed"].toString().contains("needs a trainee"));
}

TEST_F(Scenarios, SelectPutsItOnTheDashboard) {
  int done = 0;
  QObject::connect(s.get(), &ScenarioController::selected, s.get(), [&done] { ++done; });
  enter();
  s->open("GROUND-01");
  s->select();
  s->select();  // once
  drain();
  EXPECT_EQ(done, 1);
  EXPECT_EQ(selected, std::vector<std::string>{"GROUND-01"});
}

TEST_F(Scenarios, ARefusalIsShownAndNotDuringAnExercise) {
  int done = 0;
  QObject::connect(s.get(), &ScenarioController::selected, s.get(), [&done] { ++done; });
  b.select = [](const std::string&, auto cb) { cb("the file changed"); };
  s->setBackend(b);
  enter();
  s->select();
  drain();
  EXPECT_EQ(s->message(), "the file changed");
  EXPECT_EQ(done, 0);
  EXPECT_TRUE(s->canSelect());
  s->setTrainerBusy(true);
  EXPECT_FALSE(s->canSelect());
}

TEST_F(Scenarios, AnAnswerFromBeforeTheFilesWereReadAgainIsIgnored) {
  std::vector<std::function<void(ScenarioCoverage)>> late;
  b.coverage = [&late](const std::string&, auto cb) { late.push_back(cb); };
  s->setBackend(b);
  enter();
  enter();
  ASSERT_EQ(late.size(), 2u);
  ScenarioCoverage ok;
  ok.state = ScenarioCoverage::State::Ok;
  late[0](ok);
  drain();
  EXPECT_EQ(row("DEPARTURE-01")["state"].toString(), "Checking the clips");
  late[1](ok);
  drain();
  EXPECT_EQ(row("DEPARTURE-01")["state"].toString(), "Clips complete");
}

TEST_F(Scenarios, UnreadableFilesAreSaid) {
  b.load = [](auto cb) { cb({}, "data/scenarios cannot be read"); };
  s->setBackend(b);
  s->enter();
  drain();
  EXPECT_FALSE(s->ready());
  EXPECT_EQ(s->message(), "data/scenarios cannot be read");
  EXPECT_FALSE(s->canSelect());
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  QCoreApplication app(argc, argv);
  return RUN_ALL_TESTS();
}
