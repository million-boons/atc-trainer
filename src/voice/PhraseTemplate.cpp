#include "voice/PhraseTemplate.h"

#include <algorithm>
#include <cctype>

namespace atc {
namespace {

bool isPunctuation(char c) { return c == ',' || c == '.' || c == ';' || c == ':' || c == '?' || c == '!'; }

std::string trim(const std::string& s) {
  const auto a = s.find_first_not_of(" \t");
  if (a == std::string::npos) return {};
  return s.substr(a, s.find_last_not_of(" \t") - a + 1);
}

std::string slug(const std::string& s) {
  std::string r;
  for (const unsigned char c : s) {
    if (c < 128 && std::isalnum(c))
      r += static_cast<char>(std::toupper(c));
    else if (!r.empty() && r.back() != '_')
      r += '_';
  }
  while (!r.empty() && r.back() == '_') r.pop_back();
  return r;
}

std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string at(ClipPosition p) { return "@" + std::string(toString(p)); }

// Values a generator can take, from the airfield's pools.
std::vector<std::string> poolValues(const Airfield& a, const std::string& pool) {
  std::vector<std::string> v;
  if (pool == "runways") v = a.runways;
  if (pool == "taxiways")
    for (const auto& x : a.taxiways) v.push_back(x.id);
  if (pool == "aircraftTypes")
    for (const auto& x : a.aircraftTypes) v.push_back(x.id);
  if (pool == "callsigns")
    for (const auto& x : a.callsigns) v.push_back(x.id);
  if (pool == "frequencies")  // every unit's: a few clips more than one scenario needs
    for (const auto& u : a.units) v.insert(v.end(), u.frequencies.begin(), u.frequencies.end());
  return v;
}

}  // namespace

bool parsePhraseTemplate(const std::string& text, std::vector<PhraseSegment>& out, std::string& error) {
  out.clear();
  std::vector<std::string> slots;
  if (!templateSlots(text, slots)) {
    error = "bad {slot}";
    return false;
  }
  std::string words;
  const auto flush = [&] {
    const std::string w = trim(words);
    words.clear();
    if (w.empty()) return true;
    if (slug(w).empty()) {
      error = "no words in: " + w;
      return false;
    }
    PhraseSegment s;
    s.text = w;
    out.push_back(std::move(s));
    return true;
  };
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '{') {
      if (!flush()) return false;
      const auto end = text.find('}', i);
      PhraseSegment s;
      s.dynamic = true;
      s.text = text.substr(i + 1, end - i - 1);
      out.push_back(std::move(s));
      i = end;
    } else if (isPunctuation(c)) {
      if (!flush()) return false;
      if (out.empty()) {
        error = "starts with punctuation";
        return false;
      }
      out.back().commaAfter = true;
    } else {
      words += c;
    }
  }
  if (!flush()) return false;
  if (out.empty()) {
    error = "nothing to say";
    return false;
  }
  for (std::size_t i = 0; i < out.size(); ++i)
    out[i].position = i + 1 == out.size() ? ClipPosition::Final : i == 0 ? ClipPosition::Initial : ClipPosition::Medial;
  out.back().commaAfter = false;
  return true;
}

std::vector<std::string> lintPhrase(const std::vector<PhraseSegment>& segments, const Airfield& a) {
  std::vector<std::string> warnings;
  for (const auto& s : segments) {
    if (s.dynamic) continue;
    if (std::any_of(s.text.begin(), s.text.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
      warnings.push_back("\"" + s.text + "\" contains digits: should be a {slot}");
    const std::string words = lower(s.text);
    for (const auto& c : a.callsigns)
      if (words.find(lower(c.spoken)) != std::string::npos || words.find(lower(c.id)) != std::string::npos)
        warnings.push_back("\"" + s.text + "\" contains callsign " + c.id + ": should be a {slot}");
  }
  return warnings;
}

std::string phraseClipId(const std::string& words, ClipPosition p) { return "PH_" + slug(words) + at(p); }

std::string valueClipId(const std::string& pool, const std::string& value, ClipPosition p) {
  std::string name = pool;
  if (pool == "runways") name = "RUNWAY";
  if (pool == "taxiways") name = "TAXIWAY";
  if (pool == "aircraftTypes") name = "AIRCRAFT_TYPE";
  if (pool == "callsigns") name = "CALLSIGN";
  if (pool == "frequencies") name = "FREQUENCY";
  return name + "_" + slug(value) + at(p);
}

std::vector<std::string> clipSequence(const std::string& text, const Scenario& s, const ParamValues& values, std::string& error) {
  std::vector<PhraseSegment> segments;
  if (!parsePhraseTemplate(text, segments, error)) return {};
  std::vector<std::string> ids;
  for (const auto& x : segments) {
    if (x.dynamic) {
      const auto g = s.generators.find(x.text);
      const auto v = values.find(x.text);
      if (g == s.generators.end() || v == values.end()) {
        error = "no value for {" + x.text + "}";
        return {};
      }
      ids.push_back(valueClipId(g->second.pool, v->second, x.position));
    } else {
      ids.push_back(phraseClipId(x.text, x.position));
    }
    if (x.commaAfter) ids.emplace_back(kCommaPause);
  }
  return ids;
}

bool requiredClips(const Exercise& e, std::map<std::string, RequiredClip>& out, std::vector<std::string>& errors) {
  out.clear();
  errors.clear();
  for (const auto& [id, reply] : e.replies) {
    std::vector<PhraseSegment> segments;
    std::string error;
    if (!parsePhraseTemplate(reply.text, segments, error)) {
      errors.push_back(id + ": " + error);
      continue;
    }
    for (const auto& x : segments) {
      if (!x.dynamic) {
        const std::string clip = phraseClipId(x.text, x.position);
        out.emplace(clip, RequiredClip{clip, x.text, "", "", id});
        continue;
      }
      const auto g = e.scenario.generators.find(x.text);
      if (g == e.scenario.generators.end()) {
        errors.push_back(id + ": no generator for {" + x.text + "}");
        continue;
      }
      for (const auto& v : poolValues(e.airfield, g->second.pool)) {
        const std::string clip = valueClipId(g->second.pool, v, x.position);
        out.emplace(clip, RequiredClip{clip, "", g->second.pool, v, id});
      }
    }
  }
  return errors.empty();
}

}  // namespace atc
