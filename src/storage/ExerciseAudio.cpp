#include "storage/ExerciseAudio.h"

#include <sys/stat.h>
#include <sys/statvfs.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "audio/WavFile.h"
#include "storage/PrivateFile.h"

namespace fs = std::filesystem;

namespace atc {
namespace {
using json = nlohmann::json;
bool validSession(const std::string& s) {
  return !s.empty() && s.size() < 80 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || c == '-'; });
}
std::string nowText() {
  const std::time_t t = std::time(nullptr);
  std::tm l{};
  localtime_r(&t, &l);
  char b[32];
  std::strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &l);
  return b;
}
std::vector<json> index(const std::string& folder) {
  std::vector<json> out;
  std::ifstream f(folder + "/index.jsonl");
  for (std::string line; std::getline(f, line);) {
    json j = json::parse(line, nullptr, false);
    if (j.is_object()) out.push_back(std::move(j));
  }
  return out;
}
}  // namespace

ExerciseAudio::ExerciseAudio(Config c) : cfg_(std::move(c)), thread_([this] { run(); }) {}

ExerciseAudio::~ExerciseAudio() {
  {
    std::lock_guard l(qm_);
    stop_ = true;
  }
  cv_.notify_one();
  thread_.join();
}

void ExerciseAudio::post(std::function<void()> f) {
  {
    std::lock_guard l(qm_);
    q_.push_back(std::move(f));
  }
  cv_.notify_one();
}

void ExerciseAudio::run() {
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

void ExerciseAudio::wait() {
  std::unique_lock l(qm_);
  idle_.wait(l, [this] { return q_.empty() && !busy_; });
}

std::string ExerciseAudio::folder(const std::string& session) const { return cfg_.dir + "/" + session; }

void ExerciseAudio::begin(const std::string& session, const std::string& speaker) {
  std::lock_guard l(m_);
  const bool keep = validSession(session) && !speaker.empty();
  session_ = keep ? session : std::string();
  speaker_ = keep ? speaker : std::string();
  noted_ = false;
}

void ExerciseAudio::end() { begin({}, {}); }

void ExerciseAudio::transmission(std::uint64_t tx, std::vector<std::int16_t> audio, int rate, int preRoll, int keyed) {
  std::string session, speaker;
  {
    std::lock_guard l(m_);
    session = session_;
    speaker = speaker_;
  }
  if (session.empty() || audio.empty()) return;
  post([this, tx, audio = std::move(audio), rate, preRoll, keyed, session, speaker]() mutable {
    const Answer a = cfg_.answer ? cfg_.answer(speaker) : Answer{};
    if (!a.yes) return;  // asked again before each file: a "no" writes nothing more
    const std::string dir = folder(session);
    std::error_code ec;
    fs::create_directories(cfg_.dir, ec);
    ::chmod(cfg_.dir.c_str(), 0700);
    if (!fs::is_directory(dir, ec) && ::mkdir(dir.c_str(), 0700) != 0) return;
    long free = -1;
    if (cfg_.freeMb) free = cfg_.freeMb();
    else if (struct statvfs s {}; statvfs(dir.c_str(), &s) == 0) free = long(s.f_bavail / 1024 * s.f_frsize / 1024);
    if (free >= 0 && free < cfg_.minFreeMb) {
      bool first = false;
      {
        std::lock_guard l(m_);
        first = !std::exchange(noted_, true);
      }
      if (first) appendPrivateLine(dir + "/index.jsonl", json{{"note", "Exercise audio not saved: disk nearly full"}}.dump());
      return;
    }
    const std::string file = "tx" + std::to_string(tx) + ".wav";
    if (fs::exists(dir + "/" + file, ec)) return;  // never overwrite
    if (!writeWav(dir + "/" + file, rate, audio)) return;
    ::chmod((dir + "/" + file).c_str(), 0600);
    const json line{{"tx", tx}, {"file", file}, {"samples", audio.size()}, {"rate", rate}, {"preRoll", preRoll}, {"keyed", keyed},
                    {"speaker", speaker}, {"consentDate", a.date}, {"consentVersion", a.version}, {"at", nowText()}};
    if (!appendPrivateLine(dir + "/index.jsonl", line.dump())) std::remove((dir + "/" + file).c_str());
  });
}

std::set<std::uint64_t> ExerciseAudio::saved(const std::string& session) const {
  std::set<std::uint64_t> out;
  if (!validSession(session)) return out;
  std::error_code ec;
  for (const auto& j : index(folder(session)))
    if (j.contains("tx") && j.contains("file") && fs::exists(folder(session) + "/" + j.value("file", ""), ec)) out.insert(j["tx"].get<std::uint64_t>());
  return out;
}

std::string ExerciseAudio::speaker(const std::string& session) const {
  if (!validSession(session)) return "";
  for (const auto& j : index(folder(session)))
    if (j.contains("speaker")) return j.value("speaker", "");
  return "";
}

std::string ExerciseAudio::note(const std::string& session) const {
  if (!validSession(session)) return "";
  for (const auto& j : index(folder(session)))
    if (j.contains("note")) return j.value("note", "");
  return "";
}

std::optional<ExerciseAudio::Clip> ExerciseAudio::read(const std::string& session, std::uint64_t tx) const {
  if (!validSession(session)) return std::nullopt;
  for (const auto& j : index(folder(session))) {
    if (!j.contains("tx") || j["tx"].get<std::uint64_t>() != tx) continue;
    const std::string file = j.value("file", "");
    if (file.find('/') != std::string::npos) return std::nullopt;
    auto w = readWav(folder(session) + "/" + file);
    if (!w) return std::nullopt;
    return Clip{std::move(w->samples), w->rate, j.value("preRoll", 0), j.value("keyed", 0)};
  }
  return std::nullopt;
}

bool ExerciseAudio::remove(const std::string& session) {
  if (!validSession(session)) return false;
  std::error_code ec;
  fs::remove_all(folder(session), ec);
  return !ec;
}

}  // namespace atc
