#pragma once
#include <functional>
#include <string>
#include <vector>

// What Scenario Selection needs from outside (04 section 5d, 07). PROPOSED: b2 has not accepted it yet.
// Texts: empty, or the reason of a refusal. Callbacks may come on any thread, after the call has returned.

struct ScenarioStepInfo {
  std::string name{}, intent{};  // what the step expects, never the expected values: a trainee must not read them here
  int waitSeconds = 0, pilotCalls = 0;
};

struct ScenarioInfo {
  std::string id{}, name{}, category{}, difficulty{}, airfield{}, sopRef{}, rule{};
  bool placeholder = true;
  std::vector<std::string> objectives{};
  std::vector<ScenarioStepInfo> steps{};
  int callsigns = 0, types = 0, runways = 0, taxiways = 0;  // values of the pools
  std::vector<std::string> errors{};  // file and path (D-408); then it cannot be selected
};

struct ScenarioCoverage {
  enum class State { Checking, Ok, Missing, Failed };
  State state = State::Checking;
  int clips = 0;
  std::vector<std::string> missing{};  // clip IDs, or why the check failed
};

struct ScenarioBackend {
  std::function<void(std::function<void(std::vector<ScenarioInfo>, const std::string& why)>)> load;
  std::function<void(const std::string& id, std::function<void(ScenarioCoverage)>)> coverage;  // in the background; may answer more than once
  // The scenario is on the Dashboard, its values are drawn and shown there, and its coverage check runs (D-115).
  // It replaces a plan that was chosen.
  std::function<void(const std::string& id, std::function<void(const std::string& why)>)> select;
};
