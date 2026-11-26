#include "app/ScenarioService.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "runtime/ExerciseCheck.h"
#include "voice/PhraseTemplate.h"

namespace fs = std::filesystem;

namespace atc {
namespace {
std::string text(const nlohmann::json& j, const char* k) {
  const auto it = j.find(k);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}
}  // namespace

ScenarioService::ScenarioService(Config c, Rules r) : cfg_(std::move(c)), rules_(std::move(r)), thread_([this] { run(); }) {}

ScenarioService::~ScenarioService() {
  {
    std::lock_guard l(qm_);
    stop_ = true;
  }
  cv_.notify_one();
  thread_.join();
}

void ScenarioService::post(std::function<void()> f) {
  {
    std::lock_guard l(qm_);
    q_.push_back(std::move(f));
  }
  cv_.notify_one();
}

void ScenarioService::run() {
  for (;;) {
    std::function<void()> f;
    {
      std::unique_lock l(qm_);
      busy_ = false;
      if (q_.empty()) idle_.notify_all();
      cv_.wait(l, [this] { return stop_ || !q_.empty(); });
      if (q_.empty()) return;
      f = std::move(q_.front());
      q_.pop_front();
      busy_ = true;
    }
    f();
  }
}

void ScenarioService::wait() {
  std::unique_lock l(qm_);
  idle_.wait(l, [this] { return q_.empty() && !busy_; });
}

std::vector<std::string> ScenarioService::files() const {
  std::vector<std::string> out;
  std::error_code ec;
  for (fs::directory_iterator it(fs::path(cfg_.data) / "scenarios", ec), end; !ec && it != end; it.increment(ec))
    if (it->path().extension() == ".json") out.push_back(it->path().string());
  std::sort(out.begin(), out.end());
  return out;
}

ScenarioBackend ScenarioService::backend() {
  ScenarioBackend b;
  b.load = [this](auto done) {
    post([this, done = std::move(done)] {
      std::vector<ScenarioInfo> list;
      const std::string phraseology = rules_.phraseology ? rules_.phraseology() : std::string();
      for (const auto& f : files()) {
        ScenarioInfo s;
        s.id = fs::path(f).stem().string();
        // Fields that the loader checks but does not keep (brief for engine, D-334): read from the file itself.
        std::ifstream in(f);
        const auto j = nlohmann::json::parse(in, nullptr, false);
        if (j.is_object()) {
          s.category = text(j, "category");
          s.difficulty = text(j, "difficulty");
          if (const auto o = j.find("objectives"); o != j.end() && o->is_array())
            for (const auto& x : *o)
              if (x.is_string()) s.objectives.push_back(x.get<std::string>());
          if (const auto v = j.find("validation"); v != j.end() && v->is_object()) s.sopRef = text(*v, "sopRef");
        }
        Exercise e;
        std::vector<LoadError> errors;
        ScenarioSources src = scenarioSources(cfg_.data, f);
        if (!phraseology.empty()) src.phraseologyDir = phraseology;
        if (!loadExercise(src, e, errors)) {
          for (const auto& x : errors) s.errors.push_back(toString(x));
          list.push_back(std::move(s));
          continue;
        }
        s.id = e.scenario.id;
        s.name = e.scenario.name;
        if (s.category.empty()) s.category = e.scenario.category;
        s.airfield = e.airfield.id;
        s.rule = e.scenario.pressDuringPilot == PressRule::Interrupt ? "interrupt" : e.scenario.pressDuringPilot == PressRule::Block ? "block" : "queue";
        s.placeholder = e.placeholder;
        for (const auto& st : e.scenario.steps) {
          if (st.final) continue;
          // What the step expects, never the expected values (a trainee may read this screen).
          s.steps.push_back({st.label, st.expects.empty() ? std::string() : st.expects.front().intent, int(st.timeout / ms(1000)), st.maxPilotCalls});
        }
        s.callsigns = int(e.airfield.callsigns.size());
        s.types = int(e.airfield.aircraftTypes.size());
        s.runways = int(e.airfield.runways.size());
        s.taxiways = int(e.airfield.taxiways.size());
        list.push_back(std::move(s));
      }
      done(std::move(list), list.empty() ? "no scenario files" : "");
    });
  };
  b.coverage = [this](const std::string& id, auto done) {
    done(ScenarioCoverage{});  // Checking
    post([this, id, done = std::move(done)] {
      ScenarioCoverage c;
      std::string file;
      for (const auto& f : files())
        if (fs::path(f).stem().string() == id) file = f;
      if (file.empty()) {
        c.state = ScenarioCoverage::State::Failed;
        c.missing = {"scenario " + id + " is not there"};
        return done(c);
      }
      const auto r = ExerciseCheck::check(cfg_.data, cfg_.voices, file, rules_.mayUse, rules_.phraseology ? rules_.phraseology() : std::string());
      if (r.ready) {
        std::map<std::string, RequiredClip> need;
        std::vector<std::string> bad;
        requiredClips(*r.ready.exercise, need, bad);
        c.clips = int(need.size());
      }
      using S = CheckResult::State;
      c.state = r.state == S::Covered ? ScenarioCoverage::State::Ok : r.state == S::MissingClips ? ScenarioCoverage::State::Missing : ScenarioCoverage::State::Failed;
      c.missing = r.missing;
      for (const auto& e : r.errors) c.missing.push_back(toString(e));
      done(c);
    });
  };
  b.select = [this](const std::string& id, auto done) {
    if (rules_.exerciseActive && rules_.exerciseActive()) return done("not during an exercise");
    post([this, id, done = std::move(done)] {
      std::string file;
      for (const auto& f : files())
        if (fs::path(f).stem().string() == id) file = f;
      if (file.empty()) return done("scenario " + id + " is not there");
      Exercise e;
      std::vector<LoadError> errors;
      ScenarioSources src = scenarioSources(cfg_.data, file);
      if (const std::string p = rules_.phraseology ? rules_.phraseology() : std::string(); !p.empty()) src.phraseologyDir = p;
      if (!loadExercise(src, e, errors)) return done("it cannot be loaded: " + toString(errors.front()));
      rules_.gui([this, file, done] {
        if (rules_.exerciseActive && rules_.exerciseActive()) return done("not during an exercise");
        if (rules_.select) rules_.select(file);
        done("");
      });
    });
  };
  return b;
}

}  // namespace atc
