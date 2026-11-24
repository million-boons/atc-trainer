#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

#include "phraseology/Phraseology.h"
using namespace atc;

namespace {
Phraseology real() {
  Phraseology p;
  std::string e;
  EXPECT_TRUE(loadPhraseology(ATC_ROOT "/data/phraseology/placeholder.json", p, e)) << e;
  return p;
}
std::string loadChanged(const std::string& from, const std::string& to) {
  std::ifstream f(ATC_ROOT "/data/phraseology/placeholder.json");
  std::stringstream s;
  s << f.rdbuf();
  std::string t = s.str();
  const auto at = t.find(from);
  EXPECT_NE(at, std::string::npos) << from;
  t.replace(at, from.size(), to);
  const std::string path = testing::TempDir() + "phraseology_test.json";
  std::ofstream(path) << t;
  Phraseology p;
  std::string e;
  const bool ok = loadPhraseology(path, p, e);
  std::remove(path.c_str());
  return ok ? "" : e;
}
}  // namespace

TEST(Phraseology, LoadsPlaceholderAsNotValidated) {
  const auto p = real();
  EXPECT_FALSE(p.validated);
  EXPECT_EQ(p.pools.at("digit").size(), 10u);
  EXPECT_EQ(p.pools.at("letter").size(), 26u);
}
TEST(Phraseology, SameSeedSameSentences) {
  const auto p = real();
  ExpandOptions o;
  o.perExample = 20;
  EXPECT_EQ(expandPhrases(p, o), expandPhrases(p, o));
  auto o2 = o;
  o2.seed = 2;
  EXPECT_NE(expandPhrases(p, o), expandPhrases(p, o2));
}
// D-48: no digit position and no letter may be limited, whatever the pools hold.
TEST(Phraseology, EveryDigitAndLetterOccurs) {
  const auto p = real();
  std::set<std::string> all;
  for (const auto& s : expandPhrases(p, {})) {
    std::istringstream in(s);
    for (std::string w; in >> w;) all.insert(w);
  }
  for (const auto& w : phraseologyWords(p)) EXPECT_TRUE(all.count(w)) << w;
  for (const char* w : {"zero", "niner", "tree", "fife", "zulu", "cobra"}) EXPECT_TRUE(all.count(w)) << w;
}
TEST(Phraseology, BoostKeepsEveryOtherIntentComplete) {
  const auto p = real();
  ExpandOptions plain, boosted;
  plain.perExample = boosted.perExample = 50;
  boosted.boostIntent = "TURN";
  boosted.boost = 3;
  const auto count = [](const std::vector<std::string>& v, const char* w) {
    return std::count_if(v.begin(), v.end(), [&](const std::string& s) { return s.find(w) != std::string::npos; });
  };
  const auto a = expandPhrases(p, plain), b = expandPhrases(p, boosted);
  EXPECT_EQ(count(b, "turn "), 3 * count(a, "turn "));
  EXPECT_EQ(count(b, "cleared to land"), count(a, "cleared to land"));
}
TEST(Phraseology, UnkOnlyWhenAsked) {
  const auto p = real();
  ExpandOptions o;
  o.perExample = 10;
  const auto unk = [&] {
    int n = 0;
    for (const auto& s : expandPhrases(p, o)) n += s.find("[unk]") != std::string::npos;
    return n;
  };
  EXPECT_EQ(unk(), 0);
  o.unkEvery = 5;
  EXPECT_EQ(unk(), 22);  // 11 examples x 10 / 5
}
TEST(Phraseology, PromptHasAllNamesAndNoDigitsOfAScenario) {
  const auto s = whisperPrompt(real());
  for (const char* w : {"Eagle", "Falcon", "Hawk", "Cobra", "Zulu", "niner", "hold short runway", "decimal"})
    EXPECT_NE(s.find(w), std::string::npos) << w;
  EXPECT_EQ(s.find_first_of("0123456789{}"), std::string::npos);
}
TEST(Phraseology, ErrorsNameThePlace) {
  EXPECT_NE(loadChanged("\"7\": [", "\"x\": [").find("digits/7"), std::string::npos);
  EXPECT_NE(loadChanged("\"Q\": [", "\"QQ\": [").find("letters/Q"), std::string::npos);
  EXPECT_NE(loadChanged("{heading}", "{headng}").find("unknown slot {headng}"), std::string::npos);
  EXPECT_NE(loadChanged("{side}", "{syde}").find("unknown pool {syde}"), std::string::npos);
  EXPECT_NE(loadChanged("\"placeholder\"", "\"maybe\"").find("validation/status"), std::string::npos);
}

TEST(Phraseology, ExcludeRemovesTheFormNotTheDigit) {
  const auto p = real();
  ExpandOptions o;
  o.exclude = {"niner", "x-ray"};
  std::string e;
  const auto v = expandPhrases(p, o, &e);
  ASSERT_EQ(v.size(), 2200u) << e;
  int nine = 0, seven = 0;
  for (const auto& s : v) {
    EXPECT_EQ(s.find("niner"), std::string::npos);
    std::istringstream in(s);
    for (std::string w; in >> w;) nine += w == "nine", seven += w == "seven";
  }
  EXPECT_NEAR(double(nine) / seven, 1.0, 0.15);
  o.exclude = {"seven"};
  EXPECT_TRUE(expandPhrases(p, o, &e).empty());
  EXPECT_NE(e.find("seven"), std::string::npos);
  o.exclude = {"decimal"};
  EXPECT_TRUE(expandPhrases(p, o, &e).empty());
}

TEST(Phraseology, ValidationIsPerIntent) {
  const auto p = real();
  for (const auto& i : p.intents) EXPECT_FALSE(i.validated) << i.id;
  ExpandOptions o;
  o.validatedOnly = true;
  EXPECT_TRUE(expandPhrases(p, o).empty());
  EXPECT_NE(loadChanged("\"draft\"", "\"validated\"").find("intents/TAXI_INSTRUCTION: Validated needs sopRef"), std::string::npos);
  EXPECT_NE(loadChanged("\"status\": \"draft\"", "\"status\": \"validated\", \"sopRef\": \"x\", \"validated_by\": \"y\", \"validated_at\": \"z\"")
                .find("Validated in a placeholder file"), std::string::npos);
  EXPECT_NE(loadChanged("\"draft\"", "\"done\"").find("status must be draft or validated"), std::string::npos);
}
