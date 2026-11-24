#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

struct libevdev;

namespace atc {

struct VidPid {
  std::uint16_t vendor = 0, product = 0;
  bool operator==(const VidPid&) const = default;
};
// "04d8:0055": four hex digits each, as lsusb prints it. Empty for anything else.
std::optional<VidPid> vidPidFromString(std::string_view s);
std::string toString(const VidPid& id);

struct PttDeviceRule {
  bool grab = false;
  std::optional<VidPid> id;  // the only device allowed, if set
};
struct PttDeviceFacts {
  bool keyboard = false;  // reports any keyboard key, not only buttons
  VidPid id;
};
PttDeviceFacts pttDeviceFacts(const libevdev* dev);

// Why this device must not be used as PTT; empty if it may (D-310). A keyboard-type device needs both the
// exclusive grab and its exact VID:PID. A configured VID:PID must match for every kind of device.
std::string pttDeviceRefusal(const PttDeviceFacts& f, const PttDeviceRule& r);

// The check, then the grab if the rule asks for it. grab returns 0 or a negative errno. A refused device is never
// grabbed, and one whose grab failed is refused.
std::string claimPttDevice(libevdev* dev, const PttDeviceRule& r, const std::function<int(libevdev*)>& grab);

}  // namespace atc
