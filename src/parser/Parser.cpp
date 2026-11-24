#include "parser/Parser.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <tuple>

#include "bench/Scoring.h"  // normaliseWords

namespace atc {
namespace {
using nlohmann::json;
using Words = ParserRules::Words;

bool wordLists(const json& j, std::vector<Words>& out) {
  if (!j.is_array() || j.empty()) return false;
  for (const auto& x : j) {
    if (!x.is_string()) return false;
    Words w = normaliseText(x.get<std::string>());
    if (w.empty()) return false;
    out.push_back(std::move(w));
  }
  return true;
}
bool names(const json& j, std::vector<std::string>& out) {
  if (!j.is_array()) return false;
  for (const auto& x : j) {
    if (!x.is_string()) return false;
    out.push_back(x);
  }
  return true;
}

// One normalised word and the raw word it came from.
struct Token {
  std::string text;
  double conf = -1;
  std::size_t from = 0, to = 0;  // raw words
  bool used = false;
};
std::string trim(std::string s) {
  const auto edge = [](unsigned char c) { return std::isspace(c) || c == ',' || c == '.' || c == '?' || c == '!'; };
  while (!s.empty() && edge(static_cast<unsigned char>(s.back()))) s.pop_back();
  std::size_t i = 0;
  while (i < s.size() && edge(static_cast<unsigned char>(s[i]))) ++i;
  return s.substr(i);
}
std::string spaces(const std::string& s) {
  std::istringstream in(s);
  std::string o;
  for (std::string w; in >> w;) o += (o.empty() ? "" : " ") + w;
  return o;
}

class Reader {
 public:
  Reader(const SttResult& h, const ParserRules& r, const ParseOptions& o) : rules_(r), opt_(o) {
    raw_ = h.words;
    if (raw_.empty()) {  // text only
      std::istringstream in(h.text);
      for (std::string w; in >> w;) raw_.push_back({w, -1});
    }
    for (std::size_t i = 0; i < raw_.size(); ++i)
      for (auto& t : normaliseWords({raw_[i]})) tokens_.push_back({t.text, t.conf, i, i});
    for (std::size_t i = 0; i + 1 < tokens_.size(); ++i)  // "x ray" in two raw words
      if (tokens_[i].text == "x" && tokens_[i + 1].text == "ray") {
        tokens_[i] = {"xray", std::min(tokens_[i].conf, tokens_[i + 1].conf), tokens_[i].from, tokens_[i + 1].to};
        tokens_.erase(tokens_.begin() + static_cast<std::ptrdiff_t>(i) + 1);
      }
    out_.rawText = spaces(h.text);
    if (out_.rawText.empty())
      for (const auto& w : raw_) out_.rawText += (out_.rawText.empty() ? "" : " ") + spaces(w.text);
    for (const auto& t : tokens_) out_.normalisedText += (out_.normalisedText.empty() ? "" : " ") + t.text;
  }

  ParsedInstruction run() {
    std::vector<const ParserRules::Intent*> found;
    for (const auto& in : rules_.intents) {
      bool any = false;
      for (const auto& c : in.cues)
        for (std::size_t i = 0; i < tokens_.size(); ++i)
          if (matches(c, i)) use(i, c.size()), any = true;
      if (any) found.push_back(&in);
    }
    for (const auto& [name, phrases] : rules_.flags)
      for (const auto& p : phrases)
        for (std::size_t i = 0; i < tokens_.size(); ++i)
          if (matches(p, i)) {
            use(i, p.size());
            set(name, "yes", i, i + p.size());
          }
    for (const auto& [name, slot] : rules_.slots) readSlot(name, slot);

    std::string extra;
    for (const auto& t : tokens_)
      if (!t.used) extra += (extra.empty() ? "" : " ") + t.text;
    if (tokens_.empty()) return unrecognised("nothing heard");
    if (found.empty()) return unrecognised("no known instruction");

    bool doubt = false;
    const auto because = [&](const std::string& why) {
      out_.reasons.push_back(why);
      doubt = true;
    };
    if (found.size() > 1) {
      std::string ids;
      for (const auto* f : found) ids += (ids.empty() ? "" : ", ") + f->id;
      because("several instructions: " + ids);
    } else {
      out_.intent = found[0]->id;
      const auto has = [](const std::vector<std::string>& v, const std::string& x) { return std::find(v.begin(), v.end(), x) != v.end(); };
      for (const auto& [name, f] : out_.fields)
        if (!has(found[0]->required, name) && !has(found[0]->optional, name)) because(name + " does not belong to " + out_.intent);
      for (const auto& r : found[0]->required)
        if (!out_.fields.count(r)) out_.missing.push_back(r);
    }
    for (const auto& [name, f] : out_.fields)
      if (f.uncertain) because(name + " uncertain: " + f.heard);
    if (!extra.empty()) because("unexplained words: " + extra);
    for (const auto& m : out_.missing) out_.reasons.push_back("missing " + m);
    out_.cls = doubt ? RecognitionClass::Ambiguous : out_.missing.empty() ? RecognitionClass::Recognized : RecognitionClass::Partial;
    return std::move(out_);
  }

 private:
  ParsedInstruction unrecognised(const std::string& why) {
    out_.reasons = {why};
    return std::move(out_);
  }
  bool matches(const Words& w, std::size_t i) const {
    if (i + w.size() > tokens_.size()) return false;
    for (std::size_t k = 0; k < w.size(); ++k)
      if (tokens_[i + k].text != w[k]) return false;
    return true;
  }
  void use(std::size_t i, std::size_t n) {
    for (std::size_t k = i; k < i + n; ++k) tokens_[k].used = true;
  }
  // Length and id of a value of the pool at i; length 0 = none.
  std::pair<std::size_t, std::string> valueAt(const std::string& pool, std::size_t i) const {
    std::pair<std::size_t, std::string> best{0, ""};
    for (const auto& v : rules_.pools.at(pool))
      for (const auto& s : v.spoken)
        if (s.size() > best.first && matches(s, i)) best = {s.size(), v.id};
    return best;
  }
  static bool decimal(const std::string& t) { return t == "decimal" || t == "point"; }

  void readSlot(const std::string& name, const ParserRules::Slot& slot) {
    for (std::size_t i = 0; i < tokens_.size(); ++i) {
      std::size_t at = i;
      bool starts = false;
      if (slot.markers.empty()) {
        starts = valueAt(slot.forms[0][0], i).first > 0;
      } else {
        for (const auto& m : slot.markers)
          if (matches(m, i)) at = std::max(at, i + m.size()), starts = true;
      }
      if (!starts) continue;
      // Everything that could belong to the value. Too much is as uncertain as too little: a value
      // is never cut to fit. A decimal ("0.9") is kept in, so it can never match.
      std::size_t end = at;
      while (end < tokens_.size()) {
        std::size_t n = 0;
        for (const auto& form : slot.forms)
          for (const auto& pool : form) n = std::max(n, valueAt(pool, end).first);
        // A name may only open the span: "eagle 421 hawk" is two callsigns.
        if (slot.markers.empty() && end > at && valueAt(slot.forms[0][0], end).first > 0) n = 0;
        if (n == 0 && end > at && decimal(tokens_[end].text)) n = 1;
        if (n == 0) break;
        end += n;
      }
      use(i, end - i);
      if (end == at) continue;  // marker without any value: the slot stays missing
      std::string value;
      for (const auto& form : slot.forms)
        if (std::string v = fit(form, at, end); !v.empty()) value = v;
      if (!value.empty() && slot.hi > 0) {
        std::string d;
        for (char c : value)
          if (std::isdigit(static_cast<unsigned char>(c))) d += c;
        const long n = d.empty() || d.size() > 6 ? -1 : std::stol(d);
        if (n < slot.lo || n > slot.hi) value.clear();
      }
      set(name, value, at, end);
      i = end - 1;
    }
  }
  std::string fit(const std::vector<std::string>& form, std::size_t at, std::size_t end) const {
    std::string v;
    for (const auto& pool : form) {
      const auto [n, id] = at < end ? valueAt(pool, at) : std::pair<std::size_t, std::string>{0, ""};
      if (n == 0) return "";
      v += id;
      if (id.size() > 1) v += '-';  // EAGLE-421
      at += n;
    }
    return at == end ? v : "";
  }
  void set(const std::string& name, const std::string& value, std::size_t from, std::size_t to) {
    ParsedField f;
    f.value = value;
    f.uncertain = value.empty();
    std::size_t next = tokens_[from].from;
    for (std::size_t i = from; i < to; ++i) {
      const Token& t = tokens_[i];
      for (; next <= t.to; ++next) f.heard += (f.heard.empty() ? "" : " ") + trim(raw_[next].text);
      f.confidence = i == from ? t.conf : std::min(f.confidence, t.conf);
    }
    if (opt_.minConfidence >= 0 && f.confidence >= 0 && f.confidence < opt_.minConfidence) f.value.clear(), f.uncertain = true;
    const auto [it, fresh] = out_.fields.emplace(name, f);
    if (fresh) return;
    ParsedField& old = it->second;
    if (!old.uncertain && !f.uncertain && old.value == f.value) {
      old.confidence = std::min(old.confidence, f.confidence);
      return;
    }
    old = {"", old.heard + " / " + f.heard, true, std::min(old.confidence, f.confidence)};
  }

  const ParserRules& rules_;
  ParseOptions opt_;
  std::vector<SttWord> raw_;
  std::vector<Token> tokens_;
  ParsedInstruction out_;
};
}  // namespace

ParsedInstruction parseInstruction(const SttResult& heard, const ParserRules& rules, const ParseOptions& o) {
  return Reader(heard, rules, o).run();
}

bool loadParserRules(const std::string& file, ParserRules& out, std::string& error) {
  const auto fail = [&](const std::string& why) {
    error = file + ": " + why;
    return false;
  };
  std::ifstream f(file);
  if (!f) return fail("cannot read");
  const json j = json::parse(f, nullptr, false);
  if (!j.is_object() || j.value("schemaVersion", 0) != 1) return fail("not phraseology, schemaVersion 1");
  for (const char* k : {"validation", "digits", "letters", "pools", "slots", "parse"})
    if (!j.contains(k) || !j[k].is_object()) return fail(std::string(k) + ": missing");
  if (!j.contains("intents") || !j["intents"].is_array()) return fail("intents: missing");
  ParserRules r;
  const auto st = j["validation"].find("status");
  r.placeholder = st == j["validation"].end() || *st != "validated";

  for (const auto& [key, pool, count] : {std::tuple{"digits", "digit", 10}, std::tuple{"letters", "letter", 26}})
    for (int i = 0; i < count; ++i) {
      ParserRules::Value v{std::string(1, static_cast<char>((count == 10 ? '0' : 'A') + i)), {}};
      if (!j[key].contains(v.id) || !wordLists(j[key][v.id], v.spoken)) return fail(std::string(key) + "/" + v.id + ": spoken forms missing");
      r.pools[pool].push_back(std::move(v));
    }
  for (const auto& [name, values] : j["pools"].items()) {
    if (r.pools.count(name) || !values.is_array() || values.empty()) return fail("pools/" + name + ": reserved name or empty");
    for (const auto& x : values) {
      ParserRules::Value v;
      if (!x.is_object() || !x.contains("id") || !x["id"].is_string() || !x.contains("spoken") || !wordLists(x["spoken"], v.spoken))
        return fail("pools/" + name + ": each value needs id and spoken forms");
      v.id = x["id"];
      if (v.id.empty()) return fail("pools/" + name + ": empty id");
      r.pools[name].push_back(std::move(v));
    }
  }
  // The same words must never mean two values of one pool.
  for (const auto& [name, values] : r.pools) {
    std::map<Words, std::string> seen;
    for (const auto& v : values)
      for (const auto& s : v.spoken)
        if (const auto [it, fresh] = seen.emplace(s, v.id); !fresh && it->second != v.id)
          return fail("pools/" + name + ": same words for " + it->second + " and " + v.id);
  }

  const json& parse = j["parse"];
  for (const char* k : {"markers", "flags", "ranges"})
    if (!parse.contains(k) || !parse[k].is_object()) return fail(std::string("parse/") + k + ": missing");
  for (const auto& [name, phrases] : parse["flags"].items())
    if (j["slots"].contains(name) || !wordLists(phrases, r.flags[name])) return fail("parse/flags/" + name + ": no phrases, or a slot has this name");

  std::set<std::string> wanted;
  for (const auto& i : j["intents"]) {
    if (!i.is_object() || !i.contains("id") || !i["id"].is_string()) return fail("intents: entry without id");
    if (!i.contains("parse")) continue;  // known wording, not parsed yet
    const json& p = i["parse"];
    ParserRules::Intent in;
    in.id = i["id"];
    if (!p.is_object() || !p.contains("cues") || !wordLists(p["cues"], in.cues)) return fail("intents/" + in.id + ": no cues");
    if (!p.contains("required") || !names(p["required"], in.required) || !p.contains("optional") || !names(p["optional"], in.optional))
      return fail("intents/" + in.id + ": required and optional must be lists of names");
    if (in.required.empty()) return fail("intents/" + in.id + ": nothing required");
    for (const auto* list : {&in.required, &in.optional})
      for (const auto& n : *list) {
        if (r.flags.count(n)) continue;
        if (!j["slots"].contains(n)) return fail("intents/" + in.id + ": unknown slot " + n);
        wanted.insert(n);
      }
    for (const auto& other : r.intents) {
      if (other.id == in.id) return fail("intents/" + in.id + ": used twice");
      for (const auto& c : other.cues)
        if (std::find(in.cues.begin(), in.cues.end(), c) != in.cues.end()) return fail("intents/" + in.id + ": same cue as " + other.id);
    }
    r.intents.push_back(std::move(in));
  }
  if (r.intents.empty()) return fail("no intent has a parse block");

  for (const auto& name : wanted) {
    ParserRules::Slot s;
    for (const auto& form : j["slots"][name]) {
      if (!form.is_string()) return fail("slots/" + name + ": not text");
      std::istringstream in(form.get<std::string>());
      std::vector<std::string> items;
      for (std::string w; in >> w;) {
        if (w.size() < 3 || w.front() != '{' || w.back() != '}' || !r.pools.count(w.substr(1, w.size() - 2)))
          return fail("slots/" + name + ": the parser reads pools only, not " + w);
        items.push_back(w.substr(1, w.size() - 2));
      }
      if (items.empty()) return fail("slots/" + name + ": empty form");
      s.forms.push_back(std::move(items));
    }
    if (s.forms.empty()) return fail("slots/" + name + ": no forms");
    if (parse["markers"].contains(name)) {
      if (!wordLists(parse["markers"][name], s.markers)) return fail("parse/markers/" + name + ": no words");
    } else {
      const std::string first = s.forms[0][0];
      if (first == "digit" || first == "letter") return fail("slots/" + name + ": needs a marker");
      for (const auto& form : s.forms)
        if (form[0] != first) return fail("slots/" + name + ": every form must start with the same pool");
    }
    if (parse["ranges"].contains(name)) {
      const json& g = parse["ranges"][name];
      if (!g.is_array() || g.size() != 2 || !g[0].is_number_integer() || !g[1].is_number_integer() || g[0].get<int>() > g[1].get<int>() || g[1].get<int>() < 1)
        return fail("parse/ranges/" + name + ": needs [low, high]");
      s.lo = g[0];
      s.hi = g[1];
    }
    r.slots.emplace(name, std::move(s));
  }
  out = std::move(r);
  return true;
}

}  // namespace atc
