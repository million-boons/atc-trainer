#include "phraseology/ScriptGen.h"

#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <sstream>

#include "bench/Scoring.h"

namespace atc {
namespace {
std::vector<std::string> split(const std::string& s) {
  std::istringstream in(s);
  std::vector<std::string> v;
  for (std::string w; in >> w;) v.push_back(w);
  return v;
}
std::string join(const std::vector<std::string>& v) {
  std::string o;
  for (const auto& w : v) o += (o.empty() ? "" : " ") + w;
  return o;
}
std::string title(std::string s) {
  if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
  return s;
}
std::string displayed(const std::vector<std::string>& w) {  // numbers and letters capitalised, like the prompts
  std::vector<std::string> o;
  static const std::set<std::string> small{"taxi", "via", "hold", "short", "runway", "cleared", "for", "to", "land",
                                           "takeoff", "turn", "heading", "climb", "descend", "flight", "level",
                                           "contact", "tower", "decimal", "thousand", "hundred", "feet"};
  for (const auto& x : w) o.push_back(small.count(x) ? x : title(x));
  return join(o);
}

struct Filled {
  std::vector<std::string> words;
  std::string value;
};

class Gen {
 public:
  Gen(const Phraseology& p, std::uint32_t seed) : p_(p), g_(seed) {}
  std::size_t pick(std::size_t n) { return g_() % n; }

  // Digits of one slot, chosen from valid candidates so that each position stays balanced.
  std::string digits(const std::string& slot, int count) {
    std::string best;
    long bestCost = -1;
    std::vector<std::string> all;
    int top = 1;
    for (int i = 0; i < count; ++i) top *= 10;
    for (int v = 0; v < top; ++v) {
      std::string c = std::to_string(v);
      c.insert(0, static_cast<std::size_t>(count) - c.size(), '0');
      if (valid(slot, c)) all.push_back(std::move(c));
    }
    for (int tries = 0; tries < 60; ++tries) {
      const std::string& c = all[pick(all.size())];
      long cost = 0;
      for (int i = 0; i < count; ++i) {
        const int n = used_[slot + std::to_string(i) + c[static_cast<std::size_t>(i)]];
        cost += n * n;
      }
      if (best.empty() || cost < bestCost) best = c, bestCost = cost;
    }
    for (int i = 0; i < count; ++i) ++used_[slot + std::to_string(i) + best[static_cast<std::size_t>(i)]];
    return best;
  }
  static bool valid(const std::string& slot, const std::string& d) {
    const int v = std::stoi(d);
    if (slot == "runway") return v >= 1 && v <= 36;
    if (slot == "heading") return v >= 1 && v <= 360;
    if (slot == "level") return v >= 10 && v <= 450;
    if (slot == "taxiway" || slot == "altitude" || slot == "altitudeHundred") return v >= 1;
    if (slot == "frequency") return v >= 118 && v <= 136;
    return true;
  }
  std::size_t balanced(const std::string& key, std::size_t n) {
    int low = -1;
    std::vector<std::size_t> c;
    for (std::size_t i = 0; i < n; ++i) {
      const int u = used_[key + "#" + std::to_string(i)];
      if (low < 0 || u < low) low = u, c.clear();
      if (u == low) c.push_back(i);
    }
    const auto i = c[pick(c.size())];
    ++used_[key + "#" + std::to_string(i)];
    return i;
  }
  Filled fill(const std::string& slot, bool variant) {
    Filled f;
    const auto& forms = p_.slots.at(slot);
    const auto& form = forms[balanced("form" + slot, forms.size())];
    const auto tokens = split(form);
    int n = 0;
    for (const auto& t : tokens) n += t == "{digit}";
    std::string d;
    if (slot == "altitude") {
      d = digits("altitude", 1);
      if (n == 2) d += digits("altitudeHundred", 1);
    } else if (slot == "frequency") {
      d = digits("frequency", 3) + digits("frequencyDecimal", n - 3);
    } else if (n > 0) {
      d = digits(slot, n);
    }
    std::size_t k = 0;
    std::string letter, side, name;
    for (const auto& t : tokens) {
      if (t == "{digit}") {
        const auto& s = p_.pools.at("digit")[static_cast<std::size_t>(d[k++] - '0')];
        f.words.push_back(variant && s.size() > 1 ? s[1 + pick(s.size() - 1)] : s[0]);
      } else if (t == "{letter}") {
        const auto i = balanced("letter", 26);
        letter = std::string(1, static_cast<char>('A' + i));
        f.words.push_back(p_.pools.at("letter")[i][0]);
      } else if (t == "{side}") {
        const auto i = balanced("side", p_.pools.at("side").size());
        f.words.push_back(p_.pools.at("side")[i][0]);
        side = title(f.words.back()).substr(0, 1);
      } else if (t == "{callsignName}") {
        const auto i = balanced("name", p_.pools.at("callsignName").size());
        name = p_.pools.at("callsignName")[i][0];
        f.words.push_back(name);
      } else {
        f.words.push_back(t);
      }
    }
    if (slot == "callsign") {
      for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      f.value = name + "-" + d;
    } else if (slot == "runway") f.value = d + side;
    else if (slot == "taxiway") f.value = letter + d;
    else if (slot == "heading") f.value = d;
    else if (slot == "level") f.value = "FL" + d;
    else if (slot == "altitude") f.value = std::to_string((d[0] - '0') * 1000 + (d.size() > 1 ? (d[1] - '0') * 100 : 0));
    else if (slot == "frequency") f.value = d.substr(0, 3) + "." + d.substr(3);
    return f;
  }
  std::mt19937& rng() { return g_; }

 private:
  const Phraseology& p_;
  std::mt19937 g_;
  std::map<std::string, int> used_;
};

std::vector<SttWord> asWords(const std::string& text) {
  std::vector<SttWord> w;
  for (auto& s : normaliseText(text)) w.push_back({s, 1.0});
  return w;
}
}  // namespace

std::string phraseLine(const Phrase& p) {
  std::string o = p.text + " |";
  if (p.negative) o += " negative";
  for (const auto& [k, v] : p.expect) o += " " + k + "=" + v;
  if (!p.tags.empty()) {
    o += " tags=";
    for (std::size_t i = 0; i < p.tags.size(); ++i) o += (i ? "," : "") + p.tags[i];
  }
  return o;
}

std::vector<Phrase> makeScript(const Phraseology& p, const ScriptOptions& o, std::string& error) {
  Gen gen(p, o.seed);
  std::set<std::string> names;
  for (const auto& v : p.pools.at("callsignName")) names.insert(v[0]);
  std::vector<std::pair<std::string, std::string>> examples;  // intent, text
  for (const auto& i : p.intents)
    for (const auto& e : i.examples) examples.emplace_back(i.id, e);

  std::vector<Phrase> out;
  std::set<std::string> texts;
  const auto build = [&](bool variant) -> Phrase {
    const auto& [intent, e] = examples[gen.balanced("example", examples.size())];
    Phrase ph;
    std::vector<std::string> words;
    for (const auto& t : split(e)) {
      if (t.front() != '{') {
        words.push_back(t);
        continue;
      }
      const std::string slot = t.substr(1, t.size() - 2);
      if (!p.slots.count(slot)) {
        error = "script needs slots, not bare pools: " + t;
        return ph;
      }
      const auto f = gen.fill(slot, variant);
      words.insert(words.end(), f.words.begin(), f.words.end());
      if (!f.value.empty()) ph.expect[slot] = f.value;
      if (slot == "taxiway") ph.expect["taxiway"] = f.value;
    }
    const std::string text = join(words);
    if (text.find("hold short") != std::string::npos) ph.expect["hold_short"] = "yes";
    if (const auto a = actionOf(asWords(text)); !a.empty()) ph.expect["action"] = a;
    ph.tags = {intent};
    ph.text = displayed(words);
    return ph;
  };

  int guard = 0;
  while (static_cast<int>(out.size()) < o.scripted && ++guard < o.scripted * 50) {
    const bool variant = static_cast<int>(out.size()) < o.variants;
    Phrase ph = build(variant);
    if (!error.empty()) return {};
    if (variant) {
      if (ph.text.find("Tree") == std::string::npos && ph.text.find("Fife") == std::string::npos && ph.text.find("Niner") == std::string::npos) continue;
      ph.tags.push_back("variant");
    }
    if (!texts.insert(ph.text).second) continue;
    auto ex = extractFields(asWords(ph.text), names);
    if (ph.expect.count("action")) ex.fields["action"] = actionOf(asWords(ph.text));
    if (judge(ph, ex, 0, true) != Outcome::Correct) {
      error = "generated phrase does not score as correct: " + ph.text;
      return {};
    }
    out.push_back(std::move(ph));
  }
  if (static_cast<int>(out.size()) < o.scripted) {
    error = "could not make enough different phrases";
    return {};
  }

  // Negatives.
  std::vector<Phrase> neg;
  for (const char* kind : {"off", "near"}) {
    std::vector<std::string> pool;
    for (const auto& [k, t] : o.negativeTexts)
      if (k == kind) pool.push_back(t);
    const int want = o.negatives / 3;
    if (static_cast<int>(pool.size()) < want) {
      error = std::string("not enough negatives of kind ") + kind;
      return {};
    }
    for (int i = 0; i < want; ++i) {
      const auto at = gen.pick(pool.size());
      neg.push_back({pool[at], true, {}, {kind}});
      pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(at));
    }
  }
  guard = 0;
  while (static_cast<int>(neg.size()) < o.negatives && ++guard < 1000) {  // wrong digit count
    Phrase ph = build(false);
    auto words = split(ph.text);
    std::vector<std::size_t> at;
    static const std::set<std::string> dig{"Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine"};
    for (std::size_t i = 0; i < words.size(); ++i)
      if (dig.count(words[i])) at.push_back(i);
    if (at.empty()) continue;
    const auto i = at[gen.pick(at.size())];
    if (gen.pick(2)) words.erase(words.begin() + static_cast<std::ptrdiff_t>(i));
    else words.insert(words.begin() + static_cast<std::ptrdiff_t>(i), words[i]);
    Phrase n{join(words), true, {}, {"malformed"}};
    const auto ex = extractFields(asWords(n.text), names);
    // Must be detectably wrong: a dropped digit that leaves a plausible value proves nothing.
    if (accepted(ex, 0) && shapeOk(ex)) continue;
    if (texts.insert(n.text).second) neg.push_back(std::move(n));
  }
  if (static_cast<int>(neg.size()) < o.negatives) {
    error = "could not make enough malformed negatives";
    return {};
  }
  out.insert(out.end(), neg.begin(), neg.end());
  for (std::size_t i = out.size() - 1; i > 0; --i) std::swap(out[i], out[gen.pick(i + 1)]);
  return out;
}

}  // namespace atc
