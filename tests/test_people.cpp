#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iterator>

#include "storage/PeopleStore.h"
using namespace atc;
namespace fs = std::filesystem;

namespace {
constexpr const char* kToday = "2026-11-24";
struct People : ::testing::Test {
  fs::path dir = fs::temp_directory_path() / ("atc_people_" + std::to_string(::getpid()) + "_" +
                                              ::testing::UnitTest::GetInstance()->current_test_info()->name());
  std::string file = (dir / "state" / "people.json").string();
  void TearDown() override { fs::remove_all(dir); }
  std::string text() const {
    std::ifstream i(file);
    return {std::istreambuf_iterator<char>(i), {}};
  }
};
}  // namespace

TEST_F(People, RoundTripAndOwnerOnly) {
  PeopleStore s(file);
  ASSERT_EQ(s.load(), "");  // no file yet
  EXPECT_EQ(s.add({"S01", "Name One", "Rank", true, kToday}, kToday), "");
  EXPECT_EQ(s.add({"S02", "", "", false, "2026-11-20"}, kToday), "");
  PeopleStore again(file);
  ASSERT_EQ(again.load(), "");
  ASSERT_EQ(again.people().size(), 2u);
  EXPECT_EQ(again.find("S01")->name, "Name One");
  EXPECT_TRUE(again.find("s01")->consent);
  EXPECT_FALSE(again.find("S02")->consent);
  EXPECT_EQ(again.find("S02")->consentDate, "2026-11-20");
  EXPECT_EQ(again.find("S03"), nullptr);
  struct stat st {};
  ASSERT_EQ(stat(file.c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0600u);
}

TEST_F(People, BadRecordsAreRefused) {
  PeopleStore s(file);
  ASSERT_EQ(s.load(), "");
  for (const char* id : {"", "two words", "../x", "a.b", "12345678901234567"}) EXPECT_NE(s.add({id, "", "", true, kToday}, kToday), "") << id;
  EXPECT_NE(s.add({"S01", "", "", true, ""}, kToday), "");            // consent without a date
  EXPECT_NE(s.add({"S01", "", "", false, ""}, kToday), "");           // the answer no has a date too
  EXPECT_NE(s.add({"S01", "", "", true, "2026-11-25"}, kToday), "");  // future
  EXPECT_NE(s.add({"S01", "", "", true, "2026-02-30"}, kToday), "");
  EXPECT_NE(s.add({"S01", "", "", true, "24.11.2026"}, kToday), "");
  EXPECT_NE(s.add({"S01", std::string(61, 'x'), "", true, kToday}, kToday), "");
  EXPECT_FALSE(fs::exists(file));
  EXPECT_EQ(s.add({"S01", "", "", true, kToday}, kToday), "");
  EXPECT_NE(s.add({"s01", "", "", true, kToday}, kToday), "");        // same person
  EXPECT_EQ(s.people().size(), 1u);
}

TEST_F(People, ConsentCanBeWithdrawn) {
  PeopleStore s(file);
  ASSERT_EQ(s.load(), "");
  ASSERT_EQ(s.add({"S01", "", "", true, "2026-11-01"}, kToday), "");
  EXPECT_EQ(s.setConsent("S01", false, kToday, kToday), "");
  PeopleStore again(file);
  ASSERT_EQ(again.load(), "");
  EXPECT_FALSE(again.find("S01")->consent);
  EXPECT_EQ(again.find("S01")->consentDate, kToday);
  EXPECT_NE(s.setConsent("S09", true, kToday, kToday), "");
  EXPECT_NE(s.setConsent("S01", true, "", kToday), "");
}

TEST_F(People, PilotVoiceIsASeparateAnswer) {  // D-132, D-133
  PeopleStore s(file);
  ASSERT_EQ(s.load(), "");
  Person p{"S01", "", "", true, "2026-11-01"};
  p.pilotVoice = true;  // cannot come with the record
  p.pilotVoiceDate = kToday;
  ASSERT_EQ(s.add(p, kToday), "");
  EXPECT_FALSE(mayRecordPilot(*s.find("S01"), {}));
  EXPECT_EQ(text().find("pilot_voice"), std::string::npos);  // never asked
  EXPECT_NE(s.setPilotVoice("S01", true, "2026-11-25", kToday), "");  // future
  EXPECT_NE(s.setPilotVoice("S01", true, "", kToday), "");
  EXPECT_NE(s.setPilotVoice("S09", true, kToday, kToday), "");
  EXPECT_EQ(s.setPilotVoice("S01", true, "2026-11-20", kToday), "");
  PeopleStore again(file);
  ASSERT_EQ(again.load(), "");
  EXPECT_TRUE(mayRecordPilot(*again.find("S01"), {}));
  EXPECT_EQ(again.find("S01")->pilotVoiceDate, "2026-11-20");
  EXPECT_EQ(again.find("S01")->consentDate, "2026-11-01");  // unchanged
  EXPECT_EQ(again.setPilotVoice("S01", false, kToday, kToday), "");  // withdrawn
  EXPECT_FALSE(mayUsePilotVoice(*again.find("S01"), {}));
  EXPECT_TRUE(again.find("S01")->consent);
  EXPECT_EQ(again.setPilotVoice("S01", true, kToday, kToday), "");
  EXPECT_EQ(again.setConsent("S01", false, kToday, kToday), "");
  EXPECT_FALSE(mayRecordPilot(*again.find("S01"), {}));  // recording needs both
  EXPECT_TRUE(mayUsePilotVoice(*again.find("S01"), {}));
}

TEST_F(People, DraftAnswersCountUntilAnApprovedTextIsInPlace) {  // D-135
  const ConsentVersions draft{"draft-1", "draft-1"}, newer{"draft-2", "draft-2"}, approved{"approved-1", "approved-1"};
  {
    PeopleStore s(file, draft);
    ASSERT_EQ(s.load(), "");
    ASSERT_EQ(s.add({"S01", "", "", true, kToday}, kToday), "");
    ASSERT_EQ(s.add({"S02", "", "", true, kToday}, kToday), "");
    ASSERT_EQ(s.setPilotVoice("S01", true, kToday, kToday), "");
    EXPECT_EQ(s.find("S01")->consentVersion, "draft-1");
  }
  PeopleStore s(file, approved);
  ASSERT_EQ(s.load(), "");
  EXPECT_EQ(s.find("S01")->pilotVoiceVersion, "draft-1");
  const Person p = *s.find("S01");
  for (const auto& v : {draft, newer}) EXPECT_TRUE(mayRecordPilot(p, v) && mayRecord(p, v));
  EXPECT_TRUE(recordingAgain(p, approved));
  EXPECT_TRUE(pilotVoiceAgain(p, approved));
  EXPECT_FALSE(mayRecord(p, approved));
  EXPECT_FALSE(mayUsePilotVoice(p, approved));
  EXPECT_FALSE(pilotVoiceAgain(*s.find("S02"), approved));  // never asked is not "again"
  ASSERT_EQ(s.setConsent("S01", true, kToday, kToday), "");  // each answer on its own
  EXPECT_TRUE(mayRecord(*s.find("S01"), approved));
  EXPECT_FALSE(mayRecordPilot(*s.find("S01"), approved));
  ASSERT_EQ(s.setPilotVoice("S01", true, kToday, kToday), "");
  EXPECT_TRUE(mayRecordPilot(*s.find("S01"), approved));
  EXPECT_FALSE(recordingAgain(*s.find("S01"), approved));
  ASSERT_EQ(s.setConsent("S02", false, kToday, kToday), "");  // asked again, and the answer is no
  EXPECT_FALSE(recordingAgain(*s.find("S02"), approved));
  EXPECT_FALSE(mayRecord(*s.find("S02"), approved));
}

TEST_F(People, AnotherApprovedTextCountsAsWiderUntilItIsMarked) {  // D-137, the mark is not built
  Person p{"S01", "", "", true, kToday};
  p.consentVersion = "approved-1";
  EXPECT_TRUE(mayRecord(p, {"approved-1", "draft-1"}));
  EXPECT_FALSE(mayRecord(p, {"approved-2", "draft-1"}));
}

TEST_F(People, AnAnswerWithoutAVersionIsADraftAnswer) {
  fs::create_directories(dir / "state");
  std::ofstream(file) << "{\"format\":1,\"people\":[{\"id\":\"S01\",\"consent\":true,\"consent_date\":\"2026-11-01\"}]}";
  PeopleStore s(file);
  ASSERT_EQ(s.load(), "");
  EXPECT_TRUE(mayRecord(*s.find("S01"), {}));
  EXPECT_FALSE(mayRecord(*s.find("S01"), {"approved-1", "approved-1"}));
}

TEST_F(People, APilotAnswerWithoutADateIsNotRead) {
  fs::create_directories(dir / "state");
  std::ofstream(file) << "{\"format\":1,\"people\":[{\"id\":\"S01\",\"consent\":true,\"consent_date\":\"2026-11-01\",\"pilot_voice\":true}]}";
  PeopleStore s(file);
  EXPECT_NE(s.load(), "");
}

TEST_F(People, UnreadableFileIsNeverOverwritten) {
  fs::create_directories(dir / "state");
  for (const char* t : {"not json", "{\"format\":2,\"people\":[]}", "{\"format\":1,\"people\":[{\"id\":\"S01\"}]}"}) {
    std::ofstream(file) << t;
    PeopleStore s(file);
    EXPECT_NE(s.load(), "");
    EXPECT_NE(s.add({"S01", "", "", true, kToday}, kToday), "");
    EXPECT_NE(s.setConsent("S01", true, kToday, kToday), "");
    EXPECT_EQ(text(), t);
  }
}

TEST_F(People, NothingIsWrittenBeforeLoad) {
  PeopleStore s(file);
  EXPECT_NE(s.add({"S01", "", "", true, kToday}, kToday), "");
  EXPECT_FALSE(fs::exists(file));
}
