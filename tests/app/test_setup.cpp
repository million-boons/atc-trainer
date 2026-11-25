#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <memory>

#include "app/SetupController.h"
#include "app/SetupMock.h"
using namespace atc;
using L = LabController;

namespace {
struct Setup : testing::Test {
  Ns now = ms(1000);
  std::unique_ptr<AccessController> a;
  std::unique_ptr<SetupController> s;
  SetupBackend b = mockSetup();
  int numbersSet = 0, choicesSet = 0, learnStops = 0;

  void SetUp() override {
    AccessBackend ab;
    ab.load = [] { return std::string(); };
    ab.people = [] {
      Person p;
      p.id = "S01";
      return std::vector<Person>{p};
    };
    ab.pinSet = [] { return true; };
    ab.checkPin = [](const std::string& x) { return x == "482913"; };
    a = std::make_unique<AccessController>(ab, [this] { return now; });
    s = std::make_unique<SetupController>(a.get());
    const auto count = [](auto& f, int& n) { f = [inner = f, &n](auto&&... x) { ++n; return inner(std::forward<decltype(x)>(x)...); }; };
    count(b.setNumber, numbersSet);
    count(b.setChoice, choicesSet);
    count(b.stopLearn, learnStops);
    s->setBackend(b);
    s->enter();
    drain();
  }
  void TearDown() override { s.reset(); }
  static void drain() {
    for (int i = 0; i < 6; ++i) QCoreApplication::processEvents();
  }
  void instructor() {
    a->loginInstructor("482913");
    QElapsedTimer t;
    t.start();
    while (!a->instructor() && t.elapsed() < 3000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    ASSERT_TRUE(a->instructor());
  }
  QVariantMap item(const QVariantMap& all, const char* page, const char* key) const {
    for (const QVariant& v : all[page].toList())
      if (v.toMap()["key"].toString() == key) return v.toMap();
    return {};
  }
  QVariantMap number(const char* key, const char* page = "ptt") const { return item(s->numbers(), page, key); }
  QVariantMap choice(const char* key, const char* page) const { return item(s->choices(), page, key); }
};
}  // namespace

TEST_F(Setup, EverybodyMayLookAndChangeTheThemeOnly) {  // E-1
  EXPECT_EQ(number("tail")["text"].toString(), "200 ms");
  s->choose("theme", "night");
  drain();
  EXPECT_EQ(s->theme(), "night");
  s->choose("radio", "heavy");
  s->stepNumber("tail", 1);
  s->restore("ptt");
  s->testSentence();
  drain();
  EXPECT_EQ(choice("radio", "voice")["value"].toString(), "light");
  EXPECT_EQ(numbersSet, 0);
  EXPECT_TRUE(s->message().contains("Only the instructor"));
  ASSERT_TRUE(a->loginTrainee("S01"));
  s->choose("motion", "reduce");
  drain();
  EXPECT_TRUE(s->reduceMotion());
}

TEST_F(Setup, ARefusedValueShowsTheRangeAndChangesNothing) {
  instructor();
  s->stepNumber("tail", 100);
  drain();
  EXPECT_EQ(numbersSet, 0);
  EXPECT_EQ(number("tail")["text"].toString(), "200 ms");
  EXPECT_EQ(s->message(), "Tail: 0 to 500 ms.");
  s->stepNumber("tail", 1);
  drain();
  EXPECT_EQ(number("tail")["text"].toString(), "250 ms");
  EXPECT_TRUE(number("tail")["byUser"].toBool());
  EXPECT_TRUE(s->message().contains("next transmission"));
}

TEST_F(Setup, TheBackendsRefusalIsShown) {
  b.setNumber = [](const std::string&, int, SetupBackend::Done done) { done("the tail must be shorter than the longest transmission"); };
  s->setBackend(b);
  instructor();
  s->stepNumber("tail", 1);
  drain();
  EXPECT_TRUE(s->message().contains("shorter"));
  EXPECT_EQ(number("tail")["text"].toString(), "200 ms");
}

TEST_F(Setup, RestoreBringsTheStartingValuesOfThePageBack) {
  instructor();
  s->stepNumber("tail", 1);
  drain();
  s->choose("radio", "heavy");
  drain();
  s->restore("ptt");
  drain();
  EXPECT_EQ(number("tail")["text"].toString(), "200 ms");
  EXPECT_FALSE(number("tail")["byUser"].toBool());
  EXPECT_EQ(choice("radio", "voice")["value"].toString(), "heavy");  // another page
}

TEST_F(Setup, AMissingDeviceCannotBeChosen) {
  instructor();
  s->choose("mic", "gone");
  drain();
  EXPECT_EQ(choicesSet, 0);
  s->choose("mic", "int");
  drain();
  EXPECT_EQ(choice("mic", "audio")["text"].toString(), "Internal microphone (simulated)");
}

TEST_F(Setup, NothingIsChangedDuringAnExercise) {
  instructor();
  s->startLearn();
  s->setTrainerBusy(true);
  EXPECT_FALSE(s->learning());
  EXPECT_EQ(learnStops, 1);
  s->stepNumber("tail", 1);
  s->choose("theme", "hc");
  s->deleteGuests(3);
  drain();
  EXPECT_EQ(numbersSet + choicesSet, 0);
  EXPECT_EQ(s->guests(), 3);
}

TEST_F(Setup, APedalIsTakenOnlyAfterHoldingAndReleasing) {
  std::function<void(std::vector<SetupPress>)> report;
  b.startLearn = [&report](auto r, SetupBackend::Done done) {
    report = r;
    done("");
  };
  s->setBackend(b);
  instructor();
  s->startLearn();
  report({{"pedal", "Pedal", "1234:5678", 300, true, ""}});
  drain();
  EXPECT_FALSE(s->presses()[0].toMap()["usable"].toBool());
  s->takePedal("pedal");
  drain();
  EXPECT_EQ(choicesSet, 0);
  report({{"pedal", "Pedal", "1234:5678", 2100, true, ""}});
  drain();
  EXPECT_EQ(s->presses()[0].toMap()["state"].toString(), "release it now");
  report({{"pedal", "Pedal", "1234:5678", 0, false, ""}});  // released
  drain();
  EXPECT_TRUE(s->presses()[0].toMap()["usable"].toBool());
  s->takePedal("pedal");
  drain();
  EXPECT_EQ(choicesSet, 1);
  EXPECT_FALSE(s->learning());
  EXPECT_EQ(learnStops, 1);
}

TEST_F(Setup, AKeyboardIsNotTaken) {  // D-310
  instructor();
  s->startLearn();
  drain();
  ASSERT_EQ(s->presses().size(), 2);
  EXPECT_FALSE(s->presses()[1].toMap()["usable"].toBool());
  EXPECT_TRUE(s->presses()[1].toMap()["state"].toString().contains("VID:PID"));
  s->takePedal("kbd");
  drain();
  EXPECT_EQ(choicesSet, 0);
}

TEST_F(Setup, LearningEndsWithTheScreenAndWithTheLogin) {
  instructor();
  s->startLearn();
  s->leave();
  EXPECT_FALSE(s->learning());
  s->startLearn();
  a->logout();
  EXPECT_FALSE(s->learning());
  EXPECT_EQ(learnStops, 2);
  s->startLearn();
  EXPECT_FALSE(s->learning());
}

TEST_F(Setup, ThePinNeedsTheOldOneAndTwoEqualEntries) {  // E-3
  instructor();
  int changed = 0;
  QObject::connect(s.get(), &SetupController::pinChanged, s.get(), [&changed] { ++changed; });
  s->changePin("123456", "12345", "12345");
  EXPECT_TRUE(s->message().contains("6 to 12"));
  s->changePin("123456", "12345a", "12345a");
  EXPECT_TRUE(s->message().contains("6 to 12"));
  s->changePin("123456", "654321", "654322");
  EXPECT_TRUE(s->message().contains("differ"));
  s->changePin("000000", "654321", "654321");
  drain();
  EXPECT_TRUE(s->message().contains("wrong"));
  EXPECT_EQ(changed, 0);
  s->changePin("123456", "654321", "654321");
  drain();
  EXPECT_EQ(changed, 1);
}

TEST_F(Setup, GuestResultsAreACountAndDeletedOnlyWithTheRightOne) {  // D-131
  EXPECT_EQ(s->guests(), 3);
  s->deleteGuests(3);  // nobody is logged in
  drain();
  EXPECT_EQ(s->guests(), 3);
  instructor();
  s->deleteGuests(2);
  drain();
  EXPECT_EQ(s->guests(), 3);
  EXPECT_TRUE(s->message().contains("Nothing was deleted"));
  s->deleteGuests(3);
  drain();
  EXPECT_EQ(s->guests(), 0);
  EXPECT_TRUE(s->message().contains("3 guest results deleted"));
}

TEST_F(Setup, ANewCountIsShownIfTheStoreRefuses) {
  b.deleteGuests = [](int, auto done) { done(5, "the number of guest results has changed: nothing was deleted"); };
  s->setBackend(b);
  instructor();
  s->deleteGuests(3);
  drain();
  EXPECT_EQ(s->guests(), 5);
  EXPECT_TRUE(s->message().contains("nothing was deleted"));
}

TEST_F(Setup, AFileThatCannotBeReadIsSaid) {
  b.load = [](auto done) { done({}, "settings.json is damaged"); };
  s->setBackend(b);
  s->enter();
  drain();
  EXPECT_FALSE(s->ready());
  EXPECT_EQ(s->message(), "settings.json is damaged");
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  QCoreApplication app(argc, argv);
  return RUN_ALL_TESTS();
}
