#include "ptt/PttDeviceCheck.h"

#include <libevdev/libevdev.h>

#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>

namespace atc {

std::optional<VidPid> vidPidFromString(std::string_view s) {
  const auto hex = [](std::string_view h, std::uint16_t& v) {
    const auto r = std::from_chars(h.data(), h.data() + h.size(), v, 16);
    return r.ec == std::errc() && r.ptr == h.data() + h.size();
  };
  VidPid id;
  if (s.size() != 9 || s[4] != ':' || !hex(s.substr(0, 4), id.vendor) || !hex(s.substr(5), id.product)) return std::nullopt;
  return id;
}

std::string toString(const VidPid& id) {
  char b[10];
  std::snprintf(b, sizeof b, "%04x:%04x", unsigned(id.vendor), unsigned(id.product));
  return b;
}

PttDeviceFacts pttDeviceFacts(const libevdev* dev) {
  PttDeviceFacts f;
  f.id = {static_cast<std::uint16_t>(libevdev_get_id_vendor(dev)), static_cast<std::uint16_t>(libevdev_get_id_product(dev))};
  // Buttons are BTN_MISC up to KEY_OK; everything else of EV_KEY is a key somebody could type with. Stricter than
  // udev's ID_INPUT_KEYBOARD, which needs a whole row of keys: a one-key pedal counts too.
  for (unsigned c = KEY_ESC; c <= KEY_MAX && !f.keyboard; ++c)
    f.keyboard = !(c >= BTN_MISC && c < KEY_OK) && !(c >= BTN_DPAD_UP && c <= BTN_DPAD_RIGHT) && !(c >= BTN_TRIGGER_HAPPY && c <= BTN_TRIGGER_HAPPY40) &&
                 libevdev_has_event_code(dev, EV_KEY, c);
  return f;
}

std::string pttDeviceRefusal(const PttDeviceFacts& f, const PttDeviceRule& r) {
  if (r.id && *r.id != f.id) return "refused: the device is " + toString(f.id) + ", not " + toString(*r.id);
  if (!f.keyboard) return {};
  if (!r.id) return "refused: keyboard-type device " + toString(f.id) + " without pttVidPid in the settings";
  if (!r.grab) return "refused: keyboard-type device " + toString(f.id) + " without pttGrab in the settings";
  return {};
}

std::string claimPttDevice(libevdev* dev, const PttDeviceRule& r, const std::function<int(libevdev*)>& grab) {
  if (auto why = pttDeviceRefusal(pttDeviceFacts(dev), r); !why.empty()) return why;
  if (!r.grab) return {};
  const int rc = grab ? grab(dev) : -ENOSYS;
  return rc < 0 ? std::string("exclusive use refused: ") + std::strerror(-rc) : std::string();
}

}  // namespace atc
