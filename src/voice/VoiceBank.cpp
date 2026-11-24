#include "voice/VoiceBank.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>
#include <utility>

#include "audio/WavFile.h"
#include "voice/PhraseTemplate.h"

namespace fs = std::filesystem;

namespace atc {
namespace {
using nlohmann::json;

std::string text(const json& j, const char* key) {
  const auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// A file in the voice folder or below it.
bool safeName(const std::string& f) {
  const fs::path p(f);
  if (f.empty() || p.is_absolute()) return false;
  for (const auto& part : p)
    if (part == "..") return false;
  return true;
}

}  // namespace

bool VoiceBank::load(const std::string& dir, std::vector<LoadError>& errors) {
  errors.clear();
  const std::string file = (fs::path(dir) / "manifest.json").string();
  const auto fail = [&](const std::string& path, const std::string& what) { errors.push_back({file, path, what}); };
  std::ifstream f(file, std::ios::binary);
  if (!f) {
    fail("/", "cannot read");
    return false;
  }
  const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  const json j = json::parse(bytes, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    fail("/", "not valid JSON");
    return false;
  }
  VoiceBank b;
  b.manifest_ = file;
  const auto version = j.find("schemaVersion"), rate = j.find("sampleRate"), placeholder = j.find("placeholder"), clips = j.find("clips");
  if (version == j.end() || *version != json(1)) fail("/schemaVersion", "must be 1");
  if (rate == j.end() || *rate != json(kRate)) fail("/sampleRate", "must be 16000");
  b.voice_ = text(j, "voice");
  if (b.voice_.empty()) fail("/voice", "missing");
  if (placeholder == j.end() || !placeholder->is_boolean())
    fail("/placeholder", "must be true or false");
  else
    b.placeholder_ = placeholder->get<bool>();
  if (clips == j.end() || !clips->is_array()) {
    fail("/clips", "must be a list");
    return false;
  }
  std::set<std::string> seen;
  for (std::size_t i = 0; i < clips->size(); ++i) {
    const json& c = (*clips)[i];
    const std::string path = "/clips/" + std::to_string(i);
    const std::string id = c.is_object() ? text(c, "id") : std::string();
    const auto takes = c.is_object() ? c.find("takes") : c.end();
    if (id.empty() || takes == c.end() || !takes->is_array()) {
      fail(path, "needs id and a list of takes");
      continue;
    }
    if (!seen.insert(id).second) fail(path, id + " is listed twice");
    std::vector<std::vector<std::int16_t>> primary, alternate;
    for (std::size_t k = 0; k < takes->size(); ++k) {
      const json& t = (*takes)[k];
      const std::string at = path + "/takes/" + std::to_string(k);
      const std::string name = t.is_object() ? text(t, "file") : std::string();
      const std::string kind = t.is_object() ? text(t, "take") : std::string();
      if (kind == "rejected") continue;
      if (kind != "primary" && kind != "alternate") {
        fail(at, id + ": take must be primary, alternate or rejected");
        continue;
      }
      if (!safeName(name)) {
        fail(at, id + ": file must be inside the voice folder");
        continue;
      }
      auto wav = readWav((fs::path(dir) / name).string());
      if (!wav)
        fail(at, id + ": " + name + " is missing or not 16-bit mono PCM");
      else if (wav->rate != kRate || wav->samples.empty())
        fail(at, id + ": " + name + " must be 16 kHz and not empty");
      else
        (kind == "primary" ? primary : alternate).push_back(std::move(wav->samples));
    }
    if (primary.size() > 1) fail(path, id + " has more than one primary take");
    for (auto& a : alternate) primary.push_back(std::move(a));
    if (!primary.empty()) b.clips_[id].takes = std::move(primary);
  }
  if (!errors.empty()) return false;
  *this = std::move(b);
  return true;
}

const VoiceBank::Clip* VoiceBank::find(const std::string& clipId) const {
  const auto it = clips_.find(clipId);
  return it == clips_.end() ? nullptr : &it->second;
}

std::size_t VoiceBank::bytes() const {
  std::size_t n = 0;
  for (const auto& [id, c] : clips_)
    for (const auto& t : c.takes) n += t.size() * sizeof(std::int16_t);
  return n;
}

bool checkCoverage(const Exercise& e, const VoiceBank& bank, std::vector<LoadError>& errors) {
  errors.clear();
  std::map<std::string, RequiredClip> needed;
  std::vector<std::string> bad;
  requiredClips(e, needed, bad);
  for (const auto& b : bad) errors.push_back({e.scenario.id, "/replies", b});
  for (const auto& [id, c] : needed)
    if (!bank.find(id)) errors.push_back({bank.manifestFile(), "/clips", "missing clip " + id + " (reply " + c.replyId + ")"});
  return errors.empty();
}

}  // namespace atc
