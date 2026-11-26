#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

// What the Exercise Planner needs from outside (04 section 5g, 08 section 2). Agreed with b2 in 10-planner-backend.md
// section 8 and approved with PB-1 to PB-9 (2026-11-26).
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

inline constexpr std::size_t kMaxPlanCombinations = 2000;  // more are not given: tooMany, and fits() answers

struct PlanScenario {
  std::string id{}, name{}, summary{}, difficulty{};
  std::string hash{};                     // of the scenario file and its airfield file, not of the wording (PB-2)
  int steps = 0;
  bool placeholder = true;                // some text is not Validated: no Assessed
  std::vector<std::string> errors{};      // it cannot be loaded
  std::vector<PlanValues> combinations{}; // valid ones, at most kMaxPlanCombinations
  bool tooMany = false;                   // the list is cut: ask fits(); values to choose come from the pools below
  std::vector<PlanValues> aircraft{};     // every callsign with its type (runway, taxiway empty)
  std::vector<std::string> runways{}, taxiways{};  // every value of the pools, also when the list is cut
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
  std::string valuesError{};   // set by load: fixed values that do not fit now, with the reason ("B2 does not serve 09")
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
  int clips = 0, missing = 0;  // words that differ count as missing (ST-4)
  std::string error{};         // Failed: why
};

// What was put on the Dashboard. The card shows exactly the values that run (D-321).
struct PlanChoice {
  Plan plan{};
  PlanValues values{};
  std::string scenarioName{};
  int run = 0;                 // number of the next run of this person; 0: guest or instructor, unnumbered
  bool onlyCombination = false;  // the pools allow no other, so it may be the one of the previous run
};

struct PlannerEvents {
  std::function<void()> plansChanged;  // a run was counted, the Studio saved wording, a file changed
  // A personal plan was taken off the Dashboard because its person logged out (PB-6); Anyone plans stay.
  std::function<void(const std::string& planName, const std::string& person)> putAway;
};

struct PlannerBackend {
  using Done = std::function<void(const std::string& why)>;
  std::function<void(PlannerEvents)> setEvents;
  std::function<void(std::function<void(PlannerContent, const std::string& why)>)> load;
  std::function<void(Plan, const std::string& revision, std::function<void(const std::string& id, const std::string& why)>)> save;
  std::function<void(const std::string& id, const std::string& revision, Done)> remove;  // results keep the plan's name and run number
  std::function<void(std::vector<PlanDifficulty>, const std::string& revision, Done)> savePresets;
  std::function<void(const std::string& scenario, std::function<void(PlanCoverage)>)> coverage;  // may answer more than once
  // Whether fixed values are a valid combination now; why: the reason if not. For scenarios with tooMany.
  std::function<void(const std::string& scenario, PlanValues, std::function<void(bool fits, const std::string& why)>)> fits;
  // Draws random values, selects the plan for the Dashboard and starts the coverage check. The backend reads who is
  // logged in itself, on the GUI thread in this call (never later). It checks role, plan and scenario again, and
  // again at Start (with run number and previous run of the person of that moment).
  std::function<void(const std::string& id, std::function<void(PlanChoice, const std::string& why)>)> choose;
};
