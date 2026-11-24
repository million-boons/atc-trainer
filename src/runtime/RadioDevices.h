#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "audio/LevelMeter.h"
#include "audio/PrerollRing.h"
#include "audio/PulseCapture.h"  // CaptureStatus only
#include "core/Time.h"

namespace atc {

// What RadioCore needs from the hardware. Tests supply fakes. Destructors stop the device:
// no callback may run after one returns.
struct IRadioMic {
  virtual ~IRadioMic() = default;
  virtual bool start() = 0;  // fills the ring and the meter from its own thread
};

struct IRadioPtt {
  virtual ~IRadioPtt() = default;
  virtual void start() = 0;
};

struct PttCallbacks {
  std::function<void(Ns, bool down)> edge;  // kernel time (D-300)
  std::function<void(bool down)> opened;    // key state when the device was opened
  std::function<void(Ns)> lost;
  std::function<void(const std::string&)> error;
};

using PlayId = std::uint64_t;

struct OutputStatus {
  bool available = false;
  std::string name, description;
  bool muted = false;
  int volumePercent = 0;
};

struct IRadioPlayer {
  virtual ~IRadioPlayer() = default;
  virtual bool start() = 0;
  // Replaces what is playing. False: nothing will be heard and nothing reported.
  virtual bool play(PlayId id, std::vector<std::int16_t> samples, int rate) = 0;
  // Silent when it returns (D-300). False: the output did not confirm it.
  virtual bool stop() = 0;
};

struct PlayerCallbacks {
  // Only for a playback that ended by itself; never after stop() or a replacement.
  std::function<void(PlayId, bool completed)> finished;
  std::function<void(const OutputStatus&)> status;
};

// Speech recognition input. SttService fits; calls return at once.
struct ISpeechFeed {
  virtual ~ISpeechFeed() = default;
  virtual bool begin(std::uint64_t tx) = 0;  // false: no model, recording only
  virtual void accept(std::vector<std::int16_t> audio) = 0;
  virtual void finish(Ns releaseTime) = 0;
  virtual void cancel() = 0;
};

struct RadioDevices {
  std::function<std::unique_ptr<IRadioMic>(PrerollRing&, LevelMeter&, std::function<void(const CaptureStatus&)>)> mic;
  std::function<std::unique_ptr<IRadioPlayer>(PlayerCallbacks)> player;
  std::function<std::unique_ptr<IRadioPtt>(PttCallbacks)> ptt;         // empty: no device set
  std::function<std::unique_ptr<IRadioPtt>(PttCallbacks)> otherButton; // reported, never transmits (D-01)
  ISpeechFeed* speech = nullptr;                                       // must outlive RadioCore
};

}  // namespace atc
