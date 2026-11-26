#include "app/HistoryService.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <cctype>
#include <ctime>
#include <nlohmann/json.hpp>

#include "engine/EventRead.h"
#include "engine/ExerciseResult.h"
#include "scenario/Scenario.h"
#include "storage/PrivateFile.h"

namespace fs = std::filesystem;

namespace atc {
namespace {
using json = nlohmann::json;
using End = HistoryRow::End;

bool validId(const std::string& id) {
  return !id.empty() && id.size() < 80 && std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '-'; });
}

struct Built {
  ReadLog log;
  std::optional<Exercise> exercise;
  ExerciseResult result;
};

Built build(const std::string& data, const std::string& file) {
  Built b;
  b.log = readEventLog(file);
  if (!b.log.error.empty()) return b;
  Exercise e;
  std::vector<LoadError> errors;
  // The scenario of today: if it was changed since, the steps may not match (the hash in the header says so).
  if (loadExercise(scenarioSources(data, data + "/scenarios/" + b.log.header.scenarioId + ".json"), e, errors)) b.exercise = std::move(e);
  b.result = buildResult(b.exercise ? b.exercise->scenario : Scenario{}, b.log.events);
  return b;
}

std::vector<json> reviews(const std::string& file) {
  std::vector<json> out;
  std::ifstream f(file);
  for (std::string line; std::getline(f, line);) {
    json j = json::parse(line, nullptr, false);
    if (j.is_object()) out.push_back(std::move(j));
  }
  return out;
}

std::string clock(std::int64_t startedMs, Ns since) {
  const std::time_t t = (startedMs + since / 1'000'000) / 1000;
  std::tm local{};
  localtime_r(&t, &local);
  char b[16];
  std::strftime(b, sizeof b, "%H:%M:%S", &local);
  return b;
}

HistoryRow row(const Built& b, const std::string& id, int reclassified) {
  HistoryRow r;
  const EventLogHeader& h = b.log.header;
  r.id = id;
  r.trainee = h.traineeId;
  r.scenario = b.exercise ? b.exercise->scenario.name : h.scenarioId;
  r.plan = h.planName;
  r.run = h.planRun;
  r.assessed = h.mode == ExerciseMode::Assessed;
  r.placeholder = !b.exercise || b.exercise->placeholder;
  r.startedMs = h.startedMs;
  r.endedMs = h.startedMs + b.result.duration / 1'000'000;
  r.end = !b.log.ended || !b.result.end ? End::Fault : *b.result.end == out::EndReason::Completed ? End::Completed : End::Stopped;
  std::vector<Ns> response;
  for (const auto& s : b.result.steps) {
    ++r.steps;
    r.stepsDone += s.completed;
    for (const auto& [o, n] : s.errors) r.errors += n;
    r.missed += int(s.missed.size());
    if (s.responseTime) response.push_back(*s.responseTime);
  }
  r.suspected = int(b.result.suspectedStt.size());
  const PttResult& p = b.result.ptt;
  r.ptt = p.spokeBeforeKeying + p.releasedEarly + p.shortTaps + p.noSpeech + p.keyedOverPilot + p.stuck + p.deviceLost + p.overload;
  if (!response.empty()) {
    std::sort(response.begin(), response.end());
    r.responseMs = int(response[response.size() / 2] / 1'000'000);
  }
  r.reclassified = reclassified;
  return r;
}
HistoryDetail detailOf(const Built& b, const std::string& id, const std::vector<json>& rv) {
  HistoryDetail d;
  d.row = row(b, id, int(rv.size()));
  d.model = b.log.header.modelId;
  if (const auto it = b.log.header.values.find("callsign"); it != b.log.header.values.end()) d.callsign = it->second;
  d.fallbackPtt = b.result.fallbackPttUsed();
  for (const auto& s : b.result.steps)
    if (!s.completed) d.missedSteps.push_back(s.label + (s.reached ? ": not completed" : ": not reached"));
  const PttResult& p = b.result.ptt;
  const auto note = [&](int n, const char* what) { if (n) d.pttNotes.push_back(std::to_string(n) + " " + what); };
  note(p.spokeBeforeKeying, "spoke before keying");
  note(p.releasedEarly, "released early");
  note(p.shortTaps, "short taps");
  note(p.noSpeech, "without speech");
  note(p.keyedOverPilot, "keyed over the pilot");
  note(p.stuck, "held to the limit");
  for (const auto& s : b.result.suspectedStt) d.suspectedNotes.push_back(s.step + ": \"" + s.heard + "\"");
  // The timeline, in log order. An ATC entry carries what was heard, parsed and judged.
  std::map<std::uint64_t, std::string> spoken;   // utterance -> text
  std::map<std::uint64_t, std::size_t> atc;      // tx -> entry
  std::set<std::uint64_t> early;
  const Ns t0 = b.log.events.empty() ? 0 : b.log.events.front().t;
  for (const auto& e : b.log.events) {
    const std::string at = clock(b.log.header.startedMs, e.t - t0);
    if (const auto* o = std::get_if<EngineOutput>(&e.body)) {
      if (const auto* x = std::get_if<out::PilotRequested>(o)) spoken[x->utterance.id] = x->utterance.text;
      if (const auto* x = std::get_if<out::AtcBeforePilot>(o)) early.insert(x->tx);
      if (const auto* x = std::get_if<out::Judged>(o)) {
        if (x->tx && atc.count(x->tx)) {
          HistoryEntry& h = d.entries[atc[x->tx]];
          h.outcome = std::string(toString(x->outcome));
          h.step = x->step;
          h.suspected = x->suspectedStt;
          h.error = x->outcome != Outcome::Correct && !x->suspectedStt && !early.count(x->tx);
          h.expected = x->problems.empty() ? "" : "problems: " + [&] { std::string s; for (const auto& p : x->problems) s += (s.empty() ? "" : ", ") + p; return s; }();
          h.level = x->outcome == Outcome::Correct ? 1 : 2;
        } else if (x->outcome == Outcome::Timeout) {
          d.entries.push_back({HistoryEntry::Kind::Note, at, "No ATC call: missed response", "TIMEOUT", 2});
        }
      }
      if (const auto* x = std::get_if<out::Ended>(o))
        d.entries.push_back({HistoryEntry::Kind::Note, at, x->reason == out::EndReason::ProgramClosed ? "Program closed" : "Ended", "", 0});
      continue;
    }
    const auto& i = std::get<EngineInput>(e.body);
    if (const auto* x = std::get_if<in::PilotStarted>(&i)) {
      d.entries.push_back({HistoryEntry::Kind::Pilot, at, spoken[x->utterance], "", 0});
    } else if (const auto* x = std::get_if<in::Recognised>(&i)) {
      HistoryEntry h;
      h.kind = HistoryEntry::Kind::Atc;
      h.time = at;
      h.text = x->instruction.rawText;
      h.heard = x->instruction.rawText;
      std::string parsed = x->instruction.intent;
      for (const auto& [k, v] : x->instruction.fields) parsed += " " + k + "=" + (v.value.empty() ? v.heard + "?" : v.value);
      h.parsed = parsed;
      h.cls = std::string(toString(x->instruction.cls));
      char dl[16];
      std::snprintf(dl, sizeof dl, "%.1f s", double(x->releaseToText) / 1e9);
      h.delay = dl;
      h.audio = false;  // exercise audio is not stored (D-314)
      atc[x->instruction.tx] = d.entries.size();
      d.entries.push_back(std::move(h));
    } else if (const auto* x = std::get_if<in::TxCancelled>(&i)) {
      d.entries.push_back({HistoryEntry::Kind::Ptt, at, "Short tap: TX " + std::to_string(x->tx) + " cancelled", "", 1});
    }
  }
  for (const auto& r : rv) {
    const int entry = r.value("entry", -1);
    if (entry < 0 || std::size_t(entry) >= d.entries.size()) continue;
    d.entries[std::size_t(entry)].reviews.push_back({r.value("verdict", ""), r.value("note", ""), r.value("role", ""), r.value("at", "")});
  }
  if (!b.log.ended) d.entries.push_back({HistoryEntry::Kind::Note, "", "The log stops here: fault", "", 3});
  return d;
}
}  // namespace

HistoryService::HistoryService(Config c, Rules r) : cfg_(std::move(c)), rules_(std::move(r)), thread_([this] { run(); }) {}

HistoryService::~HistoryService() {
  {
    std::lock_guard l(qm_);
    stop_ = true;
  }
  cv_.notify_one();
  thread_.join();
}

void HistoryService::post(std::function<void()> f) {
  {
    std::lock_guard l(qm_);
    q_.push_back(std::move(f));
  }
  cv_.notify_one();
}

void HistoryService::run() {
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

void HistoryService::wait() {
  std::unique_lock l(qm_);
  idle_.wait(l, [this] { return q_.empty() && !busy_; });
}

std::vector<std::string> HistoryService::files() const {
  std::vector<std::string> out;
  std::error_code ec;
  for (fs::directory_iterator it(cfg_.logs, ec), end; !ec && it != end; it.increment(ec)) {
    const auto p = it->path();
    if (p.extension() == ".jsonl" && p.stem().extension() != ".review") out.push_back(p.string());
  }
  std::sort(out.rbegin(), out.rend());  // newest first: the name starts with the UTC time
  return out;
}

std::string HistoryService::file(const std::string& id) const {
  if (!validId(id)) return "";
  const std::string f = cfg_.logs + "/" + id + ".jsonl";
  std::error_code ec;
  return fs::exists(f, ec) ? f : "";
}

HistoryBackend HistoryService::backend() {
  HistoryBackend b;
  // Who may see what, decided here on the GUI thread (H-1).
  const auto allowed = [this](const std::string& trainee, std::string& why) {
    const auto [role, person] = rules_.who ? rules_.who() : std::pair{Role::None, std::string()};
    if (role == Role::Instructor) return true;
    if (role == Role::Trainee && trainee == person) return true;
    why = role == Role::Trainee ? "a trainee sees only their own results" : "log in to see results";
    return false;
  };
  b.list = [this, allowed](const std::string& person, auto done) {
    std::string why;
    const auto [role, me] = rules_.who ? rules_.who() : std::pair{Role::None, std::string()};
    const std::string wanted = role == Role::Trainee && person.empty() ? me : person;
    if (!(wanted.empty() ? role == Role::Instructor : allowed(wanted, why))) return done({}, why.empty() ? "only the instructor sees all results" : why);
    post([this, wanted, done = std::move(done)] {
      std::vector<HistoryRow> rows;
      for (const auto& f : files()) {
        EventLogHeader h;
        std::string error;
        if (!readEventLogHeader(f, h, error)) continue;
        if (h.traineeId.empty()) continue;  // guests are in nobody's history (D-127)
        if (!wanted.empty() && h.traineeId != wanted) continue;
        const std::string id = fs::path(f).stem().string();
        rows.push_back(row(build(cfg_.data, f), id, int(reviews(cfg_.logs + "/" + id + ".review.jsonl").size())));
      }
      done(std::move(rows), "");
    });
  };
  b.open = [this, allowed](const std::string& id, auto done) {
    const std::string f = file(id);
    if (f.empty()) return done({}, "this result is not there");
    EventLogHeader h;
    std::string error;
    if (!readEventLogHeader(f, h, error)) return done({}, error);
    if (h.traineeId.empty()) return done({}, "a guest result is in nobody's history");
    if (std::string why; !allowed(h.traineeId, why)) return done({}, why);
    post([this, f, id, done = std::move(done)] {
      const Built b = build(cfg_.data, f);
      if (!b.log.error.empty()) return done({}, b.log.error);
      done(detailOf(b, id, reviews(cfg_.logs + "/" + id + ".review.jsonl")), "");
    });
  };
  b.reclassify = [this](const std::string& id, int entry, const std::string& verdict, const std::string& note, HistoryBackend::Done done) {
    if (!rules_.who || rules_.who().first != Role::Instructor) return done("only the instructor may do this");
    if (verdict != "trainee" && verdict != "stt" && verdict != "none") return done("unknown class");
    if (note.size() > 200 || std::any_of(note.begin(), note.end(), [](unsigned char c) { return c < 0x20; })) return done("the note must have at most 200 characters and no line break");
    const std::string f = file(id);
    if (f.empty()) return done("this result is not there");
    post([this, f, id, entry, verdict, note, done = std::move(done)] {
      const HistoryDetail d = detailOf(build(cfg_.data, f), id, {});
      if (entry < 0 || std::size_t(entry) >= d.entries.size() || d.entries[std::size_t(entry)].kind != HistoryEntry::Kind::Atc)
        return done("only an ATC entry can be reclassified");
      const json line{{"entry", entry}, {"verdict", verdict}, {"note", note}, {"role", "instructor"}, {"at", rules_.now ? rules_.now() : std::string()}};
      done(appendPrivateLine(cfg_.logs + "/" + id + ".review.jsonl", line.dump(-1, ' ', false, json::error_handler_t::replace)) ? "" : "the review cannot be written");
    });
  };
  b.remove = [this](const std::string& id, HistoryBackend::Done done) {
    if (!rules_.who || rules_.who().first != Role::Instructor) return done("only the instructor may do this");
    const std::string f = file(id);
    if (f.empty()) return done("this result is not there");
    post([this, f, id, done = std::move(done)] {
      const json line{{"when", rules_.now ? rules_.now() : std::string()}, {"result", id}, {"role", "instructor"}};
      if (!appendPrivateLine(cfg_.logs + "/deletions.log", line.dump())) return done("the deletion cannot be logged: nothing was deleted");
      std::remove(f.c_str());
      std::remove((cfg_.logs + "/" + id + ".review.jsonl").c_str());
      done("");
    });
  };
  b.play = [](const std::string&, int, bool, HistoryBackend::Done done) { done("exercise audio is not stored (D-314)"); };
  b.stopPlay = [] {};
  return b;
}

int HistoryService::guests() const {
  int n = 0;
  for (const auto& f : files()) {
    EventLogHeader h;
    std::string error;
    n += readEventLogHeader(f, h, error) && h.traineeId.empty();
  }
  return n;
}

std::string HistoryService::deleteGuests(int expected, int& count) {
  std::vector<std::string> list;
  for (const auto& f : files()) {
    EventLogHeader h;
    std::string error;
    if (readEventLogHeader(f, h, error) && h.traineeId.empty()) list.push_back(f);
  }
  count = int(list.size());
  if (count != expected) return "the number of guest results has changed";
  for (const auto& f : list) {
    const std::string id = fs::path(f).stem().string();
    const json line{{"when", rules_.now ? rules_.now() : std::string()}, {"result", id}, {"guest", true}, {"role", "instructor"}};
    if (!appendPrivateLine(cfg_.logs + "/deletions.log", line.dump())) return "the deletion cannot be logged: nothing more was deleted";
    std::remove(f.c_str());
    std::remove((cfg_.logs + "/" + id + ".review.jsonl").c_str());
    --count;
  }
  return "";
}

}  // namespace atc
