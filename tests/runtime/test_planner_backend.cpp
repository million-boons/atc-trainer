// The Exercise Planner without a screen (10, D-334, D-154). No device, no sound.
#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "app/PlanStore.h"
#include "app/PlannerService.h"
#include "core/PathResolver.h"
using namespace atc;
namespace fs = std::filesystem;

namespace {
std::string data() {
  Paths p;
  std::string error;
  EXPECT_TRUE(resolvePaths(systemPathInputs(), p, error)) << error;
  return p.data;
}
struct TempDir {
  fs::path path;
  TempDir() {
    static int n = 0;
    path = fs::temp_directory_path() / ("atc_plan_" + std::to_string(getpid()) + "_" + std::to_string(n++));
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};
std::string slurp(const fs::path& p) {
  std::ifstream f(p);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

struct Planner {
  TempDir t;
  Role role = Role::Instructor;
  std::string person;
  bool exercise = false;
  std::vector<std::optional<PlanSelection>> selected;
  std::vector<std::string> putAway;
  int changed = 0;
  std::unique_ptr<PlannerService> s;
  PlannerBackend b;
  Planner() {
    s = std::make_unique<PlannerService>(
        PlannerService::Config{.data = data(), .plans = (t.path / "planner").string(), .voices = (t.path / "none").string()},
        PlannerService::Rules{.who = [this] { return std::pair{role, person}; },
                              .exerciseActive = [this] { return exercise; },
                              .phraseology = {},
                              .mayUse = {},
                              .now = [] { return std::string("2026-11-26 10:00"); },
                              .gui = [](std::function<void()> f) { f(); }},
        [this](std::optional<PlanSelection> p) { selected.push_back(std::move(p)); });
    b = s->backend();
    b.setEvents({[this] { ++changed; }, [this](const std::string& n, const std::string& who) { putAway.push_back(n + " " + who); }});
  }
  PlannerContent load() {
    PlannerContent c;
    b.load([&](PlannerContent x, const std::string& why) { c = std::move(x); EXPECT_EQ(why, ""); });
    s->wait();
    return c;
  }
  // Saved as the instructor; empty id and the reason if refused.
  std::pair<std::string, std::string> save(Plan p) {
    const Role was = role;
    role = Role::Instructor;
    std::pair<std::string, std::string> r{"", "(none)"};
    b.save(p, load().revision, [&](const std::string& id, const std::string& why) { r = {id, why}; });
    s->wait();
    role = was;
    return r;
  }
  std::pair<PlanChoice, std::string> choose(const std::string& id) {
    std::pair<PlanChoice, std::string> r{{}, "(none)"};
    b.choose(id, [&](PlanChoice c, const std::string& why) { r = {std::move(c), why}; });
    s->wait();
    return r;
  }
  Plan plan(bool fixed = true) {
    const auto c = load();
    EXPECT_FALSE(c.scenarios.empty());
    Plan p;
    p.name = fixed ? "Fixed" : "Random";
    p.scenario = c.scenarios[0].id;
    p.fixed = fixed;
    p.values = c.scenarios[0].combinations[0];
    return p;
  }
};
}  // namespace

TEST(PlanStoreTest, PlansRunsAndABrokenFileThatIsNeverOverwritten) {
  TempDir t;
  PlanStore st((t.path / "p").string());
  ASSERT_EQ(st.load(), "");
  EXPECT_EQ(st.presets().size(), 3u);
  EXPECT_EQ(st.revision(), "");
  Plan p;
  p.name = "A";
  std::string id;
  ASSERT_EQ(st.save(p, "", id), "");
  EXPECT_FALSE(id.empty());
  EXPECT_NE(st.save(p, "", id), "");  // read before the change
  const std::string rev = st.revision();
  Plan same;
  same.name = "a";
  EXPECT_NE(st.save(same, rev, id), "");  // unique, without regard to case
  EXPECT_EQ(st.revision(), rev);
  EXPECT_EQ(st.nextRun(id, "S01"), 1);
  EXPECT_EQ(st.nextRun(id, ""), 0);
  ASSERT_EQ(st.addRun({id, "A", "S01", 1, "s1", "AF", "2026-11-26 09:00", {"FALCON-732", "AN-32", "27", "B2"}}), "");
  ASSERT_EQ(st.addRun({id, "A", "", 0, "s2", "AF", "2026-11-26 09:10", {}}), "");
  EXPECT_EQ(st.revision(), rev);  // counting changes nothing of the plans
  EXPECT_EQ(st.nextRun(id, "S01"), 2);
  EXPECT_EQ(st.find(id)->runs, 2);  // guests counted, not numbered (P-7)
  EXPECT_EQ(st.previous("S01", "AF")->runway, "27");
  EXPECT_FALSE(st.previous("S01", "OTHER"));
  ASSERT_EQ(st.remove(id, rev), "");
  PlanStore again((t.path / "p").string());
  ASSERT_EQ(again.load(), "");
  EXPECT_TRUE(again.plans().empty());
  EXPECT_EQ(again.nextRun(id, "S01"), 2);  // deleting keeps the runs
  std::ofstream(t.path / "p/runs.jsonl", std::ios::app) << "{\"plan\":";  // a power cut in the last line
  ASSERT_EQ(again.load(), "");
  EXPECT_FALSE(again.warning().empty());
  std::ofstream(t.path / "p/plans.json") << "{broken";
  EXPECT_NE(again.load(), "");
  EXPECT_NE(again.save(p, again.revision(), id), "");
  EXPECT_EQ(slurp(t.path / "p/plans.json"), "{broken");
}

TEST(PlannerServiceTest, ScenariosWithAllCombinationsAndPools) {
  Planner x;
  const auto c = x.load();
  ASSERT_EQ(c.scenarios.size(), 1u);
  const auto& s = c.scenarios[0];
  EXPECT_TRUE(s.errors.empty());
  EXPECT_EQ(s.combinations.size(), 4u);
  EXPECT_FALSE(s.tooMany);
  EXPECT_EQ(s.runways.size(), 2u);
  EXPECT_EQ(s.aircraft.size(), 2u);
  EXPECT_FALSE(s.hash.empty());
  EXPECT_EQ(s.steps, 3);
  EXPECT_EQ(c.presets.size(), 3u);
  EXPECT_EQ(c.startingPresets[0].replyFromMs, 600);
}

TEST(PlannerServiceTest, SavingChecksEverythingAndOnlyTheInstructorSaves) {
  Planner x;
  Plan p = x.plan();
  x.role = Role::Trainee;
  std::string why;
  x.b.save(p, x.load().revision, [&](const std::string&, const std::string& w) { why = w; });
  EXPECT_NE(why, "");
  for (auto bad : {std::string(), std::string(41, 'x')}) {
    Plan q = p;
    q.name = bad;
    EXPECT_NE(x.save(q).second, "");
  }
  Plan q = p;
  q.briefing = "line\nbreak";
  EXPECT_NE(x.save(q).second, "");
  q = p;
  q.difficulty.replyFromMs = 250;
  EXPECT_NE(x.save(q).second, "");
  q = p;
  q.assessed = true;
  q.trainee = "S01";
  EXPECT_NE(x.save(q).second, "");  // PLACEHOLDER text: no Assessed
  q = p;
  q.values.taxiway = p.values.taxiway == "A3" ? "B2" : "A3";
  EXPECT_NE(x.save(q).second.find("does not serve"), std::string::npos);
  const auto [id, w] = x.save(p);
  ASSERT_EQ(w, "");
  const auto c = x.load();
  ASSERT_EQ(c.plans.size(), 1u);
  EXPECT_EQ(c.plans[0].scenarioHash, c.scenarios[0].hash);
  EXPECT_EQ(c.plans[0].savedAt, "2026-11-26 10:00");
  EXPECT_EQ(c.plans[0].valuesError, "");
}

TEST(PlannerServiceTest, ChoosingFollowsTheRoleAndRunsExactlyTheShownValues) {
  Planner x;
  Plan personal = x.plan();
  personal.trainee = "S01";
  const auto id = x.save(personal).first;
  x.role = Role::None;
  EXPECT_NE(x.choose(id).second, "");  // a guest: Anyone plans only
  x.role = Role::Trainee;
  x.person = "S02";
  EXPECT_NE(x.choose(id).second, "");
  x.person = "S01";
  x.exercise = true;
  EXPECT_NE(x.choose(id).second, "");
  x.exercise = false;
  const auto [c, why] = x.choose(id);
  ASSERT_EQ(why, "");
  EXPECT_EQ(c.values.runway, personal.values.runway);
  EXPECT_EQ(c.run, 1);
  ASSERT_EQ(x.selected.size(), 1u);
  ASSERT_TRUE(x.selected[0]);
  EXPECT_EQ(x.selected[0]->values.at("runway"), personal.values.runway);
  EXPECT_EQ(x.selected[0]->values.at("aircraftType"), personal.values.type);
  x.s->started(*x.selected[0], "session-1");
  x.s->wait();
  EXPECT_EQ(x.changed, 1);
  EXPECT_EQ(x.load().plans[0].runs, 1);
  PlanSelection again = *x.selected[0];
  EXPECT_EQ(x.s->beforeStart(again), "");
  EXPECT_EQ(again.run, 2);
}

TEST(PlannerServiceTest, RandomValuesAvoidThePersonsPreviousCombination) {
  Planner x;
  const auto id = x.save(x.plan(false)).first;
  x.role = Role::Trainee;
  x.person = "S01";
  for (int i = 0; i < 20; ++i) {
    const auto [c, why] = x.choose(id);
    ASSERT_EQ(why, "");
    PlanSelection s = *x.selected.back();
    x.s->started(s, "s" + std::to_string(i));
    x.s->wait();
    PlanSelection next = s;
    x.s->ended(next);
    EXPECT_FALSE(next.values.at("runway") == s.values.at("runway") && next.values.at("callsign") == s.values.at("callsign")) << i;
    EXPECT_EQ(next.run, i + 2);
  }
}

TEST(PlannerServiceTest, APersonalPlanIsPutAwayAtItsPersonsLogOut) {
  Planner x;
  Plan p = x.plan();
  p.trainee = "S01";
  const auto id = x.save(p).first;
  Plan anyone = x.plan();
  anyone.name = "Anyone";
  const auto anyId = x.save(anyone).first;
  // Chosen by the instructor: it waits for its person (D-154).
  ASSERT_EQ(x.choose(id).second, "");
  x.role = Role::None;
  x.s->accessChanged();
  EXPECT_TRUE(x.putAway.empty());
  x.role = Role::Trainee;
  x.person = "S01";
  x.s->accessChanged();
  x.role = Role::None;
  x.person.clear();
  x.s->accessChanged();
  ASSERT_EQ(x.putAway.size(), 1u);
  EXPECT_EQ(x.putAway[0], "Fixed S01");
  EXPECT_FALSE(x.selected.back());
  // Somebody else logs in in its person's place.
  x.role = Role::Trainee;
  x.person = "S01";
  ASSERT_EQ(x.choose(id).second, "");
  x.person = "S02";
  x.s->accessChanged();
  EXPECT_EQ(x.putAway.size(), 2u);
  // Anyone plans stay.
  ASSERT_EQ(x.choose(anyId).second, "");
  x.role = Role::None;
  x.s->accessChanged();
  EXPECT_EQ(x.putAway.size(), 2u);
  EXPECT_TRUE(x.selected.back());
}

TEST(PlannerServiceTest, AChangedScenarioOrValuesThatNoLongerFitCannotStart) {
  Planner x;
  const auto id = x.save(x.plan()).first;
  const std::string file = x.t.path / "planner/plans.json";
  std::string text = slurp(file);
  text.replace(text.find("\"scenarioHash\": \"") + 17, 4, "0000");
  std::ofstream(file) << text;
  const auto c = x.load();  // read again, as the screen does when it opens
  EXPECT_NE(c.plans[0].scenarioHash, c.scenarios[0].hash);
  EXPECT_NE(x.choose(id).second.find("Scenario changed"), std::string::npos);
  PlanValues v = x.plan().values;
  v.taxiway = v.taxiway == "A3" ? "B2" : "A3";
  bool fits = true;
  std::string why;
  x.b.fits(x.plan().scenario, v, [&](bool f, const std::string& w) { fits = f; why = w; });
  x.s->wait();
  EXPECT_FALSE(fits);
  EXPECT_EQ(why, v.taxiway + " does not serve " + v.runway);
}

// D-142 (3): a Practice plan of S01 may be started by somebody else; the screen warns. (Assessed needs Validated
// text, which no scenario has yet: refused when saved, tested above.)
TEST(PlannerServiceTest, APracticePlanOfAnotherPersonMayStart) {
  Planner x;
  Plan p = x.plan();
  p.trainee = "S01";
  const auto id = x.save(p).first;
  ASSERT_EQ(x.choose(id).second, "");
  PlanSelection s = *x.selected.back();
  x.role = Role::Trainee;
  x.person = "S02";
  EXPECT_EQ(x.s->beforeStart(s), "");
  EXPECT_EQ(s.person, "S02");
  EXPECT_EQ(s.run, 1);  // S02's first run of this plan
}
