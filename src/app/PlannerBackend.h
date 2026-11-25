#pragma once
#include <functional>
#include <string>
#include <vector>

// What the Exercise Planner needs from outside (04 section 5g, 08 section 2; PROPOSED, b2 has not accepted it yet).
// The only place that knows this interface besides PlannerController is a backend. Texts: empty, or the reason of a
// refusal. Callbacks may come on any thread and after the call has returned; PlannerController brings them to the GUI.

// One valid set of values of a scenario's pools.
struct PlanValues {
  std::string callsign{}, type{}, runway{}, taxiway{};
  bool operator==(const PlanValues&) const = default;
};

enum class PlanPreset { Easy, Normal, Hard, Custom };

struct PlanDifficulty {
  PlanPreset preset = PlanPreset::Normal;
  int replyFromMs = 500, replyToMs = 1500;
  std::string radio = "light";  // off, light, medium, heavy
  bool operator==(const PlanDifficulty&) const = default;
};

struct PlanScenario {
  std::string id{}, name{}, summary{}, difficulty{}, hash{};
  int steps = 0;
  bool placeholder = true;                // some text is not Validated: no Assessed
  std::vector<std::string> errors{};      // it cannot be loaded
  std::vector<PlanValues> combinations{}; // all valid ones
};

struct Plan {
  std::string id{};            // empty: not saved yet
  std::string name{}, scenario{}, scenarioHash{};
  std::string trainee{};       // record ID; empty: anyone
  bool assessed = false, fixed = true;
  std::string briefing{};      // guidance by the instructor: never parsed, spoken or given to a model (D-143)
  PlanValues values{};         // fixed only
  PlanDifficulty difficulty{};
  std::string savedAt{};       // shown as it is
  int runs = 0;                // all, also of guests (P-7); counted by the backend
  std::string lastRun{};
};

struct PlannerContent {
  std::vector<PlanScenario> scenarios{};
  std::vector<Plan> plans{};
  std::vector<PlanDifficulty> presets{}, startingPresets{};  // Easy, Normal, Hard
  std::string revision{};      // given back when saving: the file must not have changed meanwhile
};

struct PlanCoverage {
  enum class State { Checking, Ok, Missing, Failed };
  State state = State::Checking;
  int clips = 0, missing = 0;
};

// What was put on the Dashboard. The card shows exactly the values that run (D-321).
struct PlanChoice {
  Plan plan{};
  PlanValues values{};
  std::string scenarioName{};
  int run = 0;                 // number of the next run of this person; 0: guest or instructor, unnumbered
  bool onlyCombination = false;  // the pools allow no other, so it may be the one of the previous run
};

struct PlannerBackend {
  using Done = std::function<void(const std::string& why)>;
  std::function<void(std::function<void()> plansChanged)> setEvents;  // e.g. a run has been counted
  std::function<void(std::function<void(PlannerContent, const std::string& why)>)> load;
  std::function<void(Plan, const std::string& revision, std::function<void(const std::string& id, const std::string& why)>)> save;
  std::function<void(const std::string& id, const std::string& revision, Done)> remove;  // results keep the plan's name and run number
  std::function<void(std::vector<PlanDifficulty>, const std::string& revision, Done)> savePresets;
  std::function<void(const std::string& scenario, std::function<void(PlanCoverage)>)> coverage;  // may answer more than once
  // Draws random values, selects the plan for the Dashboard and starts the coverage check. person: who is logged in,
  // empty for a guest or the instructor. It checks role, plan and scenario again.
  std::function<void(const std::string& id, const std::string& person, std::function<void(PlanChoice, const std::string& why)>)> choose;
};
