#include <gtest/gtest.h>

#include "core/Readiness.h"
using namespace atc;
using E = StartEffect;

namespace {
ReadinessInputs allReady() {
  return {.mic = MicReady::Ok, .speaker = SpeakerReady::Ok, .ptt = PttReady::Ok, .model = ModelReady::Ready,
          .bank = BankReady::CoverageOk, .storage = StorageReady::Ok};
}
}  // namespace

TEST(Readiness, AllReadyCountsSixAndDoesNotBlock) {
  const auto r = readiness(allReady());
  EXPECT_EQ(r.count(E::Ready), 6);
  EXPECT_EQ(r.count(E::Blocks), 0);
  EXPECT_FALSE(r.blocked());
}

TEST(Readiness, DashboardExampleIsTwoReadyTwoBlocking) {
  auto in = allReady();
  in.speaker = SpeakerReady::NotChecked;
  in.ptt = PttReady::StandIn;
  in.model = ModelReady::NotLoaded;
  in.bank = BankReady::MissingClips;
  const auto r = readiness(in);
  EXPECT_EQ(r.count(E::Ready), 2);
  EXPECT_EQ(r.count(E::Warns), 2);
  EXPECT_EQ(r.count(E::Blocks), 2);
  EXPECT_EQ(r.of(ReadyTile::Speaker), E::Warns);
  EXPECT_EQ(r.of(ReadyTile::Model), E::Blocks);
  EXPECT_TRUE(r.blocked());
}

TEST(Readiness, EveryModelStateExceptReadyBlocks) {
  for (const auto s : {ModelReady::NotLoaded, ModelReady::Loading, ModelReady::Failed, ModelReady::OtherLoaded})
    EXPECT_EQ(effect(s), E::Blocks);
  EXPECT_EQ(effect(ModelReady::Ready), E::Ready);
}

TEST(Readiness, BankBlocksUnlessLoadedOrCovered) {
  for (const auto s : {BankReady::NotLoaded, BankReady::MissingClips, BankReady::Failed}) EXPECT_EQ(effect(s), E::Blocks);
  EXPECT_EQ(effect(BankReady::Loaded), E::Warns);
  EXPECT_EQ(effect(BankReady::CoverageOk), E::Ready);
}

TEST(Readiness, MicrophoneFaultsBlockAndLevelOnlyWarns) {
  for (const auto s : {MicReady::Lost, MicReady::Muted, MicReady::NoSignal}) EXPECT_EQ(effect(s), E::Blocks);
  EXPECT_EQ(effect(MicReady::OutsideBand), E::Warns);
  EXPECT_EQ(effect(MicReady::NotTested), E::Warns);  // D-306
}

TEST(Readiness, MissingPttFollowsConfigurationAndNoHoldAlwaysBlocks) {
  EXPECT_EQ(effect(PttReady::Missing, true), E::Blocks);
  EXPECT_EQ(effect(PttReady::Missing, false), E::Warns);
  EXPECT_EQ(effect(PttReady::NoHold, false), E::Blocks);
  EXPECT_EQ(effect(PttReady::StandIn, true), E::Warns);
}

TEST(Readiness, SpeakerAndStorageNeverBlock) {
  EXPECT_EQ(effect(SpeakerReady::NotChecked), E::Warns);
  EXPECT_EQ(effect(StorageReady::Low), E::Warns);
}
