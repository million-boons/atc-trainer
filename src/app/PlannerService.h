#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "app/PlanStore.h"
#include "app/PlannerBackend.h"
#include "core/Access.h"
#include "runtime/ExerciseCheck.h"

namespace atc {

// A plan on the Dashboard: what runs, exactly (D-321, D-334).
struct PlanSelection {
  Plan plan;
  std::string scenarioFile, scenarioName, airfield;
  ParamValues values;
  int run = 0;                 // of the person at the moment it was checked; 0: unnumbered
  std::string person;          // that person; empty: guest or instructor
  bool onlyCombination = false;
  bool seen = false;           // its person has been logged in since it was chosen (PB-6, D-154)
};

// The Exercise Planner without a screen (10, D-334). Qt-free. backend() and the GUI-thread functions are called on
// the GUI thread; files and scenarios are read on one worker thread.
class PlannerService {
 public:
  struct Config {
    std::string data;    // scenarios, airfields
    std::string plans;   // folder of plans.json and runs.jsonl
    std::string voices;  // the exercise's bank, for the coverage answer
  };
  struct Rules {
    std::function<std::pair<Role, std::string>()> who;  // GUI thread: role and person ID
    std::function<bool()> exerciseActive;               // GUI thread
    std::function<std::string()> phraseology;           // the folder in use (D-318); any thread
    VoiceBank::MayUse mayUse;                           // any thread
    std::function<std::string()> now;                   // "YYYY-MM-DD HH:MM", local
    std::function<void(std::function<void()>)> gui;     // runs a function on the GUI thread, later
  };
  // The exercise side: a plan was chosen, or put away (empty). GUI thread.
  using Select = std::function<void(std::optional<PlanSelection>)>;

  PlannerService(Config c, Rules r, Select select);
  ~PlannerService();
  PlannerBackend backend();

  // GUI thread.
  void accessChanged();  // PB-6: a personal plan goes when its person has logged out
  void forget() { chosen_.reset(); }  // a scenario was chosen instead (5d)
  // Before the Start dialog: checks everything again for the person of now, and redraws Random values that are that
  // person's previous combination. Empty, or why it cannot start.
  std::string beforeStart(PlanSelection& s);
  void started(const PlanSelection& s, const std::string& session);  // counts the run (PB-1)
  void ended(PlanSelection& s);  // Random: new values for the next run (PB-4); the card shows them
  void wait();                   // tests

 private:
  struct Loaded {
    std::shared_ptr<const Exercise> exercise;
    std::string file, hash, error;
  };
  void post(std::function<void()> f);
  void run();
  std::vector<std::pair<std::string, Loaded>> loadAll() const;  // file name, content
  Loaded loadOne(const std::string& id) const;
  static PlanValues planValues(const ParamValues& v);
  static ParamValues paramValues(const PlanValues& v);
  // Empty, or why the plan cannot run now. Fills the values to run.
  std::string check(const Plan& p, const Loaded& l, const std::string& person, PlanSelection& out) const;
  std::string mayChoose(const Plan& p, Role role, const std::string& person) const;
  std::string drawn(const Loaded& l, const std::string& person, ParamValues& v, bool& only) const;

  const Config cfg_;
  const Rules rules_;
  const Select select_;
  PlanStore store_;
  std::mutex m_;
  PlannerEvents ev_;
  std::optional<PlanSelection> chosen_;  // GUI thread

  std::mutex qm_;
  std::condition_variable cv_, idle_;
  std::deque<std::function<void()>> q_;
  bool stop_ = false, busy_ = false;
  std::thread thread_;
};

}  // namespace atc
