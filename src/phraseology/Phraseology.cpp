#include "phraseology/Phraseology.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <set>
#include <sstream>
#include <tuple>

namespace atc {
namespace {
using nlohmann::json;

std::vector<std::string> split(const std::string& s) {
  std::istringstream in(s);
  std::vector<std::string> v;
  for (std::string w; in >> w;) v.push_back(w);
  return v;
}
bool isRef(const std::string& w) { return w.size() > 2 && w.front() == '{' && w.back() == '}'; }
std::string refName(const std::string& w) { return w.substr(1, w.size() - 2); }

// Not std::uniform_int_distribution: its result differs between libraries.
std::size_t pick(std::mt19937& g, std::size_t n) { return g() % n; }

void expand(const Phraseology& p, const std::string& form, std::mt19937& g, std::vector<std::string>& out) {
  for (const auto& w : split(form)) {
    if (!isRef(w)) {
      out.push_back(w);
    } else if (const auto s = p.slots.find(refName(w)); s != p.slots.end()) {
      expand(p, s->second[pick(g, s->second.size())], g, out);
    } else {
      const auto& values = p.pools.at(refName(w));
      const auto& spoken = values[pick(g, values.size())];
      for (auto& x : split(spoken[pick(g, spoken.size())])) out.push_back(std::move(x));
    }
  }
}
std::string join(const std::vector<std::string>& v) {
  std::string o;
  for (const auto& w : v) o += (o.empty() ? "" : " ") + w;
  return o;
}
bool strings(const json& j, std::vector<std::string>& out) {
  if (!j.is_array() || j.empty()) return false;
  for (const auto& x : j) {
    if (!x.is_string()) return false;
    const std::string s = x;
    if (split(s).empty() || std::any_of(s.begin(), s.end(), [](unsigned char c) { return std::isupper(c) && c < 128; }) )
      if (s.find('{') == std::string::npos) return false;  // spoken text is lower case
    out.push_back(s);
  }
  return true;
}
}  // namespace

bool loadPhraseology(const std::string& file, Phraseology& out, std::string& error) {
  std::ifstream f(file);
  const auto fail = [&](const std::string& why) {
    error = file + ": " + why;
    return false;
  };
  if (!f) return fail("cannot read");
  const json j = json::parse(f, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return fail("not valid JSON");
  if (j.value("schemaVersion", 0) != 1) return fail("schemaVersion must be 1");
  for (const char* k : {"validation", "digits", "letters", "pools", "slots"})
    if (!j.contains(k) || !j[k].is_object()) return fail(std::string(k) + ": missing");
  const auto st = j["validation"].find("status");
  if (st == j["validation"].end() || (*st != "placeholder" && *st != "validated"))
    return fail("validation/status must be placeholder or validated");
  Phraseology p;
  p.validated = *st == "validated";

  for (const auto& [key, pool, count] : {std::tuple{"digits", "digit", 10}, std::tuple{"letters", "letter", 26}}) {
    for (int i = 0; i < count; ++i) {
      const std::string id(1, static_cast<char>((count == 10 ? '0' : 'A') + i));
      std::vector<std::string> s;
      if (!j[key].contains(id) || !strings(j[key][id], s)) return fail(std::string(key) + "/" + id + ": spoken forms missing");
      p.pools[pool].push_back(std::move(s));
    }
  }
  for (const auto& [name, values] : j["pools"].items()) {
    if (p.pools.count(name)) return fail("pools/" + name + ": reserved name");
    if (!values.is_array() || values.empty()) return fail("pools/" + name + ": empty");
    for (const auto& v : values) {
      std::vector<std::string> s;
      if (!v.is_object() || !v.contains("id") || !v.contains("spoken") || !strings(v["spoken"], s))
        return fail("pools/" + name + ": each value needs id and lower-case spoken forms");
      p.pools[name].push_back(std::move(s));
    }
  }
  for (const auto& [name, forms] : j["slots"].items()) {
    if (p.pools.count(name)) return fail("slots/" + name + ": a pool has this name");
    if (!strings(forms, p.slots[name])) return fail("slots/" + name + ": no forms");
    for (const auto& form : p.slots[name])
      for (const auto& w : split(form))
        if (w.find('{') != std::string::npos && (!isRef(w) || !p.pools.count(refName(w))))
          return fail("slots/" + name + ": unknown pool " + w);  // slots hold pools only: no cycles
  }
  if (!j.contains("intents") || !j["intents"].is_array() || j["intents"].empty()) return fail("intents: missing");
  std::set<std::string> ids;
  for (const auto& i : j["intents"]) {
    if (!i.is_object() || !i.contains("id") || !i["id"].is_string()) return fail("intents: entry without id");
    Phraseology::Intent in{i["id"], {}};
    if (!ids.insert(in.id).second) return fail("intents/" + in.id + ": used twice");
    if (!i.contains("examples") || !strings(i["examples"], in.examples)) return fail("intents/" + in.id + ": no examples");
    for (const auto& e : in.examples)
      for (const auto& w : split(e))
        if (w.find('{') != std::string::npos && (!isRef(w) || (!p.slots.count(refName(w)) && !p.pools.count(refName(w)))))
          return fail("intents/" + in.id + ": unknown slot " + w);
    p.intents.push_back(std::move(in));
  }
  out = std::move(p);
  return true;
}

std::vector<std::string> expandPhrases(const Phraseology& full, const ExpandOptions& o, std::string* error) {
  Phraseology p = full;
  const std::set<std::string> ex(o.exclude.begin(), o.exclude.end());
  const auto uses = [&](const std::string& text) {
    for (const auto& w : split(text))
      if (ex.count(w)) return true;
    return false;
  };
  const auto fail = [&](const std::string& why) {
    if (error) *error = why;
    return std::vector<std::string>{};
  };
  if (!ex.empty()) {
    for (auto& [name, values] : p.pools)
      for (auto& spoken : values) {
        const std::string first = spoken.front();
        std::erase_if(spoken, uses);
        if (spoken.empty()) return fail("no spoken form left for \"" + first + "\" in " + name);
      }
    for (auto& [name, forms] : p.slots)
      for (const auto& s : forms)
        if (uses(s)) return fail("slot " + name + " uses an excluded word: " + s);
    for (const auto& i : p.intents)
      for (const auto& e : i.examples)
        if (uses(e)) return fail("intent " + i.id + " uses an excluded word: " + e);
  }
  std::mt19937 g(o.seed);
  std::vector<std::string> out;
  for (const auto& in : p.intents) {
    const int times = o.perExample * (in.id == o.boostIntent ? std::max(1, o.boost) : 1);
    for (const auto& e : in.examples)
      for (int n = 0; n < times; ++n) {
        std::vector<std::string> w;
        expand(p, e, g, w);
        if (o.unkEvery > 0 && out.size() % static_cast<std::size_t>(o.unkEvery) == 0)
          w.insert(w.begin() + static_cast<std::ptrdiff_t>(pick(g, w.size() + 1)), "[unk]");
        out.push_back(join(w));
      }
  }
  return out;
}

std::vector<std::string> phraseologyWords(const Phraseology& p) {
  std::vector<std::string> out;
  std::set<std::string> seen;
  const auto add = [&](const std::string& text) {
    for (const auto& w : split(text))
      if (w.find('{') == std::string::npos && seen.insert(w).second) out.push_back(w);
  };
  for (const auto& [n, values] : p.pools)
    for (const auto& v : values)
      for (const auto& s : v) add(s);
  for (const auto& [n, forms] : p.slots)
    for (const auto& s : forms) add(s);
  for (const auto& i : p.intents)
    for (const auto& e : i.examples) add(e);
  return out;
}

std::string whisperPrompt(const Phraseology& p) {
  const auto cap = [](std::string s) {
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
  };
  std::string o = "Air traffic control radio.";
  for (const auto& [name, values] : p.pools) {
    std::string list;
    for (const auto& v : values)
      for (const auto& s : v) list += (list.empty() ? " " : ", ") + (name == "digit" || name == "side" ? s : cap(s));
    o += list + ".";
  }
  // Fixed wording between the slots, each once.
  std::set<std::string> seen;
  std::string words;
  const auto add = [&](const std::string& form) {
    std::string run;
    for (const auto& w : split(form + " {end}")) {
      if (!isRef(w)) {
        run += (run.empty() ? "" : " ") + w;
      } else if (!run.empty()) {
        if (seen.insert(run).second) words += (words.empty() ? " " : ", ") + run;
        run.clear();
      }
    }
  };
  for (const auto& i : p.intents)
    for (const auto& e : i.examples) add(e);
  for (const auto& [n, forms] : p.slots)
    for (const auto& s : forms) add(s);
  return o + words + ".";
}

}  // namespace atc
