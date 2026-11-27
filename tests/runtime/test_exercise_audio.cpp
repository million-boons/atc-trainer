// Exercise audio with consent (11, D-341). No device, no person's voice: tones.
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <filesystem>

#include "storage/ExerciseAudio.h"
using namespace atc;
namespace fs = std::filesystem;

namespace {
struct TempDir {
  fs::path path;
  TempDir() {
    static int n = 0;
    path = fs::temp_directory_path() / ("atc_exaudio_" + std::to_string(getpid()) + "_" + std::to_string(n++));
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};
std::vector<std::int16_t> tone(int n = 16000) {
  std::vector<std::int16_t> s(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) s[std::size_t(i)] = std::int16_t(3000 * std::sin(i * 0.2));
  return s;
}
int files(const fs::path& d) {
  int n = 0;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) n += it->is_regular_file();
  return n;
}
struct Rig {
  TempDir t;
  std::atomic<bool> yes{true};
  long free = 10'000;
  ExerciseAudio a{{(t.path / "ea").string(), 200, [this] { return free; },
                   [this](const std::string& who) { return ExerciseAudio::Answer{yes && who == "S01", "2026-11-25", "draft-1"}; }}};
};
const char* kSession = "20261127T100000Z-abcd";
}  // namespace

TEST(ExerciseAudioTest, OnlyARunWithASpeakerAndConsentIsKept) {
  Rig r;
  r.a.transmission(1, tone(), 16000, 4800, 8000);  // no run
  r.a.begin(kSession, "");                         // a guest
  r.a.transmission(2, tone(), 16000, 4800, 8000);
  r.a.begin(kSession, "S02");                      // no consent
  r.a.transmission(3, tone(), 16000, 4800, 8000);
  r.a.wait();
  EXPECT_EQ(files(r.t.path), 0);
  r.a.begin(kSession, "S01");  // control
  r.a.transmission(4, tone(), 16000, 4800, 8000);
  r.a.wait();
  EXPECT_EQ(r.a.saved(kSession), std::set<std::uint64_t>{4});
  EXPECT_EQ(r.a.speaker(kSession), "S01");
  const auto c = r.a.read(kSession, 4);
  ASSERT_TRUE(c);
  EXPECT_EQ(c->samples, tone());
  EXPECT_EQ(c->preRoll, 4800);
  EXPECT_EQ(c->keyed, 8000);
  struct stat st {};
  ASSERT_EQ(stat((r.t.path / "ea" / kSession / "tx4.wav").c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0600u);
  ASSERT_EQ(stat((r.t.path / "ea" / kSession).c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0700u);
}

TEST(ExerciseAudioTest, ANoInBetweenWritesNothingMore) {
  Rig r;
  r.a.begin(kSession, "S01");
  r.a.transmission(1, tone(), 16000, 0, 0);
  r.a.wait();
  r.yes = false;
  r.a.transmission(2, tone(), 16000, 0, 0);
  r.a.wait();
  EXPECT_EQ(r.a.saved(kSession), std::set<std::uint64_t>{1});
  r.a.end();
  r.yes = true;
  r.a.transmission(3, tone(), 16000, 0, 0);  // after the end of the run
  r.a.wait();
  EXPECT_EQ(r.a.saved(kSession), std::set<std::uint64_t>{1});
}

TEST(ExerciseAudioTest, AFullDiskIsNotedOnceAndTheRunGoesOn) {
  Rig r;
  r.free = 100;
  r.a.begin(kSession, "S01");
  r.a.transmission(1, tone(), 16000, 0, 0);
  r.a.transmission(2, tone(), 16000, 0, 0);
  r.a.wait();
  EXPECT_TRUE(r.a.saved(kSession).empty());
  EXPECT_EQ(r.a.note(kSession), "Exercise audio not saved: disk nearly full");
  EXPECT_TRUE(r.a.remove(kSession));
  EXPECT_EQ(files(r.t.path), 0);
  EXPECT_FALSE(r.a.remove("../x"));
}
