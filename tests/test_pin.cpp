#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iterator>

#include "storage/InstructorPin.h"
using namespace atc;
namespace fs = std::filesystem;

TEST(Pin, Shape) {
  for (const char* ok : {"123456", "000000", "123456789012"}) EXPECT_TRUE(validPin(ok)) << ok;
  for (const char* bad : {"", "12345", "1234567890123", "12345a", "12 456", "-12345"}) EXPECT_FALSE(validPin(bad)) << bad;
}

TEST(Pin, OnlyTheRightOneMatches) {
  const auto h = hashPin("482913");
  ASSERT_TRUE(h);
  EXPECT_EQ(h->rfind("$y$", 0), 0u);
  EXPECT_EQ(h->find("482913"), std::string::npos);
  EXPECT_TRUE(checkPin("482913", *h));
  EXPECT_FALSE(checkPin("482914", *h));
  EXPECT_FALSE(checkPin("", *h));
  EXPECT_FALSE(hashPin("12ab56"));
}

TEST(Pin, SaltedAndNeverOpenWithoutAHash) {
  EXPECT_NE(*hashPin("482913"), *hashPin("482913"));
  for (const char* h : {"", "482913", "*0", "$y$", "$1$x$y", "$y$j9T$broken"}) EXPECT_FALSE(checkPin("482913", h)) << h;
}

TEST(Pin, File) {
  const fs::path dir = fs::temp_directory_path() / ("atc_pin_" + std::to_string(::getpid()));
  const std::string file = (dir / "instructor.pin").string();
  EXPECT_FALSE(loadPinHash(file));
  const auto h = hashPin("482913");
  ASSERT_TRUE(h);
  ASSERT_TRUE(savePinHash(file, *h));
  EXPECT_EQ(loadPinHash(file), h);
  struct stat st {};
  ASSERT_EQ(stat(file.c_str(), &st), 0);
  EXPECT_EQ(st.st_mode & 0777, 0600u);
  std::ofstream(file, std::ios::trunc) << "482913\n";  // a plain PIN is not a hash
  EXPECT_FALSE(loadPinHash(file));
  fs::remove_all(dir);
}
