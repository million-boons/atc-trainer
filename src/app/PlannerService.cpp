#include "app/PlannerService.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>

#include "audio/RadioAudioProcessor.h"
#include "voice/PhraseTemplate.h"

namespace fs = std::filesystem;

namespace atc {
namespace {
std::string slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
std::string fnv(const std::string& b) {
  std::uint64_t h = 14695981039346656037ull;
  for (const unsigned char c : b) h = (h ^ c) * 1099511628211ull;
  char hex[17];
  std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(h));
  return hex;
}
bool validName(const std::string& s) {
  return !s.empty() && s.size() <= 40 && std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}
bool sameValues(const ParamValues& a, const PlanValues& b) {
  const auto at = [&](const char* k) { const auto it = a.find(k); return it == a.end() ? std::string() : it->second; };
  return at("callsign") == b.callsign && at("runway") == b.runway && at("taxiway") == b.taxiway;  // D-142: these three
}
}  // namespace

PlannerService::PlannerService(Config c, Rules r, Select select)
    : cfg_(std::move(c)), rules_(std::move(r)), select_(std::move(select)), store_(cfg_.plans), thread_([this] { run(); }) {
  post([this] { (void)store_.load(); });
}

PlannerService::~PlannerService() {
  {
    std::lock_guard l(qm_);
    stop_ = true;
  }
  cv_.notify_one();
  thread_.join();
}

void PlannerService::post(std::function<void()> f) {
  {
    std::lock_guard l(qm_);
    q_.push_back(std::move(f));
  }
  cv_.notify_one();
}

void PlannerService::run() {
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

void PlannerService::wait() {
  std::unique_lock l(qm_);
  idle_.wait(l, [this] { return q_.empty() && !busy_; });
}

PlanValues PlannerService::planValues(const ParamValues& v) {
  const auto at = [&](const char* k) { const auto it = v.find(k); return it == v.end() ? std::string() : it->second; };
  return {at("callsign"), at("aircraftType"), at("runway"), at("taxiway")};
}

ParamValues PlannerService::paramValues(const PlanValues& v) {
  ParamValues p;
  if (!v.callsign.empty()) p["callsign"] = v.callsign;
  if (!v.type.empty()) p["aircraftType"] = v.type;
  if (!v.runway.empty()) p["runway"] = v.runway;
  if (!v.taxiway.empty()) p["taxiway"] = v.taxiway;
  return p;
}

std::vector<std::pair<std::string, PlannerService::Loaded>> PlannerService::loadAll() const {
  std::vector<std::pair<std::string, Loaded>> out;
  std::vector<std::string> files;
  std::error_code ec;
  for (fs::directory_iterator it(fs::path(cfg_.data) / "scenarios", ec), end; !ec && it != end; it.increment(ec))
    if (it->path().extension() == ".json") files.push_back(it->path().string());
  std::sort(files.begin(), files.end());
  const std::string phraseology = rules_.phraseology ? rules_.phraseology() : std::string();
  for (const auto& f : files) {
    Loaded l;
    l.file = f;
    auto e = std::make_shared<Exercise>();
    ScenarioSources src = scenarioSources(cfg_.data, f);
    if (!phraseology.empty()) src.phraseologyDir = phraseology;
    std::vector<LoadError> errors;
    if (loadExercise(src, *e, errors)) {
      // PB-2: the scenario and its airfield, not the wording.
      l.hash = fnv(slurp(f) + '\0' + slurp(fs::path(src.airfieldDir) / (e->scenario.airfield + ".json")));
      l.exercise = std::move(e);
    } else {
      l.error = errors.empty() ? "cannot be loaded" : toString(errors.front());
    }
    out.emplace_back(fs::path(f).stem().string(), std::move(l));
  }
  return out;
}

PlannerService::Loaded PlannerService::loadOne(const std::string& id) const {
  for (auto& [name, l] : loadAll())
    if ((l.exercise && l.exercise->scenario.id == id) || name == id) return l;
  return {{}, {}, {}, "scenario " + id + " is not there"};
}

std::string PlannerService::mayChoose(const Plan& p, Role role, const std::string& person) const {
  if (role == Role::Instructor) return "";
  if (p.trainee.empty()) return "";
  if (role == Role::Trainee && p.trainee == person) return "";
  return "this plan is for " + p.trainee;
}

// Uniform over the valid combinations, without the person's previous one (D-142); if that is the only one, it is used.
std::string PlannerService::drawn(const Loaded& l, const std::string& person, ParamValues& v, bool& only) const {
  bool tooMany = false;
  auto all = combinations(l.exercise->scenario, l.exercise->airfield, 100'000, tooMany);
  if (all.empty()) return "no valid combination of values";
  if (const auto prev = store_.previous(person, l.exercise->airfield.id); prev && all.size() > 1)
    std::erase_if(all, [&](const ParamValues& x) { return sameValues(x, *prev); });
  only = all.size() == 1;
  if (all.empty()) return "no valid combination of values";
  std::random_device d;
  v = all[std::uniform_int_distribution<std::size_t>(0, all.size() - 1)(d)];
  return "";
}

std::string PlannerService::check(const Plan& p, const Loaded& l, const std::string& person, PlanSelection& out) const {
  if (!l.exercise) return "Cannot start: " + l.error;
  if (l.hash != p.scenarioHash) return "Scenario changed: open the plan and save it again";
  if (p.assessed)
    if (std::string why = modeError(*l.exercise, ExerciseMode::Assessed); !why.empty()) return "Cannot start: " + why;
  if (p.assessed && p.trainee.empty()) return "Cannot start: Assessed needs a trainee";
  out.plan = p;
  out.scenarioFile = l.file;
  out.scenarioName = l.exercise->scenario.name;
  out.airfield = l.exercise->airfield.id;
  out.person = person;
  out.run = store_.nextRun(p.id, person);
  if (p.fixed) {
    out.values = paramValues(p.values);
    // Callsign and type are one choice (D-148): the type follows from the callsign if the plan left it out.
    if (!out.values.count("aircraftType") && l.exercise->scenario.generators.count("aircraftType"))
      for (const auto& c : l.exercise->airfield.callsigns)
        if (c.id == p.values.callsign && c.types.size() == 1) out.values["aircraftType"] = *c.types.begin();
    if (std::string why; !fitsParams(l.exercise->scenario, l.exercise->airfield, out.values, why)) return "Cannot start: " + why;
    return "";
  }
  return drawn(l, person.empty() ? p.trainee : person, out.values, out.onlyCombination);
}

PlannerBackend PlannerService::backend() {
  PlannerBackend b;
  b.setEvents = [this](PlannerEvents e) {
    std::lock_guard l(m_);
    ev_ = std::move(e);
  };
  b.load = [this](auto done) {
    post([this, done = std::move(done)] {
      PlannerContent c;
      std::string why = store_.load();
      const auto all = loadAll();
      for (const auto& [name, l] : all) {
        PlanScenario s;
        s.id = l.exercise ? l.exercise->scenario.id : name;
        if (!l.exercise) {
          s.name = name;
          s.errors = {l.error};
          c.scenarios.push_back(std::move(s));
          continue;
        }
        const Exercise& e = *l.exercise;
        s.name = e.scenario.name;
        s.hash = l.hash;
        std::string labels;
        for (const auto& st : e.scenario.steps)
          if (!st.final) {
            labels += (labels.empty() ? "" : ", ") + st.label;
            ++s.steps;
          }
        s.summary = labels;
        s.placeholder = e.placeholder;
        bool tooMany = false;
        for (const auto& v : combinations(e.scenario, e.airfield, kMaxPlanCombinations, tooMany)) s.combinations.push_back(planValues(v));
        s.tooMany = tooMany;
        for (const auto& cs : e.airfield.callsigns)
          s.aircraft.push_back({cs.id, cs.types.size() == 1 ? *cs.types.begin() : std::string(), {}, {}});
        s.runways = e.airfield.runways;
        for (const auto& t : e.airfield.taxiways) s.taxiways.push_back(t.id);
        c.scenarios.push_back(std::move(s));
      }
      c.plans = store_.plans();
      for (auto& p : c.plans) {
        if (!p.fixed) continue;
        for (const auto& [name, l] : all) {
          if (!l.exercise || l.exercise->scenario.id != p.scenario) continue;
          PlanSelection tmp;
          Plan q = p;
          q.assessed = false;
          q.scenarioHash = l.hash;
          if (std::string w = check(q, l, {}, tmp); !w.empty()) p.valuesError = w.rfind("Cannot start: ", 0) == 0 ? w.substr(14) : w;
        }
      }
      c.presets = store_.presets();
      c.startingPresets = PlanStore::startingPresets();
      c.revision = store_.revision();
      if (why.empty() && !store_.warning().empty()) why.clear();
      done(std::move(c), why);
    });
  };
  b.save = [this](Plan p, const std::string& revision, auto done) {
    const auto [role, person] = rules_.who ? rules_.who() : std::pair{Role::None, std::string()};
    if (role != Role::Instructor) return done({}, "only the instructor may do this");
    if (rules_.exerciseActive && rules_.exerciseActive()) return done({}, "not during an exercise");
    post([this, p = std::move(p), revision, done = std::move(done)]() mutable {
      if (!validName(p.name)) return done({}, "the name must have 1 to 40 characters");
      if (p.briefing.size() > 200 || std::any_of(p.briefing.begin(), p.briefing.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; }))
        return done({}, "the briefing note must have at most 200 characters and no line break");
      const PlanDifficulty& d = p.difficulty;
      if (d.replyFromMs < 300 || d.replyToMs > 5000 || d.replyFromMs > d.replyToMs || d.replyFromMs % 100 || d.replyToMs % 100)
        return done({}, "reply delays from 0.3 to 5.0 s in steps of 0.1 s, the first not above the second");
      if (!radioIntensityFromString(d.radio)) return done({}, "radio effect: off, light, medium or heavy");
      if (!p.trainee.empty() && !isIdCode(p.trainee)) return done({}, "the trainee must be a record ID");
      if (p.assessed && p.trainee.empty()) return done({}, "Assessed needs a trainee, never Anyone");
      const Loaded l = loadOne(p.scenario);
      if (!l.exercise) return done({}, l.error);
      if (p.assessed)
        if (std::string why = modeError(*l.exercise, ExerciseMode::Assessed); !why.empty()) return done({}, why);
      p.scenario = l.exercise->scenario.id;
      p.scenarioHash = l.hash;
      if (p.fixed) {
        PlanSelection tmp;
        Plan q = p;
        q.assessed = false;
        if (std::string why = check(q, l, {}, tmp); !why.empty()) return done({}, why);
      } else {
        p.values = {};
      }
      p.savedAt = rules_.now ? rules_.now() : std::string();
      std::string id;
      const std::string why = store_.save(std::move(p), revision, id);
      done(why.empty() ? id : std::string(), why);
    });
  };
  b.remove = [this](const std::string& id, const std::string& revision, PlannerBackend::Done done) {
    if (!rules_.who || rules_.who().first != Role::Instructor) return done("only the instructor may do this");
    if (rules_.exerciseActive && rules_.exerciseActive()) return done("not during an exercise");
    if (chosen_ && chosen_->plan.id == id) {  // a deleted plan is not left on the Dashboard
      chosen_.reset();
      if (select_) select_(std::nullopt);
    }
    post([this, id, revision, done = std::move(done)] { done(store_.remove(id, revision)); });
  };
  b.savePresets = [this](std::vector<PlanDifficulty> p, const std::string& revision, PlannerBackend::Done done) {
    if (!rules_.who || rules_.who().first != Role::Instructor) return done("only the instructor may do this");
    post([this, p = std::move(p), revision, done = std::move(done)]() mutable {
      for (const auto& d : p)
        if (d.replyFromMs < 300 || d.replyToMs > 5000 || d.replyFromMs > d.replyToMs || !radioIntensityFromString(d.radio))
          return done("a preset is not valid");
      done(store_.savePresets(std::move(p), revision));
    });
  };
  b.coverage = [this](const std::string& scenario, auto done) {
    done(PlanCoverage{});  // Checking
    post([this, scenario, done = std::move(done)] {
      const Loaded l = loadOne(scenario);
      PlanCoverage c;
      if (!l.exercise) {
        c.state = PlanCoverage::State::Failed;
        c.error = l.error;
        return done(c);
      }
      const auto r = ExerciseCheck::check(cfg_.data, cfg_.voices, l.file, rules_.mayUse, rules_.phraseology ? rules_.phraseology() : std::string());
      std::map<std::string, RequiredClip> need;
      std::vector<std::string> bad;
      requiredClips(*l.exercise, need, bad);
      c.clips = int(need.size());
      c.missing = int(r.missing.size());
      c.state = r.state == CheckResult::State::Covered ? PlanCoverage::State::Ok : r.state == CheckResult::State::MissingClips ? PlanCoverage::State::Missing : PlanCoverage::State::Failed;
      if (c.state == PlanCoverage::State::Failed && !r.errors.empty()) c.error = toString(r.errors.front());
      done(c);
    });
  };
  b.fits = [this](const std::string& scenario, PlanValues v, auto done) {
    post([this, scenario, v, done = std::move(done)] {
      const Loaded l = loadOne(scenario);
      if (!l.exercise) return done(false, l.error);
      Plan p;
      p.values = v;
      p.scenarioHash = l.hash;
      PlanSelection tmp;
      const std::string why = check(p, l, {}, tmp);
      done(why.empty(), why.rfind("Cannot start: ", 0) == 0 ? why.substr(14) : why);
    });
  };
  b.choose = [this](const std::string& id, auto done) {
    // Read now, on the GUI thread (D-154): a log-out after this call cannot change who chose.
    const auto [role, person] = rules_.who ? rules_.who() : std::pair{Role::None, std::string()};
    if (rules_.exerciseActive && rules_.exerciseActive()) return done({}, "not during an exercise");
    post([this, id, role, person, done = std::move(done)] {
      const auto p = store_.find(id);
      if (!p) return done({}, "the plan is gone");
      if (std::string why = mayChoose(*p, role, person); !why.empty()) return done({}, why);
      const Loaded l = loadOne(p->scenario);
      PlanSelection s;
      if (std::string why = check(*p, l, role == Role::Trainee ? person : std::string(), s); !why.empty()) return done({}, why);
      s.seen = role == Role::Trainee && person == p->trainee;
      PlanChoice c;
      c.plan = *p;
      c.values = planValues(s.values);
      c.scenarioName = s.scenarioName;
      c.run = s.run;
      c.onlyCombination = s.onlyCombination;
      rules_.gui([this, s, c, done] {
        chosen_ = s;
        if (select_) select_(s);
        done(c, "");
      });
    });
  };
  return b;
}

void PlannerService::accessChanged() {
  if (!chosen_ || chosen_->plan.trainee.empty()) return;  // Anyone plans stay
  const auto [role, person] = rules_.who ? rules_.who() : std::pair{Role::None, std::string()};
  const bool here = role == Role::Trainee && person == chosen_->plan.trainee;
  if (here) {
    chosen_->seen = true;
    return;
  }
  if (!chosen_->seen || role == Role::Instructor) return;  // chosen by the instructor: waits for its person
  const Plan p = chosen_->plan;
  chosen_.reset();
  if (select_) select_(std::nullopt);
  PlannerEvents e;
  {
    std::lock_guard l(m_);
    e = ev_;
  }
  if (e.putAway) e.putAway(p.name, p.trainee);
}

std::string PlannerService::beforeStart(PlanSelection& s) {
  const auto [role, person] = rules_.who ? rules_.who() : std::pair{Role::None, std::string()};
  const std::string who = role == Role::Trainee ? person : std::string();
  // On this thread: the dialog waits for it, and the files are small (to be measured on the panel).
  (void)store_.load();
  const auto p = store_.find(s.plan.id);
  if (!p) return "the plan was deleted";
  const Loaded l = loadOne(p->scenario);
  PlanSelection next;
  const ParamValues before = s.values;
  if (std::string why = check(*p, l, who, next); !why.empty()) return why;
  if (!p->fixed) {  // keep what the card shows, unless it is this person's previous combination
    const auto prev = store_.previous(who.empty() ? p->trainee : who, next.airfield);
    if (!(prev && sameValues(before, *prev))) next.values = before;
  }
  next.seen = s.seen || (role == Role::Trainee && person == p->trainee);
  if (p->assessed && (role != Role::Trainee || person != p->trainee)) return "This plan is for " + p->trainee + ": Assessed only when " + p->trainee + " is logged in";
  s = next;
  chosen_ = s;
  return "";
}

void PlannerService::started(const PlanSelection& s, const std::string& session) {
  PlanRun r{s.plan.id, s.plan.name, s.person, s.run, session, s.airfield, rules_.now ? rules_.now() : std::string(), planValues(s.values)};
  post([this, r = std::move(r)]() mutable {
    (void)store_.addRun(std::move(r));
    PlannerEvents e;
    {
      std::lock_guard l(m_);
      e = ev_;
    }
    if (e.plansChanged) e.plansChanged();
  });
}

void PlannerService::ended(PlanSelection& s) {
  wait();  // the run is counted first
  if (s.plan.fixed) return;
  const Loaded l = loadOne(s.plan.scenario);
  if (!l.exercise) return;
  ParamValues v;
  bool only = false;
  if (drawn(l, s.person.empty() ? s.plan.trainee : s.person, v, only).empty()) {
    s.values = v;
    s.onlyCombination = only;
    s.run = store_.nextRun(s.plan.id, s.person);
  }
  chosen_ = s;
}

}  // namespace atc
