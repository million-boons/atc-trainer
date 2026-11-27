#include "bench/PhraseFile.h"

#include <istream>
#include <set>
#include <sstream>

namespace atc {
namespace {
std::string trim(const std::string& s) {
  const auto a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
}  // namespace

bool readPhrases(std::istream& in, std::vector<Phrase>& out, std::string& error) {
  static const std::set<std::string> known{"callsign", "runway",   "taxiway",   "heading",   "readability",
                                           "level",    "altitude", "frequency", "hold_short", "action"};
  int n = 0;
  const auto fail = [&](const std::string& why) {
    error = "line " + std::to_string(n) + ": " + why;
    return false;
  };
  for (std::string line; std::getline(in, line);) {
    ++n;
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    Phrase p;
    const auto bar = line.find('|');
    p.text = trim(line.substr(0, bar));
    if (p.text.empty()) return fail("no phrase text");
    if (bar != std::string::npos) {
      std::istringstream f(line.substr(bar + 1));
      std::string item;
      while (f >> item) {
        if (item == "negative") {
          p.negative = true;
          continue;
        }
        const auto eq = item.find('=');
        if (eq == std::string::npos || eq + 1 == item.size()) return fail("expected name=value, got " + item);
        const std::string k = item.substr(0, eq);
        if (k == "tags") {
          std::istringstream ts(item.substr(eq + 1));
          for (std::string x; std::getline(ts, x, ',');) p.tags.push_back(x);
          continue;
        }
        if (!known.count(k)) return fail("unknown field " + k);
        if (!p.expect.emplace(k, item.substr(eq + 1)).second) return fail("field given twice: " + k);
      }
      if (p.negative && !p.expect.empty()) return fail("a negative cannot have expected fields");
      if (!p.negative && (!p.expect.count("callsign") || p.expect.size() < 2))
        return fail("a scripted phrase needs a callsign and at least one other field");
    }
    out.push_back(std::move(p));
  }
  return true;
}

}  // namespace atc
