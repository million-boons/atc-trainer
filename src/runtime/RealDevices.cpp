#include "runtime/RealDevices.h"

#include "audio/PulsePlayer.h"
#include "ptt/EvdevPttSource.h"

namespace atc {
namespace {

struct Mic : IRadioMic {
  Mic(CaptureConfig c, PrerollRing& r, LevelMeter& m, std::function<void(const CaptureStatus&)> f) : cap(std::move(c), r, m, std::move(f)) {}
  bool start() override { return cap.start(); }
  PulseCapture cap;
};

struct Ptt : IRadioPtt {
  Ptt(EvdevConfig c, PttCallbacks cb)
      : src(std::move(c), EvdevPttSource::Callbacks{std::move(cb.edge), std::move(cb.opened), std::move(cb.lost), std::move(cb.error)}) {}
  void start() override { src.start(); }
  EvdevPttSource src;
};

}  // namespace

RadioDevices realDevices(const RealDeviceConfig& cfg) {
  RadioDevices d;
  d.mic = [cfg](PrerollRing& r, LevelMeter& m, std::function<void(const CaptureStatus&)> f) {
    CaptureConfig c;
    c.source = cfg.micSource;
    c.server = cfg.pulseServer;
    return std::make_unique<Mic>(c, r, m, std::move(f));
  };
  d.player = [cfg](PlayerCallbacks cb) { return std::make_unique<PulsePlayer>(std::move(cb), cfg.pulseServer); };
  if (cfg.pttDevice.empty()) return d;
  EvdevConfig e{cfg.pttDevice, cfg.pttCode, cfg.pttGrab};
  d.ptt = [e](PttCallbacks cb) { return std::make_unique<Ptt>(e, std::move(cb)); };
  if (!cfg.pttGrab && cfg.pttCode == 0x111) {  // a grabbed device has only one reader
    e.code = 0x110;  // BTN_LEFT
    d.otherButton = [e](PttCallbacks cb) { return std::make_unique<Ptt>(e, std::move(cb)); };
  }
  return d;
}

}  // namespace atc
