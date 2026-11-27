#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/IEventLog.h"
#include "core/Outcome.h"
#include "core/Time.h"
#include "core/TransmissionController.h"

namespace atc {

struct LoadError {
  std::string file, path, what;  // path: JSON pointer, e.g. /steps/0/transitions/1/to
};
std::string toString(const LoadError& e);

// Instructor-approved pools.
struct Airfield {
  struct Taxiway { std::string id; std::set<std::string> serves; };
  struct AircraftType { std::string id; std::set<std::string> categories; };  // empty: any
  struct Callsign { std::string id, spoken; std::set<std::string> types; };   // empty: any
  std::string id;
  bool placeholder = true;
  std::vector<std::string> runways;
  std::vector<Taxiway> taxiways;
  std::vector<AircraftType> aircraftTypes;
  std::vector<Callsign> callsigns;
};

// data/phraseology/replies (D-402). Never read by the speech corpus generator.
struct PilotReply {
  std::string id, text;  // text with {slot}s
  std::vector<std::string> slots;
  bool placeholder = true;
  // D-429: the intent this reply reads back; empty if it is no readback. Complete: it says every
  // required value of that intent (callsign included); "Rolling" is not complete.
  std::string readbackOf;
  bool completeReadback = false;
};

struct IntentInfo {
  std::set<std::string> slots;
  std::set<std::string> required;  // parse.required
  std::set<std::string> optional;  // parse.optional
  std::string shortName;           // D-429: for the screens, e.g. "Taxi"; empty: show the id
  bool placeholder = true;
};

struct StepResponse {
  bool say = false;
  std::string reply;
  std::map<std::string, std::string> withField;  // optional field given -> this reply instead (O-3)
};
struct StepTransition { Outcome on = Outcome::Correct; std::string to; bool errorBranch = false; };
struct StepOnEnter { std::string reply; Ns delay = 0; };
struct Expectation {
  std::string intent;
  std::vector<std::string> required;
  std::vector<std::string> optional;             // accepted if given, at most one; the readback includes it
  std::map<std::string, std::string> mustMatch;  // field -> generated parameter
};
struct ScenarioStep {
  std::string id, label, phase;
  std::vector<Expectation> expects;
  std::map<Outcome, StepResponse> responses;
  std::vector<StepTransition> transitions;
  std::optional<StepOnEnter> onEnter;
  Ns timeout = 0;  // 0: none
  int maxPilotCalls = 0;
  int maxSayAgain = 0;  // 0: no limit
  bool final = false;
};
struct ParamGenerator { std::string pool, mustServe; };
struct Scenario {
  std::string id, name, category, airfield, initialPhase, initialStep;
  bool placeholder = true;
  // D-429: kept for the screens (5d, Builder); the engine does not use them.
  std::vector<std::string> objectives;
  std::string difficulty;  // empty, basic, intermediate or advanced
  std::string sopRef, validatedBy, validatedAt;  // as in the file; validated needs all three
  std::map<std::string, ParamGenerator> generators;
  std::vector<ScenarioStep> steps;
  PressRule pressDuringPilot = PressRule::Interrupt;
  bool repeatInterruptedReply = true;
  Ns maxDuration = 0;  // 0: none
  const ScenarioStep* step(const std::string& id) const;
};

// A scenario with everything it refers to; complete and checked.
struct Exercise {
  Scenario scenario;
  Airfield airfield;
  std::map<std::string, PilotReply> replies;   // those the scenario uses
  std::map<std::string, IntentInfo> intents;   // all known
  bool placeholder = true;                     // any text it uses is unvalidated
  std::string hash;                            // of every file read
};

struct ScenarioSources {
  std::string scenarioFile;
  std::string airfieldDir;     // <id>.json
  std::string phraseologyDir;  // *.json intents, replies/*.json pilot replies
};
ScenarioSources scenarioSources(const std::string& dataDir, const std::string& scenarioFile);

// All errors are collected. False if there is any; out is then unchanged.
// Anything the engine does not implement yet (guards, safety rules, unexpected events) is
// refused, never ignored.
bool loadExercise(const ScenarioSources& src, Exercise& out, std::vector<LoadError>& errors);

// Empty if the exercise may run in this mode; Assessed is refused while any text is placeholder (P-11).
std::string modeError(const Exercise& e, ExerciseMode mode);

// {slot} names in order. False for an unclosed, nested, empty or badly named slot.
bool templateSlots(const std::string& text, std::vector<std::string>& slots);

// Generated parameters, always mutually valid. Same seed, same values on every machine.
using ParamValues = std::map<std::string, std::string>;
std::uint64_t combinationCount(const Scenario& s, const Airfield& a);
bool generateParams(const Scenario& s, const Airfield& a, std::uint64_t seed, ParamValues& out);
// D-334, Planner: every valid combination, at most max (tooMany is set if there are more).
std::vector<ParamValues> combinations(const Scenario& s, const Airfield& a, std::size_t max, bool& tooMany);
// Values given by a plan: each one is a value of its pool and fits the others. False with the reason, e.g.
// "B2 does not serve 09". Nothing is corrected.
bool fitsParams(const Scenario& s, const Airfield& a, const ParamValues& v, std::string& why);

}  // namespace atc
