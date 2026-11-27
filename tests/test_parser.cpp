#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "core/PathResolver.h"
#include "parser/Parser.h"

#include <map>
#include <nlohmann/json.hpp>
using namespace atc;
using C = RecognitionClass;

namespace {
std::string rulesFile() {
  Paths p;
  std::string e;
  EXPECT_TRUE(resolvePaths(systemPathInputs(), p, e)) << e;
  return p.data + "/phraseology/placeholder.json";
}
const ParserRules& rules() {
  static const ParserRules r = [] {
    ParserRules x;
    std::string e;
    EXPECT_TRUE(loadParserRules(rulesFile(), x, e)) << e;
    return x;
  }();
  return r;
}
ParsedInstruction parse(const std::string& s) { return parseInstruction({s, {}}, rules()); }
std::string value(const ParsedInstruction& p, const std::string& k) {
  const auto it = p.fields.find(k);
  return it == p.fields.end() ? "-" : it->second.uncertain ? "?" : it->second.value;
}
bool because(const ParsedInstruction& p, const std::string& part) {
  for (const auto& r : p.reasons)
    if (r.find(part) != std::string::npos) return true;
  return false;
}
std::string loadChanged(const std::string& from, const std::string& to) {
  std::ifstream f(rulesFile());
  std::stringstream s;
  s << f.rdbuf();
  std::string t = s.str();
  const auto at = t.find(from);
  EXPECT_NE(at, std::string::npos) << from;
  t.replace(at, from.size(), to);
  const std::string path = testing::TempDir() + "parser_rules_test.json";
  std::ofstream(path) << t;
  ParserRules r;
  std::string e;
  const bool ok = loadParserRules(path, r, e);
  std::remove(path.c_str());
  return ok ? "" : e;
}
std::vector<std::string> split(const std::string& s) {
  std::istringstream in(s);
  std::vector<std::string> v;
  for (std::string w; in >> w;) v.push_back(w);
  return v;
}
const char* const kGood[] = {"eagle four two one taxi via alpha three runway zero nine",
                             "eagle four two one taxi via alpha three hold short runway zero nine left",
                             "hawk six seven four line up runway two seven",
                             "cobra two zero five runway three six cleared for takeoff"};
}  // namespace

TEST(Parser, ThreeIntents) {
  auto p = parse(kGood[0]);
  EXPECT_EQ(p.cls, C::Recognized);
  EXPECT_EQ(p.intent, "TAXI_INSTRUCTION");
  EXPECT_EQ(value(p, "callsign"), "EAGLE-421");
  EXPECT_EQ(value(p, "taxiway"), "A3");
  EXPECT_EQ(value(p, "runway"), "09");
  EXPECT_EQ(value(p, "hold_short"), "-");
  EXPECT_TRUE(p.reasons.empty());

  p = parse("Eagle Four Two One, taxi via Alpha Three, hold short runway Zero Nine.");
  EXPECT_EQ(p.cls, C::Recognized);
  EXPECT_EQ(value(p, "hold_short"), "yes");
  EXPECT_EQ(p.fields.at("runway").heard, "Zero Nine");

  p = parse(kGood[2]);
  EXPECT_EQ(p.cls, C::Recognized);
  EXPECT_EQ(p.intent, "LINE_UP");
  EXPECT_EQ(value(p, "runway"), "27");

  p = parse("Falcon 318, runway 09L, cleared for take-off.");
  EXPECT_EQ(p.intent, "TAKEOFF_CLEARANCE");
  EXPECT_EQ(value(p, "callsign"), "FALCON-318");
}
TEST(Parser, SpokenVariantsAndWrittenForms) {
  EXPECT_EQ(value(parse("eagle fife tree niner line up runway zero niner"), "callsign"), "EAGLE-539");
  EXPECT_EQ(value(parse("eagle fife tree niner line up runway zero niner"), "runway"), "09");
  auto p = parse("Cobra 890 taxi via X-ray 4 runway 18");
  EXPECT_EQ(p.cls, C::Recognized);
  EXPECT_EQ(value(p, "taxiway"), "X4");
  EXPECT_EQ(value(parse("Cobra 890 taxi via A3 runway 18"), "taxiway"), "A3");
  EXPECT_EQ(value(parse("runway three six right hawk one five six cleared for takeoff"), "runway"), "36R");
}
TEST(Parser, EveryRunwayIsReadAsSaid) {  // bias rule: no value is preferred
  const char* d[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};
  for (int n = 0; n < 100; ++n) {
    const std::string said = std::string(d[n / 10]) + " " + d[n % 10], id{char('0' + n / 10), char('0' + n % 10)};
    for (const auto& [side, letter] : {std::pair{"", ""}, {" left", "L"}, {" right", "R"}, {" center", "C"}}) {
      const auto p = parse("eagle four two one line up runway " + said + side);
      if (n >= 1 && n <= 36) {
        EXPECT_EQ(p.cls, C::Recognized);
        EXPECT_EQ(value(p, "runway"), id + letter);
      } else {
        EXPECT_EQ(p.cls, C::Ambiguous) << said;
        EXPECT_EQ(value(p, "runway"), "?");
      }
    }
  }
}
TEST(Parser, WrongDigitCountIsNeverCutToFit) {
  for (const char* s : {"eagle four line up runway zero nine", "eagle four two one one line up runway zero nine",
                        "eagle four two one line up runway nine", "eagle four two one line up runway zero nine nine",
                        "eagle four two one taxi via alpha three three runway zero nine", "eagle four two one taxi via alpha runway zero nine"}) {
    const auto p = parse(s);
    EXPECT_EQ(p.cls, C::Ambiguous) << s;
    EXPECT_TRUE(because(p, "uncertain")) << s;
  }
}
TEST(Parser, DecimalRunwayIsAmbiguousNotCorrected) {  // 06 section 7
  const auto p = parse("Hawk 674 line up runway 0.9");
  EXPECT_EQ(p.cls, C::Ambiguous);
  EXPECT_TRUE(p.fields.at("runway").uncertain);
  EXPECT_EQ(p.fields.at("runway").value, "");
  EXPECT_EQ(p.fields.at("runway").heard, "0.9");
  const auto q = parse("Hawk 674 line up runway 09.5");  // not "09" with something behind it
  EXPECT_EQ(q.cls, C::Ambiguous);
  EXPECT_EQ(q.fields.at("runway").value, "");
}
TEST(Parser, PartialNamesWhatIsMissing) {
  auto p = parse("eagle four two one taxi via alpha three");
  EXPECT_EQ(p.cls, C::Partial);
  EXPECT_EQ(p.missing, std::vector<std::string>{"runway"});
  p = parse("eagle four two one line up runway");
  EXPECT_EQ(p.cls, C::Partial);
  p = parse("runway zero nine cleared for takeoff");
  EXPECT_EQ(p.cls, C::Partial);
  EXPECT_EQ(p.missing, std::vector<std::string>{"callsign"});
}
TEST(Parser, EveryWordMustBeExplained) {
  for (const char* s : {"eagle four two one cancel runway zero nine cleared for takeoff", "eagle four two one runway zero nine not cleared for takeoff",
                        "eagle four two one line up and hold runway zero nine", "eagle four two one run with your zero nine line up",
                        "tiger four two one line up runway zero nine", "four two one line up runway zero nine",
                        "eagle four two one line up runway zero nine correction runway two seven",
                        "eagle four two one taxi via alpha three hole short runway zero nine",
                        "Falcon 318 line up runway 27 R"}) {
    const auto p = parse(s);
    EXPECT_EQ(p.cls, C::Ambiguous) << s;
    EXPECT_TRUE(because(p, "unexplained")) << s;
  }
}
TEST(Parser, TwoInstructionsOrTwoValuesAreAmbiguous) {
  auto p = parse("eagle four two one line up runway zero nine cleared for takeoff");
  EXPECT_EQ(p.cls, C::Ambiguous);
  EXPECT_EQ(p.intent, "");
  p = parse("eagle four two one hawk one five six line up runway zero nine");
  EXPECT_EQ(p.cls, C::Ambiguous);
  EXPECT_EQ(value(p, "callsign"), "?");
  EXPECT_EQ(p.fields.at("callsign").heard, "eagle four two one / hawk one five six");
  EXPECT_EQ(parse("eagle four two one line up runway zero nine runway zero nine").cls, C::Recognized);  // said twice, the same
}
TEST(Parser, AFieldTheIntentDoesNotHave) {
  EXPECT_TRUE(because(parse("eagle four two one hold short runway zero nine line up"), "hold_short does not belong"));
  EXPECT_EQ(parse("eagle four two one via alpha three runway zero nine cleared for takeoff").cls, C::Ambiguous);
}
TEST(Parser, OtherSpeechIsUnrecognized) {
  for (const char* s : {"", "   ", "Please tell the technician to check the printer", "The runway lights look fine today", "Eagle is a very large bird",
                        "hawk six seven four runway two seven cleared to land", "eagle four two one", "eagle four two one turn left heading zero nine zero"})
    EXPECT_EQ(parse(s).cls, C::Unrecognized) << s;
  EXPECT_NE(parse("The taxi to the station was late again").cls, C::Recognized);
}
TEST(Parser, OneDamagedWordNeverInventsAValue) {
  for (const char* s : kGood) {
    const auto want = parse(s);
    ASSERT_EQ(want.cls, C::Recognized) << s;
    const auto w = split(s);
    for (std::size_t i = 0; i < w.size(); ++i) {
      std::string damaged, dropped;
      for (std::size_t k = 0; k < w.size(); ++k) {
        damaged += (k ? " " : "") + (k == i ? "xyzzy" : w[k]);
        if (k != i) dropped += (dropped.empty() ? "" : " ") + w[k];
      }
      EXPECT_NE(parse(damaged).cls, C::Recognized) << damaged;
      if (w[i] != "left") {  // a runway without its side is a full runway
        EXPECT_NE(parse(dropped).cls, C::Recognized) << dropped;
      }
    }
  }
}
TEST(Parser, ConfidenceIsTheLowestInTheSpan) {
  SttResult r{"eagle four two one line up runway zero nine", {}};
  for (const auto& x : split(r.text)) r.words.push_back({" " + x, 0.9});
  r.words[7].conf = 0.4;  // zero
  auto p = parseInstruction(r, rules());
  EXPECT_EQ(p.cls, C::Recognized);  // threshold off by default
  EXPECT_DOUBLE_EQ(p.fields.at("runway").confidence, 0.4);
  EXPECT_DOUBLE_EQ(p.fields.at("callsign").confidence, 0.9);
  ParseOptions o;
  o.minConfidence = 0.5;
  p = parseInstruction(r, rules(), o);
  EXPECT_EQ(p.cls, C::Ambiguous);
  EXPECT_EQ(p.fields.at("runway").heard, "zero nine");
  EXPECT_DOUBLE_EQ(parse(r.text).fields.at("runway").confidence, -1);
}
TEST(Parser, RulesAreChecked) {
  EXPECT_TRUE(rules().placeholder);
  EXPECT_NE(loadChanged("\"cues\": [\"line up\"]", "\"cues\": [\"taxi\"]").find("same cue"), std::string::npos);
  EXPECT_NE(loadChanged("\"cues\": [\"line up\"]", "\"cues\": []").find("no cues"), std::string::npos);
  EXPECT_NE(loadChanged("\"hold_short\": [", "\"runway\": [").find("parse/flags/runway"), std::string::npos);
  EXPECT_NE(loadChanged("\"required\": [\"callsign\", \"runway\"]", "\"required\": [\"callsign\", \"gate\"]").find("unknown slot gate"), std::string::npos);
  EXPECT_NE(loadChanged("\"required\": [\"callsign\", \"runway\"]", "\"required\": [\"altitude\"]").find("end with a value"), std::string::npos);
  EXPECT_NE(loadChanged("\"runway\": [1, 36]", "\"runway\": [36, 1]").find("needs [low, high]"), std::string::npos);
  EXPECT_NE(loadChanged("   \"runway\": [\"runway\", \"run with\"],\n", "").find("needs a marker"), std::string::npos);
  EXPECT_NE(loadChanged("\"parse\": {\n  \"comment\"", "\"parsed\": {\n  \"comment\"").find("parse: missing"), std::string::npos);
}

// D-201: "run with" is a runway marker (Whisper's spelling of "runway" on s1); to re-check with
// the dev speakers. It only marks the runway: the value is still taken exactly or not at all.
TEST(Parser, RunWithMarksTheRunwayAndNothingElse) {
  const auto a = parse("Eagle, five, zero, nine, taxi via, bravo, six, run with three, six.");
  EXPECT_EQ(a.cls, C::Recognized);
  EXPECT_EQ(value(a, "runway"), "36");
  EXPECT_EQ(value(a, "taxiway"), "B6");
  const auto b = parse("eagle four two one run with your niner cleared for takeoff");
  EXPECT_EQ(b.cls, C::Ambiguous);
  EXPECT_TRUE(because(b, "unexplained words"));
  EXPECT_EQ(parse("eagle four two one taxi via alpha three run with zero").cls, C::Ambiguous);
  EXPECT_EQ(parse("eagle four two one run with").cls, C::Unrecognized);
  const auto d = parse("eagle four two one run with zero nine line up");  // was AMBIGUOUS before D-201
  EXPECT_EQ(d.cls, C::Recognized);
  EXPECT_EQ(value(d, "runway"), "09");
  const auto c = parse("eagle four two one taxi via alpha three run with zero nine runway two seven");
  EXPECT_EQ(c.cls, C::Ambiguous);  // two runway values: never one picked
  EXPECT_EQ(parse("falcon three one eight run with three six").cls, C::Unrecognized);  // no instruction cue
}

// Circuit - Rajali (D-203, engine's brief; wording PLACEHOLDER, the user's script, D-430, D-431).
namespace {
struct Line {
  const char* said;
  const char* intent;
  std::vector<std::pair<std::string, std::string>> values;
};
const Line kCircuit[] = {
    {"Falcon One Two, Rajali Tower, read you five.", "RADIO_CHECK_REPLY", {{"callsign", "FALCON-12"}, {"readability", "5"}}},
    {"Falcon one two, read you three", "RADIO_CHECK_REPLY", {{"callsign", "FALCON-12"}, {"readability", "3"}}},
    {"Falcon One Two, taxi to holding point runway zero five.", "TAXI_TO_HOLDING_POINT", {{"callsign", "FALCON-12"}, {"runway", "05"}}},
    {"Falcon One Two, hold short runway zero five.", "HOLD_SHORT", {{"callsign", "FALCON-12"}, {"runway", "05"}, {"hold_short", "-"}}},
    {"Falcon One Two, runway zero five, line up and wait.", "LINE_UP_AND_WAIT", {{"callsign", "FALCON-12"}, {"runway", "05"}}},
    {"Falcon One Two, runway zero five, cleared for takeoff.", "TAKEOFF_CLEARANCE", {{"callsign", "FALCON-12"}, {"runway", "05"}}},
    {"Falcon One Two, join the circuit, runway zero five.", "JOIN_CIRCUIT", {{"callsign", "FALCON-12"}, {"runway", "05"}}},
    {"Falcon One Two, report final runway zero five.", "REPORT_FINAL", {{"callsign", "FALCON-12"}, {"runway", "05"}}},
    {"Falcon One Two, runway zero five, cleared to land.", "LANDING_CLEARANCE", {{"callsign", "FALCON-12"}, {"runway", "05"}}},
    {"Falcon One Two, contact ground one two one decimal seven.", "CONTACT_FREQUENCY", {{"callsign", "FALCON-12"}, {"frequency", "121.7"}}},
    {"Falcon One Two, read back runway.", "READBACK_CHALLENGE", {{"callsign", "FALCON-12"}, {"runway", "-"}}},
    {"Falcon One Two, confirm runway zero five.", "READBACK_CHALLENGE", {{"callsign", "FALCON-12"}, {"runway", "05"}}},
    {"Falcon One Two, readback incomplete, confirm cleared for takeoff runway zero five.", "READBACK_CHALLENGE", {{"runway", "05"}}},
    {"Falcon One Two, read back incomplete, confirm cleared to land runway zero five.", "READBACK_CHALLENGE", {{"runway", "05"}}},
    {"Falcon 12, readback incomplete, confirm cleared for take off runway 05", "READBACK_CHALLENGE", {{"callsign", "FALCON-12"}, {"runway", "05"}}},
    {"Eagle 421, line up runway 09", "LINE_UP", {{"runway", "09"}}},  // the departure's, until engine moves it
    {"Eagle 421, taxi via A3, hold short runway 09", "TAXI_INSTRUCTION", {{"hold_short", "yes"}, {"taxiway", "A3"}}},
};
}  // namespace
TEST(Parser, EveryCircuitLineOfTheScriptParsesCompletely) {
  for (const auto& l : kCircuit) {
    const auto p = parse(l.said);
    EXPECT_EQ(p.cls, C::Recognized) << l.said << (p.reasons.empty() ? "" : ": " + p.reasons[0]);
    EXPECT_EQ(p.intent, l.intent) << l.said;
    for (const auto& [k, v] : l.values) EXPECT_EQ(value(p, k), v) << l.said << " " << k;
  }
}
TEST(Parser, EveryExampleOfAParsedIntentParsesAsItself) {
  std::ifstream f(rulesFile());
  const auto j = nlohmann::json::parse(f);
  const std::map<std::string, std::string> fill{{"{callsign}", "cobra seven"}, {"{runway}", "two three right"}, {"{taxiway}", "kilo four"},
                                                {"{readability}", "two"}, {"{frequency}", "one three zero decimal niner"}};
  int n = 0;
  for (const auto& i : j["intents"]) {
    if (!i.contains("parse")) continue;
    for (std::string e : i["examples"]) {
      for (const auto& [k, v] : fill)
        for (auto at = e.find(k); at != std::string::npos; at = e.find(k)) e.replace(at, k.size(), v);
      const auto p = parse(e);
      EXPECT_EQ(p.cls, C::Recognized) << e << (p.reasons.empty() ? "" : ": " + p.reasons[0]);
      EXPECT_EQ(p.intent, i["id"].get<std::string>()) << e;
      ++n;
    }
  }
  EXPECT_GE(n, 16);
}
TEST(Parser, CircuitValuesAreTakenAsSaidOrNotAtAll) {
  for (const char* s : {"falcon one two read you zero", "falcon one two read you six", "falcon one two read you five by five",
                        "falcon one two read you", "falcon one two contact ground one two one seven",
                        "falcon one two contact ground one two one decimal", "falcon one two contact ground one two one decimal seven five",
                        "falcon one two contact ground one two decimal seven"}) {
    const auto p = parse(s);
    EXPECT_NE(p.cls, C::Recognized) << s;
  }
  for (int d = 1; d <= 5; ++d) {  // bias rule: every readability, not only the expected one
    const char* w[] = {"", "one", "two", "three", "four", "five"};
    EXPECT_EQ(value(parse(std::string("hawk five five read you ") + w[d]), "readability"), std::to_string(d));
  }
  EXPECT_EQ(value(parse("falcon one two contact ground one one eight decimal zero"), "frequency"), "118.0");
  EXPECT_EQ(value(parse("falcon one two contact ground 136.9"), "frequency"), "136.9");
}
TEST(Parser, LongestCueAndFlagRules) {
  // The flag only belongs to an intent that has it: with the holding point it is a second instruction.
  auto p = parse("falcon one two taxi to holding point runway zero five hold short runway zero five");
  EXPECT_EQ(p.cls, C::Ambiguous);
  EXPECT_TRUE(because(p, "several instructions"));
  // A challenge has its own wording; "confirm" with a clearance is not one of them.
  p = parse("falcon one two confirm cleared for takeoff runway zero five");
  EXPECT_EQ(p.cls, C::Ambiguous);
  p = parse("falcon one two line up and wait");
  EXPECT_EQ(p.cls, C::Partial);
  EXPECT_EQ(p.intent, "LINE_UP_AND_WAIT");
}
TEST(Parser, TwoAndThreeDigitCallsigns) {
  // D-430: two-digit callsigns. A three-digit callsign heard with a digit lost now reads as a
  // two-digit one; the scenario's callsign check, not the parser, catches it (no expected value here).
  EXPECT_EQ(value(parse("eagle four two line up runway zero nine"), "callsign"), "EAGLE-42");
  EXPECT_EQ(value(parse("eagle four two one line up runway zero nine"), "callsign"), "EAGLE-421");
  EXPECT_EQ(value(parse("eagle four line up runway zero nine"), "callsign"), "?");
  EXPECT_EQ(value(parse("eagle four two one one line up runway zero nine"), "callsign"), "?");
}
TEST(Parser, FixedWordsInSlotFormsAreChecked) {
  EXPECT_NE(loadChanged("\"{digit} {digit} {digit} decimal {digit}\"", "\"{digit} {digit} {digit} {digit} decimal\"").find("end with a value"), std::string::npos);
  EXPECT_NE(loadChanged("\"{digit} {digit} {digit} decimal {digit}\"", "\"decimal {digit} {digit}\"").find("fixed word"), std::string::npos);
  EXPECT_NE(loadChanged("\"{digit} {digit} {digit} decimal {digit}\"", "\"{digit} {digyt}\"").find("unknown pool {digyt}"), std::string::npos);
}
