#include <gtest/gtest.h>

#include "core/TxDisplay.h"
using namespace atc;
using S = TransmissionController::State;

TEST(TxDisplay, FollowsCoreStates) {
  EXPECT_EQ(txDisplay({}), TxDisplay::Idle);
  EXPECT_EQ(txDisplay({.core = S::TxOpen}), TxDisplay::Tx);
  EXPECT_EQ(txDisplay({.core = S::TxTail}), TxDisplay::Tail);
  EXPECT_EQ(txDisplay({.core = S::Recognising}), TxDisplay::Wait);
  EXPECT_EQ(txDisplay({.core = S::PilotDelay}), TxDisplay::Idle);
  EXPECT_EQ(txDisplay({.core = S::PilotSpeaking}), TxDisplay::Idle);
}

TEST(TxDisplay, StuckAndBlockedLastOnlyWhileHeld) {
  EXPECT_EQ(txDisplay({.pttHeld = true, .closedAsStuck = true}), TxDisplay::Stuck);
  EXPECT_EQ(txDisplay({.pttHeld = false, .closedAsStuck = true}), TxDisplay::Idle);
  EXPECT_EQ(txDisplay({.core = S::PilotSpeaking, .pttHeld = true, .blocked = true}), TxDisplay::Blocked);
  EXPECT_EQ(txDisplay({.core = S::PilotSpeaking, .pttHeld = false, .blocked = true}), TxDisplay::Idle);
}

TEST(TxDisplay, OpenTransmissionIsNeverHiddenByAFault) {
  EXPECT_EQ(txDisplay({.core = S::TxOpen, .pttHeld = true, .closedAsStuck = true, .blocked = true,
                       .pttPresent = false, .pulseSuspected = true}), TxDisplay::Tx);
  EXPECT_EQ(txDisplay({.core = S::TxTail, .pttPresent = false}), TxDisplay::Tail);
}

TEST(TxDisplay, QueuedPressIsNotBlocked) {
  EXPECT_EQ(txDisplay({.core = S::PilotSpeaking, .pttHeld = true, .queued = true}), TxDisplay::Queued);
  EXPECT_EQ(txDisplay({.core = S::PilotSpeaking, .pttHeld = false, .queued = true}), TxDisplay::Idle);
  EXPECT_EQ(txDisplay({.core = S::PilotSpeaking, .pttHeld = true, .queued = true, .pttPresent = false}), TxDisplay::Queued)
      << "held screen PTT must show why it waits even with no pedal";
}

TEST(TxDisplay, DeviceFaultsOutrankWaiting) {
  EXPECT_EQ(txDisplay({.core = S::Recognising, .pttPresent = false}), TxDisplay::NoPtt);
  EXPECT_EQ(txDisplay({.core = S::Recognising, .pulseSuspected = true}), TxDisplay::NoHold);
  EXPECT_EQ(txDisplay({.pttPresent = false, .pulseSuspected = true}), TxDisplay::NoPtt);
}
