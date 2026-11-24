#include <gtest/gtest.h>
#include <libevdev/libevdev.h>

#include <cerrno>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

#include "ptt/EvdevPttSource.h"
#include "ptt/PttDeviceCheck.h"
using namespace atc;

// D-310, on devices that exist only in memory: nothing is opened or grabbed.
namespace {
struct Fake {
  std::unique_ptr<libevdev, decltype(&libevdev_free)> dev{libevdev_new(), libevdev_free};
  Fake(int vendor, int product, std::initializer_list<unsigned> codes) {
    libevdev_set_id_vendor(dev.get(), vendor);
    libevdev_set_id_product(dev.get(), product);
    for (unsigned c : codes) libevdev_enable_event_code(dev.get(), EV_KEY, c, nullptr);
  }
};
struct Grab {
  int calls = 0, result = 0;
  std::function<int(libevdev*)> f = [this](libevdev*) { ++calls; return result; };
};
Fake mouse() { return {0x04ca, 0x00bd, {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE}}; }
Fake keyPedal() { return {0x1a86, 0xe026, {KEY_B}}; }
const VidPid kPedal{0x1a86, 0xe026};
}  // namespace

TEST(PttDevice, ParsesVidPidStrictly) {
  EXPECT_EQ(vidPidFromString("04d8:0055"), (VidPid{0x04d8, 0x0055}));
  EXPECT_EQ(vidPidFromString("1A86:E026"), kPedal);
  EXPECT_EQ(toString(kPedal), "1a86:e026");
  for (const char* s : {"", ":", "4d8:0055", "04d8:55", "04d8-0055", "04d8:0055 ", " 4d8:0055", "0x4d:0055", "04g8:0055", "+4d8:0055", "04d8:-055", "04d8:00555"})
    EXPECT_FALSE(vidPidFromString(s)) << s;
}

TEST(PttDevice, KnowsKeysFromButtons) {
  EXPECT_FALSE(pttDeviceFacts(mouse().dev.get()).keyboard);
  EXPECT_FALSE(pttDeviceFacts(Fake(1, 2, {BTN_TRIGGER, BTN_THUMB, BTN_A, BTN_DPAD_UP, BTN_TRIGGER_HAPPY1}).dev.get()).keyboard);
  for (unsigned key : {unsigned(KEY_ESC), unsigned(KEY_B), unsigned(KEY_F24), unsigned(KEY_MICMUTE), unsigned(KEY_OK), unsigned(KEY_FN), unsigned(KEY_MACRO1)}) {
    EXPECT_TRUE(pttDeviceFacts(Fake(1, 2, {key}).dev.get()).keyboard) << key;
    EXPECT_TRUE(pttDeviceFacts(Fake(1, 2, {BTN_RIGHT, key}).dev.get()).keyboard) << key;  // mixed device
  }
  EXPECT_EQ(pttDeviceFacts(keyPedal().dev.get()).id, kPedal);
}

TEST(PttDevice, KeyboardTypeNeedsGrabAndExactId) {
  for (const bool grab : {false, true})
    for (const std::optional<VidPid>& id : {std::optional<VidPid>{}, std::optional<VidPid>{VidPid{0x1a86, 0xe027}}, std::optional<VidPid>{VidPid{0x1a87, 0xe026}},
                                            std::optional<VidPid>{VidPid{0, 0}}, std::optional<VidPid>{kPedal}}) {
      auto d = keyPedal();
      Grab g;
      const std::string why = claimPttDevice(d.dev.get(), {grab, id}, g.f);
      const bool allowed = grab && id == kPedal;
      EXPECT_EQ(why.empty(), allowed) << why;
      EXPECT_EQ(g.calls, allowed ? 1 : 0);  // a refused device is left alone
    }
}

TEST(PttDevice, FailedGrabRefuses) {
  for (int error : {-EBUSY, -EPERM, -EINVAL}) {
    auto d = keyPedal();
    Grab g;
    g.result = error;
    EXPECT_FALSE(claimPttDevice(d.dev.get(), {true, kPedal}, g.f).empty());
    auto m = mouse();
    EXPECT_FALSE(claimPttDevice(m.dev.get(), {true, {}}, g.f).empty());
  }
  auto d = keyPedal();
  EXPECT_FALSE(claimPttDevice(d.dev.get(), {true, kPedal}, {}).empty());
}

TEST(PttDevice, MouseStandInStaysAllowed) {
  auto m = mouse();
  Grab g;
  EXPECT_EQ(claimPttDevice(m.dev.get(), {false, {}}, g.f), "");  // today's settings
  EXPECT_EQ(g.calls, 0);
  EXPECT_EQ(claimPttDevice(m.dev.get(), {true, {}}, g.f), "");
  EXPECT_EQ(g.calls, 1);
}

TEST(PttDevice, ConfiguredIdHoldsForEveryDevice) {
  auto m = mouse();
  Grab g;
  EXPECT_EQ(claimPttDevice(m.dev.get(), {false, VidPid{0x04ca, 0x00bd}}, g.f), "");
  EXPECT_FALSE(claimPttDevice(m.dev.get(), {false, kPedal}, g.f).empty());
  // Another keyboard found under the pedal's name after a replug.
  Fake keyboard(0x413c, 0x2113, {KEY_A, KEY_B, KEY_ENTER});
  EXPECT_FALSE(claimPttDevice(keyboard.dev.get(), {true, kPedal}, g.f).empty());
  EXPECT_EQ(g.calls, 0);
}

TEST(PttDevice, RefusalNamesTheReason) {
  auto d = keyPedal();
  Grab g;
  EXPECT_NE(claimPttDevice(d.dev.get(), {true, {}}, g.f).find("pttVidPid"), std::string::npos);
  EXPECT_NE(claimPttDevice(d.dev.get(), {false, kPedal}, g.f).find("pttGrab"), std::string::npos);
  EXPECT_NE(claimPttDevice(d.dev.get(), {true, VidPid{1, 2}}, g.f).find("1a86:e026"), std::string::npos);
}

// A setting that cannot be read never means "any device".
TEST(PttDevice, UnreadableIdSettingOpensNothing) {
  std::mutex m;
  std::string error;
  std::atomic<int> opened{0};
  EvdevPttSource::Callbacks cb;
  cb.opened = [&](bool) { ++opened; };
  cb.error = [&](const std::string& e) { std::lock_guard l(m); error = e; };
  EvdevConfig c;
  c.path = "/dev/null";
  c.vidPid = "04d8/0055";
  EvdevPttSource src(c, cb);
  src.start();
  for (int i = 0; i < 100; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::lock_guard l(m);
    if (!error.empty()) break;
  }
  src.stop();
  EXPECT_NE(error.find("pttVidPid"), std::string::npos) << error;
  EXPECT_EQ(opened, 0);
}
