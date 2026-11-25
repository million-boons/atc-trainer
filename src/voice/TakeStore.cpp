#include "voice/TakeStore.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <tuple>
#include <type_traits>

#include "audio/WavFile.h"
#include "storage/PrivateFile.h"

namespace fs = std::filesystem;

namespace atc {
namespace {
using json = nlohmann::ordered_json;
using State = Take::State;

std::string text(const json& j, const char* key) {
  const auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}
template <class T>
T number(const json& j, const char* key) {
  const auto it = j.find(key);
  return it != j.end() && it->is_number() && it->get<double>() >= (std::is_unsigned_v<T> ? 0.0 : -1e15) ? it->get<T>() : T{};
}

// Clip IDs become file names.
bool safeId(const std::string& s) {
  const auto ok = [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_' || c == '@'; };
  return !s.empty() && s.size() <= 160 && std::all_of(s.begin(), s.end(), ok);
}
bool validSpeaker(const std::string& s) {
  return !s.empty() && s.size() <= 16 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}
bool inGit(const fs::path& dir) {
  std::error_code ec;
  for (fs::path p = fs::weakly_canonical(fs::absolute(dir), ec); !p.empty(); p = p.parent_path()) {
    if (fs::exists(p / ".git", ec)) return true;
    if (p == p.root_path()) break;
  }
  return false;
}
double tenth(double x) { return std::round(x * 10) / 10; }
std::int16_t toSample(double x) { return std::int16_t(std::lround(std::clamp(x, -32768.0, 32767.0))); }

json toJson(const TakeAnswer& a) { return {{"consent", a.consent}, {"date", a.date}, {"version", a.version}}; }
TakeAnswer answerOf(const json& j, const char* key) {
  const auto it = j.find(key);
  if (it == j.end() || !it->is_object()) return {};
  const auto yes = it->find("consent");
  return {yes != it->end() && yes->is_boolean() && yes->get<bool>(), text(*it, "date"), text(*it, "version")};
}

const char* nameOf(State s) { return s == State::Primary ? "primary" : s == State::Alternate ? "alternate" : s == State::Rejected ? "rejected" : "pending"; }

// The chosen part, with the level and the fades of trimClip.
std::vector<std::int16_t> cutOf(std::span<const std::int16_t> raw, Take& t, const TrimConfig& c) {
  const auto kept = raw.subspan(t.trimStart, t.trimEnd - t.trimStart);
  int peak = 1;
  for (const int x : kept) peak = std::max(peak, std::abs(x));
  t.gainDb = std::min(c.targetDb - t.speechDb, c.peakLimitDb - 20 * std::log10(peak / 32768.0));
  t.durationMs = kept.size() * 1000 / TakeStore::kRate;
  const double gain = std::pow(10.0, t.gainDb / 20);
  const auto samples = [](int ms) { return std::size_t(std::max(0, ms)) * TakeStore::kRate / 1000; };
  const std::size_t in = std::min(samples(c.fadeInMs), kept.size() / 2), tail = std::min(samples(c.fadeOutMs), kept.size() / 2);
  std::vector<std::int16_t> out;
  out.reserve(kept.size());
  for (std::size_t i = 0; i < kept.size(); ++i) {
    double x = kept[i] * gain;
    if (i < in) x *= double(i) / double(in);
    if (const std::size_t left = kept.size() - 1 - i; left < tail) x *= double(left) / double(tail);
    out.push_back(toSample(x));
  }
  return out;
}

double rmsDb(std::span<const std::int16_t> s) {
  double sum = 0;
  for (const double x : s) sum += x * x;
  return s.empty() || sum == 0 ? -120 : 20 * std::log10(std::sqrt(sum / double(s.size())) / 32768);
}

// Owner only and on disk before anything names it.
bool writePrivateWav(const std::string& path, std::span<const std::int16_t> s) {
  if (!writeWav(path, TakeStore::kRate, s)) return false;
  ::chmod(path.c_str(), 0600);
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) return false;
  const bool ok = ::fsync(fd) == 0;
  return ::close(fd) == 0 && ok;
}

json* clipIn(json& m, const std::string& id) {
  for (auto& c : m["clips"])
    if (c.is_object() && text(c, "id") == id) return &c;
  return nullptr;
}
std::string takeId(const json& t) {
  const std::string id = text(t, "id");
  return id.empty() ? fs::path(text(t, "file")).stem().string() : id;
}
}  // namespace

const Take* TakeStore::find(const std::string& id) const {
  const auto it = std::find_if(takes_.begin(), takes_.end(), [&](const Take& t) { return t.id == id; });
  return it == takes_.end() ? nullptr : &*it;
}
Take* TakeStore::mutableFind(const std::string& id) { return const_cast<Take*>(find(id)); }

std::string TakeStore::bankWords(const std::string& clipId) const {
  const auto it = words_.find(clipId);
  return it == words_.end() ? std::string() : it->second;
}

std::string TakeStore::readManifest(std::string& bytes) const {
  bytes.clear();
  const std::string file = cfg_.bank + "/manifest.json";
  std::error_code ec;
  if (!fs::exists(file, ec)) return "";
  std::ifstream f(file, std::ios::binary);
  if (!f) return file + ": cannot read";
  bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  const json j = json::parse(bytes, nullptr, false);
  const auto clips = j.is_object() ? j.find("clips") : j.end();
  if (!j.is_object() || clips == j.end() || !clips->is_array()) return file + ": not a manifest";
  return "";
}

std::string TakeStore::load() {
  std::string bytes;
  if (std::string why = readManifest(bytes); !why.empty()) return why;
  std::map<std::string, Take> all;
  std::map<std::string, std::string> words;
  std::error_code ec;
  // Descriptions of recorded takes.
  for (fs::directory_iterator it(cfg_.pending, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->path().extension() != ".json") continue;
    std::ifstream f(it->path());
    const json j = json::parse(f, nullptr, false);
    Take t;
    t.id = it->path().stem().string();
    if (!j.is_object() || number<int>(j, "format") != 1 || !safeId(t.id)) return it->path().string() + ": not the description of a take";
    t.clipId = text(j, "clip");
    t.words = text(j, "words");
    t.number = number<int>(j, "number");
    t.speaker = {text(j, "speaker"), answerOf(j, "recording"), answerOf(j, "pilotVoice")};
    t.state = text(j, "state") == "rejected" ? State::Rejected : State::Pending;
    t.recordedMs = number<std::int64_t>(j, "recordedMs");
    t.samples = number<std::size_t>(j, "samples");
    t.trimStart = number<std::size_t>(j, "trimStart");
    t.trimEnd = number<std::size_t>(j, "trimEnd");
    t.speechDb = number<double>(j, "speechDb");
    t.gainDb = number<double>(j, "gainDb");
    t.durationMs = number<std::size_t>(j, "durationMs");
    if (const auto w = j.find("warnings"); w != j.end() && w->is_array())
      for (const auto& x : *w)
        if (x.is_string()) t.warnings.push_back(x.get<std::string>());
    if (!safeId(t.clipId) || !validSpeaker(t.speaker.id) || t.trimStart >= t.trimEnd || t.trimEnd > t.samples)
      return it->path().string() + ": not the description of a take";
    all[t.id] = std::move(t);
  }
  // The manifest decides about every take that it names.
  std::set<std::string> named;
  bool placeholder = true;
  if (!bytes.empty()) {
    const json m = json::parse(bytes);
    if (const auto p = m.find("placeholder"); p != m.end() && p->is_boolean()) placeholder = p->get<bool>();
    for (const auto& c : m["clips"]) {
      if (!c.is_object() || !c.contains("takes") || !c["takes"].is_array()) continue;
      const std::string clip = text(c, "id");
      int n = 0;
      for (const auto& x : c["takes"]) {
        ++n;
        if (!x.is_object()) continue;
        const std::string id = takeId(x), kind = text(x, "take");
        // Files are deleted by this name later: it must be one in the bank itself.
        if (const std::string f = text(x, "file"); f.empty() || fs::path(f).filename().string() != f || id.empty())
          return cfg_.bank + "/manifest.json: " + clip + " names a file outside the bank";
        const bool known = all.count(id) > 0;
        Take& t = all[id];
        if (!known) {
          t.id = id;
          t.words = text(c, "words");
          t.number = n;
          t.speaker = {text(x, "speaker"), {}, answerOf(x, "pilotVoice")};
          t.synthetic = t.speaker.id.empty();
          t.speechDb = number<double>(x, "speechDb");
          t.gainDb = number<double>(x, "gainDb");
          t.trimStart = number<std::size_t>(x, "trimStart");
          t.trimEnd = number<std::size_t>(x, "trimEnd");
        }
        if (const auto a = number<std::size_t>(x, "trimStart"), b = number<std::size_t>(x, "trimEnd"); known && a < b && b <= t.samples) {
          t.trimStart = a;  // the manifest is newer after a crash while trimming
          t.trimEnd = b;
        }
        t.clipId = clip;
        t.file = text(x, "file");
        t.durationMs = number<std::size_t>(x, "durationMs");
        t.state = kind == "primary" ? State::Primary : kind == "alternate" ? State::Alternate : State::Rejected;
        named.insert(t.file);
        if (t.state != State::Rejected) words[clip] = text(c, "words");
      }
    }
  }
  unlisted_ = 0;
  for (fs::directory_iterator it(cfg_.bank, ec), end; !ec && it != end; it.increment(ec))
    if (it->path().extension() == ".wav" && !named.count(it->path().filename().string())) ++unlisted_;
  takes_.clear();
  for (auto& [id, t] : all) takes_.push_back(std::move(t));
  std::sort(takes_.begin(), takes_.end(), [](const Take& a, const Take& b) { return std::tie(a.clipId, a.number, a.id) < std::tie(b.clipId, b.number, b.id); });
  words_ = std::move(words);
  placeholder_ = placeholder;
  manifest_ = std::move(bytes);
  return "";
}

std::string TakeStore::prepare(const std::string& dir) const {
  std::error_code ec;
  if (inGit(dir)) return dir + " is inside a git working copy: audio must stay outside (D-31)";
  if (!fs::is_directory(dir, ec)) {
    fs::create_directories(fs::path(dir).parent_path(), ec);
    if (::mkdir(dir.c_str(), 0700) != 0) return "cannot create " + dir;
  }
  long free = -1;
  if (cfg_.freeMb) {
    free = cfg_.freeMb();
  } else if (struct statvfs s {}; statvfs(dir.c_str(), &s) == 0) {
    free = long(s.f_bavail / 1024 * s.f_frsize / 1024);
  }
  if (free >= 0 && free < cfg_.minFreeMb) return "disk nearly full: " + std::to_string(free) + " MB free, " + std::to_string(cfg_.minFreeMb) + " MB kept";
  return "";
}

std::string TakeStore::writeTake(const Take& t) const {
  const json j{{"format", 1}, {"kind", "studio-take"}, {"clip", t.clipId}, {"words", t.words}, {"number", t.number},
               {"speaker", t.speaker.id}, {"recording", toJson(t.speaker.recording)}, {"pilotVoice", toJson(t.speaker.pilotVoice)},
               {"state", t.state == State::Rejected ? "rejected" : "pending"}, {"recordedMs", t.recordedMs}, {"rate", kRate}, {"samples", t.samples},
               {"trimStart", t.trimStart}, {"trimEnd", t.trimEnd}, {"speechDb", tenth(t.speechDb)}, {"gainDb", tenth(t.gainDb)},
               {"durationMs", t.durationMs}, {"warnings", t.warnings}};
  return writePrivateFile(path(t.id, ".json"), j.dump(1, ' ', false, json::error_handler_t::replace) + "\n") ? "" : path(t.id, ".json") + ": cannot write";
}

std::string TakeStore::writeManifest(const std::string& bytes) {
  std::string now;
  if (std::string why = readManifest(now); !why.empty()) return why;
  if (now != manifest_) return "the voice bank changed on disk: read it again";
  if (!writePrivateFile(cfg_.bank + "/manifest.json", bytes)) return cfg_.bank + "/manifest.json: cannot write";
  return load();
}

std::string TakeStore::add(NewTake n, Take& out) {
  if (!safeId(n.clipId)) return "no clip";
  if (!validSpeaker(n.speaker.id)) return "no speaker ID";
  if (n.raw.empty()) return "nothing was recorded";
  if (std::string why = prepare(cfg_.pending); !why.empty()) return why;
  Take t;
  t.clipId = n.clipId;
  t.words = n.words;
  t.speaker = n.speaker;
  t.recordedMs = n.recordedMs;
  t.samples = n.raw.size();
  t.warnings = std::move(n.warnings);
  std::error_code ec;
  for (const auto& x : takes_)
    if (x.clipId == n.clipId) t.number = std::max(t.number, x.number);
  do {
    ++t.number;
    char b[8];
    std::snprintf(b, sizeof b, "__%03d", t.number);
    t.id = n.clipId + b;
  } while (fs::exists(path(t.id, ".wav"), ec) || fs::exists(path(t.id, ".json"), ec));  // never overwrite a recording
  TrimConfig c = cfg_.trim;
  c.noiseFloorDb = n.noiseFloorDb;
  TrimResult r;
  std::string error;
  if (trimClip(n.raw, kRate, c, r, error)) {
    t.trimStart = r.start;
    t.trimEnd = r.end;
    t.speechDb = r.speechDb;
    t.gainDb = r.gainDb;
    t.durationMs = r.samples.size() * 1000 / kRate;
    t.warnings.insert(t.warnings.end(), r.warnings.begin(), r.warnings.end());
  } else {  // kept all the same: the instructor decides
    t.trimEnd = n.raw.size();
    t.speechDb = rmsDb(n.raw);
    (void)cutOf(n.raw, t, cfg_.trim);
    t.warnings.push_back(error);
  }
  if (!writePrivateWav(path(t.id, ".wav"), n.raw)) return "cannot write the audio file";
  if (std::string why = writeTake(t); !why.empty()) {
    std::remove(path(t.id, ".wav").c_str());  // ours, written just now
    return why;
  }
  out = t;
  takes_.push_back(std::move(t));
  return "";
}

std::optional<std::vector<std::int16_t>> TakeStore::raw(const std::string& id) const {
  const Take* t = find(id);
  if (!t || !t->samples) return std::nullopt;
  auto w = readWav(path(id, ".wav"));
  if (!w || w->rate != kRate || w->samples.size() != t->samples) return std::nullopt;
  return std::move(w->samples);
}

std::optional<std::vector<std::int16_t>> TakeStore::cut(const std::string& id) const {
  const Take* t = find(id);
  if (!t) return std::nullopt;
  if (!t->file.empty()) {
    auto w = readWav(cfg_.bank + "/" + t->file);
    if (w && w->rate == kRate) return std::move(w->samples);
  }
  const auto r = raw(id);
  if (!r) return std::nullopt;
  Take copy = *t;
  return cutOf(*r, copy, cfg_.trim);
}

std::string TakeStore::setTrim(const std::string& id, double start, double end) {
  Take* t = mutableFind(id);
  if (!t) return "the take is gone";
  const auto r = raw(id);
  if (!r) return "there is no raw recording of this take";
  if (!(start >= 0 && end <= 1 && start < end)) return "the part is empty";
  Take n = *t;
  n.trimStart = std::size_t(start * double(r->size()));
  n.trimEnd = std::min(r->size(), std::size_t(std::ceil(end * double(r->size()))));
  if (n.trimEnd - n.trimStart < std::size_t(kRate) * std::size_t(cfg_.trim.minSpeechMs) / 1000) return "the part is too short";
  const auto samples = cutOf(*r, n, cfg_.trim);
  if (n.file.empty()) {
    if (std::string why = writeTake(n); !why.empty()) return why;
    *t = n;
    return "";
  }
  // In the bank: a new file first, then the manifest, then the old file goes.
  if (std::string why = prepare(cfg_.bank); !why.empty()) return why;
  std::error_code ec;
  const std::string old = n.file;
  for (int k = 2;; ++k) {
    n.file = id + "-" + std::to_string(k) + ".wav";
    if (!fs::exists(cfg_.bank + "/" + n.file, ec)) break;
  }
  if (!writePrivateWav(cfg_.bank + "/" + n.file, samples)) return "cannot write the clip";
  json m = json::parse(manifest_);
  if (json* c = clipIn(m, n.clipId))
    for (auto& x : (*c)["takes"])
      if (takeId(x) == id) {
        x["id"] = id;
        x["file"] = n.file;
        x["durationMs"] = n.durationMs;
        x["trimStart"] = n.trimStart;
        x["trimEnd"] = n.trimEnd;
        x["gainDb"] = tenth(n.gainDb);
      }
  if (std::string why = writeManifest(m.dump(1) + "\n"); !why.empty()) return why;
  std::remove((cfg_.bank + "/" + old).c_str());
  if (std::string why = writeTake(n); !why.empty()) return why;
  return load();
}

std::string TakeStore::accept(const std::string& id, bool primary, const std::string& words) {
  const Take* t = find(id);
  if (!t) return "the take is gone";
  if (words.empty()) return "no scenario needs this clip";
  if (t->words != words) return "this take says \"" + t->words + "\", the data says \"" + words + "\"";
  Take n = *t;
  if (std::string why = prepare(cfg_.bank); !why.empty()) return why;
  if (n.file.empty()) {
    const auto r = raw(id);
    if (!r) return "the raw recording cannot be read";
    const auto samples = cutOf(*r, n, cfg_.trim);
    std::error_code ec;
    n.file = id + ".wav";
    for (int k = 2; fs::exists(cfg_.bank + "/" + n.file, ec); ++k) n.file = id + "-" + std::to_string(k) + ".wav";  // never overwrite
    if (!writePrivateWav(cfg_.bank + "/" + n.file, samples)) return "cannot write the clip";
  }
  json m = manifest_.empty() ? json{{"schemaVersion", 1}, {"voice", cfg_.voice}, {"sampleRate", kRate}, {"placeholder", true}, {"clips", json::array()}}
                             : json::parse(manifest_);
  json* c = clipIn(m, n.clipId);
  if (!c) {
    m["clips"].push_back(json{{"id", n.clipId}, {"words", words}, {"takes", json::array()}});
    c = &m["clips"].back();
  }
  const bool otherWords = text(*c, "words") != words;
  json list = json::array();
  for (auto& x : (*c)["takes"]) {
    if (takeId(x) == id) continue;
    const std::string kind = text(x, "take");
    const bool synthetic = x.contains("synthetic") || !x.contains("speaker");
    if (kind != "rejected" && otherWords) x["take"] = "rejected";
    else if (kind == "primary" && primary) x["take"] = synthetic && !n.synthetic ? "rejected" : "alternate";
    list.push_back(x);
  }
  json e{{"id", id}, {"file", n.file}, {"take", primary ? "primary" : "alternate"}};
  if (n.synthetic) {
    e["synthetic"] = true;
  } else {
    e["speaker"] = n.speaker.id;
    e["pilotVoice"] = toJson(n.speaker.pilotVoice);
  }
  e["durationMs"] = n.durationMs;
  e["trimStart"] = n.trimStart;
  e["trimEnd"] = n.trimEnd;
  e["speechDb"] = tenth(n.speechDb);
  e["gainDb"] = tenth(n.gainDb);
  list.push_back(std::move(e));
  (*c)["words"] = words;
  (*c)["takes"] = std::move(list);
  return writeManifest(m.dump(1) + "\n");
}

std::string TakeStore::reject(const std::string& id) {
  Take* t = mutableFind(id);
  if (!t) return "the take is gone";
  if (t->state == State::Rejected) return "";
  if (t->file.empty()) {
    Take n = *t;
    n.state = State::Rejected;
    if (std::string why = writeTake(n); !why.empty()) return why;
    *t = n;
    return "";
  }
  json m = json::parse(manifest_);
  if (json* c = clipIn(m, t->clipId))
    for (auto& x : (*c)["takes"])
      if (takeId(x) == id) x["take"] = "rejected";
  return writeManifest(m.dump(1) + "\n");
}

std::string TakeStore::remove(const std::string& id, bool withdrawn, const std::string& role, const std::string& when) {
  const Take* found = find(id);
  if (!found) return "the take is gone";
  const Take t = *found;
  if (!withdrawn && (t.state == State::Primary || t.state == State::Alternate)) return "this take is in use: reject it first";
  if (std::string why = prepare(cfg_.pending); !why.empty() && why.rfind("disk", 0) != 0) return why;
  const json line{{"when", when}, {"take", t.id}, {"clip", t.clipId}, {"speaker", t.speaker.id}, {"state", nameOf(t.state)}, {"withdrawn", withdrawn}, {"role", role}};
  if (!appendPrivateLine(cfg_.pending + "/deletions.log", line.dump(-1, ' ', false, json::error_handler_t::replace)))
    return "the deletion cannot be logged: nothing was deleted";
  if (!t.file.empty()) {
    json m = json::parse(manifest_);
    if (json* c = clipIn(m, t.clipId)) {
      json list = json::array();
      for (const auto& x : (*c)["takes"])
        if (takeId(x) != id) list.push_back(x);
      (*c)["takes"] = std::move(list);
    }
    if (std::string why = writeManifest(m.dump(1) + "\n"); !why.empty()) return why;
    std::remove((cfg_.bank + "/" + t.file).c_str());
  }
  if (t.samples) {
    std::remove(path(id, ".json").c_str());
    std::remove(path(id, ".wav").c_str());
  }
  return load();
}

}  // namespace atc
