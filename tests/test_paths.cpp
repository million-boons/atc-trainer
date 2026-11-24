#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "core/PathResolver.h"
using namespace atc;
namespace fs = std::filesystem;

namespace {
class PathsTest : public testing::Test {
 protected:
  void SetUp() override {
    base = fs::path(testing::TempDir()) / ("atc_paths_" + std::to_string(getpid()) + "_" +
                                           testing::UnitTest::GetInstance()->current_test_info()->name());
    fs::remove_all(base);
    fs::create_directories(base / "home");
    in.exe = (base / "opt/bin/atc_trainer").string();
    in.home = (base / "home").string();
    in.etc = (base / "etc").string();
  }
  void TearDown() override { fs::remove_all(base); }
  std::string root(const std::string& rel) {
    fs::create_directories(base / rel / "config");
    return (base / rel).string();
  }
  void file(const std::string& rel, const std::string& text) {
    fs::create_directories((base / rel).parent_path());
    std::ofstream(base / rel) << text;
  }
  bool run() { return resolvePaths(in, out, error); }
  bool says(const std::string& s) { return error.find(s) != std::string::npos; }

  fs::path base;
  PathInputs in;
  Paths out;
  std::string error;
  const std::string user = "home/.config/atc-trainer/paths.conf", sys = "etc/atc-trainer/paths.conf";
};
}  // namespace

TEST_F(PathsTest, NextToTheExecutable) {
  const auto r = root("opt/share/atc-trainer");
  ASSERT_TRUE(run()) << error;
  EXPECT_EQ(out.root, r);
  EXPECT_EQ(out.config, r + "/config");
  EXPECT_EQ(out.data, r + "/data");
  EXPECT_EQ(out.models, r);
  EXPECT_EQ(out.voices, r + "/voices");
  EXPECT_EQ(out.engines, (base / "opt/lib/ggml").string());
  EXPECT_EQ(out.writable, in.home + "/.local/state/atc-trainer");
}

TEST_F(PathsTest, UserFileBeatsSystemFileBeatsExecutable) {
  root("opt/share/atc-trainer");
  const auto a = root("a"), b = root("b");
  file(sys, "root=" + b + "\n");
  ASSERT_TRUE(run()) << error;
  EXPECT_EQ(out.root, b);
  file(user, "# chosen here\n\nroot = " + a + "\n");
  ASSERT_TRUE(run()) << error;
  EXPECT_EQ(out.root, a);
  EXPECT_EQ(out.engines, (base / "opt/lib/ggml").string());  // engines stay beside the executable
}

TEST_F(PathsTest, ModelsAndVoicesCanBeElsewhere) {
  const auto r = root("opt/share/atc-trainer");
  const auto m = root("m"), v = root("v"), v2 = root("v2");
  file(sys, "models=" + m + "\nvoices=" + v + "\n");
  file(user, "voices=" + v2 + "\n");
  ASSERT_TRUE(run()) << error;
  EXPECT_EQ(out.root, r);
  EXPECT_EQ(out.models, m);
  EXPECT_EQ(out.voices, v2);
}

TEST_F(PathsTest, ExplicitButMissingIsAnErrorAndNeverFallsThrough) {
  root("opt/share/atc-trainer");
  for (const char* key : {"root", "models", "voices"}) {
    const auto gone = (base / "gone").string();
    file(user, std::string(key) + "=" + gone + "\n");
    EXPECT_FALSE(run()) << key;
    EXPECT_TRUE(says(gone)) << error;
    EXPECT_TRUE(says("paths.conf")) << error;
  }
  fs::create_directories(base / "empty");
  file(user, "root=" + (base / "empty").string() + "\n");
  EXPECT_FALSE(run());
  EXPECT_TRUE(says("no config folder")) << error;
}

TEST_F(PathsTest, NothingFoundListsEveryPlace) {
  EXPECT_FALSE(run());
  EXPECT_TRUE(says((base / user).string())) << error;
  EXPECT_TRUE(says((base / sys).string())) << error;
  EXPECT_TRUE(says((base / "opt/share/atc-trainer").string())) << error;
}

TEST_F(PathsTest, BadFilesAreErrors) {
  root("opt/share/atc-trainer");
  const auto r = root("a");
  for (const std::string& t : {std::string("root"), std::string("root=relative/dir"), std::string("roots=/x"),
                               "root=" + r + "\nroot=" + r}) {
    file(user, t + "\n");
    EXPECT_FALSE(run()) << t;
    EXPECT_TRUE(says("paths.conf:")) << error;
  }
}

TEST_F(PathsTest, WritableFolderIsNeverInsideTheProgramFiles) {
  const auto r = root("home");
  file(user, "root=" + r + "\n");
  EXPECT_FALSE(run());
  EXPECT_TRUE(says("writable")) << error;
}

TEST_F(PathsTest, HomeMustBeKnown) {
  root("opt/share/atc-trainer");
  in.home.clear();
  EXPECT_FALSE(run());
}

TEST(SystemPaths, FindTheRunningProgram) {
  const auto in = systemPathInputs();
  EXPECT_TRUE(fs::exists(in.exe)) << in.exe;
  Paths p;
  std::string e;
  ASSERT_TRUE(resolvePaths(in, p, e)) << e;
  EXPECT_TRUE(fs::exists(p.config + "/ui.json")) << p.config;
}
