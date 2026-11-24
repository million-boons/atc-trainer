#pragma once
#include <string>

#include "runtime/RadioDevices.h"

namespace atc {

struct RealDeviceConfig {
  std::string pttDevice;  // stable path; empty = none
  int pttCode = 0x111;    // BTN_RIGHT (D-01); a replacement pedal is a configuration change
  bool pttGrab = false;   // true for a pedal, false for a desk mouse
  std::string pttVidPid;  // the only device accepted; required for a keyboard-type pedal (D-310)
  std::string micSource, pulseServer;
};

// PulseAudio and evdev behind RadioDevices. speech stays for the caller to set.
RadioDevices realDevices(const RealDeviceConfig& cfg);

}  // namespace atc
