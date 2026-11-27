#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

#include "bench/Scoring.h"
#include "core/PathResolver.h"
#include "phraseology/Phraseology.h"
#include "phraseology/ScriptGen.h"
using namespace atc;

namespace {
std::string phraseologyFile() {
  Paths p;
  std::string e;
  EXPECT_TRUE(resolvePaths(systemPathInputs(), p, e)) << e;
  return p.data + "/phraseology/placeholder.json";
}
Phraseology real() {
  Phraseology p;
  std::string e;
  EXPECT_TRUE(loadPhraseology(phraseologyFile(), p, e)) << e;
  return p;
}
std::size_t examples(const Phraseology& p) {
  std::size_t n = 0;
  for (const auto& i : p.intents) n += i.examples.size();
  return n;
}
std::string loadChanged(const std::string& from, const std::string& to) {
  std::ifstream f(phraseologyFile());
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
  EXPECT_EQ(unk(), int(examples(p)) * 10 / 5);
}
TEST(Phraseology, PromptHasAllNamesAndNoDigitsOfAScenario) {
  const auto s = whisperPrompt(real());
  for (const char* w : {"Eagle", "Falcon", "Hawk", "Cobra", "Zulu", "niner", "hold short runway", "decimal"})
    EXPECT_NE(s.find(w), std::string::npos) << w;
  EXPECT_EQ(s.find_first_of("0123456789{}"), std::string::npos);
}
TEST(Phraseology, PromptStaysUnderWhispersLimit) {
  // 223 tokens at most (text context 448 / 2 - 1); beyond that Whisper drops the start, the names.
  // Measured with the tiny.en tokenizer: 165 tokens for 604 characters, 182 for 657, 209 for 800
  // (3.6 to 3.8 characters a token), so 810 characters is the guard; count the tokens again when
  // it fails. Every word of the phraseology stays in.
  std::string s = whisperPrompt(real());
  EXPECT_LE(s.size(), 810u) << s;
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  for (char& c : s)
    if (c == ',' || c == '.') c = ' ';
  for (const auto& w : phraseologyWords(real())) EXPECT_NE((" " + s + " ").find(" " + w + " "), std::string::npos) << w;
}
TEST(Phraseology, PromptWordsComeLastOnceAndOnlyInThePrompt) {
  const auto p = real();
  ASSERT_FALSE(p.promptWords.empty());
  const auto s = whisperPrompt(p);
  for (const auto& w : p.promptWords) EXPECT_NE(s.find(w), std::string::npos) << w;
  EXPECT_GT(s.find("Rajali"), s.find("hold short runway"));
  std::size_t n = 0;
  for (auto at = s.find("decimal"); at != std::string::npos; at = s.find("decimal", at + 1)) ++n;
  EXPECT_EQ(n, 1u);  // already in the frequency slot
  ExpandOptions o;
  o.perExample = 20;
  for (const auto& line : expandPhrases(p, o))
    EXPECT_EQ(line.find("arakkonam"), std::string::npos) << line;  // "rajali" is in the radio check reply (D-203)
  for (const auto& w : phraseologyWords(p)) EXPECT_NE(w, "arakkonam");
}
TEST(Phraseology, PromptWordsAreChecked) {
  EXPECT_NE(loadChanged("\"Rajali\"", "\"Rajali 7\"").find("promptWords"), std::string::npos);
  EXPECT_NE(loadChanged("\"Rajali\"", "3").find("promptWords"), std::string::npos);
  EXPECT_NE(loadChanged("\"words\": [", "\"wordz\": [").find("promptWords"), std::string::npos);
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
  ASSERT_EQ(v.size(), examples(p) * 200) << e;
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

namespace {
ScriptOptions scriptOptions(std::uint32_t seed) {
  ScriptOptions o;
  o.seed = seed;
  for (int i = 0; i < 12; ++i) o.negativeTexts.push_back({"off", "off text " + std::string(1, char('a' + i))});
  for (int i = 0; i < 12; ++i) o.negativeTexts.push_back({"near", "near text " + std::string(1, char('a' + i))});
  return o;
}
}  // namespace

TEST(Script, SeededBalancedAndEveryLineReadable) {
  const auto p = real();
  std::set<std::string> allText;
  for (std::uint32_t seed = 1; seed <= 4; ++seed) {
    std::string e;
    const auto s = makeScript(p, scriptOptions(seed), e);
    ASSERT_EQ(s.size(), 60u) << e;
    const auto again = makeScript(p, scriptOptions(seed), e);
    ASSERT_EQ(again.size(), s.size());
    std::map<char, int> digits;
    std::set<char> letters;
    int taxiLines = 0;
    int neg = 0, variant = 0, malformed = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
      EXPECT_EQ(phraseLine(s[i]), phraseLine(again[i]));
      std::istringstream line(phraseLine(s[i]));
      std::vector<Phrase> back;
      ASSERT_TRUE(readPhrases(line, back, e)) << e;
      EXPECT_EQ(back[0].text, s[i].text);
      EXPECT_EQ(back[0].expect, s[i].expect);
      EXPECT_EQ(back[0].tags, s[i].tags);
      neg += s[i].negative;
      variant += std::count(s[i].tags.begin(), s[i].tags.end(), "variant") > 0;
      malformed += std::count(s[i].tags.begin(), s[i].tags.end(), "malformed") > 0;
      if (!s[i].negative) {
        allText.insert(s[i].text);
        if (const auto c = s[i].expect.find("callsign"); c != s[i].expect.end())
          for (char d : c->second.substr(c->second.find('-') + 1)) ++digits[d];
        if (const auto t = s[i].expect.find("taxiway"); t != s[i].expect.end()) letters.insert(t->second[0]), ++taxiLines;
      }
    }
    EXPECT_EQ(neg, 20);
    EXPECT_EQ(variant, 10);
    EXPECT_EQ(malformed, 8);
    for (char d = '0'; d <= '9'; ++d) EXPECT_GE(digits[d], 6) << d;
    // Letters are balanced: every taxiway line has its own (D-203: the circuit intents leave
    // fewer taxiway lines in 40).
    EXPECT_GE(taxiLines, 2);
    EXPECT_EQ(letters.size(), std::size_t(taxiLines));
  }
  EXPECT_GE(allText.size(), 150u);
}
TEST(Script, MalformedIsNeverAnAcceptableInstruction) {
  const auto p = real();
  std::string e;
  for (const auto& x : makeScript(p, scriptOptions(7), e)) {
    if (!x.negative || x.tags.empty() || x.tags[0] != "malformed") continue;
    std::vector<SttWord> w;
    for (auto& s : normaliseText(x.text)) w.push_back({s, 1.0});
    const std::set<std::string> names{"eagle", "falcon", "hawk", "cobra"};
    EXPECT_NE(judge(x, extractFields(w, names), 0, true), Outcome::FalseAccept) << x.text;
  }
}
TEST(Script, TooFewNegativesIsAnError) {
  auto o = scriptOptions(1);
  o.negativeTexts.clear();
  std::string e;
  EXPECT_TRUE(makeScript(real(), o, e).empty());
  EXPECT_NE(e.find("not enough negatives"), std::string::npos);
}
