#include "app/SetupService.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "storage/PrivateFile.h"

namespace fs = std::filesystem;

namespace atc {
namespace {
struct NumberDef {
  const char *key, *page, *title, *unit;
  int starting, low, high, step;
};
// D-150 ranges (ASSUMED there, kept here). The starting value is ui.json's where it has one.
const NumberDef kNumbers[] = {
    {"tap", "ptt", "Short tap below", "ms", 250, 100, 500, 10},
    {"preroll", "ptt", "Pre-roll", "ms", 300, 100, 500, 50},
    {"tail", "ptt", "Tail", "ms", 200, 0, 500, 50},
    {"longest", "ptt", "Longest transmission", "s", 30, 10, 60, 5},
    {"debounce", "ptt", "Debounce", "ms", 20, 5, 50, 5},
    {"settle", "ptt", "Queue settle", "ms", 25, 0, 100, 5},
    {"cut", "ptt", "PILOT CUT notice", "ms", 2000, 500, 5000, 500},
    {"toneMs", "ptt", "Side-tone length", "ms", 20, 10, 100, 10},
    {"toneHz", "ptt", "Side-tone pitch", "Hz", 1000, 300, 2000, 100},
    {"toneDb", "ptt", "Side-tone level", "dBFS", -18, -40, -6, 2},
    {"free", "recordings", "Keep free on disk", "MB", 200, 100, 10000, 100},
    {"idle", "instructor", "Instructor log-out after", "min", 10, 1, 60, 1},
};
const char* kNext = "next start of the program";
bool everybody(const std::string& key) { return key == "theme" || key == "motion" || key == "clock"; }
}  // namespace

SetupService::SetupService(Config c, Rules r)
    : cfg_(std::move(c)), rules_(std::move(r)), values_(stored(cfg_.file)), thread_([this] {
        for (;;) {
          std::function<void()> f;
          {
            std::unique_lock l(qm_);
            cv_.wait(l, [this] { return stop_ || !q_.empty(); });
            if (q_.empty()) return;
            f = std::move(q_.front());
            q_.pop_front();
          }
          f();
        }
      }) {}

SetupService::~SetupService() {
  {
    std::lock_guard l(qm_);
    stop_ = true;
  }
  cv_.notify_one();
  thread_.join();
}

void SetupService::post(std::function<void()> f) {
  {
    std::lock_guard l(qm_);
    q_.push_back(std::move(f));
  }
  cv_.notify_one();
}

std::map<std::string, std::string> SetupService::stored(const std::string& file) {
  std::map<std::string, std::string> out;
  std::ifstream f(file);
  const auto j = nlohmann::json::parse(f, nullptr, false);
  if (j.is_object())
    for (const auto& [k, v] : j.items())
      if (v.is_string()) out[k] = v.get<std::string>();
  return out;
}

std::string SetupService::save() {
  nlohmann::json j = nlohmann::json::object();
  for (const auto& [k, v] : values_) j[k] = v;
  return writePrivateFile(cfg_.file, j.dump(1) + "\n") ? "" : "cannot write " + cfg_.file;
}

SetupContent SetupService::content() const {
  std::lock_guard l(m_);
  SetupContent c;
  const auto start = [&](const std::string& k, const std::string& fallback) {
    const auto it = cfg_.starting.find(k);
    return it == cfg_.starting.end() ? fallback : it->second;
  };
  for (const auto& n : kNumbers) {
    SetupNumber x{n.key, n.page, n.title, n.unit, kNext, 0, std::stoi(start(n.key, std::to_string(n.starting))), n.low, n.high, n.step, false};
    const auto it = values_.find(n.key);
    x.byUser = it != values_.end();
    x.value = x.byUser ? std::stoi(it->second) : x.starting;
    c.numbers.push_back(x);
  }
  const auto choice = [&](std::string key, std::string page, std::string title, std::string starting, std::vector<SetupOption> options) {
    SetupChoice x{key, page, title, everybody(key) ? "now" : kNext, starting, starting, std::move(options), everybody(key), false};
    if (const auto it = values_.find(key); it != values_.end()) {
      x.value = it->second;
      x.byUser = true;
    }
    c.choices.push_back(std::move(x));
  };
  choice("theme", "general", "Theme", start("theme", "dark"), {{"dark", "Dark", ""}, {"night", "Night", ""}, {"hc", "High-contrast", ""}});
  choice("motion", "general", "Motion", start("motion", "normal"), {{"normal", "Normal", ""}, {"reduce", "Reduced", ""}});
  choice("clock", "general", "Clock", start("clock", "both"), {{"both", "UTC and local", ""}, {"utc", "UTC", ""}, {"local", "Local", ""}});
  // Not built: lists of devices. The one in use is shown; another one is chosen in ui.json.
  choice("mic", "audio", "Microphone", "current", {{"current", "As set in ui.json", "", false}, {"list", "Other devices", "not built yet", true}});
  choice("out", "audio", "Output", "current", {{"current", "As set in ui.json", "", false}, {"list", "Other devices", "not built yet", true}});
  choice("ptt", "ptt", "Device", "current", {{"current", "As set in ui.json", "", false}, {"list", "Other devices", "not built yet", true}});
  choice("sidetone", "ptt", "Side-tone", start("sidetone", "on"), {{"on", "On", ""}, {"off", "Off", ""}});
  std::vector<SetupOption> models;
  for (const auto& m : cfg_.models) models.push_back({m, m, ""});
  choice("model", "speech", "Exercise model", start("model", "whisper-tiny-genprompt"), models);
  // S-5: the bank of today, and every Studio bank.
  std::vector<SetupOption> banks{{cfg_.activeBank, fs::path(cfg_.activeBank).filename().string(), "in use now"}};
  std::error_code ec;
  for (fs::directory_iterator it(cfg_.studioBanks, ec), end; !ec && it != end; it.increment(ec))
    if (it->is_directory() && it->path().string() != cfg_.activeBank) {
      const bool ready = fs::exists(it->path() / "manifest.json", ec);
      banks.push_back({it->path().string(), it->path().filename().string() + " (Studio)", ready ? "" : "no accepted clip yet", !ready});
    }
  choice("bank", "voice", "Voice bank", cfg_.activeBank, banks);
  choice("radio", "voice", "Radio effect", start("radio", "light"), {{"off", "Off", ""}, {"light", "Light", ""}, {"medium", "Medium", ""}, {"heavy", "Heavy", ""}});
  c.facts = cfg_.facts;
  c.guests = rules_.guests ? rules_.guests() : 0;
  return c;
}

SetupBackend SetupService::backend() {
  SetupBackend b;
  const auto allowed = [this](const std::string& key) -> std::string {
    const Role r = rules_.role ? rules_.role() : Role::None;
    if (!everybody(key) && r != Role::Instructor) return "only the instructor may change this";
    if (rules_.busy && rules_.busy()) return "not during an exercise or a transmission";
    return "";
  };
  b.load = [this](auto done) { done(content(), ""); };
  b.setNumber = [this, allowed](const std::string& key, int value, SetupBackend::Done done) {
    if (std::string why = allowed(key); !why.empty()) return done(why);
    const auto n = std::find_if(std::begin(kNumbers), std::end(kNumbers), [&](const NumberDef& d) { return key == d.key; });
    if (n == std::end(kNumbers)) return done("unknown setting");
    if (value < n->low || value > n->high || (value - n->low) % n->step) return done("out of range");
    std::lock_guard l(m_);
    if (key == "tail" || key == "longest") {  // the tail must stay below the longest transmission
      const auto get = [&](const char* k, int fallback) { const auto it = values_.find(k); return it == values_.end() ? fallback : std::stoi(it->second); };
      const int tail = key == "tail" ? value : get("tail", 200), longest = key == "longest" ? value : get("longest", 30);
      if (tail >= longest * 1000) return done("the tail must be shorter than the longest transmission");
    }
    const auto before = values_;
    values_[key] = std::to_string(value);
    const std::string why = save();
    if (!why.empty()) values_ = before;
    done(why);
  };
  b.setChoice = [this, allowed](const std::string& key, const std::string& id, SetupBackend::Done done) {
    if (std::string why = allowed(key); !why.empty()) return done(why);
    const SetupContent c = content();
    const auto ch = std::find_if(c.choices.begin(), c.choices.end(), [&](const SetupChoice& x) { return x.key == key; });
    if (ch == c.choices.end()) return done("unknown setting");
    const auto op = std::find_if(ch->options.begin(), ch->options.end(), [&](const SetupOption& o) { return o.id == id; });
    if (op == ch->options.end() || op->off) return done(op == ch->options.end() ? "not one of the choices" : op->note);
    std::lock_guard l(m_);
    const auto before = values_;
    values_[key] = id;
    const std::string why = save();
    if (!why.empty()) values_ = before;
    done(why);
  };
  b.restore = [this, allowed](const std::string& page, SetupBackend::Done done) {
    if (std::string why = allowed(page == "general" ? "theme" : ""); !why.empty()) return done(why);
    const SetupContent c = content();
    std::lock_guard l(m_);
    const auto before = values_;
    for (const auto& n : c.numbers)
      if (n.page == page) values_.erase(n.key);
    for (const auto& ch : c.choices)
      if (ch.page == page) values_.erase(ch.key);
    const std::string why = save();
    if (!why.empty()) values_ = before;
    done(why);
  };
  b.startLearn = [](auto, SetupBackend::Done done) { done("learning a pedal is not built yet (D-310, G1-P)"); };
  b.stopLearn = [] {};
  b.changePin = [this](const std::string& oldPin, const std::string& newPin, SetupBackend::Done done) {
    if (!rules_.role || rules_.role() != Role::Instructor) return done("only the instructor may do this");
    if (!rules_.changePin) return done("this build cannot store a PIN");
    post([this, oldPin, newPin, done] { done(rules_.changePin(oldPin, newPin)); });
  };
  b.testSentence = [this](SetupBackend::Done done) {
    if (rules_.busy && rules_.busy()) return done("the radio is in use");
    if (!rules_.testSentence) return done("not available in this build");
    rules_.testSentence(std::move(done));
  };
  b.deleteGuests = [this](int expected, auto done) {
    if (!rules_.role || rules_.role() != Role::Instructor) return done(rules_.guests ? rules_.guests() : 0, "only the instructor may do this");
    if (!rules_.deleteGuests) return done(0, "not available in this build");
    post([this, expected, done] {
      int count = 0;
      const std::string why = rules_.deleteGuests(expected, count);
      done(count, why);
    });
  };
  return b;
}

}  // namespace atc
