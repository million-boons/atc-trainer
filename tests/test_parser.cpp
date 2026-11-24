#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "core/PathResolver.h"
#include "parser/Parser.h"
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
  for (const char* s : {"eagle four two line up runway zero nine", "eagle four two one one line up runway zero nine",
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
                        "eagle four two one line up and wait runway zero nine", "eagle four two one run with zero nine line up",
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
  EXPECT_NE(loadChanged("\"required\": [\"callsign\", \"runway\"]", "\"required\": [\"altitude\"]").find("pools only"), std::string::npos);
  EXPECT_NE(loadChanged("\"runway\": [1, 36]", "\"runway\": [36, 1]").find("needs [low, high]"), std::string::npos);
  EXPECT_NE(loadChanged("   \"runway\": [\"runway\"],\n", "").find("needs a marker"), std::string::npos);
  EXPECT_NE(loadChanged("\"parse\": {\n  \"comment\"", "\"parsed\": {\n  \"comment\"").find("parse: missing"), std::string::npos);
}
