// Checks scenario files and shows generated values.
//   scenario_check [--data DIR] [--seed N] [--assessed] [FILE.json ...]
// Without files: every scenario in DIR/scenarios. DIR defaults to the installed data folder.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "core/PathResolver.h"
#include "scenario/Scenario.h"
using namespace atc;

int main(int argc, char** argv) {
  std::string data;
  std::uint64_t seed = 1;
  ExerciseMode mode = ExerciseMode::Practice;
  std::vector<std::string> files;
  for (int i = 1; i < argc; ++i) {
    const std::string k = argv[i];
    if (k == "--assessed") mode = ExerciseMode::Assessed;
    else if (k == "--data" && i + 1 < argc) data = argv[++i];
    else if (k == "--seed" && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
    else if (k.rfind("--", 0) == 0) {
      std::fprintf(stderr, "usage: scenario_check [--data DIR] [--seed N] [--assessed] [FILE.json ...]\n");
      return 2;
    } else files.push_back(k);
  }
  if (data.empty()) {
    Paths p;
    std::string error;
    if (!resolvePaths(systemPathInputs(), p, error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return 2;
    }
    data = p.data;
  }
  if (files.empty()) {
    std::error_code ec;
    for (std::filesystem::directory_iterator it(std::filesystem::path(data) / "scenarios", ec), end; !ec && it != end; it.increment(ec))
      if (it->path().extension() == ".json") files.push_back(it->path().string());
    std::sort(files.begin(), files.end());
  }
  if (files.empty()) {
    std::fprintf(stderr, "no scenario files in %s/scenarios\n", data.c_str());
    return 1;
  }
  bool ok = true;
  for (const auto& f : files) {
    Exercise e;
    std::vector<LoadError> errors;
    if (!loadExercise(scenarioSources(data, f), e, errors)) {
      ok = false;
      for (const auto& x : errors) std::printf("%s\n", toString(x).c_str());
      continue;
    }
    ParamValues v;
    generateParams(e.scenario, e.airfield, seed, v);
    std::printf("%s: OK, hash %s%s, seed %llu:", e.scenario.id.c_str(), e.hash.c_str(), e.placeholder ? ", PLACEHOLDER" : "",
                static_cast<unsigned long long>(seed));
    for (const auto& [name, value] : v) std::printf(" %s=%s", name.c_str(), value.c_str());
    std::printf("\n");
    if (const auto why = modeError(e, mode); !why.empty()) {
      ok = false;
      std::printf("%s\n", why.c_str());
    }
  }
  return ok ? 0 : 1;
}
