#include "voice/ClipList.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace atc {
namespace {
using nlohmann::json;

// {"0": ["zero"], ...}: the first form of each.
bool firstForms(const json& j, std::size_t count, std::map<std::string, std::string>& out) {
  if (!j.is_object()) return false;
  for (const auto& [key, forms] : j.items())
    if (forms.is_array() && !forms.empty() && forms[0].is_string()) out[key] = forms[0].get<std::string>();
  return out.size() == count;
}

void readSides(const json& j, std::map<std::string, std::string>& out) {
  const auto pools = j.find("pools");
  if (pools == j.end() || !pools->is_object()) return;
  const auto side = pools->find("side");
  if (side == pools->end() || !side->is_array()) return;
  for (const auto& x : *side) {
    if (!x.is_object()) continue;
    const auto id = x.find("id"), spoken = x.find("spoken");
    if (id == x.end() || !id->is_string() || spoken == x.end() || !spoken->is_array() || spoken->empty() || !(*spoken)[0].is_string()) continue;
    out[id->get<std::string>()] = (*spoken)[0].get<std::string>();
  }
}

}  // namespace

bool loadSpokenForms(const std::string& dir, SpokenForms& out, std::string& error) {
  std::vector<fs::path> files;
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    if (it->path().extension() == ".json") files.push_back(it->path());
  std::sort(files.begin(), files.end());
  for (const auto& file : files) {
    std::ifstream in(file);
    const json j = json::parse(in, nullptr, false);
    if (!j.is_object() || !j.contains("digits") || !j.contains("letters")) continue;
    SpokenForms f;
    if (!firstForms(j["digits"], 10, f.digits) || !firstForms(j["letters"], 26, f.letters)) {
      error = file.string() + ": digits or letters incomplete";
      return false;
    }
    readSides(j, f.sides);
    out = std::move(f);
    return true;
  }
  error = dir + ": no file with digits and letters";
  return false;
}

std::string spokenValue(const Airfield& a, const SpokenForms& f, const std::string& pool, const std::string& value, std::string& error) {
  if (pool == "callsigns") {
    for (const auto& c : a.callsigns)
      if (c.id == value && !c.spoken.empty()) return c.spoken;
    error = value + ": no spoken form";
    return {};
  }
  std::string r;
  for (std::size_t i = 0; i < value.size(); ++i) {
    const char c = value[i];
    if (c == '-' || c == ' ') continue;
    const std::string key(1, static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    const bool side = pool == "runways" && i > 0 && i + 1 == value.size() && std::isalpha(static_cast<unsigned char>(c));
    const auto& forms = side ? f.sides : std::isdigit(static_cast<unsigned char>(c)) ? f.digits : f.letters;
    const auto w = forms.find(key);
    if (w == forms.end()) {
      error = value + ": cannot say '" + key + "'";
      return {};
    }
    r += (r.empty() ? "" : " ") + w->second;
  }
  if (r.empty()) error = "nothing to say in a value of " + pool;
  return r;
}

std::string ClipLine::prompt() const { return words + (id.ends_with("@final") ? "." : ","); }

bool clipList(const Exercise& e, const SpokenForms& f, std::vector<ClipLine>& out, std::vector<std::string>& errors) {
  out.clear();
  std::map<std::string, RequiredClip> need;
  requiredClips(e, need, errors);
  for (const auto& [id, c] : need) {
    std::string words = c.words, error;
    if (words.empty()) words = spokenValue(e.airfield, f, c.pool, c.value, error);
    if (words.empty())
      errors.push_back(id + ": " + error);
    else
      out.push_back({id, words});
  }
  return errors.empty();
}

bool checkWords(const Exercise& e, const SpokenForms& f, const VoiceBank& bank, std::vector<LoadError>& errors) {
  std::vector<ClipLine> lines;
  std::vector<std::string> bad;
  if (!clipList(e, f, lines, bad)) {
    for (auto& b : bad) errors.push_back({e.scenario.id, "/", b});
    return false;
  }
  bool ok = true;
  for (const auto& l : lines) {
    const auto* c = bank.find(l.id);
    if (!c || c->words.empty() || c->words == l.words) continue;
    errors.push_back({bank.manifestFile(), "/clips", l.id + " says \"" + c->words + "\", the data says \"" + l.words + "\""});
    ok = false;
  }
  return ok;
}

}  // namespace atc
