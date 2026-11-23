#include "storage/RecordingStore.h"

#include <sys/stat.h>
#include <sys/statvfs.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "audio/WavFile.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace atc {
namespace {
bool validKey(const std::string& k) {  // also keeps paths inside the folder
  return !k.empty() && k.size() < 80 &&
         std::all_of(k.begin(), k.end(), [](unsigned char c) { return std::isalnum(c) || c == '-'; });
}
long fsFreeMb(const std::string& dir) {
  struct statvfs s {};
  if (statvfs(dir.c_str(), &s) != 0) return -1;
  return long(s.f_bavail / 1024 * s.f_frsize / 1024);
}
json toJson(const StoredRecording& r) {
  json rs = json::array();
  for (const auto& x : r.results) {
    json words = json::array();
    for (const auto& w : x.words) words.push_back({{"text", w.text}, {"conf", w.conf}});
    json cfg = json::parse(x.modelConfig, nullptr, false);
    if (cfg.is_discarded()) cfg = x.modelConfig;
    rs.push_back({{"stt_model_id", x.modelId}, {"stt_model_name", x.modelName}, {"stt_model_config", cfg},
                  {"raw", x.text}, {"words", words}, {"release_to_text_ms", x.releaseToTextMs}});
  }
  json j{{"format", 1}, {"kind", "speech-lab"}, {"started_ms", r.startedMs}, {"tx", r.tx}, {"ptt_source", r.pttSource},
         {"end", r.end}, {"gap", r.gap}, {"rate", r.rate}, {"samples", r.samples}, {"press_sample", r.pressSample},
         {"end_sample", r.endSample}, {"recognition", rs}};
  json c{{"empty", r.report.empty}, {"overload", r.report.overload}, {"spoke_before_keying", r.report.spokeBeforeKeying},
         {"released_early", r.report.releasedEarly}, {"peak_dbfs", r.report.peakDbfs}};
  if (r.report.keyToSpeechMs) c["key_to_speech_ms"] = *r.report.keyToSpeechMs;
  j["checks"] = c;
  return j;
}
StoredRecording fromJson(const json& j, const std::string& key) {  // throws on a wrong shape
  if (j.at("format").get<int>() != 1) throw std::runtime_error("format");
  StoredRecording r;
  r.key = key;
  j.at("started_ms").get_to(r.startedMs);
  j.at("tx").get_to(r.tx);
  j.at("ptt_source").get_to(r.pttSource);
  j.at("end").get_to(r.end);
  j.at("gap").get_to(r.gap);
  j.at("rate").get_to(r.rate);
  j.at("samples").get_to(r.samples);
  j.at("press_sample").get_to(r.pressSample);
  j.at("end_sample").get_to(r.endSample);
  const json& c = j.at("checks");
  c.at("empty").get_to(r.report.empty);
  c.at("overload").get_to(r.report.overload);
  c.at("spoke_before_keying").get_to(r.report.spokeBeforeKeying);
  c.at("released_early").get_to(r.report.releasedEarly);
  c.at("peak_dbfs").get_to(r.report.peakDbfs);
  if (c.contains("key_to_speech_ms")) r.report.keyToSpeechMs = c["key_to_speech_ms"].get<int>();
  for (const json& x : j.at("recognition")) {
    StoredResult s;
    x.at("stt_model_id").get_to(s.modelId);
    x.at("stt_model_name").get_to(s.modelName);
    s.modelConfig = x.at("stt_model_config").is_string() ? x["stt_model_config"].get<std::string>() : x["stt_model_config"].dump();
    x.at("raw").get_to(s.text);
    x.at("release_to_text_ms").get_to(s.releaseToTextMs);
    for (const json& w : x.at("words")) s.words.push_back({w.at("text").get<std::string>(), w.at("conf").get<double>()});
    r.results.push_back(std::move(s));
  }
  return r;
}
}  // namespace

RecordingStore::RecordingStore(StoreConfig cfg, Saved onSaved)
    : cfg_(std::move(cfg)), saved_(std::move(onSaved)), thread_([this] { run(); }) {}

RecordingStore::~RecordingStore() {
  {
    std::lock_guard l(m_);
    stop_ = true;
  }
  cv_.notify_one();
  thread_.join();
}

std::string RecordingStore::makeKey(std::int64_t startedMs, int tx) {
  const std::time_t t = startedMs / 1000;
  std::tm g{};
  gmtime_r(&t, &g);
  char b[64];
  std::snprintf(b, sizeof b, "%04d%02d%02dT%02d%02d%02dZ-%03d-tx%d", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour,
                g.tm_min, g.tm_sec, int(startedMs % 1000), tx);
  return b;
}

bool RecordingStore::post(std::function<void()> f, bool always) {
  {
    std::lock_guard l(m_);
    if (!always && q_.size() >= cfg_.maxQueue) return false;
    q_.push_back(std::move(f));
  }
  cv_.notify_one();
  return true;
}

void RecordingStore::run() {
  for (;;) {
    std::function<void()> f;
    {
      std::unique_lock l(m_);
      cv_.wait(l, [this] { return stop_ || !q_.empty(); });
      if (q_.empty()) return;
      f = std::move(q_.front());
      q_.pop_front();
    }
    f();
  }
}

std::string RecordingStore::prepare() {
  std::error_code ec;
  if (!fs::is_directory(cfg_.dir, ec)) {
    fs::create_directories(fs::path(cfg_.dir).parent_path(), ec);
    if (mkdir(cfg_.dir.c_str(), 0700) != 0) return "cannot create " + cfg_.dir;
  }
  const long free = cfg_.freeMb ? cfg_.freeMb() : fsFreeMb(cfg_.dir);
  if (free >= 0 && free < cfg_.minFreeMb)
    return "disk nearly full: " + std::to_string(free) + " MB free, " + std::to_string(cfg_.minFreeMb) + " MB kept";
  return "";
}

bool RecordingStore::writeMeta(const StoredRecording& r) {
  const std::string to = path(r.key, ".json"), tmp = to + ".part";
  {
    std::ofstream o(tmp, std::ios::trunc);
    o << toJson(r).dump(1, ' ', false, json::error_handler_t::replace) << '\n';
    o.flush();
    if (!o) {
      std::remove(tmp.c_str());
      return false;
    }
  }
  chmod(tmp.c_str(), 0600);
  return std::rename(tmp.c_str(), to.c_str()) == 0;
}

std::optional<StoredRecording> RecordingStore::readMeta(const std::string& key) const {
  if (!validKey(key)) return std::nullopt;
  std::ifstream i(path(key, ".json"));
  try {
    return fromJson(json::parse(i), key);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

void RecordingStore::save(StoredRecording rec, std::vector<std::int16_t> audio) {
  const std::string key = rec.key;
  const auto report = [this, key](const std::string& why) {
    if (saved_) saved_(key, why.empty(), why);
  };
  const bool queued = post([this, report, rec = std::move(rec), audio = std::move(audio)]() mutable {
    if (!validKey(rec.key)) return report("invalid name");
    if (const std::string why = prepare(); !why.empty()) return report(why);
    const std::string wav = path(rec.key, ".wav");
    std::error_code ec;
    if (fs::exists(wav, ec)) return report("already exists");  // never overwrite a recording
    rec.samples = int(audio.size());
    if (!writeWav(wav, rec.rate, audio)) return report("cannot write the audio file");
    chmod(wav.c_str(), 0600);
    if (!writeMeta(rec)) {
      std::remove(wav.c_str());  // ours, written just now
      return report("cannot write the description file");
    }
    report("");
  }, false);
  if (!queued) report("disk too slow: too many recordings waiting");
}

void RecordingStore::addResult(std::string key, StoredResult result) {
  post([this, key = std::move(key), result = std::move(result)]() mutable {
    auto r = readMeta(key);
    if (!r) return;  // the save failed and was reported
    r->results.push_back(std::move(result));
    if (!writeMeta(*r) && saved_) saved_(key, false, "cannot store the recognised text");
  }, true);
}

void RecordingStore::list(std::size_t max, std::function<void(std::vector<StoredRecording>, int)> done) {
  post([this, max, done = std::move(done)] {
    std::vector<std::string> keys;
    std::error_code ec;
    for (fs::directory_iterator it(cfg_.dir, ec), end; !ec && it != end; it.increment(ec))
      if (it->path().extension() == ".json") keys.push_back(it->path().stem().string());
    std::sort(keys.rbegin(), keys.rend());  // the name starts with the UTC time
    std::vector<StoredRecording> out;
    int bad = 0;
    for (const auto& k : keys) {
      if (out.size() >= max) break;
      if (auto r = readMeta(k)) out.push_back(std::move(*r));
      else ++bad;
    }
    done(std::move(out), bad);
  }, true);
}

void RecordingStore::loadAudio(std::string key, std::function<void(std::optional<std::vector<std::int16_t>>)> done) {
  post([this, key = std::move(key), done = std::move(done)] {
    std::optional<std::vector<std::int16_t>> out;
    if (validKey(key))
      if (auto w = readWav(path(key, ".wav")); w && w->rate == 16000) out = std::move(w->samples);
    done(std::move(out));
  }, true);
}

}  // namespace atc
