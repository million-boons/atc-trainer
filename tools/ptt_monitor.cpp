// Live check of the PTT chain: evdev -> PttInputManager -> TransmissionController.
//   sudo ./ptt_monitor /dev/input/eventN [right|left] [--grab]
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "core/TransmissionController.h"
#include "ptt/EvdevPttSource.h"

using namespace atc;
namespace {
std::atomic<bool> g_stop{false};
double ms_(Ns v) { return double(v) / 1e6; }
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::puts("usage: ptt_monitor /dev/input/eventN [right|left] [--grab]");
    return 2;
  }
  EvdevConfig ec;
  ec.path = argv[1];
  for (int i = 2; i < argc; ++i) {
    if (!std::strcmp(argv[i], "left")) ec.code = 0x110;
    if (!std::strcmp(argv[i], "--grab")) ec.grab = true;
  }
  std::signal(SIGINT, [](int) { g_stop = true; });
  std::setvbuf(stdout, nullptr, _IOLBF, 0);

  TxConfig tc;
  std::uint64_t ended = 0;
  std::vector<std::uint64_t> done;
  TransmissionController tx(tc, {
      [](std::uint64_t id, Ns) { std::printf("TX %lu OPEN   (RT START)\n", (unsigned long)id); },
      [&](std::uint64_t id, Ns a, Ns b, TxEnd e) {
        std::printf("TX %lu CLOSED audio %.0f ms%s\n", (unsigned long)id, ms_(b - a),
                    e == TxEnd::Stuck ? "  STUCK PEDAL" : e == TxEnd::DeviceLost ? "  DEVICE LOST" : "");
        ++ended;
        done.push_back(id);
      },
      [](std::uint64_t id) { std::printf("TX %lu ignored: too short\n", (unsigned long)id); },
      nullptr, nullptr});
  PttInputManager pim({}, {
      [&](Ns t, const std::string&) { tx.onPress(t); },
      [&](Ns t, const std::string&, ReleaseReason r) {
        std::puts("   release   (RT STOP)");
        tx.onRelease(t, r);
      },
      nullptr,
      [](Ns held, const std::string&) {
        std::printf("   WARNING: %.1f ms press, pulse-type device cannot do hold-to-talk\n", ms_(held));
      }});

  std::mutex m;
  std::vector<std::function<void()>> q;
  const auto post = [&](std::function<void()> f) {
    std::lock_guard l(m);
    q.push_back(std::move(f));
  };
  EvdevPttSource src(ec, {
      [&](Ns t, bool d) { post([&, t, d] { pim.rawEdge(t, "pedal", d); }); },
      [&](bool d) { post([&, d] { std::printf("device open, button %s\n", d ? "DOWN" : "up"); pim.setInitialState("pedal", d); }); },
      [&](Ns t) { post([&, t] { std::puts("device lost"); pim.deviceLost(t, "pedal"); }); },
      [&](const std::string& e) { post([e] { std::printf("cannot open %s\n", e.c_str()); }); }});
  src.start();
  while (!g_stop) {
    std::vector<std::function<void()>> now;
    {
      std::lock_guard l(m);
      now.swap(q);
    }
    for (auto& f : now) f();
    const Ns t = monotonicNow();
    pim.tick(t);
    tx.tick(t);
    for (auto id : done) tx.onProcessed(id, std::nullopt, t);  // no STT yet
    done.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  src.stop();
  std::printf("transmissions completed: %lu\n", (unsigned long)ended);
}
