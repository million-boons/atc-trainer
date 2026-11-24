// Runs a scenario from text, in virtual time. Exit code 0 if the exercise completes.
//   sim_cli SCENARIO.json SCRIPT [--data DIR] [--seed N] [--log FILE.jsonl]
// Script, one command per line, # comments:
//   say INTENT field=value ...   recognised; {name} in a value is the exercise's own value
//   unclear                      not recognised (the pilot says again)
//   wait SECONDS
//   stop                         instructor stop
// Free text instead of INTENT needs the parser (package 2e).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "core/PathResolver.h"
#include "engine/EventJson.h"
#include "engine/ExerciseResult.h"
#include "engine/Simulator.h"
using namespace atc;

namespace {
struct MemoryLog : IEventLog {
  std::string header;
  std::vector<EngineEvent> events;
  bool begin(const EventLogHeader& h, std::string& error) override {
    error = headerError(h);
    header = headerJson(h);
    return error.empty();
  }
  void append(const EngineEvent& e) override { events.push_back(e); }
  void end() override {}
  std::uint64_t lost() const override { return 0; }
};
}  // namespace

int main(int argc, char** argv) {
  std::string data, logFile;
  std::uint64_t seed = 1;
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    const std::string k = argv[i];
    if (k == "--data" && i + 1 < argc) data = argv[++i];
    else if (k == "--seed" && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
    else if (k == "--log" && i + 1 < argc) logFile = argv[++i];
    else args.push_back(k);
  }
  if (args.size() != 2 || args[0].rfind("--", 0) == 0 || args[1].rfind("--", 0) == 0) {
    std::fprintf(stderr, "usage: sim_cli SCENARIO.json SCRIPT [--data DIR] [--seed N] [--log FILE.jsonl]\n");
    return 2;
  }
  std::string error;
  if (data.empty()) {
    Paths p;
    if (!resolvePaths(systemPathInputs(), p, error)) {
      std::fprintf(stderr, "%s\n", error.c_str());
      return 2;
    }
    data = p.data;
  }
  auto exercise = std::make_shared<Exercise>();
  std::vector<LoadError> errors;
  if (!loadExercise(scenarioSources(data, args[0]), *exercise, errors)) {
    for (const auto& e : errors) std::fprintf(stderr, "%s\n", toString(e).c_str());
    return 1;
  }
  std::ifstream script(args[1]);
  if (!script) {
    std::fprintf(stderr, "cannot read %s\n", args[1].c_str());
    return 2;
  }
  EventLogHeader h;
  h.sessionId = "sim";
  h.scenarioId = exercise->scenario.id;
  h.scenarioHash = exercise->hash;
  h.seed = seed;
  h.modelId = "none-text";  // no speech model: instructions come from the script
  h.modelConfig = "{}";
  MemoryLog log;
  if (!log.begin(h, error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    return 2;
  }
  Simulator sim(SequenceEngine(exercise, seed), log);
  if (exercise->placeholder) std::printf("PLACEHOLDER scenario, not approved\n");
  sim.start();
  std::string text;
  for (int n = 1; std::getline(script, text); ++n) {
    std::istringstream in(text);
    std::string cmd;
    if (!(in >> cmd) || cmd[0] == '#') continue;
    const auto bad = [&](const char* why) {
      std::fprintf(stderr, "%s:%d: %s\n", args[1].c_str(), n, why);
      return 2;
    };
    if (cmd == "wait") {
      double s = 0;
      if (!(in >> s) || s < 0) return bad("wait needs seconds");
      sim.wait(static_cast<Ns>(s * 1e9));
    } else if (cmd == "say") {
      std::string intent, pair;
      if (!(in >> intent)) return bad("say needs an intent");
      std::map<std::string, std::string> fields;
      while (in >> pair) {
        const auto eq = pair.find('=');
        if (eq == std::string::npos) return bad("expected field=value");
        fields[pair.substr(0, eq)] = fillSlots(pair.substr(eq + 1), sim.engine().context().params);
      }
      sim.transmit(Simulator::instruction(intent, fields));
    } else if (cmd == "unclear") {
      sim.transmit(ParsedInstruction{});
    } else if (cmd == "stop") {
      sim.stop();
    } else {
      return bad("unknown command");
    }
  }
  sim.waitForPilot();
  log.end();

  for (const auto& e : log.events) {
    const double s = static_cast<double>(e.t) / 1e9;
    if (const auto* o = std::get_if<EngineOutput>(&e.body)) {
      if (const auto* j = std::get_if<out::Judged>(o)) {
        std::string why;
        for (const auto& p : j->problems) why += " " + p;
        std::printf("%7.1f  %-8s %s%s%s\n", s, j->step.c_str(), std::string(toString(j->outcome)).c_str(), why.c_str(),
                    j->suspectedStt ? " (suspected recognition error)" : "");
      } else if (const auto* p = std::get_if<out::PilotRequested>(o)) {
        std::printf("%7.1f  PILOT    %s\n", s, p->utterance.text.c_str());
      } else if (const auto* a = std::get_if<out::InstructorAlert>(o)) {
        std::printf("%7.1f  ALERT    %s\n", s, a->why.c_str());
      }
    }
  }
  if (!logFile.empty()) {
    std::ofstream f(logFile);
    f << log.header << "\n";
    for (const auto& e : log.events) f << eventJson(e) << "\n";
    f.close();
    if (!f) {
      std::fprintf(stderr, "cannot write %s\n", logFile.c_str());
      return 2;
    }
  }
  for (const auto& s : buildResult(exercise->scenario, log.events).steps)
    std::printf("%-8s %s, attempts %d, suspected recognition errors %d, timeouts %d\n", s.id.c_str(),
                s.completed ? "completed" : s.reached ? "not completed" : "not reached", s.attempts, s.suspectedStt, s.timeouts);
  return sim.reason() == out::EndReason::Completed ? 0 : 1;
}
