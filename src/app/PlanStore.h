#pragma once
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "app/PlannerBackend.h"

namespace atc {

// The Planner's files (10 section 2): plans.json (plans and presets, replaced in one step) and runs.jsonl (one line
// for each started exercise with a plan, appended and synced). Owner only, in the writable folder, never in git.
// Thread-safe. A file that cannot be read is reported and never overwritten.
struct PlanRun {
  std::string plan, planName, person;  // person: ID; empty for a guest or the instructor
  int run = 0;                         // per plan and person; 0: unnumbered
  std::string session, airfield, at;   // at: YYYY-MM-DD HH:MM, local
  PlanValues values;
};

class PlanStore {
 public:
  explicit PlanStore(std::string dir) : dir_(std::move(dir)) {}
  std::string load();  // empty, or why not; missing files are an empty store
  std::vector<Plan> plans() const;       // with runs and last run filled in
  std::vector<PlanDifficulty> presets() const;
  std::string revision() const;          // of plans.json only: counting a run never changes it
  std::optional<Plan> find(const std::string& id) const;

  // Empty, or why not. id of a new plan is given back.
  std::string save(Plan p, const std::string& revision, std::string& id);
  std::string remove(const std::string& id, const std::string& revision);  // its runs stay
  std::string savePresets(std::vector<PlanDifficulty> p, const std::string& revision);

  int nextRun(const std::string& plan, const std::string& person) const;  // 0 for nobody
  // The latest run of this person with this airfield, of any plan (PB-3).
  std::optional<PlanValues> previous(const std::string& person, const std::string& airfield) const;
  std::string addRun(PlanRun r);
  std::string warning() const;  // e.g. a broken last line of runs.jsonl was skipped

  static std::vector<PlanDifficulty> startingPresets();  // D-142

 private:
  std::string write(const std::vector<Plan>& plans, const std::vector<PlanDifficulty>& presets);
  mutable std::mutex m_;
  const std::string dir_;
  bool broken_ = false, loaded_ = false;
  std::string bytes_, warning_;
  std::vector<Plan> plans_;
  std::vector<PlanDifficulty> presets_ = startingPresets();
  std::vector<PlanRun> runs_;
};

}  // namespace atc
