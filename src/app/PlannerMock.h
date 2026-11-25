#pragma once
#include "app/PlannerBackend.h"

// Planner on invented plans in memory: no files, no exercise, no voice bank. PLACEHOLDER.
PlannerBackend mockPlanner(std::function<void(const PlanChoice&)> chosen = {});
