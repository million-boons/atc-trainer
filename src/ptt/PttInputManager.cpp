#include "ptt/PttInputManager.h"

namespace atc {

void PttInputManager::setInitialState(const std::string& source, bool down) {
  sources_[source] = down;
}

void PttInputManager::rawEdge(Ns t, const std::string& source, bool down) {
  tick(t);  // settle anything that became due before this edge
  bool& cur = sources_[source];
  if (cur == down) return;  // repeat, or still held since start-up: not an edge
  cur = down;

  if (!down) {
    if (owner_ == source) {
      releasePending_ = true;
      releaseT_ = t;
    }
    return;
  }
  if (owner_ == source) {
    releasePending_ = false;  // bounce: the press never really ended
    return;
  }
  if (!owner_.empty()) {  // no later edge arrives when the owner lets go, so no takeover
    if (cb_.ignored) cb_.ignored(source);
    return;
  }
  owner_ = source;
  pressT_ = t;
  if (cb_.pressed) cb_.pressed(t, source);
}

void PttInputManager::deviceLost(Ns t, const std::string& source) {
  tick(t);
  sources_.erase(source);
  if (owner_ == source) {
    if (!releasePending_) releaseT_ = t;
    confirmRelease(ReleaseReason::DeviceLost);
  }
}

void PttInputManager::tick(Ns now) {
  if (releasePending_ && now - releaseT_ >= cfg_.debounce) confirmRelease(ReleaseReason::Normal);
}

void PttInputManager::confirmRelease(ReleaseReason reason) {
  const std::string source = owner_;
  const Ns held = releaseT_ - pressT_;
  owner_.clear();
  releasePending_ = false;
  if (reason == ReleaseReason::Normal && held < cfg_.pulseLimit && cb_.pulse) cb_.pulse(held, source);
  if (cb_.released) cb_.released(releaseT_, source, reason);
}

}  // namespace atc
