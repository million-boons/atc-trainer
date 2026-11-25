#include "app/PlannerMock.h"

#include <algorithm>
#include <map>
#include <memory>
#include <utility>

namespace {
struct Store {
  PlannerContent content;
  std::map<std::string, PlanValues> previous;  // by person
  std::map<std::pair<std::string, std::string>, int> runs;  // plan, person
  std::size_t next = 0;
  int nextId = 10;
};

const PlanValues kEagle09{"EAGLE-421", "C295", "09", "A3"}, kEagle27{"EAGLE-421", "C295", "27", "B2"},
    kFalcon09{"FALCON-732", "AN-32", "09", "A3"}, kFalcon27{"FALCON-732", "AN-32", "27", "B2"};

// D-142.
std::vector<PlanDifficulty> starting() {
  return {{PlanPreset::Easy, 600, 1000, "light"}, {PlanPreset::Normal, 500, 1500, "light"}, {PlanPreset::Hard, 500, 1500, "medium"}};
}

Plan plan(const char* id, const char* name, const char* trainee, bool fixed, int runs, const char* last) {
  Plan p;
  p.id = id;
  p.name = name;
  p.scenario = "DEPARTURE-01";
  p.scenarioHash = "h1";
  p.trainee = trainee;
  p.fixed = fixed;
  if (fixed) p.values = kFalcon27;
  p.difficulty = starting()[1];
  p.savedAt = "16:02";
  p.runs = runs;
  p.lastRun = last;
  return p;
}
}  // namespace

PlannerBackend mockPlanner(std::function<void(const PlanChoice&)> chosen) {
  auto st = std::make_shared<Store>();
  PlannerContent& c = st->content;
  PlanScenario dep;
  dep.id = "DEPARTURE-01";
  dep.name = "Departure 01";
  dep.summary = "Taxi, line up, takeoff";
  dep.difficulty = "basic";
  dep.hash = "h1";
  dep.steps = 3;
  dep.combinations = {kEagle09, kFalcon27, kEagle27, kFalcon09};
  PlanScenario bad;
  bad.id = "BROKEN-01";
  bad.name = "Broken 01 (mock)";
  bad.errors = {"steps[0]: unknown intent", "steps[1]: guards are not built"};
  c.scenarios = {dep, bad};
  Plan first = plan("p1", "Departure, runway 27", "S01", true, 3, "25 Nov 16:20");
  first.briefing = "Listen for the runway. Read the card first.";
  Plan changed = plan("p3", "Departure S02", "S02", true, 5, "20 Nov 09:12");
  changed.scenarioHash = "h0";
  Plan old = plan("p4", "Departure, slow", "", true, 1, "19 Nov 14:00");
  old.difficulty.replyToMs = 2000;
  c.plans = {first, plan("p2", "Departure, random", "", false, 0, ""), changed, old};
  c.presets = c.startingPresets = starting();
  c.revision = "1";
  st->runs[{"p1", "S01"}] = 3;

  PlannerBackend b;
  b.setEvents = [](std::function<void()>) {};
  b.load = [st](auto done) { done(st->content, ""); };
  b.save = [st](Plan p, const std::string& revision, auto done) {
    if (revision != st->content.revision) return done("", "the plans changed on disk: undo and edit again");
    if (p.id.empty()) p.id = "p" + std::to_string(st->nextId++);
    p.savedAt = "now";
    auto& all = st->content.plans;
    const auto at = std::find_if(all.begin(), all.end(), [&](const Plan& x) { return x.id == p.id; });
    if (at == all.end()) all.push_back(p);
    else *at = p;
    st->content.revision += "1";
    done(p.id, "");
  };
  b.remove = [st](const std::string& id, const std::string& revision, PlannerBackend::Done done) {
    if (revision != st->content.revision) return done("the plans changed on disk: undo and try again");
    std::erase_if(st->content.plans, [&](const Plan& p) { return p.id == id; });
    st->content.revision += "1";
    done("");
  };
  b.savePresets = [st](std::vector<PlanDifficulty> d, const std::string& revision, PlannerBackend::Done done) {
    if (revision != st->content.revision) return done("the presets changed on disk: try again");
    st->content.presets = std::move(d);
    st->content.revision += "1";
    done("");
  };
  b.coverage = [](const std::string&, auto done) { done({PlanCoverage::State::Ok, 22, 0}); };
  b.choose = [st, chosen = std::move(chosen)](const std::string& id, const std::string& person, auto done) {
    const auto& all = st->content.plans;
    const auto at = std::find_if(all.begin(), all.end(), [&](const Plan& p) { return p.id == id; });
    if (at == all.end()) return done({}, "the plan is gone");
    PlanChoice x;
    x.plan = *at;
    const auto sc = std::find_if(st->content.scenarios.begin(), st->content.scenarios.end(), [&](const PlanScenario& s) { return s.id == at->scenario; });
    if (sc == st->content.scenarios.end() || sc->combinations.empty()) return done({}, "the scenario cannot be loaded");
    x.scenarioName = sc->name;
    x.values = at->values;
    if (!at->fixed) {
      const std::string who = at->trainee.empty() ? person : at->trainee;
      const auto& v = sc->combinations;
      x.onlyCombination = v.size() == 1;
      x.values = v[st->next++ % v.size()];
      if (!x.onlyCombination && x.values == st->previous[who]) x.values = v[st->next++ % v.size()];
      st->previous[who] = x.values;  // the mock runs nothing: as if every choice were run
    }
    if (!person.empty()) {
      const auto n = st->runs.find({id, person});
      x.run = (n == st->runs.end() ? 0 : n->second) + 1;
    }
    if (chosen) chosen(x);
    done(x, "");
  };
  return b;
}
