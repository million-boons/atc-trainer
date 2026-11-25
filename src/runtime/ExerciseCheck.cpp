#include "runtime/ExerciseCheck.h"

#include <utility>

#include "voice/ClipList.h"

namespace atc {

ExerciseCheck::ExerciseCheck(std::string dataDir, std::string voicesDir, Report report, VoiceBank::MayUse mayUse, Folder phraseology)
    : data_(std::move(dataDir)), voices_(std::move(voicesDir)), report_(std::move(report)), mayUse_(std::move(mayUse)),
      phraseology_(std::move(phraseology)), thread_([this] { run(); }) {}

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
    const CheckResult c = check(data_, voices_, file, mayUse_, phraseology_ ? phraseology_() : std::string());
    std::lock_guard r(reporting_);
    {
      std::lock_guard l(m_);
      if (stop_) return;
      if (generation != generation_) continue;
    }
    report_(c);
  }
}

CheckResult ExerciseCheck::check(const std::string& dataDir, const std::string& voicesDir, const std::string& scenarioFile,
                                 const VoiceBank::MayUse& mayUse, const std::string& phraseology) {
  CheckResult c;
  c.scenarioFile = scenarioFile;
  c.state = CheckResult::State::Failed;
  auto e = std::make_shared<Exercise>();
  ScenarioSources from = scenarioSources(dataDir, scenarioFile);
  if (!phraseology.empty()) from.phraseologyDir = phraseology;
  if (!loadExercise(from, *e, c.errors)) return c;
  auto b = std::make_shared<VoiceBank>();
  if (!b->load(voicesDir, c.errors, mayUse)) return c;
  SpokenForms forms;
  if (std::string why; !loadSpokenForms(from.phraseologyDir, forms, why)) {
    c.errors.push_back({from.phraseologyDir, "/", why});
    return c;
  }
  std::vector<LoadError> gaps;
  const bool covered = checkCoverage(*e, *b, gaps), said = checkWords(*e, forms, *b, gaps);
  if (!covered || !said) {
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
