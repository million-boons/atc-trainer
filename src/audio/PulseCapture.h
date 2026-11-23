#pragma once
#include <functional>
#include <mutex>
#include <string>

#include "audio/LevelMeter.h"
#include "audio/PrerollRing.h"
#include "core/Time.h"

struct pa_context;
struct pa_stream;
struct pa_threaded_mainloop;

namespace atc {

struct CaptureConfig {
  std::string source;  // PulseAudio source name; empty = current default (never a monitor)
  int rate = 16000;
  int fragmentMs = 10;
  std::string server;  // empty = the user's server
};

enum class CaptureState { Stopped, Connecting, Running, Lost };

struct CaptureStatus {
  CaptureState state = CaptureState::Stopped;
  std::string source, description, message;
  bool muted = false;
  int volumePercent = 0;
  Ns lastBlock = 0;  // when the newest block arrived
};

// Always-on microphone capture into a PrerollRing. PulseAudio resamples to 16 kHz mono.
// The stream is pinned to its source: if the headset is unplugged PulseAudio must not
// silently move capture to another microphone. It reports Lost and retries every second.
class PulseCapture {
 public:
  PulseCapture(CaptureConfig cfg, PrerollRing& ring, LevelMeter& meter,
               std::function<void(const CaptureStatus&)> onStatus = {});
  ~PulseCapture();
  PulseCapture(const PulseCapture&) = delete;
  PulseCapture& operator=(const PulseCapture&) = delete;

  bool start();
  void stop();
  CaptureStatus status() const;

 private:
  struct Cb;  // PulseAudio C callbacks
  friend struct Cb;
  void connect();
  void openStream(const std::string& source);
  void teardown();
  void fail(const std::string& why);
  void publish();

  CaptureConfig cfg_;
  PrerollRing& ring_;
  LevelMeter& meter_;
  std::function<void(const CaptureStatus&)> onStatus_;
  pa_threaded_mainloop* loop_ = nullptr;
  pa_context* ctx_ = nullptr;
  pa_stream* stream_ = nullptr;
  bool retryPending_ = false;
  bool announced_ = false;  // loop thread only
  mutable std::mutex m_;
  CaptureStatus st_;
  std::atomic<Ns> lastBlock_{0};  // kept out of the mutex: audio callback
};

}  // namespace atc
