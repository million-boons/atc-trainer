#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct pa_context;
struct pa_operation;
struct pa_stream;
struct pa_threaded_mainloop;

namespace atc {

// Plays one buffer at a time, 16-bit mono, unprocessed. play() and stop() return at once
// and may be called from any thread except the callback.
class PulsePlayback {
 public:
  // completed: heard to the end. Runs on the PulseAudio thread; must not call back in.
  using Finished = std::function<void(std::uint64_t id, bool completed)>;
  explicit PulsePlayback(Finished onFinished, std::string server = {});
  ~PulsePlayback();
  PulsePlayback(const PulsePlayback&) = delete;
  PulsePlayback& operator=(const PulsePlayback&) = delete;

  bool start();
  // Replaces what is playing. False if the audio server is not available.
  bool play(std::uint64_t id, std::vector<std::int16_t> samples, int rate);
  void stop();

 private:
  struct Cb;
  friend struct Cb;
  bool connect();              // locked
  void end(bool completed);    // locked
  void dropContext();          // locked

  Finished done_;
  std::string server_;
  pa_threaded_mainloop* loop_ = nullptr;
  pa_context* ctx_ = nullptr;
  pa_stream* st_ = nullptr;
  pa_operation* drain_ = nullptr;
  std::uint64_t id_ = 0;
  std::vector<std::int16_t> buf_;
  std::size_t pos_ = 0;  // samples written
};

}  // namespace atc
