#pragma once
#include <functional>
#include <string>

#include "app/PlannerBackend.h"

// Planner on invented plans in memory: no files, no exercise, no voice bank. PLACEHOLDER.
// chosen: a plan was put on the Dashboard; an empty plan id means it was taken off (PB-6).
// person: who is logged in now, empty for a guest or the instructor (the live backend asks AccessController itself).
struct MockPlanner {
  PlannerBackend backend;
  std::function<void()> accessChanged;  // call it when somebody logs in or out
};
MockPlanner mockPlanner(std::function<void(const PlanChoice&)> chosen = {}, std::function<std::string()> person = {});
