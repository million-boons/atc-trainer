#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "runtime/RadioDevices.h"

struct pa_context;
struct pa_operation;
struct pa_stream;
struct pa_threaded_mainloop;
struct pa_time_event;

namespace atc {

// The one sound output of the radio: pilot speech, recordings and the test tone. One buffer
// at a time, 16-bit mono. Follows the default output and reports it. If the server is lost
// it reconnects every second. Call from one thread at a time, never from a callback.
class PulsePlayer : public IRadioPlayer {
 public:
  // Callbacks run on the PulseAudio thread and must not call back in.
  explicit PulsePlayer(PlayerCallbacks cb, std::string server = {}, std::string sink = {});
  ~PulsePlayer() override;
  PulsePlayer(const PulsePlayer&) = delete;
  PulsePlayer& operator=(const PulsePlayer&) = delete;

  bool start() override;
  bool play(PlayId id, std::vector<std::int16_t> samples, int rate) override;
  // Waits until the server has stopped taking audio from us, at most 100 ms. What the sound
  // card already holds is not covered: measured on the panel headset.
  bool stop() override;

 private:
  struct Cb;
  friend struct Cb;
  // All with the main loop locked.
  void connect();
  void dropContext();
  void retryLater();
  void dropStream();
  void ended(bool completed);
  void query();
  void report(const OutputStatus& s);

  PlayerCallbacks cb_;
  std::string server_, sink_;
  pa_threaded_mainloop* loop_ = nullptr;
  pa_context* ctx_ = nullptr;
  pa_stream* st_ = nullptr;
  pa_operation* drain_ = nullptr;
  pa_time_event* retry_ = nullptr;
  bool closing_ = false, stopping_ = false, timedOut_ = false;
  int acks_ = 0;
  PlayId id_ = 0;
  std::vector<std::int16_t> buf_;
  std::size_t pos_ = 0;  // samples written
  OutputStatus last_;
  bool reported_ = false;
};

}  // namespace atc
