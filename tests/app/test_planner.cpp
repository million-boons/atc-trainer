#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <memory>

#include "app/PlannerController.h"
#include "app/PlannerMock.h"
using namespace atc;
using L = LabController;

namespace {
struct Planner : testing::Test {
  std::vector<Person> people;
  Ns now = ms(1000);
  std::unique_ptr<AccessController> a;
  std::unique_ptr<PlannerController> p;
  PlannerBackend b;
  std::vector<PlanChoice> chosen;
  int saved = 0, asked = 0;
  std::string saveAnswer;
  std::function<void(PlannerContent&)> change;  // of what load() gives
  std::function<void()> mockAccess;              // the mock's PB-6 hook
  std::vector<std::pair<std::string, std::string>> putAway;

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
    p = std::make_unique<PlannerController>(a.get());
    MockPlanner m = mockPlanner([this](const PlanChoice& c) { chosen.push_back(c); },
                                [this] { return a->trainee() ? a->personId().toStdString() : std::string(); });
    b = m.backend;
    mockAccess = m.accessChanged;
    QObject::connect(a.get(), &AccessController::changed, a.get(), [this] { mockAccess(); });
    b.setEvents = [this, inner = b.setEvents](PlannerEvents e) {
      e.putAway = [this, f = e.putAway](const std::string& name, const std::string& person) {
        putAway.emplace_back(name, person);
        if (f) f(name, person);
      };
      inner(e);
    };
    b.load = [this, inner = b.load](auto done) {
      inner([this, done](PlannerContent c, const std::string& why) {
        if (change) change(c);
        done(c, why);
      });
    };
    b.save = [this, inner = b.save](Plan x, const std::string& r, auto done) {
      ++saved;
      if (!saveAnswer.empty()) return done("", saveAnswer);
      inner(x, r, done);
    };
    b.choose = [this, inner = b.choose](const std::string& id, auto done) {
      ++asked;
      inner(id, done);
    };
    p->setBackend(b);
  }
  void TearDown() override { p.reset(); }
  static void drain() {
    for (int i = 0; i < 6; ++i) QCoreApplication::processEvents();
  }
  void login() {
    a->loginInstructor("482913");
    QElapsedTimer t;
    t.start();
    while (!(a->instructor() && p->ready() && p->hasPlan()) && t.elapsed() < 3000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    drain();
    ASSERT_TRUE(p->hasPlan());
  }
  void open(const char* id) {
    p->select(id);
    drain();
    ASSERT_EQ(p->plan()["id"].toString(), id);
  }
  QString stateOf(const char* id) const {
    for (const QVariant& v : p->plans())
      if (v.toMap()["id"].toString() == id) return v.toMap()["state"].toString();
    return "?";
  }
  QStringList choiceIds() const {
    QStringList l;
    for (const QVariant& v : p->choices()) l << v.toMap()["id"].toString();
    return l;
  }
  bool hasCheck(const QString& t) const {
    for (const QVariant& v : p->checks())
      if (v.toMap()["text"].toString() == t) return true;
    return false;
  }
  // Every text Validated.
  static void validated(PlannerContent& c) { c.scenarios.front().placeholder = false; }
};
}  // namespace

TEST_F(Planner, OnlyTheInstructorSeesAndChangesPlans) {
  p->enter();
  drain();
  EXPECT_FALSE(p->ready());
  EXPECT_TRUE(p->plans().isEmpty());
  EXPECT_FALSE(p->hasPlan());
  p->newPlan();
  EXPECT_FALSE(p->hasPlan());
  EXPECT_TRUE(p->message().contains("Only the instructor"));
  login();
  EXPECT_EQ(p->plans().size(), 4);
  a->logout();
  EXPECT_TRUE(p->plans().isEmpty());
  EXPECT_FALSE(p->hasPlan());
}

TEST_F(Planner, NotDuringAnExercise) {
  login();
  p->setTrainerBusy(true);
  p->setName("x");
  EXPECT_FALSE(p->dirty());
  EXPECT_FALSE(p->canPut());
  p->choose("p2");
  drain();
  EXPECT_EQ(asked, 0);
}

TEST_F(Planner, ListShowsRunsAndState) {  // D-143
  login();
  EXPECT_EQ(p->plans()[0].toMap()["runs"].toString(), "3 runs, 25 Nov 16:20");
  EXPECT_EQ(p->plans()[1].toMap()["runs"].toString(), "0 runs");
  EXPECT_EQ(stateOf("p1"), "Ready");
  EXPECT_EQ(stateOf("p3"), "Scenario changed");
  EXPECT_EQ(stateOf("p4"), "Warns: preset changed");
  p->setFilter("anyone");
  EXPECT_EQ(p->plans().size(), 2);
  p->setFilter("S02");
  EXPECT_EQ(p->plans().size(), 1);
  p->setFilter("all");
  p->search("random");
  EXPECT_EQ(p->plans().size(), 1);
}

TEST_F(Planner, AnEditIsNeverLeftSilently) {
  login();
  open("p1");
  p->setName("Other");
  EXPECT_TRUE(p->dirty());
  p->select("p2");
  p->newPlan();
  p->copyPlan();
  EXPECT_EQ(p->plan()["id"].toString(), "p1");
  EXPECT_TRUE(p->message().contains("Save or undo"));
  EXPECT_FALSE(p->canPut());  // the Dashboard gets saved plans only
  p->enter();                 // another screen and back: the edit is still there
  drain();
  EXPECT_EQ(p->plan()["name"].toString(), "Other");
  p->undo();
  drain();
  EXPECT_EQ(p->plan()["name"].toString(), "Departure, runway 27");
}

TEST_F(Planner, AValueThatNoLongerFitsIsMarkedAndNothingIsCorrected) {
  login();
  open("p1");
  EXPECT_FALSE(p->canSave());  // nothing changed
  p->setRunway("09");
  EXPECT_EQ(p->plan()["taxiway"].toString(), "B2");
  EXPECT_TRUE(p->plan()["taxiwayBad"].toBool());
  EXPECT_FALSE(p->plan()["runwayBad"].toBool());
  EXPECT_FALSE(p->plan()["aircraftBad"].toBool());
  EXPECT_FALSE(p->canSave());
  EXPECT_EQ(p->saveNote(), "The values do not fit together.");
  EXPECT_TRUE(hasCheck("Values do not fit"));
  EXPECT_EQ(p->taxiways()[0].toMap()["note"].toString(), "");                        // A3
  EXPECT_EQ(p->taxiways()[1].toMap()["note"].toString(), "does not fit the others");  // B2
  p->setTaxiway("A3");
  EXPECT_FALSE(p->plan()["taxiwayBad"].toBool());
  EXPECT_TRUE(p->canSave());
  p->save();
  drain();
  EXPECT_EQ(saved, 1);
  EXPECT_FALSE(p->dirty());
  EXPECT_EQ(p->plan()["runway"].toString(), "09");
}

TEST_F(Planner, AircraftIsCallsignAndType) {
  login();
  open("p1");
  p->setAircraft("EAGLE-421  C295");
  EXPECT_EQ(p->plan()["aircraft"].toString(), "EAGLE-421  C295");
  EXPECT_TRUE(p->canSave());
}

// The user's review of 2026-11-26 (D-152): the card shows the saved values that will run, never those of an edit.
TEST_F(Planner, TheCardShowsTheSavedPlanAndSaysThatThereAreUnsavedChanges) {
  login();
  open("p1");
  const QVariantMap before = p->preview();
  ASSERT_TRUE(before["saved"].toBool());
  EXPECT_FALSE(before["unsaved"].toBool());
  p->setAircraft("EAGLE-421  C295");
  p->setRunway("09");  // with B2 this does not fit (as "planmis")
  QVariantMap m = p->preview();
  EXPECT_TRUE(m["unsaved"].toBool());
  for (const char* k : {"callsign", "type", "runway", "taxiway", "name", "planLine", "briefing"}) EXPECT_EQ(m[k], before[k]) << k;
  p->undo();
  drain();
  EXPECT_FALSE(p->preview()["unsaved"].toBool());
  p->setAircraft("EAGLE-421  C295");
  ASSERT_TRUE(p->canSave());
  p->save();
  drain();
  m = p->preview();
  EXPECT_FALSE(m["unsaved"].toBool());
  EXPECT_EQ(m["callsign"].toString(), "EAGLE-421");  // saved: now it is what runs
}

TEST_F(Planner, ANewPlanShowsNoValuesBeforeItIsSaved) {
  login();
  p->newPlan();
  drain();
  ASSERT_TRUE(p->hasPlan());
  const QVariantMap m = p->preview();
  EXPECT_FALSE(m["saved"].toBool());
  EXPECT_TRUE(m["unsaved"].toBool());
  EXPECT_FALSE(m.contains("callsign"));
  EXPECT_FALSE(m.contains("runway"));
}

TEST_F(Planner, AScenarioWithErrorsCannotBeChosen) {
  login();
  open("p1");
  EXPECT_TRUE(p->scenarios()[1].toMap()["off"].toBool());
  p->setScenario("BROKEN-01");
  EXPECT_EQ(p->plan()["scenarioId"].toString(), "DEPARTURE-01");
}

TEST_F(Planner, BriefingIsOneLineOfAtMost200Characters) {  // D-143
  login();
  open("p1");
  p->setBriefing("a\nb" + QString(300, 'x'));
  EXPECT_EQ(p->plan()["briefing"].toString().size(), 200);
  EXPECT_TRUE(p->plan()["briefing"].toString().startsWith("a bx"));
  EXPECT_EQ(p->plan()["briefingLeft"].toString(), "200 of 200");
}

TEST_F(Planner, NameIsNeededAndUnique) {
  login();
  open("p1");
  p->setName("  ");
  EXPECT_EQ(p->saveNote(), "Enter a name.");
  p->setName("departure, RANDOM");
  EXPECT_EQ(p->saveNote(), "Another plan has this name.");
  EXPECT_FALSE(p->canSave());
}

TEST_F(Planner, AssessedNeedsAValidatedScenarioAndATrainee) {
  login();
  open("p1");
  p->setAssessed(true);
  EXPECT_FALSE(p->plan()["assessed"].toBool());
  EXPECT_EQ(p->plan()["assessedNote"].toString(), "Assessed: scenario not Validated");
  change = validated;
  p->undo();
  drain();
  EXPECT_TRUE(p->plan()["assessedOn"].toBool());
  p->setAssessed(true);
  EXPECT_TRUE(p->canSave());
  p->setTrainee("");  // Anyone
  EXPECT_FALSE(p->canSave());
  EXPECT_EQ(p->saveNote(), "Assessed needs a trainee.");
}

TEST_F(Planner, AnAssessedPlanStopsWhenItsScenarioIsNoLongerValidated) {
  change = [](PlannerContent& c) { c.plans.front().assessed = true; };
  login();
  EXPECT_EQ(stateOf("p1"), "Cannot start: not Validated");
  EXPECT_FALSE(p->canPut());
}

TEST_F(Planner, PresetsFillTheFieldsAndAnEditMakesItCustom) {  // D-143
  login();
  open("p1");
  p->setPreset(0);
  EXPECT_EQ(p->plan()["from"].toString(), "0.6 s");
  EXPECT_EQ(p->plan()["to"].toString(), "1.0 s");
  EXPECT_EQ(p->plan()["radio"].toString(), "light");
  p->setPreset(2);
  EXPECT_EQ(p->plan()["radio"].toString(), "medium");
  EXPECT_EQ(p->plan()["preset"].toInt(), 2);
  p->stepReply(true, 1);
  EXPECT_EQ(p->plan()["from"].toString(), "0.6 s");
  EXPECT_EQ(p->plan()["preset"].toInt(), int(PlanPreset::Custom));
  p->stepReply(true, 100);  // never above the longest
  EXPECT_EQ(p->plan()["from"].toString(), "1.5 s");
  p->stepReply(false, -100);
  EXPECT_EQ(p->plan()["to"].toString(), "1.5 s");
  p->setPreset(1);
  p->setRadio("heavy");
  EXPECT_EQ(p->plan()["preset"].toInt(), int(PlanPreset::Custom));
  p->setPreset(1);
  EXPECT_TRUE(p->plan()["events"].toString().contains("not built"));
}

TEST_F(Planner, AChangedPresetLeavesSavedPlansAsTheyAre) {  // P-6, D-144
  login();
  open("p1");
  p->editPresets();
  p->stepPreset(1, false, 5);
  EXPECT_EQ(p->presets()[1].toMap()["to"].toString(), "2.0 s");
  p->savePresets();
  drain();
  EXPECT_EQ(p->plan()["to"].toString(), "1.5 s");
  EXPECT_TRUE(p->plan()["presetChanged"].toBool());
  EXPECT_EQ(stateOf("p1"), "Warns: preset changed");
  EXPECT_TRUE(p->canPut());  // a warning only
  EXPECT_TRUE(p->saveNote().contains("as Custom"));
  p->save();
  drain();
  EXPECT_EQ(p->plan()["preset"].toInt(), int(PlanPreset::Custom));
  EXPECT_EQ(p->plan()["to"].toString(), "1.5 s");
  EXPECT_EQ(stateOf("p1"), "Ready");
  p->setPreset(1);  // now the new values
  EXPECT_EQ(p->plan()["to"].toString(), "2.0 s");
}

TEST_F(Planner, UseNewPresetValuesTakesThePresetOfToday) {  // D-147
  login();
  open("p1");
  p->useNewPreset();  // nothing has changed
  EXPECT_FALSE(p->dirty());
  open("p4");  // Normal, saved with 2.0 s
  ASSERT_TRUE(p->plan()["presetChanged"].toBool());
  p->useNewPreset();
  EXPECT_EQ(p->plan()["to"].toString(), "1.5 s");
  EXPECT_FALSE(p->plan()["presetChanged"].toBool());
  EXPECT_EQ(stateOf("p4"), "Warns: preset changed");  // until it is saved
  p->save();
  drain();
  EXPECT_EQ(p->plan()["preset"].toInt(), int(PlanPreset::Normal));
  EXPECT_EQ(stateOf("p4"), "Ready");
}

TEST_F(Planner, PresetsWaitForAnOpenEditAndCanBeRestored) {
  login();
  open("p1");
  p->setName("Other");
  p->editPresets();
  p->stepPreset(0, true, -2);
  p->savePresets();
  drain();
  EXPECT_TRUE(p->message().contains("Save or undo"));
  EXPECT_EQ(p->presets()[0].toMap()["from"].toString(), "0.4 s");
  p->startingPresets();
  EXPECT_EQ(p->presets()[0].toMap()["from"].toString(), "0.6 s");
  p->stepPreset(0, true, -2);
  p->cancelPresets();
  EXPECT_EQ(p->presets()[0].toMap()["from"].toString(), "0.6 s");
}

TEST_F(Planner, AChangedScenarioStopsThePlanUntilItIsSaved) {
  login();
  open("p3");
  EXPECT_FALSE(p->canPut());
  EXPECT_TRUE(hasCheck("Scenario changed"));
  EXPECT_FALSE(p->dirty());
  EXPECT_TRUE(p->canSave());
  p->choose("p3");
  drain();
  EXPECT_EQ(asked, 0);
  p->save();
  drain();
  EXPECT_EQ(stateOf("p3"), "Ready");
  EXPECT_TRUE(p->canPut());
}

TEST_F(Planner, NewAndCopyAreSavedWithTheirOwnId) {
  login();
  p->newPlan();
  EXPECT_TRUE(p->plan()["isNew"].toBool());
  EXPECT_FALSE(p->plan()["fixed"].toBool());
  EXPECT_FALSE(p->canSave());  // no name
  p->setName("Morning");
  p->save();
  drain();
  EXPECT_EQ(p->planCount(), 5);
  EXPECT_EQ(p->plan()["name"].toString(), "Morning");
  EXPECT_FALSE(p->plan()["isNew"].toBool());
  open("p1");
  p->copyPlan();
  EXPECT_EQ(p->plan()["name"].toString(), "Departure, runway 27 (copy)");
  p->save();
  drain();
  EXPECT_EQ(p->planCount(), 6);
  EXPECT_EQ(p->plans()[0].toMap()["runs"].toString(), "3 runs, 25 Nov 16:20");
  EXPECT_EQ(p->plans()[5].toMap()["runs"].toString(), "0 runs");
}

TEST_F(Planner, ARefusedSaveKeepsTheEdit) {
  login();
  open("p1");
  p->setName("Other");
  saveAnswer = "the plans changed on disk";
  p->save();
  drain();
  EXPECT_EQ(p->message(), "the plans changed on disk");
  EXPECT_TRUE(p->dirty());
  EXPECT_EQ(p->plan()["name"].toString(), "Other");
  EXPECT_FALSE(p->busy());
}

TEST_F(Planner, DeleteRemovesThePlanOnly) {
  login();
  open("p1");
  p->deletePlan();
  drain();
  EXPECT_EQ(p->planCount(), 3);
  EXPECT_TRUE(p->message().contains("results are kept"));
  EXPECT_TRUE(p->hasPlan());
}

TEST_F(Planner, EverybodySeesTheirOwnPlansAndThoseForAnyone) {  // D-142
  p->enter();
  drain();
  EXPECT_EQ(choiceIds(), (QStringList{"p2", "p4"}));  // guest
  p->choose("p1");
  drain();
  EXPECT_EQ(asked, 0);
  ASSERT_TRUE(a->loginTrainee("S01"));
  EXPECT_EQ(choiceIds(), (QStringList{"p1", "p2", "p4"}));
  ASSERT_TRUE(a->loginTrainee("S02"));
  EXPECT_EQ(choiceIds(), (QStringList{"p2", "p3", "p4"}));
  EXPECT_TRUE(p->choices()[1].toMap()["off"].toBool());  // scenario changed
  login();
  EXPECT_EQ(choiceIds().size(), 4);
}

TEST_F(Planner, ChoosingNumbersTheRunOfAPersonAndNotOfAGuest) {  // P-7, D-144
  p->enter();
  drain();
  p->choose("p2");
  drain();
  ASSERT_EQ(chosen.size(), 1u);
  EXPECT_EQ(chosen[0].run, 0);
  ASSERT_TRUE(a->loginTrainee("S01"));
  p->choose("p1");
  drain();
  ASSERT_EQ(chosen.size(), 2u);
  EXPECT_EQ(chosen[1].run, 4);
  EXPECT_EQ(chosen[1].values.runway, "27");
  EXPECT_TRUE(p->message().contains("On the Dashboard"));
}

TEST_F(Planner, ARandomPlanIsDrawnWhenItIsChosenAndChanges) {
  login();
  open("p2");
  EXPECT_TRUE(p->preview()["drawn"].toBool());
  EXPECT_EQ(p->preview()["callsign"].toString(), "");
  for (int i = 0; i < 8; ++i) {
    p->put();
    drain();
  }
  ASSERT_EQ(chosen.size(), 8u);
  for (std::size_t i = 1; i < chosen.size(); ++i) EXPECT_FALSE(chosen[i].values == chosen[i - 1].values) << i;
}

TEST_F(Planner, TheCardHasTheValuesThePlanLineAndTheNote) {
  PlanChoice c;
  c.plan.id = "p1";
  c.plan.name = "N";
  c.plan.trainee = "S01";
  c.plan.briefing = "B";
  c.scenarioName = "Departure 01";
  c.values = {"FALCON-732", "AN-32", "27", "B2"};
  const QVariantMap m = PlannerController::card(c);
  EXPECT_EQ(m["planNote"].toString(), "plan, Departure 01");
  EXPECT_EQ(m["planLine"].toString(), "Values: fixed   Trainee S01   Practice   Normal");
  EXPECT_EQ(m["taxiway"].toString(), "B2");
  EXPECT_EQ(m["briefing"].toString(), "B");
}

TEST_F(Planner, AnAnswerAfterLogoutChangesNothing) {
  login();
  std::function<void(PlannerContent, const std::string&)> late;
  PlannerBackend other = b;
  other.load = [&](auto done) { late = done; };
  p->setBackend(other);
  p->enter();
  a->logout();
  late(PlannerContent{}, "");
  drain();
  EXPECT_FALSE(p->hasPlan());
  EXPECT_TRUE(p->plans().isEmpty());
}

TEST_F(Planner, AFileThatCannotBeReadIsSaid) {
  b.load = [](auto done) { done(PlannerContent{}, "plans.json is damaged"); };
  p->setBackend(b);
  a->loginInstructor("482913");
  QElapsedTimer t;
  t.start();
  while (p->message().isEmpty() && t.elapsed() < 3000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  EXPECT_EQ(p->message(), "plans.json is damaged");
  EXPECT_EQ(p->messageLevel(), int(L::Fault));
  EXPECT_FALSE(p->ready());
  EXPECT_TRUE(p->choices().isEmpty());
}

TEST_F(Planner, ABuildWithoutABackendSaysSo) {
  login();
  p->setBackend({});
  p->newPlan();
  EXPECT_TRUE(p->message().contains("no Planner backend"));
}

TEST_F(Planner, CoverageIsShownInWordsAndOlderAnswersAreIgnored) {
  std::vector<std::function<void(PlanCoverage)>> late;
  b.coverage = [&late](const std::string&, auto done) { late.push_back(done); };
  p->setBackend(b);
  login();
  open("p2");
  ASSERT_GE(late.size(), 2u);
  EXPECT_TRUE(hasCheck("Voice bank: checking"));
  late.front()({PlanCoverage::State::Ok, 9, 0});
  drain();
  EXPECT_TRUE(hasCheck("Voice bank: checking"));
  late.back()({PlanCoverage::State::Missing, 22, 3});
  drain();
  EXPECT_TRUE(hasCheck("Voice bank: 3 clips missing"));
  late.back()({PlanCoverage::State::Failed, 0, 0});  // it may answer again
  drain();
  EXPECT_TRUE(hasCheck("Voice bank: check failed"));
}

TEST_F(Planner, ARecordThatIsGoneStopsThePlan) {
  login();
  open("p1");
  people.erase(people.begin());
  EXPECT_EQ(stateOf("p1"), "Cannot start: no record S01");
  EXPECT_TRUE(hasCheck("No record S01"));
  EXPECT_FALSE(p->canPut());
  EXPECT_EQ(p->saveNote(), "There is no record S01.");
}

TEST_F(Planner, AScenarioThatIsGoneStopsThePlan) {
  change = [](PlannerContent& c) { c.scenarios.erase(c.scenarios.begin()); };
  login();
  EXPECT_EQ(stateOf("p1"), "Cannot start: no such scenario");
  EXPECT_TRUE(hasCheck("No such scenario"));
  EXPECT_FALSE(p->canPut());
  EXPECT_FALSE(p->canSave());
}

TEST_F(Planner, ValuesThatLeftThePoolsStopThePlan) {
  change = [](PlannerContent& c) { c.scenarios.front().combinations.resize(1); };  // only EAGLE, 09, A3
  login();
  EXPECT_EQ(stateOf("p1"), "Cannot start: values do not fit");
  EXPECT_TRUE(p->plan()["aircraftBad"].toBool());
  EXPECT_EQ(p->plan()["runway"].toString(), "27");  // not corrected
}

TEST_F(Planner, OneCombinationIsUsedAndSaid) {  // 04 section 5g
  change = [](PlannerContent& c) { c.scenarios.front().combinations.resize(1); };
  login();
  open("p2");
  EXPECT_TRUE(hasCheck("Only one combination: no change"));
  b.choose = [](const std::string&, auto done) {
    PlanChoice c;
    c.plan.name = "N";
    c.onlyCombination = true;
    done(c, "");
  };
  p->setBackend(b);
  p->put();
  drain();
  EXPECT_TRUE(p->message().contains("only one combination"));
}

TEST_F(Planner, ARefusedChoiceIsShownAndNothingIsSignalled) {
  int signalled = 0;
  QObject::connect(p.get(), &PlannerController::chosen, p.get(), [&signalled] { ++signalled; });
  b.choose = [](const std::string&, auto done) { done(PlanChoice{}, "the scenario changed on disk"); };
  p->setBackend(b);
  login();
  p->put();
  drain();
  EXPECT_EQ(p->message(), "the scenario changed on disk");
  EXPECT_EQ(signalled, 0);
  EXPECT_TRUE(p->canPut());  // may be tried again
}

TEST_F(Planner, OneChoiceAtATime) {
  std::function<void(PlanChoice, const std::string&)> late;
  int n = 0;
  b.choose = [&](const std::string&, auto done) {
    ++n;
    late = done;
  };
  p->setBackend(b);
  login();
  p->put();
  EXPECT_FALSE(p->canPut());
  p->choose("p2");
  EXPECT_EQ(n, 1);
  late(PlanChoice{}, "");
  drain();
  EXPECT_TRUE(p->canPut());
}

TEST_F(Planner, ACountedRunIsReadAgainAndKeepsAnEdit) {
  std::function<void()> changed;
  b.setEvents = [&changed](PlannerEvents e) { changed = std::move(e.plansChanged); };
  p->setBackend(b);
  login();
  open("p1");
  p->setName("Other");
  change = [](PlannerContent& c) { c.plans.front().runs = 4; };
  changed();
  drain();
  EXPECT_TRUE(p->plans()[0].toMap()["runs"].toString().startsWith("4 runs"));
  EXPECT_EQ(p->plan()["name"].toString(), "Other");
}

TEST_F(Planner, AReloadDoesNotGiveAnEditTheNewRevision) {
  login();
  open("p1");
  p->setName("Other");
  std::string sent;
  b.save = [&sent](Plan, const std::string& r, auto done) {
    sent = r;
    done("", "the plans changed on disk");
  };
  p->setBackend(b);
  change = [](PlannerContent& c) { c.revision = "changed elsewhere"; };
  p->enter();
  drain();
  p->save();
  drain();
  EXPECT_EQ(sent, "1");
  EXPECT_TRUE(p->dirty());
}

TEST_F(Planner, DeletingANewPlanWritesNothing) {
  int removed = 0;
  b.remove = [&removed](const std::string&, const std::string&, PlannerBackend::Done done) {
    ++removed;
    done("");
  };
  p->setBackend(b);
  login();
  p->newPlan();
  p->deletePlan();
  drain();
  EXPECT_EQ(removed, 0);
  EXPECT_EQ(p->planCount(), 4);
  EXPECT_FALSE(p->plan()["isNew"].toBool());
}

TEST_F(Planner, RandomPlansStoreNoValues) {
  Plan sent;
  login();
  open("p1");
  b.save = [&sent](Plan x, const std::string&, auto done) {
    sent = x;
    done(x.id, "");
  };
  p->setBackend(b);
  p->setFixed(false);
  EXPECT_TRUE(p->canSave());
  p->save();
  drain();
  EXPECT_FALSE(sent.fixed);
  EXPECT_TRUE(sent.values == PlanValues{});
}

TEST_F(Planner, TheSearchAndTheFilterNeverHideWhatIsBeingEdited) {
  login();
  open("p1");
  p->search("random");
  EXPECT_EQ(p->plan()["id"].toString(), "p1");
  EXPECT_TRUE(p->hasPlan());
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  QCoreApplication app(argc, argv);
  return RUN_ALL_TESTS();
}

// ---- the interface agreed in 10-planner-backend.md section 8 and PB-6 (2026-11-26)

TEST_F(Planner, APersonalPlanIsPutAwayWhenItsPersonLogsOut) {  // PB-6
  ASSERT_TRUE(a->loginTrainee("S01"));
  p->choose("p1");  // S01's own plan
  drain();
  ASSERT_EQ(chosen.size(), 1u);
  EXPECT_EQ(chosen[0].run, 4);  // the backend read S01 itself
  a->logout();
  drain();
  ASSERT_EQ(putAway.size(), 1u);
  EXPECT_EQ(putAway[0], (std::pair<std::string, std::string>{"Departure, runway 27", "S01"}));
  ASSERT_EQ(chosen.size(), 2u);
  EXPECT_TRUE(chosen[1].plan.id.empty());  // off the Dashboard
  EXPECT_TRUE(p->message().contains("taken off the Dashboard"));
}

TEST_F(Planner, AnotherPersonLoggingInPutsAPersonalPlanAwayToo) {  // PB-6
  ASSERT_TRUE(a->loginTrainee("S01"));
  p->choose("p1");
  drain();
  ASSERT_TRUE(a->loginTrainee("S02"));
  drain();
  EXPECT_EQ(putAway.size(), 1u);
}

TEST_F(Planner, APlanThatTheInstructorPutWaitsForItsPerson) {  // PB-6: "at that person's log-out"
  login();
  open("p1");  // S01's
  p->put();
  drain();
  ASSERT_EQ(chosen.size(), 1u);
  a->logout();  // the instructor
  drain();
  EXPECT_TRUE(putAway.empty());
  ASSERT_TRUE(a->loginTrainee("S01"));
  drain();
  EXPECT_TRUE(putAway.empty());
  a->logout();  // now S01
  drain();
  EXPECT_EQ(putAway.size(), 1u);
}

TEST_F(Planner, AnAnyonePlanStaysAtLogout) {  // PB-6
  ASSERT_TRUE(a->loginTrainee("S01"));
  p->choose("p2");
  drain();
  ASSERT_EQ(chosen.size(), 1u);
  a->logout();
  drain();
  EXPECT_TRUE(putAway.empty());
  EXPECT_EQ(chosen.size(), 1u);
}

TEST_F(Planner, ValuesThatDoNotFitSayWhyFromTheBackend) {  // fits() with a reason
  login();
  open("p1");
  p->setRunway("09");  // with B2
  drain();
  EXPECT_FALSE(p->canSave());
  EXPECT_TRUE(p->saveNote().contains("B2 does not serve 09")) << p->saveNote().toStdString();
  EXPECT_TRUE(hasCheck("Values do not fit: B2 does not serve 09"));
}

TEST_F(Planner, WithTooManyCombinationsTheBackendJudgesTheValues) {  // 10 section 8: at most 2000 are given
  change = [](PlannerContent& c) {
    PlanScenario& s = c.scenarios.front();
    s.combinations.resize(1);  // only EAGLE, 09, A3 in the list
    s.tooMany = true;
  };
  int fitAsked = 0;
  b.fits = [&fitAsked, inner = b.fits](const std::string& sc, PlanValues v, auto done) {
    ++fitAsked;
    inner(sc, v, done);
  };
  p->setBackend(b);
  login();
  EXPECT_EQ(stateOf("p1"), "Ready");  // FALCON 27 B2 is not in the cut list, but fits: not marked
  open("p1");
  QStringList runways;
  for (const QVariant& v : p->runways()) {
    runways << v.toMap()["id"].toString();
    EXPECT_TRUE(v.toMap()["note"].toString().isEmpty());
  }
  EXPECT_EQ(runways, (QStringList{"09", "27"}));  // from the pools, not from the cut list
  EXPECT_FALSE(p->plan()["runwayBad"].toBool());
  p->setRunway("09");
  EXPECT_FALSE(p->canSave());
  EXPECT_EQ(p->saveNote(), "Checking the values...");
  EXPECT_TRUE(hasCheck("Values: checking"));
  drain();
  EXPECT_TRUE(p->saveNote().contains("B2 does not serve 09"));
  p->setTaxiway("A3");  // FALCON 09 A3 fits
  drain();
  EXPECT_TRUE(p->canSave()) << p->saveNote().toStdString();
  EXPECT_GE(fitAsked, 2);
}

TEST_F(Planner, TheBackendsReasonStopsAPlanInTheList) {
  change = [](PlannerContent& c) { c.plans.front().valuesError = "B2 does not serve 27"; };
  login();
  EXPECT_EQ(stateOf("p1"), "Cannot start: B2 does not serve 27");
}

TEST_F(Planner, AFailedVoiceBankCheckSaysWhy) {
  b.coverage = [](const std::string&, auto done) { done({PlanCoverage::State::Failed, 0, 0, "the manifest cannot be read"}); };
  p->setBackend(b);
  login();
  open("p1");
  drain();
  EXPECT_TRUE(hasCheck("Voice bank: check failed: the manifest cannot be read"));
}
