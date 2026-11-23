#pragma once
#include <atomic>
#include <string>

#include "audio/SideTone.h"

struct pa_context;
struct pa_stream;
struct pa_threaded_mainloop;

namespace atc {

// Keeps the click in the PulseAudio sample cache, so playing it needs no stream set-up.
// play() returns at once and may be called from the GUI thread. If the server was lost it
// returns false, starts a reconnect, and that click is missed.
class PulseSideTone {
 public:
  explicit PulseSideTone(SideToneConfig cfg = {}, std::string server = {});
  ~PulseSideTone();
  PulseSideTone(const PulseSideTone&) = delete;
  PulseSideTone& operator=(const PulseSideTone&) = delete;

  bool start();
  bool ready() const { return ready_; }
  bool play();

 private:
  struct Cb;
  friend struct Cb;
  bool connect();  // main loop locked
  void drop();     // main loop locked

  std::vector<std::int16_t> pcm_;
  int rate_;
  std::string server_, name_;
  std::atomic<bool> ready_{false};
  std::size_t written_ = 0;
  pa_threaded_mainloop* loop_ = nullptr;
  pa_context* ctx_ = nullptr;
  pa_stream* up_ = nullptr;
};

}  // namespace atc
