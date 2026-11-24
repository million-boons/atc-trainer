#include "runtime/ExerciseCheck.h"

#include <utility>

namespace atc {

ExerciseCheck::ExerciseCheck(std::string dataDir, std::string voicesDir, Report report)
    : data_(std::move(dataDir)), voices_(std::move(voicesDir)), report_(std::move(report)), thread_([this] { run(); }) {}

ExerciseCheck::~ExerciseCheck() {
  {
    std::lock_guard l(m_);
    stop_ = true;
  }
  cv_.notify_one();
  thread_.join();
}

void ExerciseCheck::select(const std::string& scenarioFile) {
  {
    // Held while reporting, so that no older result can follow this Checking.
    std::lock_guard r(reporting_);
    {
      std::lock_guard l(m_);
      next_ = scenarioFile;
      ++generation_;
    }
    CheckResult c;
    c.scenarioFile = scenarioFile;
    report_(c);
  }
  cv_.notify_one();
}

void ExerciseCheck::run() {
  for (;;) {
    std::string file;
    std::uint64_t generation;
    {
      std::unique_lock l(m_);
      cv_.wait(l, [this] { return stop_ || next_; });
      if (stop_) return;
      file = *std::exchange(next_, std::nullopt);
      generation = generation_;
    }
    const CheckResult c = check(data_, voices_, file);
    std::lock_guard r(reporting_);
    {
      std::lock_guard l(m_);
      if (stop_) return;
      if (generation != generation_) continue;
    }
    report_(c);
  }
}

CheckResult ExerciseCheck::check(const std::string& dataDir, const std::string& voicesDir, const std::string& scenarioFile) {
  CheckResult c;
  c.scenarioFile = scenarioFile;
  c.state = CheckResult::State::Failed;
  auto e = std::make_shared<Exercise>();
  if (!loadExercise(scenarioSources(dataDir, scenarioFile), *e, c.errors)) return c;
  auto b = std::make_shared<VoiceBank>();
  if (!b->load(voicesDir, c.errors)) return c;
  std::vector<LoadError> gaps;
  if (!checkCoverage(*e, *b, gaps)) {
    for (const auto& g : gaps) {
      if (g.path == "/clips") c.missing.push_back(g.what);
      else c.errors.push_back(g);
    }
    if (c.errors.empty()) c.state = CheckResult::State::MissingClips;
    return c;
  }
  c.state = CheckResult::State::Covered;
  c.ready = {std::move(e), std::move(b)};
  return c;
}

}  // namespace atc
