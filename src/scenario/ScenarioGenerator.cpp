#include <algorithm>
#include <functional>
#include <random>

#include "scenario/Scenario.h"

namespace atc {
namespace {

std::vector<std::string> candidates(const Scenario& s, const Airfield& a, const ParamGenerator& g, const ParamValues& chosen) {
  std::vector<std::string> out;
  const auto served = [&](const std::set<std::string>& allowed, bool emptyMeansAny) {
    if (g.mustServe.empty()) return true;
    if (allowed.empty()) return emptyMeansAny;
    const auto it = chosen.find(g.mustServe);
    return it != chosen.end() && allowed.count(it->second) > 0;
  };
  if (g.pool == "runways") {
    out = a.runways;
  } else if (g.pool == "taxiways") {
    for (const auto& t : a.taxiways)
      if (served(t.serves, false)) out.push_back(t.id);
  } else if (g.pool == "aircraftTypes") {
    for (const auto& t : a.aircraftTypes)
      if (t.categories.empty() || t.categories.count(s.category)) out.push_back(t.id);
  } else if (g.pool == "callsigns") {
    for (const auto& c : a.callsigns)
      if (served(c.types, true)) out.push_back(c.id);
  } else if (g.pool == "frequencies") {  // D-430: those of its unit
    for (const auto& u : a.units)
      if (u.id == g.unit) out = u.frequencies;
  }
  return out;
}

// Calls done for every valid combination until it returns false.
void enumerate(const Scenario& s, const Airfield& a, const std::function<bool(const ParamValues&)>& done) {
  // A parameter that others must serve is chosen before them.
  std::vector<std::pair<std::string, ParamGenerator>> order;
  for (const bool dependent : {false, true})
    for (const auto& [name, g] : s.generators)
      if (g.mustServe.empty() != dependent) order.emplace_back(name, g);
  ParamValues chosen;
  const std::function<bool(std::size_t)> go = [&](std::size_t i) {
    if (i == order.size()) return done(chosen);
    for (const auto& v : candidates(s, a, order[i].second, chosen)) {
      chosen[order[i].first] = v;
      if (!go(i + 1)) return false;
    }
    chosen.erase(order[i].first);
    return true;
  };
  go(0);
}

}  // namespace

std::uint64_t combinationCount(const Scenario& s, const Airfield& a) {
  std::uint64_t n = 0;
  enumerate(s, a, [&](const ParamValues&) {
    ++n;
    return true;
  });
  return n;
}

std::vector<ParamValues> combinations(const Scenario& s, const Airfield& a, std::size_t max, bool& tooMany) {
  std::vector<ParamValues> out;
  tooMany = false;
  enumerate(s, a, [&](const ParamValues& v) {
    if (out.size() == max) {
      tooMany = true;
      return false;
    }
    out.push_back(v);
    return true;
  });
  return out;
}

bool fitsParams(const Scenario& s, const Airfield& a, const ParamValues& v, std::string& why) {
  for (const auto& [name, value] : v)
    if (!s.generators.count(name)) {
      why = name + " is not a value of " + s.id;
      return false;
    }
  for (const bool dependent : {false, true})
    for (const auto& [name, g] : s.generators) {
      if (g.mustServe.empty() == dependent) continue;
      const auto it = v.find(name);
      if (it == v.end() || it->second.empty()) {
        why = "no " + name;
        return false;
      }
      const auto c = candidates(s, a, g, v);
      if (std::find(c.begin(), c.end(), it->second) != c.end()) continue;
      ParamGenerator any = g;
      any.mustServe.clear();
      const auto all = candidates(s, a, any, v);
      const auto other = v.find(g.mustServe);
      if (std::find(all.begin(), all.end(), it->second) == all.end() || other == v.end())
        why = it->second + " is not one of the " + g.pool + " of " + a.id;
      else if (g.pool == "taxiways")
        why = it->second + " does not serve " + other->second;
      else
        why = it->second + " does not fit " + other->second;
      return false;
    }
  return true;
}

bool generateParams(const Scenario& s, const Airfield& a, std::uint64_t seed, ParamValues& out) {
  const std::uint64_t n = combinationCount(s, a);
  if (n == 0) return false;
  std::mt19937_64 rng(seed);
  std::uint64_t skip = rng() % n;  // not uniform_int_distribution: it differs between libraries
  enumerate(s, a, [&](const ParamValues& v) {
    if (skip-- > 0) return true;
    out = v;
    return false;
  });
  return true;
}

}  // namespace atc
