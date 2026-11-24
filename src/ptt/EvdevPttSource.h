#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "core/Time.h"

namespace atc {

struct EvdevConfig {
  std::string path;    // /dev/input/eventN; matching by VID:PID/name is done by the caller
  int code = 0x111;    // BTN_RIGHT (D-01)
  bool grab = false;   // true for a pedal; false for a desk mouse used as a stand-in
  std::string vidPid;  // "04d8:0055": the only device accepted; required for a keyboard-type one (D-310)
};

// Reads one key/button on its own thread. Callbacks run on that thread; the caller
// posts them to the core thread. Timestamps are CLOCK_MONOTONIC.
// Re-binds by retrying the path; libudev matching comes with the real pedal.
class EvdevPttSource {
 public:
  struct Callbacks {
    std::function<void(Ns, bool down)> edge;
    std::function<void(bool down)> opened;  // initial key state
    std::function<void(Ns)> lost;
    std::function<void(const std::string&)> error;
  };
  EvdevPttSource(EvdevConfig cfg, Callbacks cb) : cfg_(std::move(cfg)), cb_(std::move(cb)) {}
  ~EvdevPttSource() { stop(); }
  void start();
  void stop();

 private:
  void run();
  bool session(std::string& error);  // false, with the reason, if the device could not be used

  EvdevConfig cfg_;
  Callbacks cb_;
  std::atomic<bool> stop_{false};
  std::thread thread_;
};


}  // namespace atc
