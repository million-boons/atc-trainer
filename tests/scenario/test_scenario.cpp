#include <gtest/gtest.h>

#include <map>
#include <random>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/PathResolver.h"
#include "scenario/Scenario.h"
using namespace atc;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::string shippedData() {
  Paths p;
  std::string error;
  EXPECT_TRUE(resolvePaths(systemPathInputs(), p, error)) << error;
  return p.data;
}

json readJson(const fs::path& f) {
  std::ifstream in(f);
  return json::parse(in, nullptr, false);
}

// The shipped departure, copied to a temporary folder so that each test can break one thing.
struct Departure : ::testing::Test {
  fs::path dir;
  json scenario, airfield, replies, phraseology;
  std::vector<LoadError> errors;
  Exercise e;

  void SetUp() override {
    static int n = 0;
    dir = fs::temp_directory_path() / ("atc_scenario_" + std::to_string(getpid()) + "_" + std::to_string(n++));
    const fs::path d = shippedData();
    scenario = readJson(d / "scenarios" / "DEPARTURE-01.json");
    airfield = readJson(d / "airfields" / "SAMPLE-AF.json");
    replies = readJson(d / "phraseology" / "replies" / "placeholder.json");
    phraseology = readJson(d / "phraseology" / "placeholder.json");
    ASSERT_TRUE(scenario.is_object() && airfield.is_object() && replies.is_object() && phraseology.is_object());
    // LINE_UP comes with package 2e; until then the tests add it.
    bool have = false;
    for (const auto& i : phraseology["intents"]) have = have || i["id"] == "LINE_UP";
    if (!have)
      phraseology["intents"].push_back({{"id", "LINE_UP"},
                                        {"examples", json::array({"{callsign} line up runway {runway}"})},
                                        {"validation", {{"status", "draft"}}}});
  }
  void TearDown() override {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  void write(const fs::path& f, const json& j) {
    fs::create_directories(f.parent_path());
    std::ofstream(f) << j.dump(1);
  }
  bool load() {
    write(dir / "scenarios" / "DEPARTURE-01.json", scenario);
    write(dir / "airfields" / "SAMPLE-AF.json", airfield);
    write(dir / "phraseology" / "placeholder.json", phraseology);
    write(dir / "phraseology" / "replies" / "placeholder.json", replies);
    return loadExercise(scenarioSources(dir.string(), (dir / "scenarios" / "DEPARTURE-01.json").string()), e, errors);
  }
  std::string all() const {
    std::string s;
    for (const auto& x : errors) s += toString(x) + "\n";
    return s;
  }
  // Loading fails and one error has this file, path and text.
  void expectError(const std::string& file, const std::string& path, const std::string& what) {
    EXPECT_FALSE(load()) << path;
    for (const auto& x : errors)
      if (fs::path(x.file).filename() == file && x.path == path && x.what.find(what) != std::string::npos) return;
    ADD_FAILURE() << "no error in " << file << " at " << path << " with: " << what << "\ngot:\n" << all();
  }
  json& step(int i) { return scenario["steps"][static_cast<std::size_t>(i)]; }
};
const char* const kFile = "DEPARTURE-01.json";
}  // namespace

TEST_F(Departure, ShippedScenarioLoads) {
  ASSERT_TRUE(load()) << all();
  EXPECT_EQ(e.scenario.steps.size(), 4u);
  EXPECT_EQ(e.scenario.initialStep, "TAXI");
  ASSERT_TRUE(e.scenario.step("LINE_UP"));
  ASSERT_TRUE(e.scenario.step("TAXI")->onEnter.has_value());
  EXPECT_EQ(e.scenario.step("TAXI")->onEnter->delay, ms(1000));
  EXPECT_TRUE(e.scenario.step("DONE")->final);
  EXPECT_EQ(e.scenario.step("TAXI")->timeout, ms(30'000));
  EXPECT_EQ(e.hash.size(), 16u);
}

TEST_F(Departure, OnlyCorrectAdvancesInTheShippedData) {
  ASSERT_TRUE(load()) << all();
  for (const auto& s : e.scenario.steps)
    for (const auto& t : s.transitions) EXPECT_EQ(t.on, Outcome::Correct) << s.id;
}

TEST_F(Departure, PlaceholderTextRefusesAssessedMode) {
  ASSERT_TRUE(load()) << all();
  EXPECT_TRUE(e.placeholder);
  EXPECT_EQ(modeError(e, ExerciseMode::Practice), "");
  EXPECT_NE(modeError(e, ExerciseMode::Assessed).find("P-11"), std::string::npos);
}

TEST_F(Departure, OnePlaceholderReplyIsEnoughToRefuseAssessedMode) {
  const json ok = {{"status", "validated"}, {"sopRef", "TEST"}, {"validated_by", "TEST"}, {"validated_at", "TEST"}};
  const json okText = {{"status", "validated"}, {"sopRef", "TEST"}, {"validated_by", "TEST"}, {"validated_at", "TEST"}};
  scenario["validation"] = airfield["validation"] = ok;
  replies["validation"] = phraseology["validation"] = {{"status", "validated"}};
  for (auto& r : replies["replies"]) r["validation"] = okText;
  for (auto& i : phraseology["intents"]) i["validation"] = okText;
  ASSERT_TRUE(load()) << all();
  EXPECT_EQ(modeError(e, ExerciseMode::Assessed), "");
  replies["replies"][0]["validation"] = {{"status", "draft"}};
  ASSERT_TRUE(load()) << all();
  EXPECT_NE(modeError(e, ExerciseMode::Assessed), "");
}

TEST_F(Departure, ValidatedNeedsItsRecord) {
  scenario["validation"] = {{"status", "validated"}, {"sopRef", "TEST"}};
  expectError(kFile, "/validation", "validated_by");
  expectError(kFile, "/validation", "validated_at");  // D-429
  scenario["validation"]["validatedBy"] = "TEST";      // the old name
  expectError(kFile, "/validation/validatedBy", "validated_by");
}

// D-429: what the screens need is kept.
TEST_F(Departure, ScreenFieldsAreKept) {
  scenario["objectives"] = {"PLACEHOLDER one", "PLACEHOLDER two"};
  scenario["difficulty"] = "intermediate";
  scenario["validation"] = {{"status", "placeholder"}, {"sopRef", "SOP-1"}};
  ASSERT_TRUE(load()) << all();
  EXPECT_EQ(e.scenario.objectives, (std::vector<std::string>{"PLACEHOLDER one", "PLACEHOLDER two"}));
  EXPECT_EQ(e.scenario.difficulty, "intermediate");
  EXPECT_EQ(e.scenario.sopRef, "SOP-1");
  EXPECT_EQ(e.scenario.validatedAt, "");
  EXPECT_EQ(e.intents.at("TAXI_INSTRUCTION").shortName, "Taxi");
  EXPECT_EQ(e.intents.at("TAXI_INSTRUCTION").required, (std::set<std::string>{"callsign", "taxiway", "runway"}));
  phraseology["intents"][0]["shortName"] = std::string(25, 'x');
  expectError("placeholder.json", "/intents/0/shortName", "at most 24");
}

// D-429: a reply says which intent it reads back, and whether it says every required value.
TEST_F(Departure, CorrectIsAnsweredWithAReadbackOfTheStepsIntent) {
  ASSERT_TRUE(load()) << all();
  EXPECT_EQ(e.replies.at("READBACK_TAXI").readbackOf, "TAXI_INSTRUCTION");
  EXPECT_TRUE(e.replies.at("READBACK_TAXI").completeReadback);
  const json good = replies;
  const auto at = [&](const std::string& id) -> json& {
    for (auto& r : replies["replies"])
      if (r["id"] == id) return r;
    throw std::runtime_error(id);
  };
  at("READBACK_TAKEOFF")["text"] = "rolling";  // deliberately incomplete: still a readback
  ASSERT_TRUE(load()) << all();
  EXPECT_FALSE(e.replies.at("READBACK_TAKEOFF").completeReadback);
  replies = good;
  at("READBACK_LINE_UP").erase("readbackOf");
  expectError(kFile, "/steps/1/responses/CORRECT/reply", "READBACK_LINE_UP is no readback");
  replies = good;
  at("READBACK_LINE_UP")["readbackOf"] = "TAKEOFF_CLEARANCE";
  expectError(kFile, "/steps/1/responses/CORRECT/reply", "which this step does not expect");
  replies = good;
  at("READBACK_TAXI_HOLD_SHORT").erase("readbackOf");
  expectError(kFile, "/steps/0/responses/CORRECT/withField/hold_short", "is no readback");
  replies = good;
  at("QUERY")["readbackOf"] = "NOWHERE";
  expectError("placeholder.json", "/replies/9/readbackOf", "no intent named NOWHERE");
}

// O-3: what is given is read back, and a field is optional only where the intent says so.
TEST_F(Departure, OptionalFieldNeedsItsReadback) {
  ASSERT_TRUE(load()) << all();
  EXPECT_EQ(e.scenario.step("TAXI")->expects[0].optional, std::vector<std::string>{"hold_short"});
  EXPECT_TRUE(e.replies.count("READBACK_TAXI_HOLD_SHORT"));  // its clips are required too
  const json good = scenario;
  step(0)["responses"]["CORRECT"].erase("withField");
  expectError(kFile, "/steps/0/responses", "withField/hold_short");
  scenario = good;
  step(0)["expects"][0]["optional"] = {"heading"};
  expectError(kFile, "/steps/0/expects/0/optional", "no optional field heading");
  scenario = good;
  step(0)["expects"][0]["optional"] = {"hold_short", "runway"};
  expectError(kFile, "/steps/0/expects/0/optional", "at most one");
  scenario = good;
  step(0)["responses"]["INCORRECT"]["withField"] = {{"hold_short", "QUERY"}};
  expectError(kFile, "/steps/0/responses", "only for CORRECT");
  scenario = good;
  step(0)["responses"]["CORRECT"]["withField"]["hold_short"] = "NOWHERE";
  expectError(kFile, "/steps/0/responses/CORRECT/withField/hold_short", "no reply named NOWHERE");
}

TEST_F(Departure, ErrorsNameFileAndPath) {
  step(0)["transitions"][0]["to"] = "NOWHERE";
  expectError(kFile, "/steps/0/transitions", "no step named NOWHERE");
}
TEST_F(Departure, AllErrorsAreReported) {
  scenario["difficulty"] = "silly";
  airfield["runways"][0] = "99";
  EXPECT_FALSE(load());
  EXPECT_NE(all().find("/difficulty"), std::string::npos) << all();
  EXPECT_NE(all().find("/runways/0"), std::string::npos) << all();
}
TEST_F(Departure, UnknownNameIsRefused) {
  step(0)["timeoutSecond"] = 5;
  expectError(kFile, "/steps/0/timeoutSecond", "unknown name");
}
TEST_F(Departure, MissingNameIsRefused) {
  scenario.erase("pttRules");
  expectError(kFile, "/", "missing pttRules");
}
TEST_F(Departure, WrongTypeIsRefused) {
  step(1)["timeoutSeconds"] = "30";
  expectError(kFile, "/steps/1/timeoutSeconds", "whole number");
}
TEST_F(Departure, WrongOutcomeNeedsAnErrorBranch) {
  step(0)["transitions"].push_back({{"on", "INCORRECT"}, {"to", "DONE"}});
  expectError(kFile, "/steps/0/transitions/1", "errorBranch");
  step(0)["transitions"][1]["errorBranch"] = true;
  EXPECT_TRUE(load()) << all();
}
TEST_F(Departure, NoTransitionOnTimeout) {  // D-424
  step(0)["transitions"].push_back({{"on", "TIMEOUT"}, {"to", "DONE"}, {"errorBranch", true}});
  expectError(kFile, "/steps/0/transitions/1", "TIMEOUT");
}
TEST_F(Departure, StepWithoutCorrectIsRefused) {
  step(2)["transitions"] = json::array();
  expectError(kFile, "/steps/2", "no transition on CORRECT");
}
TEST_F(Departure, UnreachableStepIsRefused) {
  step(0)["transitions"][0]["to"] = "TAKEOFF";
  expectError(kFile, "/steps/1", "cannot be reached");
}
TEST_F(Departure, NoReachableEndIsRefused) {
  step(2)["transitions"][0]["to"] = "TAXI";
  expectError(kFile, "/", "no final step can be reached");
}
TEST_F(Departure, EveryPossibleOutcomeNeedsAResponse) {
  step(1)["responses"].erase("WRONG_CALLSIGN");
  expectError(kFile, "/steps/1/responses", "missing WRONG_CALLSIGN");
}
TEST_F(Departure, TimeoutResponseIsNeededOnlyWithATimeout) {
  step(1)["responses"].erase("TIMEOUT");
  expectError(kFile, "/steps/1/responses", "missing TIMEOUT");
  step(1).erase("timeoutSeconds");
  EXPECT_TRUE(load()) << all();
}
TEST_F(Departure, RequiredFieldWithoutMatchIsRefused) {
  step(1)["expects"][0]["mustMatch"].erase("runway");
  expectError(kFile, "/steps/1/expects/0", "runway needs a mustMatch");
}
TEST_F(Departure, CallsignIsAlwaysRequired) {
  step(1)["expects"][0]["required"] = {"runway"};
  step(1)["expects"][0]["mustMatch"] = {{"runway", "{runway}"}};
  expectError(kFile, "/steps/1/expects/0/required", "callsign");
}
TEST_F(Departure, UnknownIntentSlotAndGeneratorAreRefused) {
  step(0)["expects"][0]["intent"] = "FLY_AWAY";
  expectError(kFile, "/steps/0/expects/0/intent", "FLY_AWAY");
  step(0)["expects"][0]["intent"] = "TAKEOFF_CLEARANCE";
  expectError(kFile, "/steps/0/expects/0/required", "no slot taxiway");
  step(0)["expects"][0]["intent"] = "TAXI_INSTRUCTION";
  step(0)["expects"][0]["mustMatch"]["taxiway"] = "{gate}";
  expectError(kFile, "/steps/0/expects/0/mustMatch/taxiway", "no generator named gate");
}
TEST_F(Departure, UnknownReplyIsRefused) {
  step(0)["responses"]["CORRECT"]["reply"] = "NONE";
  expectError(kFile, "/steps/0/responses/CORRECT/reply", "no reply named NONE");
}
TEST_F(Departure, ReplySlotWithoutGeneratorIsRefused) {
  replies["replies"][0]["text"] = "{callsign}, heading {heading}";
  expectError(kFile, "/steps/0/onEnter/say", "{heading}");
}
TEST_F(Departure, BadReplySlotIsRefused) {
  replies["replies"][0]["text"] = "{callsign, request taxi";
  expectError("placeholder.json", "/replies/0/text", "bad {slot}");
}
TEST_F(Departure, UnsupportedFeaturesAreRefusedNotIgnored) {
  step(0)["safetyRules"] = {"RULE"};
  expectError(kFile, "/steps/0/safetyRules", "not supported");
  step(0).erase("safetyRules");
  step(0)["transitions"][0]["guard"] = {{"left", "a"}, {"op", "eq"}, {"right", "b"}};
  expectError(kFile, "/steps/0/transitions/0/guard", "not supported");
  step(0)["transitions"][0].erase("guard");
  scenario["unexpectedEvents"] = json::array({json::object()});
  expectError(kFile, "/unexpectedEvents", "not supported");
}
TEST_F(Departure, TaxiwayMustServeAKnownRunway) {
  airfield["taxiways"][0]["serves"] = {"18"};
  expectError("SAMPLE-AF.json", "/taxiways/0/serves", "unknown runway 18");
}
TEST_F(Departure, NoValidCombinationIsRefused) {
  airfield["taxiways"] = json::array();
  expectError(kFile, "/generators", "no valid combination");
}
TEST_F(Departure, AirfieldNameCannotLeaveItsFolder) {
  scenario["airfield"] = "../SAMPLE-AF";
  expectError(kFile, "/airfield", "only");
}
TEST_F(Departure, MissingFilesAreNamed) {
  scenario["airfield"] = "OTHER";
  expectError("OTHER.json", "/", "cannot read");
}
TEST_F(Departure, FailedLoadLeavesTheOldExercise) {
  ASSERT_TRUE(load()) << all();
  const std::string hash = e.hash;
  scenario["difficulty"] = "silly";
  ASSERT_FALSE(load());
  EXPECT_EQ(e.hash, hash);
}
TEST_F(Departure, HashChangesWithAnyFile) {
  ASSERT_TRUE(load()) << all();
  const std::string before = e.hash;
  replies["replies"][0]["text"] = "{callsign}, request taxi now";
  ASSERT_TRUE(load()) << all();
  EXPECT_NE(e.hash, before);
}

TEST_F(Departure, GeneratedValuesAreAlwaysValidAndAllOccur) {
  ASSERT_TRUE(load()) << all();
  EXPECT_EQ(combinationCount(e.scenario, e.airfield), 4u);  // 2 runways with one taxiway each, 2 types with one callsign each
  std::set<std::string> seen;
  for (std::uint64_t seed = 0; seed < 300; ++seed) {
    ParamValues v;
    ASSERT_TRUE(generateParams(e.scenario, e.airfield, seed, v));
    ASSERT_EQ(v.size(), 4u);
    EXPECT_EQ(v["taxiway"], v["runway"] == "09" ? "A3" : "B2");
    EXPECT_EQ(v["callsign"], v["aircraftType"] == "C295" ? "EAGLE-421" : "FALCON-732");
    seen.insert(v["runway"] + v["callsign"]);
  }
  EXPECT_EQ(seen.size(), 4u);
}
// D-334: the draw gives every combination an equal share. 40 000 fixed seeds, so the test is the same on every run;
// the tolerance is about 6 standard deviations of a fair draw.
TEST_F(Departure, EveryCombinationIsDrawnAboutEquallyOften) {
  ASSERT_TRUE(load()) << all();
  std::map<std::string, int> n;
  std::mt19937_64 seeds(20261126);
  const int draws = 40'000;
  for (int i = 0; i < draws; ++i) {
    ParamValues v;
    ASSERT_TRUE(generateParams(e.scenario, e.airfield, seeds() >> 11, v));  // below 2^53, as the program's seeds
    ++n[v["callsign"] + " " + v["runway"]];
  }
  ASSERT_EQ(n.size(), 4u);
  for (const auto& [k, c] : n) EXPECT_NEAR(c, draws / 4, 520) << k;
}
TEST_F(Departure, CombinationsAndFitsAgree) {
  ASSERT_TRUE(load()) << all();
  bool tooMany = true;
  const auto all = combinations(e.scenario, e.airfield, 100, tooMany);
  EXPECT_FALSE(tooMany);
  ASSERT_EQ(all.size(), 4u);
  std::string why;
  for (const auto& v : all) EXPECT_TRUE(fitsParams(e.scenario, e.airfield, v, why)) << why;
  EXPECT_EQ(combinations(e.scenario, e.airfield, 3, tooMany).size(), 3u);
  EXPECT_TRUE(tooMany);
  ParamValues v = all[0];
  v["taxiway"] = v["runway"] == "09" ? "B2" : "A3";
  EXPECT_FALSE(fitsParams(e.scenario, e.airfield, v, why));
  EXPECT_EQ(why, v["taxiway"] + " does not serve " + v["runway"]);
  v = all[0];
  v["runway"] = "18";
  EXPECT_FALSE(fitsParams(e.scenario, e.airfield, v, why));
  EXPECT_NE(why.find("18"), std::string::npos);
  v = all[0];
  v.erase("callsign");
  EXPECT_FALSE(fitsParams(e.scenario, e.airfield, v, why));
  v = all[0];
  v["extra"] = "x";
  EXPECT_FALSE(fitsParams(e.scenario, e.airfield, v, why));
}
TEST_F(Departure, SameSeedSameValues) {
  ASSERT_TRUE(load()) << all();
  for (std::uint64_t seed = 0; seed < 20; ++seed) {
    ParamValues a, b;
    ASSERT_TRUE(generateParams(e.scenario, e.airfield, seed, a));
    ASSERT_TRUE(generateParams(e.scenario, e.airfield, seed, b));
    EXPECT_EQ(a, b);
  }
}
TEST_F(Departure, AircraftTypeMustAllowTheCategory) {
  airfield["aircraftTypes"][0]["categories"] = {"landing"};
  ASSERT_TRUE(load()) << all();
  for (std::uint64_t seed = 0; seed < 50; ++seed) {
    ParamValues v;
    ASSERT_TRUE(generateParams(e.scenario, e.airfield, seed, v));
    EXPECT_EQ(v["callsign"], "FALCON-732");
  }
}

TEST(TemplateSlots, FindsNamesInOrderAndRefusesBadOnes) {
  std::vector<std::string> s;
  ASSERT_TRUE(templateSlots("runway {runway}, {callsign}", s));
  EXPECT_EQ(s, (std::vector<std::string>{"runway", "callsign"}));
  ASSERT_TRUE(templateSlots("no slots", s));
  EXPECT_TRUE(s.empty());
  for (const char* bad : {"{", "}", "{}", "{a", "a}", "{a{b}}", "{a b}", "{runway@initial}"})
    EXPECT_FALSE(templateSlots(bad, s)) << bad;
}

// D-430: what the circuit needs, on the departure data (test values, not the circuit's wording).
namespace {
struct Circuit : Departure {
  void SetUp() override {
    Departure::SetUp();
    phraseology["intents"].push_back({{"id", "TEST_CHALLENGE"},
                                      {"examples", json::array({"{callsign} confirm runway {runway}"})},
                                      {"parse", {{"required", {"callsign"}}, {"optional", {"runway"}}}},
                                      {"validation", {{"status", "draft"}}}});
    replies["replies"].push_back({{"id", "TEST_LINING_UP"}, {"readbackOf", "LINE_UP"}, {"text", "lining up, {callsign}"}});
    replies["replies"].push_back({{"id", "TEST_ROLLING"}, {"readbackOf", "TAKEOFF_CLEARANCE"}, {"text", "rolling, {callsign}"}});
  }
  json check(double rate = 0.25) const {
    return {{"incompleteRate", rate},
            {"challenge", {{"intent", "TEST_CHALLENGE"}, {"required", {"callsign"}}, {"optional", {"runway"}},
                           {"mustMatch", {{"callsign", "{callsign}"}, {"runway", "{runway}"}}}}}};
  }
};
}  // namespace

TEST_F(Circuit, IncompleteReadbackNeedsItsCheckAndAFullOne) {
  const json good = scenario;
  step(1)["responses"]["CORRECT"]["incompleteReply"] = "TEST_LINING_UP";
  expectError(kFile, "/steps/1/responses/CORRECT/incompleteReply", "needs readbackCheck");
  scenario["readbackCheck"] = check();
  ASSERT_TRUE(load()) << all();
  EXPECT_EQ(e.scenario.step("LINE_UP")->responses.at(Outcome::Correct).incompleteReply, "TEST_LINING_UP");
  ASSERT_TRUE(e.scenario.readbackCheck);
  EXPECT_DOUBLE_EQ(e.scenario.readbackCheck->incompleteRate, 0.25);
  EXPECT_EQ(e.scenario.readbackCheck->challenge.optional, std::vector<std::string>{"runway"});
  EXPECT_TRUE(e.replies.count("TEST_LINING_UP"));  // its clips are required too
  const json withCheck = scenario;
  step(1)["responses"]["CORRECT"]["incompleteReply"] = "READBACK_LINE_UP";
  expectError(kFile, "/steps/1/responses/CORRECT/incompleteReply", "not incomplete");
  scenario = withCheck;
  scenario["readbackCheck"]["incompleteRate"] = 1.5;
  expectError(kFile, "/readbackCheck/incompleteRate", "from 0 to 1");
  scenario = withCheck;
  step(2)["responses"]["CORRECT"]["incompleteReply"] = "TEST_ROLLING";  // TAKEOFF leads to DONE
  expectError(kFile, "/steps/2/responses/CORRECT/incompleteReply", "next step is final");
  scenario = withCheck;
  step(1)["responses"]["INCORRECT"]["incompleteReply"] = "TEST_LINING_UP";
  expectError(kFile, "/steps/1/responses", "only for CORRECT");
  scenario = good;
  step(1)["responses"]["CORRECT"]["alternates"] = {"TEST_LINING_UP"};
  ASSERT_TRUE(load()) << all();  // an alternate needs no check
  EXPECT_EQ(e.scenario.step("LINE_UP")->responses.at(Outcome::Correct).alternates, std::vector<std::string>{"TEST_LINING_UP"});
  step(1)["responses"]["CORRECT"]["alternates"] = {"READBACK_TAKEOFF"};
  expectError(kFile, "/steps/1/responses/CORRECT/alternates/0", "which this step does not expect");
}

TEST_F(Circuit, ChallengeMayCheckAnOptionalValue) {
  scenario["readbackCheck"] = check();
  scenario["readbackCheck"]["challenge"]["mustMatch"]["taxiway"] = "{taxiway}";
  expectError(kFile, "/readbackCheck/challenge/mustMatch", "neither a required nor an optional field");
  scenario["readbackCheck"] = check();
  scenario["readbackCheck"]["challenge"]["intent"] = "NOWHERE";
  expectError(kFile, "/readbackCheck/challenge/intent", "unknown intent NOWHERE");
}

TEST_F(Circuit, FrequenciesComeFromTheirUnit) {
  airfield["units"] = {{{"id", "GROUND"}, {"spoken", "sample ground"}, {"frequencies", {"121.7"}}},
                       {{"id", "TOWER"}, {"spoken", "sample tower"}, {"frequencies", {"118.275"}}}};
  scenario["generators"]["groundFrequency"] = {{"pool", "frequencies"}, {"unit", "GROUND"}};
  ASSERT_TRUE(load()) << all();
  ASSERT_EQ(e.airfield.units.size(), 2u);
  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    ParamValues v;
    ASSERT_TRUE(generateParams(e.scenario, e.airfield, seed, v));
    EXPECT_EQ(v.at("groundFrequency"), "121.7");
  }
  const json good = airfield;
  airfield["units"][0]["frequencies"] = {"121,7"};
  expectError("SAMPLE-AF.json", "/units/0/frequencies/0", "such as 121.7");
  airfield = good;
  scenario["generators"]["groundFrequency"]["unit"] = "APRON";
  expectError(kFile, "/generators/groundFrequency/unit", "no unit APRON");
  scenario["generators"]["groundFrequency"] = {{"pool", "frequencies"}};
  expectError(kFile, "/generators/groundFrequency", "needs a unit");
  scenario["generators"]["groundFrequency"] = {{"pool", "runways"}, {"unit", "GROUND"}};
  expectError(kFile, "/generators/groundFrequency/unit", "only for pool frequencies");
}

TEST_F(Circuit, AValueTheTraineeChoosesNeedsARange) {
  for (auto& i : phraseology["intents"])
    if (i["id"] == "LINE_UP") i["examples"].push_back("{callsign} line up runway {runway} read you {readability}");
  step(1)["expects"][0]["required"].push_back("readability");
  expectError(kFile, "/steps/1/expects/0", "needs a mustMatch or a check");
  step(1)["expects"][0]["check"] = {{"readability", {{"range", {1, 5}}}}};
  ASSERT_TRUE(load()) << all();
  EXPECT_EQ(e.scenario.step("LINE_UP")->expects[0].ranges.at("readability"), (std::pair<int, int>{1, 5}));
  step(1)["expects"][0]["check"]["readability"]["range"] = {5, 1};
  expectError(kFile, "/steps/1/expects/0/check/readability/range", "from at most to");
}

// The shipped circuit (D-430, D-431). Needs stt's circuit intents (stt db77be4) in data/phraseology.
TEST(ShippedCircuit, Loads) {
  const std::string d = shippedData();
  Exercise e;
  std::vector<LoadError> errors;
  ASSERT_TRUE(loadExercise(scenarioSources(d, d + "/scenarios/CIRCUIT-RAJALI.json"), e, errors)) << (errors.empty() ? "" : toString(errors[0]));
  EXPECT_EQ(e.scenario.steps.size(), 10u);
  EXPECT_EQ(e.scenario.initialStep, "RADIO_CHECK");
  ASSERT_TRUE(e.scenario.readbackCheck);
  EXPECT_EQ(e.scenario.readbackCheck->challenge.intent, "READBACK_CHALLENGE");
  EXPECT_EQ(e.scenario.step("TAKEOFF")->responses.at(Outcome::Correct).incompleteReply, "CIRCUIT_READBACK_TAKEOFF_SHORT");
  EXPECT_FALSE(e.replies.at("CIRCUIT_READBACK_TAKEOFF_SHORT").completeReadback);
  EXPECT_TRUE(e.replies.at("CIRCUIT_READBACK_TAKEOFF").completeReadback);
  EXPECT_EQ(e.scenario.step("RADIO_CHECK")->expects[0].ranges.at("readability"), (std::pair<int, int>{1, 5}));
  EXPECT_TRUE(e.placeholder);
  ParamValues v;
  ASSERT_TRUE(generateParams(e.scenario, e.airfield, 1, v));
  EXPECT_EQ(v, (ParamValues{{"callsign", "FALCON-12"}, {"frequency", "121.7"}, {"runway", "05"}}));
}
