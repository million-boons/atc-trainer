#include <gtest/gtest.h>

#include "core/Access.h"
using namespace atc;

TEST(Access, OnlyTheInstructorManagesPeopleAndDeletes) {
  for (Role r : {Role::None, Role::Trainee}) {
    EXPECT_FALSE(mayManagePeople(r));
    EXPECT_FALSE(mayDeleteRecordings(r));
  }
  EXPECT_TRUE(mayManagePeople(Role::Instructor));
  EXPECT_TRUE(mayDeleteRecordings(Role::Instructor));
}

TEST(Access, SavedOnlyForAConsentingTraineeWithTheSwitchOn) {
  EXPECT_EQ(saveRule(true, Role::Trainee, true), SaveRule::Save);
  EXPECT_EQ(saveRule(true, Role::Trainee, false), SaveRule::NoConsent);
  EXPECT_EQ(saveRule(true, Role::None, true), SaveRule::NotLoggedIn);
  EXPECT_EQ(saveRule(true, Role::Instructor, true), SaveRule::NotLoggedIn);
  for (Role r : {Role::None, Role::Trainee, Role::Instructor}) EXPECT_EQ(saveRule(false, r, true), SaveRule::Off);
}

TEST(Access, GuardWaitsAfterFiveWrongPins) {
  LoginGuard g;
  Ns t = ms(1000);
  for (int i = 0; i < 4; ++i) {
    g.failed(t);
    EXPECT_TRUE(g.allowed(t));
  }
  g.failed(t);
  EXPECT_FALSE(g.allowed(t));
  EXPECT_EQ(g.left(t + ms(20'000)), ms(40'000));
  EXPECT_TRUE(g.allowed(t + ms(60'000)));
}

TEST(Access, ACorrectPinClearsTheCount) {
  LoginGuard g;
  for (int i = 0; i < 4; ++i) g.failed(0);
  g.succeeded();
  for (int i = 0; i < 4; ++i) g.failed(0);
  EXPECT_TRUE(g.allowed(0));
}
