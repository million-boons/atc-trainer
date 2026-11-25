#pragma once
#include "app/ScenarioBackend.h"

// Nine invented scenarios in memory: PLACEHOLDER, none can be run.
ScenarioBackend mockScenarios(std::function<void(const ScenarioInfo&)> selected = {});
