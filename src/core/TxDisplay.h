#pragma once
#include "core/TransmissionController.h"

namespace atc {

// TX state shown to the trainee. Definitions: docs/architecture/04-ui.md section 3.
// Queued is shown as WAIT with its own sub-text.
enum class TxDisplay { Idle, Tx, Tail, Wait, Queued, Blocked, Stuck, NoPtt, NoHold };

struct TxDisplayInputs {
  TransmissionController::State core = TransmissionController::State::Idle;
  bool pttHeld = false;         // a source is held (debounced)
  bool closedAsStuck = false;   // the held press was closed at the maximum duration
  bool blocked = false;         // the held press was discarded (rule "block")
  bool queued = false;          // the held press waits for the pilot (rule "queue")
  bool pttPresent = true;
  bool pulseSuspected = false;
};

constexpr TxDisplay txDisplay(const TxDisplayInputs& in) {
  using S = TransmissionController::State;
  if (in.core == S::TxOpen) return TxDisplay::Tx;
  if (in.core == S::TxTail) return TxDisplay::Tail;
  if (in.pttHeld && in.closedAsStuck) return TxDisplay::Stuck;
  if (in.pttHeld && in.blocked) return TxDisplay::Blocked;
  if (in.pttHeld && in.queued) return TxDisplay::Queued;
  if (!in.pttPresent) return TxDisplay::NoPtt;
  if (in.pulseSuspected) return TxDisplay::NoHold;
  if (in.core == S::Recognising) return TxDisplay::Wait;
  return TxDisplay::Idle;
}

}  // namespace atc
