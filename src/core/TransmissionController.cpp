#include "core/TransmissionController.h"

#include <algorithm>

namespace atc {

std::optional<PressRule> pressRuleFromString(std::string_view s) {
  if (s == "interrupt") return PressRule::Interrupt;
  if (s == "block") return PressRule::Block;
  if (s == "queue") return PressRule::Queue;
  return std::nullopt;
}

void TransmissionController::onPress(Ns t) {
  advance(t);  // not tick: a pilot due at this instant must not start before the press (D-303)
  if (pedalDown_) return;
  pedalDown_ = true;
  eligibleSince_.reset();

  if (speaking_ && t <= pilotStart_) {
    stopPilot(PilotStop::PressCameFirst, t);
  } else if (speaking_ || (pilotEnd_ && t > pilotStart_ && t < *pilotEnd_)) {
    // The second case: the pilot ended while this press was still on its way to us.
    if (cfg_.pressDuringPilot == PressRule::Block) {
      press_ = Press::Blocked;
      if (cb_.pressBlocked) cb_.pressBlocked(t);
      return;
    }
    if (cfg_.pressDuringPilot == PressRule::Queue) {
      press_ = Press::Queued;
      if (!speaking_) queuedOpenAt_ = *pilotEnd_ + cfg_.queueSettle;
      if (cb_.pressQueued) cb_.pressQueued(t);
      return;
    }
    if (speaking_) stopPilot(PilotStop::Interrupted, t);  // D-05
  }
  if (tx_ == Tx::Tail && endReason_ == TxEnd::Normal) {  // same transmission continues
    tx_ = Tx::Open;
    return;
  }
  if (tx_ == Tx::Tail) closeTx(t, endReason_);
  openedFromQueue_ = false;
  openTx(t, t - cfg_.preRoll);
}

void TransmissionController::stopPilot(PilotStop why, Ns t) {
  const Reply r = std::move(*speaking_);
  speaking_.reset();
  pilotEnd_ = t;
  const PlaybackId id = playback_;
  if (why == PilotStop::PressCameFirst) replies_.push_front(r);
  else if (why == PilotStop::Interrupted && cfg_.repeatInterruptedReply) cut_[id_ + 1] = r;  // the press opens it
  if (cb_.pilotStop) cb_.pilotStop(r, id, why);
}

void TransmissionController::pilotEnded(Ns t) {
  if (press_ == Press::Queued) queuedOpenAt_ = t + cfg_.queueSettle;
}

void TransmissionController::openTx(Ns t, Ns audioFrom) {
  tx_ = Tx::Open;
  endReason_ = TxEnd::Normal;
  pressT_ = t;
  audioFrom_ = audioFrom;
  ++id_;
  open_[id_] = std::nullopt;
  // D-333: what was not said yet waits for this transmission: a real one replaces it, a tap gives it back.
  for (auto it = replies_.begin(); it != replies_.end();)
    if (supersedes(*it)) {
      held_.push_back(std::move(*it));
      it = replies_.erase(it);
    } else {
      ++it;
    }
  if (cb_.txBegin) cb_.txBegin(id_, audioFrom_);
}

void TransmissionController::onRelease(Ns t, ReleaseReason reason) {
  advance(t);
  pedalDown_ = false;
  const bool queued = press_ == Press::Queued;
  press_ = Press::Normal;
  queuedOpenAt_.reset();
  if (queued && cb_.queuedDropped) cb_.queuedDropped(t);
  if (tx_ == Tx::Open && openedFromQueue_ && t <= pressT_) {
    // Reported too late: PTT was already up when the transmission opened.
    tx_ = Tx::None;
    open_.erase(id_);
    if (cb_.txCancelled) cb_.txCancelled(id_);
    if (cb_.queuedDropped) cb_.queuedDropped(t);
  } else if (tx_ == Tx::Open) {
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
    // A tap changes nothing (D-325): back to where they were, behind a first call.
    const auto at = std::find_if(replies_.begin(), replies_.end(), [](const Reply& r) { return !r.first; });
    replies_.insert(at, held_.begin(), held_.end());
    held_.clear();
    if (const auto c = cut_.extract(id_)) replies_.push_front(c.mapped());  // nothing was said: again
    if (cb_.txCancelled) cb_.txCancelled(id_);
    return;
  }
  lastReal_ = id_;
  if (cb_.txEnd) cb_.txEnd(id_, audioFrom_, audioTo, end);
  drop(held_);  // after the end of the transmission that replaced them
}

void TransmissionController::onProcessed(std::uint64_t id, std::optional<Reply> reply, Ns now) {
  tick(now);
  auto it = open_.find(id);
  if (it == open_.end() || it->second) return;  // unknown, cancelled or duplicate
  it->second = std::move(reply);
  // Release results from the front only, so a late one cannot be overtaken.
  while (!open_.empty() && open_.begin()->second) {
    auto& r = *open_.begin()->second;
    const auto c = cut_.extract(open_.begin()->first);
    if (c && c.mapped().owed) replies_.push_front(c.mapped());
    const std::uint64_t k = open_.begin()->first;
    if (r && supersedes(*r) && k < lastReal_) {  // a newer instruction was given meanwhile (D-333)
      std::deque<Reply> one{std::move(*r)};
      drop(one);
    } else if (r && supersedes(*r) && k < id_ && tx_ != Tx::None) {
      held_.push_back(std::move(*r));  // decided by the transmission that is open now
    } else if (r && c) {
      replies_.push_front(std::move(*r));  // the new instruction is answered first
    } else if (r) {
      replies_.push_back(std::move(*r));
    }
    open_.erase(open_.begin());
  }
  maybeStartPilot(now);
}

void TransmissionController::requestPilot(Reply reply, Ns now) {
  tick(now);
  if (reply.first) replies_.push_front(std::move(reply));
  else replies_.push_back(std::move(reply));
  maybeStartPilot(now);
}

bool TransmissionController::setRules(PressRule pressDuringPilot, bool repeatInterruptedReply) {
  if (state() != State::Idle || pedalDown_) return false;
  cfg_.pressDuringPilot = pressDuringPilot;
  cfg_.repeatInterruptedReply = repeatInterruptedReply;
  return true;
}

void TransmissionController::drop(std::deque<Reply>& rs) {
  for (auto& r : std::exchange(rs, {}))
    if (cb_.replyDropped) cb_.replyDropped(r);
}

void TransmissionController::dropPilot(Ns now) {
  replies_.clear();
  held_.clear();
  cut_.clear();
  for (auto& [id, processed] : open_)
    if (processed) processed->reset();  // answered, but waiting for an earlier one
  if (!speaking_) return;
  stopPilot(PilotStop::Withdrawn, now);
  pilotEnded(now);
}

void TransmissionController::onPlaybackDone(PlaybackId id, Ns now) {
  advance(now);
  if (!speaking_ || id != playback_) return;
  speaking_.reset();
  pilotEnd_ = now;
  pilotEnded(now);
  maybeStartPilot(now);
}

void TransmissionController::advance(Ns now) {
  if (tx_ == Tx::Open && now - pressT_ >= cfg_.maxDuration) {
    // Stuck pedal. pedalDown_ stays true, so nothing reopens until a release.
    closeTx(pressT_ + cfg_.maxDuration, TxEnd::Stuck);
  }
  if (tx_ == Tx::Tail && now - releaseT_ >= cfg_.tail) closeTx(releaseT_ + cfg_.tail, endReason_);
  if (speaking_ && now >= pilotDeadline_) {
    stopPilot(PilotStop::Timeout, now);
    pilotEnded(now);
  }
  if (queuedOpenAt_ && now >= *queuedOpenAt_) {
    const Ns t = *queuedOpenAt_;
    queuedOpenAt_.reset();
    press_ = Press::Normal;
    openedFromQueue_ = true;
    if (cb_.queuedOpened) cb_.queuedOpened(t);
    openTx(t, t);  // no pre-roll: speech before the click is lost (D-302)
  }
}

void TransmissionController::tick(Ns now) {
  advance(now);
  maybeStartPilot(now);
}

void TransmissionController::maybeStartPilot(Ns now) {
  const bool eligible = !pedalDown_ && tx_ == Tx::None && !speaking_ && !replies_.empty() &&
                        (open_.empty() || replies_.front().first);
  if (!eligible) {
    eligibleSince_.reset();
    return;
  }
  if (!eligibleSince_) eligibleSince_ = now;
  if (now - *eligibleSince_ < replies_.front().delay) return;
  eligibleSince_.reset();
  speaking_ = std::move(replies_.front());
  replies_.pop_front();
  ++playback_;
  pilotStart_ = now;
  pilotDeadline_ = now + (speaking_->expected > 0 ? std::min(speaking_->expected + cfg_.pilotMargin, cfg_.pilotCap) : cfg_.pilotCap);
  // A copy: the callback may report the end at once, which destroys speaking_.
  const Reply r = *speaking_;
  if (cb_.pilotStart) cb_.pilotStart(r, playback_);
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
