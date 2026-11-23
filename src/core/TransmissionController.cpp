#include "core/TransmissionController.h"

namespace atc {

std::optional<PressRule> pressRuleFromString(std::string_view s) {
  if (s == "interrupt") return PressRule::Interrupt;
  if (s == "block") return PressRule::Block;
  if (s == "queue") return PressRule::Queue;
  return std::nullopt;
}

void TransmissionController::onPress(Ns t) {
  tick(t);
  if (pedalDown_) return;
  pedalDown_ = true;
  eligibleSince_.reset();

  if (speaking_ && cfg_.pressDuringPilot == PressRule::Block) {
    press_ = Press::Blocked;
    if (cb_.pressBlocked) cb_.pressBlocked(t);
    return;
  }
  if (speaking_ && cfg_.pressDuringPilot == PressRule::Queue) {
    press_ = Press::Queued;
    if (cb_.pressQueued) cb_.pressQueued(t);
    return;
  }
  if (speaking_) {  // D-05
    Reply r = std::move(*speaking_);
    speaking_.reset();
    if (cfg_.repeatInterruptedReply) replies_.push_front(r);
    if (cb_.pilotInterrupted) cb_.pilotInterrupted(r);
  }
  if (tx_ == Tx::Tail && endReason_ == TxEnd::Normal) {  // same transmission continues
    tx_ = Tx::Open;
    return;
  }
  if (tx_ == Tx::Tail) closeTx(t, endReason_);
  openTx(t);
}

void TransmissionController::openTx(Ns t) {
  tx_ = Tx::Open;
  endReason_ = TxEnd::Normal;
  pressT_ = t;
  ++id_;
  open_[id_] = std::nullopt;
  if (cb_.txBegin) cb_.txBegin(id_, pressT_ - cfg_.preRoll);
}

void TransmissionController::onRelease(Ns t, ReleaseReason reason) {
  tick(t);
  pedalDown_ = false;
  if (press_ == Press::Queued && cb_.queuedDropped) cb_.queuedDropped(t);
  press_ = Press::Normal;
  if (tx_ == Tx::Open) {
    tx_ = Tx::Tail;
    releaseT_ = t;
    endReason_ = reason == ReleaseReason::DeviceLost ? TxEnd::DeviceLost : TxEnd::Normal;
  }
  maybeStartPilot(t);
}

void TransmissionController::closeTx(Ns audioTo, TxEnd end) {
  tx_ = Tx::None;
  if (end == TxEnd::Normal && releaseT_ - pressT_ < cfg_.minDuration) {
    open_.erase(id_);
    if (cb_.txCancelled) cb_.txCancelled(id_);
    return;
  }
  if (cb_.txEnd) cb_.txEnd(id_, pressT_ - cfg_.preRoll, audioTo, end);
}

void TransmissionController::onProcessed(std::uint64_t id, std::optional<Reply> reply, Ns now) {
  tick(now);
  auto it = open_.find(id);
  if (it == open_.end() || it->second) return;  // unknown, cancelled or duplicate
  it->second = std::move(reply);
  // Release results from the front only, so a late one cannot be overtaken.
  while (!open_.empty() && open_.begin()->second) {
    auto& r = *open_.begin()->second;
    if (r) replies_.push_back(std::move(*r));
    open_.erase(open_.begin());
  }
  maybeStartPilot(now);
}

void TransmissionController::requestPilot(Reply reply, Ns now) {
  tick(now);
  replies_.push_back(std::move(reply));
  maybeStartPilot(now);
}

void TransmissionController::onPlaybackDone(Ns now) {
  tick(now);
  speaking_.reset();  // harmless if it was interrupted just before
  if (press_ == Press::Queued) {
    press_ = Press::Normal;
    if (cb_.queuedOpened) cb_.queuedOpened(now);
    openTx(now);  // timed from here, not from the press
    return;
  }
  maybeStartPilot(now);
}

void TransmissionController::tick(Ns now) {
  if (tx_ == Tx::Open && now - pressT_ >= cfg_.maxDuration) {
    // Stuck pedal. pedalDown_ stays true, so nothing reopens until a release.
    closeTx(pressT_ + cfg_.maxDuration, TxEnd::Stuck);
  }
  if (tx_ == Tx::Tail && now - releaseT_ >= cfg_.tail) closeTx(releaseT_ + cfg_.tail, endReason_);
  maybeStartPilot(now);
}

void TransmissionController::maybeStartPilot(Ns now) {
  const bool eligible =
      !pedalDown_ && tx_ == Tx::None && open_.empty() && !speaking_ && !replies_.empty();
  if (!eligible) {
    eligibleSince_.reset();
    return;
  }
  if (!eligibleSince_) eligibleSince_ = now;
  if (now - *eligibleSince_ < replies_.front().delay) return;
  eligibleSince_.reset();
  speaking_ = std::move(replies_.front());
  replies_.pop_front();
  if (cb_.pilotStart) cb_.pilotStart(*speaking_);
}

TransmissionController::State TransmissionController::state() const {
  if (tx_ == Tx::Open) return State::TxOpen;
  if (tx_ == Tx::Tail) return State::TxTail;
  if (speaking_) return State::PilotSpeaking;
  if (!open_.empty()) return State::Recognising;
  if (!replies_.empty()) return State::PilotDelay;
  return State::Idle;
}

}  // namespace atc
