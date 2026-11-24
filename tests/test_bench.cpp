#include <gtest/gtest.h>

#include <sstream>

#include "bench/PhraseFile.h"
#include "bench/Scoring.h"
using namespace atc;

namespace {
using Words = std::vector<std::string>;
const std::set<std::string> kNames{"eagle", "falcon"};

std::vector<SttWord> words(const std::string& t, double conf = 1.0) {
  std::vector<SttWord> w;
  for (auto& s : normaliseText(t)) w.push_back({s, conf});
  return w;
}
Fields fields(const std::string& t) { return extractFields(words(t), kNames).fields; }
Phrase taxi() {
  return {"", false, {{"callsign", "EAGLE-421"}, {"taxiway", "A3"}, {"runway", "09"}, {"hold_short", "yes"}}};
}
Outcome outcome(const std::string& t, const Phrase& p, double thr = 0) { return judge(p, extractFields(words(t), kNames), thr); }
bool parse(const std::string& t, std::vector<Phrase>& p, std::string& e) {
  std::istringstream s(t);
  return readPhrases(s, p, e);
}
}  // namespace

TEST(Phrases, ReadsScriptedNegativeAndPlain) {
  std::vector<Phrase> p;
  std::string e;
  ASSERT_TRUE(parse("# note\n\nEagle one runway two | callsign=EAGLE-1 runway=2\n Hello there | negative\nPlain\n", p, e)) << e;
  ASSERT_EQ(p.size(), 3u);
  EXPECT_EQ(p[0].text, "Eagle one runway two");
  EXPECT_EQ(p[0].expect.at("runway"), "2");
  EXPECT_TRUE(p[1].negative);
  EXPECT_EQ(p[1].text, "Hello there");
  EXPECT_FALSE(p[2].negative);
}

TEST(Phrases, RejectsBadLinesWithLineNumber) {
  for (const std::string bad : {"x | runway", "x | bogus=1 callsign=A-1", "x | callsign=A-1", "x | runway=09 level=FL1",
                                "x | callsign=A-1 callsign=A-2 runway=1", "x | negative runway=09", "| negative", "x | runway="}) {
    std::vector<Phrase> p;
    std::string e;
    EXPECT_FALSE(parse("ok\n" + bad, p, e)) << bad;
    EXPECT_EQ(e.rfind("line 2:", 0), 0u) << e;
  }
}

TEST(Normalise, EngineSpellingsAgree) {
  const Words w{"eagle", "four", "two", "one", "taxi", "via", "alpha", "three", "hold", "short", "runway", "zero", "nine"};
  EXPECT_EQ(normaliseText("Eagle 421, taxi via A3, hold short runway 09."), w);
  EXPECT_EQ(normaliseText("eagle fower two one taxi via alfa tree hold short runway zero niner"), w);
  EXPECT_EQ(normaliseText("Eagle-421 taxi via Alpha-3 hold-short runway 0-9"), w);
}

TEST(Normalise, NumbersKeepTheirValue) {
  EXPECT_EQ(normaliseText("5,000"), (Words{"five", "thousand"}));
  EXPECT_EQ(normaliseText("3500"), (Words{"three", "thousand", "five", "hundred"}));
  EXPECT_EQ(normaliseText("10000"), (Words{"one", "zero", "thousand"}));
  EXPECT_EQ(normaliseText("0500"), (Words{"zero", "five", "zero", "zero"}));
  EXPECT_EQ(normaliseText("090"), (Words{"zero", "nine", "zero"}));
  EXPECT_EQ(normaliseText("100"), (Words{"one", "zero", "zero"}));
  EXPECT_EQ(normaliseText("1234"), (Words{"one", "two", "three", "four"}));
  EXPECT_EQ(normaliseText("118.1"), (Words{"one", "one", "eight", "decimal", "one"}));
  EXPECT_EQ(normaliseText("X-ray don't"), (Words{"xray", "dont"}));
}

TEST(Normalise, NeverCorrectsAValue) {
  EXPECT_EQ(normaliseText("runway to nine oh for"), (Words{"runway", "to", "nine", "oh", "for"}));
}

TEST(Wer, CountsEachKind) {
  auto c = editCounts({"a", "b", "c"}, {"a", "x", "c", "d"});
  EXPECT_EQ(c.sub, 1);
  EXPECT_EQ(c.ins, 1);
  EXPECT_EQ(c.del, 0);
  EXPECT_EQ(c.ref, 3);
  c = editCounts({"a", "b", "c"}, {});
  EXPECT_EQ(c.del, 3);
  c = editCounts({}, {"a"});
  EXPECT_EQ(c.ins, 1);
  EXPECT_EQ(editCounts({"a", "b"}, {"a", "b"}).errors(), 0);
}

TEST(Extract, AllFieldKinds) {
  EXPECT_EQ(fields("Eagle 421 taxi via Alpha Three hold short runway 09"), taxi().expect);
  EXPECT_EQ(fields("falcon seven three two runway two seven right").at("runway"), "27R");
  EXPECT_EQ(fields("runway zero nine centre").at("runway"), "09C");
  EXPECT_EQ(fields("turn left heading 090").at("heading"), "090");
  EXPECT_EQ(fields("climb flight level one zero zero"), (Fields{{"level", "FL100"}}));
  EXPECT_EQ(fields("contact tower one one eight decimal one"), (Fields{{"frequency", "118.1"}}));
  EXPECT_EQ(fields("eagle one climb three thousand five hundred feet"), (Fields{{"callsign", "EAGLE-1"}, {"altitude", "3500"}}));
  EXPECT_EQ(fields("descend 5000 feet").at("altitude"), "5000");
  EXPECT_EQ(fields("climb eight hundred feet").at("altitude"), "800");
}

TEST(Extract, ExtraOrWrongDigitsAreKeptAsHeard) {
  EXPECT_EQ(fields("runway zero nine five").at("runway"), "095");
  EXPECT_EQ(fields("runway nine").at("runway"), "9");  // not completed to 09
  EXPECT_FALSE(fields("beagle four two one").count("callsign"));
  EXPECT_FALSE(fields("eagle taxi").count("callsign"));
  EXPECT_FALSE(fields("runway left").count("runway"));
}

TEST(Extract, SameFieldTwiceWithDifferentValuesIsAConflict) {
  EXPECT_TRUE(extractFields(words("runway zero nine runway two seven"), kNames).conflict);
  EXPECT_FALSE(extractFields(words("runway zero nine runway zero nine"), kNames).conflict);
}

TEST(Extract, ConfidenceIsTheLowestFieldWord) {
  auto w = words("eagle four taxi runway zero nine");
  w[2].conf = 0.1;  // "taxi" carries no critical value
  w[4].conf = 0.6;  // not the last field word
  EXPECT_DOUBLE_EQ(extractFields(w, kNames).minConf, 0.6);
}

TEST(Judge, Outcomes) {
  const auto p = taxi();
  EXPECT_EQ(outcome("eagle 421 taxi via alpha 3 hold short runway 09", p), Outcome::Correct);
  EXPECT_EQ(outcome("eagle 421 taxi via alpha 3 hold short runway 08", p), Outcome::Wrong);
  EXPECT_EQ(outcome("falcon 421 taxi via alpha 3 hold short runway 09", p), Outcome::Wrong);
  EXPECT_EQ(outcome("eagle 421 taxi via alpha 3 runway 09", p), Outcome::Wrong) << "lost hold short";
  EXPECT_EQ(outcome("eagle 421 hold short runway 09", p), Outcome::Incomplete);
  EXPECT_EQ(outcome("eagle 421 heading 090 via alpha 3 hold short runway 09", p), Outcome::Wrong) << "extra value";
  EXPECT_EQ(outcome("taxi via alpha 3 hold short runway 09", p), Outcome::Rejected);
  EXPECT_EQ(outcome("eagle 421", p), Outcome::Rejected);
  EXPECT_EQ(outcome("eagle 421 hold short runway 09 runway 27", p), Outcome::Rejected);
  EXPECT_EQ(outcome("", p), Outcome::Rejected);
}

TEST(Judge, ThresholdIsInclusive) {
  const auto e = extractFields(words("eagle 421 taxi via alpha 3 hold short runway 09", 0.7), kNames);
  EXPECT_EQ(judge(taxi(), e, 0.7), Outcome::Correct);
  EXPECT_EQ(judge(taxi(), e, 0.71), Outcome::Rejected);
}

TEST(Judge, Negatives) {
  Phrase n{"", true, {}};
  EXPECT_EQ(outcome("the runway lights look fine", n), Outcome::NegativeOk);
  EXPECT_EQ(outcome("eagle 421 runway 09", n), Outcome::FalseAccept);
}

TEST(Percentile, NearestRank) {
  std::vector<double> v;
  for (int i = 20; i >= 1; --i) v.push_back(i);
  EXPECT_EQ(percentile(v, 50), 10);
  EXPECT_EQ(percentile(v, 95), 19);
  EXPECT_EQ(percentile(v, 100), 20);
  EXPECT_EQ(percentile({}, 95), 0);
  EXPECT_EQ(percentile({3}, 1), 3);
  EXPECT_EQ(percentile({1, 2, 3}, 95), 3);
}

TEST(Shape, RejectsDroppedDigitsAndNeverChangesValues) {
  const auto ok = [](const std::string& s) { return shapeOk(extractFields(words(s), kNames)); };
  EXPECT_TRUE(ok("eagle four two one taxi via alpha three hold short runway zero nine left"));
  EXPECT_FALSE(ok("eagle four two taxi via alpha three runway zero nine"));   // callsign digit lost
  EXPECT_FALSE(ok("eagle four two one runway nine cleared to land"));         // runway digit lost
  EXPECT_FALSE(ok("eagle four two one runway three seven cleared to land"));  // no such runway
  EXPECT_FALSE(ok("eagle four two one turn left heading four zero"));
  EXPECT_TRUE(ok("eagle four two one turn left heading zero four zero"));
  EXPECT_FALSE(ok("eagle four two one taxi via alpha runway zero nine"));
  EXPECT_TRUE(ok("eagle four two one contact tower one two four decimal five"));
  EXPECT_FALSE(ok("eagle four two one contact tower two four decimal five"));
  EXPECT_FALSE(ok("eagle four two one climb flight level zero eight"));
  const auto e = extractFields(words("eagle four two one runway zero eight cleared to land"), kNames);
  EXPECT_EQ(e.fields.at("runway"), "08");
  Phrase p{"", false, {{"callsign", "EAGLE-421"}, {"runway", "09"}}};
  EXPECT_EQ(judge(p, e, 0, true), Outcome::Wrong);  // right shape, wrong value: shape cannot hide it
  EXPECT_EQ(judge(p, extractFields(words("eagle four two runway zero nine"), kNames), 0, true), Outcome::Rejected);
}

TEST(Action, SeesASwappedClearance) {
  const auto a = [](const std::string& s) { return actionOf(words(s)); };
  EXPECT_EQ(a("eagle four two one runway zero nine cleared for takeoff"), "takeoff");
  EXPECT_EQ(a("eagle four two one runway zero nine cleared for take off"), "takeoff");
  EXPECT_EQ(a("eagle four two one runway zero nine cleared to land"), "land");
  EXPECT_EQ(a("eagle turn left heading zero nine zero"), "turn_left");
  EXPECT_EQ(a("eagle turn right heading zero nine zero"), "turn_right");
  EXPECT_EQ(a("eagle taxi via alpha three"), "");
  EXPECT_EQ(a("eagle four two one line up runway zero nine"), "line_up");
  EXPECT_EQ(a("line up cleared for takeoff"), "conflict");
  EXPECT_EQ(a("climb descend"), "conflict");
  EXPECT_EQ(a("runway zero nine left"), "");  // a runway side is not a turn
}
TEST(Phrases, ReadsActionAndTags) {
  std::vector<Phrase> p;
  std::string e;
  ASSERT_TRUE(parse("x | callsign=EAGLE-421 action=takeoff tags=variant,dev", p, e)) << e;
  EXPECT_EQ(p[0].expect.at("action"), "takeoff");
  EXPECT_EQ(p[0].tags, (std::vector<std::string>{"variant", "dev"}));
}
