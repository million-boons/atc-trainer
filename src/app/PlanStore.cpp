#include "app/PlanStore.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <sys/stat.h>
#include <cctype>

#include "storage/PrivateFile.h"

namespace fs = std::filesystem;

namespace atc {
namespace {
using json = nlohmann::ordered_json;

std::string text(const json& j, const char* k) {
  const auto it = j.find(k);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}
int number(const json& j, const char* k, int fallback) {
  const auto it = j.find(k);
  return it != j.end() && it->is_number_integer() ? it->get<int>() : fallback;
}
std::string fnv(const std::string& b) {
  if (b.empty()) return "";
  std::uint64_t h = 14695981039346656037ull;
  for (const unsigned char c : b) h = (h ^ c) * 1099511628211ull;
  char hex[17];
  std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(h));
  return hex;
}
const char* presetName(PlanPreset p) { return p == PlanPreset::Easy ? "easy" : p == PlanPreset::Normal ? "normal" : p == PlanPreset::Hard ? "hard" : "custom"; }
PlanPreset presetOf(const std::string& s) { return s == "easy" ? PlanPreset::Easy : s == "normal" ? PlanPreset::Normal : s == "hard" ? PlanPreset::Hard : PlanPreset::Custom; }

json toJson(const PlanValues& v) { return {{"callsign", v.callsign}, {"type", v.type}, {"runway", v.runway}, {"taxiway", v.taxiway}}; }
PlanValues valuesOf(const json& j) { return {text(j, "callsign"), text(j, "type"), text(j, "runway"), text(j, "taxiway")}; }
json toJson(const PlanDifficulty& d) { return {{"preset", presetName(d.preset)}, {"replyFromMs", d.replyFromMs}, {"replyToMs", d.replyToMs}, {"radio", d.radio}}; }
PlanDifficulty difficultyOf(const json& j) { return {presetOf(text(j, "preset")), number(j, "replyFromMs", 500), number(j, "replyToMs", 1500), text(j, "radio")}; }

json toJson(const Plan& p) {
  return {{"id", p.id}, {"name", p.name}, {"scenario", p.scenario}, {"scenarioHash", p.scenarioHash}, {"trainee", p.trainee},
          {"assessed", p.assessed}, {"fixed", p.fixed}, {"briefing", p.briefing}, {"values", toJson(p.values)},
          {"difficulty", toJson(p.difficulty)}, {"savedAt", p.savedAt}};
}
Plan planOf(const json& j) {
  Plan p;
  p.id = text(j, "id");
  p.name = text(j, "name");
  p.scenario = text(j, "scenario");
  p.scenarioHash = text(j, "scenarioHash");
  p.trainee = text(j, "trainee");
  p.assessed = j.value("assessed", false);
  p.fixed = j.value("fixed", true);
  p.briefing = text(j, "briefing");
  if (j.contains("values")) p.values = valuesOf(j["values"]);
  if (j.contains("difficulty")) p.difficulty = difficultyOf(j["difficulty"]);
  p.savedAt = text(j, "savedAt");
  return p;
}

std::string newId() {
  std::random_device d;
  char b[20];
  std::snprintf(b, sizeof b, "p%08x%08x", d(), d());
  return b;
}
std::string lower(std::string s) {
  for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

std::vector<PlanDifficulty> PlanStore::startingPresets() {
  return {{PlanPreset::Easy, 600, 1000, "light"}, {PlanPreset::Normal, 500, 1500, "light"}, {PlanPreset::Hard, 500, 1500, "medium"}};
}

std::string PlanStore::load() {
  std::lock_guard l(m_);
  loaded_ = true;
  broken_ = false;
  warning_.clear();
  plans_.clear();
  runs_.clear();
  presets_ = startingPresets();
  bytes_.clear();
  std::error_code ec;
  const fs::path plans = fs::path(dir_) / "plans.json", runs = fs::path(dir_) / "runs.jsonl";
  if (fs::exists(plans, ec)) {
    std::ifstream f(plans, std::ios::binary);
    bytes_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    const json j = json::parse(bytes_, nullptr, false);
    if (!f || j.is_discarded() || !j.is_object() || number(j, "format", 0) != 1 || !j.contains("plans") || !j["plans"].is_array()) {
      broken_ = true;
      return plans.string() + ": cannot be read; it is not changed";
    }
    for (const auto& x : j["plans"])
      if (x.is_object()) plans_.push_back(planOf(x));
    if (j.contains("presets") && j["presets"].is_array() && j["presets"].size() == 3) {
      presets_.clear();
      for (const auto& x : j["presets"]) presets_.push_back(difficultyOf(x));
    }
  }
  if (fs::exists(runs, ec)) {
    std::ifstream f(runs);
    std::vector<std::string> lines;
    for (std::string line; std::getline(f, line);) lines.push_back(line);
    for (std::size_t i = 0; i < lines.size(); ++i) {
      if (lines[i].empty()) continue;
      const json j = json::parse(lines[i], nullptr, false);
      if (j.is_discarded() || !j.is_object()) {
        if (i + 1 == lines.size()) {  // a power cut during the last write
          warning_ = runs.string() + ": the last line is incomplete and was skipped";
          continue;
        }
        broken_ = true;
        return runs.string() + ": line " + std::to_string(i + 1) + " cannot be read; nothing is changed";
      }
      PlanRun r{text(j, "plan"), text(j, "planName"), text(j, "person"), number(j, "run", 0), text(j, "session"), text(j, "airfield"), text(j, "at"), {}};
      if (j.contains("values")) r.values = valuesOf(j["values"]);
      runs_.push_back(std::move(r));
    }
  }
  return "";
}

std::vector<Plan> PlanStore::plans() const {
  std::lock_guard l(m_);
  std::vector<Plan> out = plans_;
  for (auto& p : out)
    for (const auto& r : runs_)
      if (r.plan == p.id) {
        ++p.runs;
        p.lastRun = r.at;
      }
  return out;
}

std::vector<PlanDifficulty> PlanStore::presets() const {
  std::lock_guard l(m_);
  return presets_;
}

std::string PlanStore::revision() const {
  std::lock_guard l(m_);
  return fnv(bytes_);
}

std::optional<Plan> PlanStore::find(const std::string& id) const {
  for (const auto& p : plans())
    if (p.id == id) return p;
  return std::nullopt;
}

std::string PlanStore::write(const std::vector<Plan>& plans, const std::vector<PlanDifficulty>& presets) {
  json list = json::array(), pre = json::array();
  for (const auto& p : plans) list.push_back(toJson(p));
  for (const auto& p : presets) pre.push_back(toJson(p));
  const std::string bytes = json{{"format", 1}, {"presets", pre}, {"plans", list}}.dump(1, ' ', false, json::error_handler_t::replace) + "\n";
  if (!writePrivateFile((fs::path(dir_) / "plans.json").string(), bytes)) return "cannot write " + dir_ + "/plans.json";
  bytes_ = bytes;
  plans_ = plans;
  presets_ = presets;
  return "";
}

std::string PlanStore::save(Plan p, const std::string& revision, std::string& id) {
  std::lock_guard l(m_);
  if (!loaded_ || broken_) return "the plan file cannot be read: nothing is saved";
  if (revision != fnv(bytes_)) return "the plans were changed meanwhile: open them again";
  std::vector<Plan> next = plans_;
  for (const auto& x : next)
    if (x.id != p.id && lower(x.name) == lower(p.name)) return "a plan with this name exists";
  p.runs = 0;
  p.lastRun.clear();
  p.valuesError.clear();
  if (p.id.empty()) {
    p.id = newId();
    next.push_back(p);
  } else {
    const auto it = std::find_if(next.begin(), next.end(), [&](const Plan& x) { return x.id == p.id; });
    if (it == next.end()) return "the plan was deleted meanwhile";
    *it = p;
  }
  id = p.id;
  return write(next, presets_);
}

std::string PlanStore::remove(const std::string& id, const std::string& revision) {
  std::lock_guard l(m_);
  if (!loaded_ || broken_) return "the plan file cannot be read: nothing is changed";
  if (revision != fnv(bytes_)) return "the plans were changed meanwhile: open them again";
  std::vector<Plan> next = plans_;
  const auto n = std::erase_if(next, [&](const Plan& x) { return x.id == id; });
  if (!n) return "the plan was deleted meanwhile";
  return write(next, presets_);
}

std::string PlanStore::savePresets(std::vector<PlanDifficulty> p, const std::string& revision) {
  std::lock_guard l(m_);
  if (!loaded_ || broken_) return "the plan file cannot be read: nothing is changed";
  if (revision != fnv(bytes_)) return "the plans were changed meanwhile: open them again";
  if (p.size() != 3) return "three presets are needed";
  for (std::size_t i = 0; i < 3; ++i) p[i].preset = PlanPreset(int(i));
  return write(plans_, p);
}

int PlanStore::nextRun(const std::string& plan, const std::string& person) const {
  if (person.empty()) return 0;
  std::lock_guard l(m_);
  int n = 0;
  for (const auto& r : runs_) n += r.plan == plan && r.person == person;
  return n + 1;
}

std::optional<PlanValues> PlanStore::previous(const std::string& person, const std::string& airfield) const {
  if (person.empty()) return std::nullopt;
  std::lock_guard l(m_);
  for (auto it = runs_.rbegin(); it != runs_.rend(); ++it)
    if (it->person == person && it->airfield == airfield) return it->values;
  return std::nullopt;
}

std::string PlanStore::addRun(PlanRun r) {
  std::lock_guard l(m_);
  if (broken_) return "the run file cannot be read: the run is not counted";
  const json j{{"plan", r.plan}, {"planName", r.planName}, {"person", r.person}, {"run", r.run}, {"session", r.session},
               {"airfield", r.airfield}, {"at", r.at}, {"values", toJson(r.values)}};
  std::error_code ec;
  fs::create_directories(dir_, ec);
  ::chmod(dir_.c_str(), 0700);
  if (!appendPrivateLine((fs::path(dir_) / "runs.jsonl").string(), j.dump(-1, ' ', false, json::error_handler_t::replace)))
    return "cannot write " + dir_ + "/runs.jsonl";
  runs_.push_back(std::move(r));
  return "";
}

std::string PlanStore::warning() const {
  std::lock_guard l(m_);
  return warning_;
}

}  // namespace atc
