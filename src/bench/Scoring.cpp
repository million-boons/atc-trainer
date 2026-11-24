#include "bench/Scoring.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace atc {
namespace {
const char* const kDigits[10] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};
const char* const kPhonetic[26] = {"alpha", "bravo", "charlie", "delta", "echo", "foxtrot", "golf",
                                   "hotel", "india", "juliet", "kilo", "lima", "mike", "november",
                                   "oscar", "papa", "quebec", "romeo", "sierra", "tango", "uniform",
                                   "victor", "whiskey", "xray", "yankee", "zulu"};

// Spoken or spelling variants of the same word.
const std::map<std::string, std::string> kVariants{
    {"niner", "nine"}, {"tree", "three"},  {"fife", "five"},    {"fower", "four"},
    {"alfa", "alpha"}, {"juliett", "juliet"}, {"whisky", "whiskey"}, {"centre", "center"}};

bool isDigits(const std::string& s) {
  return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}
void spellDigits(const std::string& d, std::vector<std::string>& out) {
  for (char c : d) out.push_back(kDigits[c - '0']);
}
// One run of letters or digits.
void spell(const std::string& t, std::vector<std::string>& out) {
  if (!isDigits(t)) {
    const auto v = kVariants.find(t);
    out.push_back(v == kVariants.end() ? t : v->second);
    return;
  }
  // "5000" and "2500" are written for "five thousand" and "two thousand five hundred".
  if (t.size() >= 4 && t.size() <= 5 && t[0] != '0' && t.compare(t.size() - 2, 2, "00") == 0) {
    spellDigits(t.substr(0, t.size() - 3), out);
    out.push_back("thousand");
    if (t[t.size() - 3] != '0') {
      out.push_back(kDigits[t[t.size() - 3] - '0']);
      out.push_back("hundred");
    }
    return;
  }
  spellDigits(t, out);
}
std::vector<std::string> spellWord(const std::string& raw) {
  std::string s;
  for (std::size_t i = 0; i < raw.size(); ++i) {
    const auto c = static_cast<unsigned char>(raw[i]);
    const bool digitsAround = i > 0 && i + 1 < raw.size() && std::isdigit(static_cast<unsigned char>(raw[i - 1])) &&
                              std::isdigit(static_cast<unsigned char>(raw[i + 1]));
    if (c == ',' && digitsAround) continue;            // 5,000
    if (c == '.' && digitsAround) s += " decimal ";    // 118.1
    else if (c == '\'') continue;                      // don't -> dont
    else s += c < 128 && std::isalnum(c) ? static_cast<char>(std::tolower(c)) : ' ';
  }
  // Split at spaces and between letters and digits.
  std::vector<std::string> runs;
  std::string cur;
  for (char ch : s + " ") {
    const auto c = static_cast<unsigned char>(ch);
    if (!cur.empty() && (c == ' ' || bool(std::isdigit(c)) != bool(std::isdigit(static_cast<unsigned char>(cur.back()))))) {
      runs.push_back(cur);
      cur.clear();
    }
    if (c != ' ') cur += ch;
  }
  std::vector<std::string> out;
  for (std::size_t i = 0; i < runs.size(); ++i) {
    // "A3" inside one written word is "alpha three".
    if (runs[i].size() == 1 && std::isalpha(static_cast<unsigned char>(runs[i][0])) && i + 1 < runs.size() &&
        isDigits(runs[i + 1]))
      out.push_back(kPhonetic[runs[i][0] - 'a']);
    else
      spell(runs[i], out);
  }
  return out;
}
int digitOf(const std::string& w) {
  for (int i = 0; i < 10; ++i)
    if (w == kDigits[i]) return i;
  return -1;
}
int phoneticOf(const std::string& w) {
  for (int i = 0; i < 26; ++i)
    if (w == kPhonetic[i]) return i;
  return -1;
}
}  // namespace

std::vector<SttWord> normaliseWords(const std::vector<SttWord>& words) {
  std::vector<SttWord> out;
  for (const auto& w : words)
    for (auto& s : spellWord(w.text)) out.push_back({std::move(s), w.conf});
  // "x ray" and "x-ray"
  for (std::size_t i = 0; i + 1 < out.size(); ++i)
    if (out[i].text == "x" && out[i + 1].text == "ray") {
      out[i] = {"xray", std::min(out[i].conf, out[i + 1].conf)};
      out.erase(out.begin() + static_cast<std::ptrdiff_t>(i) + 1);
    }
  return out;
}

std::vector<std::string> normaliseText(const std::string& text) {
  std::vector<std::string> out;
  std::vector<SttWord> words;
  std::string cur;
  for (char c : text + " ") {
    if (!std::isspace(static_cast<unsigned char>(c))) cur += c;
    else if (!cur.empty()) words.push_back({std::exchange(cur, {}), 1.0});
  }
  for (auto& w : normaliseWords(words)) out.push_back(std::move(w.text));
  return out;
}

EditCounts editCounts(const std::vector<std::string>& ref, const std::vector<std::string>& hyp) {
  struct Cell {
    int cost, sub, del, ins;
  };
  std::vector<Cell> prev(hyp.size() + 1), cur(hyp.size() + 1);
  for (std::size_t j = 0; j <= hyp.size(); ++j) prev[j] = {int(j), 0, 0, int(j)};
  for (std::size_t i = 1; i <= ref.size(); ++i) {
    cur[0] = {int(i), 0, int(i), 0};
    for (std::size_t j = 1; j <= hyp.size(); ++j) {
      if (ref[i - 1] == hyp[j - 1]) {
        cur[j] = prev[j - 1];
        continue;
      }
      const Cell &s = prev[j - 1], &d = prev[j], &n = cur[j - 1];
      if (s.cost <= d.cost && s.cost <= n.cost) cur[j] = {s.cost + 1, s.sub + 1, s.del, s.ins};
      else if (d.cost <= n.cost) cur[j] = {d.cost + 1, d.sub, d.del + 1, d.ins};
      else cur[j] = {n.cost + 1, n.sub, n.del, n.ins + 1};
    }
    std::swap(prev, cur);
  }
  const Cell& c = prev[hyp.size()];
  return {c.sub, c.del, c.ins, int(ref.size())};
}

Extracted extractFields(const std::vector<SttWord>& w, const std::set<std::string>& names) {
  Extracted e;
  const std::size_t n = w.size();
  const auto is = [&](std::size_t i, const char* s) { return i < n && w[i].text == s; };
  const auto digits = [&](std::size_t i) {  // digit words from i
    std::string d;
    for (; i < n && digitOf(w[i].text) >= 0; ++i) d += static_cast<char>('0' + digitOf(w[i].text));
    return d;
  };
  const auto set = [&](const std::string& k, const std::string& v, std::size_t from, std::size_t to) {
    const auto [it, fresh] = e.fields.emplace(k, v);
    if (!fresh && it->second != v) e.conflict = true;
    for (std::size_t i = from; i < to; ++i) e.minConf = std::min(e.minConf, w[i].conf);
  };

  for (std::size_t i = 0; i < n; ++i) {
    if (names.count(w[i].text)) {
      const std::string d = digits(i + 1);
      std::string name = w[i].text;
      for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      if (!d.empty()) set("callsign", name + "-" + d, i, i + 1 + d.size());
    }
    if (is(i, "runway")) {
      std::string d = digits(i + 1);
      if (!d.empty()) {
        std::size_t end = i + 1 + d.size();
        if (is(end, "left")) d += 'L', ++end;
        else if (is(end, "right")) d += 'R', ++end;
        else if (is(end, "center")) d += 'C', ++end;
        set("runway", d, i, end);
      }
    }
    if ((is(i, "via") || is(i, "taxiway")) && i + 1 < n && phoneticOf(w[i + 1].text) >= 0) {
      const std::string d = digits(i + 2);
      set("taxiway", std::string(1, static_cast<char>('A' + phoneticOf(w[i + 1].text))) + d, i, i + 2 + d.size());
    }
    if (is(i, "heading")) {
      const std::string d = digits(i + 1);
      if (!d.empty()) set("heading", d, i, i + 1 + d.size());
    }
    if (is(i, "flight") && is(i + 1, "level")) {
      const std::string d = digits(i + 2);
      if (!d.empty()) set("level", "FL" + d, i, i + 2 + d.size());
    }
    if (is(i, "hold") && is(i + 1, "short")) set("hold_short", "yes", i, i + 2);
    if (is(i, "decimal") || is(i, "point")) {
      std::size_t a = i;
      while (a > 0 && digitOf(w[a - 1].text) >= 0) --a;
      const std::string before = digits(a), after = digits(i + 1);
      if (!after.empty() && a < i) set("frequency", before.substr(0, i - a) + "." + after, a, i + 1 + after.size());
    }
    // Start of a number not inside another number.
    if (digitOf(w[i].text) >= 0 && (i == 0 || digitOf(w[i - 1].text) < 0)) {
      const std::string d = digits(i);
      std::size_t j = i + d.size();
      const bool afterHundred = i > 0 && (w[i - 1].text == "thousand" || w[i - 1].text == "hundred");
      long v = d.size() <= 5 ? std::stol(d) : -1;
      bool scaled = false;
      if (v >= 0 && !afterHundred) {
        if (is(j, "thousand")) {
          scaled = true;
          v *= 1000;
          ++j;
          const std::string h = digits(j);
          if (h.size() == 1 && is(j + 1, "hundred")) {
            v += (h[0] - '0') * 100;
            j += 2;
          }
        } else if (is(j, "hundred")) {
          scaled = true;
          v *= 100;
          ++j;
        }
        if (is(j, "feet")) scaled = true, ++j;
        if (scaled) set("altitude", std::to_string(v), i, j);
      }
    }
  }
  return e;
}

std::string actionOf(const std::vector<SttWord>& w) {
  std::set<std::string> got;
  const auto is = [&](std::size_t i, const char* s) { return i < w.size() && w[i].text == s; };
  for (std::size_t i = 0; i < w.size(); ++i) {
    if (is(i, "takeoff") || (is(i, "take") && is(i + 1, "off"))) got.insert("takeoff");
    if (is(i, "land") || is(i, "landing")) got.insert("land");
    if (is(i, "turn") && is(i + 1, "left")) got.insert("turn_left");
    if (is(i, "turn") && is(i + 1, "right")) got.insert("turn_right");
    if (is(i, "line") && is(i + 1, "up")) got.insert("line_up");
    if (is(i, "climb")) got.insert("climb");
    if (is(i, "descend")) got.insert("descend");
  }
  return got.empty() ? "" : got.size() > 1 ? "conflict" : *got.begin();
}

bool accepted(const Extracted& e, double threshold) {
  return e.fields.count("callsign") && e.fields.size() >= 2 && !e.conflict && e.minConf >= threshold;
}

bool shapeOk(const Extracted& e) {
  const auto number = [](const std::string& s, std::size_t n, int lo, int hi) {
    return s.size() == n && isDigits(s) && std::stoi(s) >= lo && std::stoi(s) <= hi;
  };
  for (const auto& [k, v] : e.fields) {
    bool ok = true;
    if (k == "callsign") ok = v.find('-') != std::string::npos && number(v.substr(v.find('-') + 1), 3, 0, 999);
    else if (k == "runway") ok = number(v.substr(0, 2), 2, 1, 36) && (v.size() == 2 || (v.size() == 3 && std::string("LRC").find(v[2]) != std::string::npos));
    else if (k == "taxiway") ok = v.size() >= 2 && v.size() <= 3 && isDigits(v.substr(1));
    else if (k == "heading") ok = number(v, 3, 1, 360);
    else if (k == "level") ok = v.size() > 2 && number(v.substr(2), 3, 0, 999);
    else if (k == "frequency") {
      const auto dot = v.find('.');
      ok = dot == 3 && number(v.substr(0, 3), 3, 100, 399) && v.size() >= 5 && v.size() <= 7 && isDigits(v.substr(4));
    }
    if (!ok) return false;
  }
  return true;
}

Outcome judge(const Phrase& p, const Extracted& e, double threshold, bool useShape) {
  const bool ok = accepted(e, threshold) && (!useShape || shapeOk(e));
  if (p.negative) return ok ? Outcome::FalseAccept : Outcome::NegativeOk;
  if (!ok) return Outcome::Rejected;
  for (const auto& [k, v] : e.fields) {
    const auto it = p.expect.find(k);
    if (it == p.expect.end() || it->second != v) return Outcome::Wrong;
  }
  if (p.expect.count("hold_short") && !e.fields.count("hold_short")) return Outcome::Wrong;
  return e.fields.size() == p.expect.size() ? Outcome::Correct : Outcome::Incomplete;
}

double percentile(std::vector<double> v, double p) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  const auto k = static_cast<std::size_t>(std::ceil(p / 100.0 * double(v.size())));
  return v[std::clamp<std::size_t>(k, 1, v.size()) - 1];
}

}  // namespace atc
