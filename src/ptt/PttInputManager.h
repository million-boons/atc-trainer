#pragma once
#include <functional>
#include <map>
#include <string>

#include "core/Time.h"

namespace atc {

enum class ReleaseReason { Normal, DeviceLost };

struct PttConfig {
  Ns debounce = ms(20);
  Ns pulseLimit = ms(50);  // a confirmed press shorter than this is reported as a pulse
};

// Merges raw edges from several sources into one debounced press/release stream.
// A press is reported at once; a release is confirmed after the debounce window
// but carries the time of the raw edge. The first source to press owns the
// transmission until it releases; other sources are ignored and reported.
// Not thread-safe: call from the core thread only.
class PttInputManager {
 public:
  struct Callbacks {
    std::function<void(Ns, const std::string& source)> pressed;
    std::function<void(Ns, const std::string& source, ReleaseReason)> released;
    std::function<void(const std::string& source)> ignored;         // pressed while another owns
    std::function<void(Ns held, const std::string& source)> pulse;  // pulse-type device suspected
  };

  PttInputManager(PttConfig cfg, Callbacks cb) : cfg_(cfg), cb_(std::move(cb)) {}

  void rawEdge(Ns t, const std::string& source, bool down);
  void deviceLost(Ns t, const std::string& source);
  // Key state at open or re-plug: a key already down must be seen up before it counts.
  void setInitialState(const std::string& source, bool down);
  void tick(Ns now);

  bool isDown() const { return !owner_.empty(); }

 private:
  void confirmRelease(ReleaseReason reason);

  PttConfig cfg_;
  Callbacks cb_;
  std::map<std::string, bool> sources_;  // last known down state
  std::string owner_;
  Ns pressT_ = 0;
  bool releasePending_ = false;
  Ns releaseT_ = 0;
};

}  // namespace atc
