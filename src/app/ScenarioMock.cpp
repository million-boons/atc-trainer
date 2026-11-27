#include "app/ScenarioMock.h"

#include <algorithm>
#include <memory>

ScenarioBackend mockScenarios(std::function<void(const ScenarioInfo&)> selected) {
  const auto make = [](const char* id, const char* name, const char* category, const char* difficulty, int steps) {
    ScenarioInfo s;
    s.id = id;
    s.name = name;
    s.category = category;
    s.difficulty = difficulty;
    s.airfield = "SAMPLE-AF";
    s.rule = "interrupt";
    s.objectives = {"PLACEHOLDER - validate against approved SOP"};
    const ScenarioStepInfo all[] = {{"Taxi", "TAXI_INSTRUCTION", 30, 2}, {"Line up", "LINE_UP_AND_WAIT", 30, 2}, {"Takeoff", "TAKEOFF_CLEARANCE", 30, 2}};
    for (int i = 0; i < steps; ++i) s.steps.push_back(all[i % 3]);
    s.callsigns = s.types = s.runways = s.taxiways = 2;
    return s;
  };
  auto all = std::make_shared<std::vector<ScenarioInfo>>();
  *all = {make("DEPARTURE-01", "Departure 01", "departure", "basic", 3), make("DEPARTURE-02", "Departure 02", "departure", "intermediate", 4),
          make("DEPARTURE-03", "Departure 03", "departure", "advanced", 6), make("ARRIVAL-01", "Arrival 01", "arrival", "basic", 4),
          make("ARRIVAL-02", "Arrival 02", "arrival", "intermediate", 5), make("GROUND-01", "Ground 01", "ground", "basic", 2),
          make("GROUND-02", "Ground 02", "ground", "intermediate", 5), make("LANDING-01", "Landing 01", "landing", "basic", 3),
          make("GOAROUND-01", "Go-around 01", "go-around", "advanced", 4)};
  (*all)[6].placeholder = false;
  (*all)[6].sopRef = "SOP (mock)";
  (*all)[6].errors = {"GROUND-02.json /steps/1/expects/0/intent: unknown intent", "GROUND-02.json /steps/3/transitions/0/to: no such step"};
  (*all)[5].placeholder = false;
  (*all)[5].sopRef = "SOP (mock)";
  ScenarioBackend b;
  b.load = [all](auto done) { done(*all, ""); };
  b.coverage = [](const std::string& id, auto done) {
    ScenarioCoverage c;
    if (id.rfind("ARRIVAL", 0) == 0) {
      c.state = ScenarioCoverage::State::Missing;
      for (int i = 1; i <= 7; ++i) c.missing.push_back("PLACEHOLDER_CLIP_0" + std::to_string(i));
    } else if (id == "DEPARTURE-03") {
      return done(c);  // goes on checking
    } else {
      c.state = ScenarioCoverage::State::Ok;
    }
    c.clips = 22;
    done(c);
  };
  b.select = [all, selected = std::move(selected)](const std::string& id, auto done) {
    const auto at = std::find_if(all->begin(), all->end(), [&](const ScenarioInfo& s) { return s.id == id; });
    if (at == all->end()) return done("the scenario is gone");
    if (selected) selected(*at);
    done("");
  };
  return b;
}
