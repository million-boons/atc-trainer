// Microphone check. Shows device, mute, level and capture health. Saves nothing.
//   ./audio_probe [--source NAME] [--seconds N] [--server S]
#include <atomic>
#include <chrono>
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>

#include "audio/PulseCapture.h"
using namespace atc;
namespace {
std::atomic<bool> g_stop{false};
Ns monotonic() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return Ns{ts.tv_sec} * 1'000'000'000 + ts.tv_nsec;
}
}

int main(int argc, char** argv) {
  CaptureConfig cfg;
  int seconds = 0;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string k = argv[i];
    if (k == "--source") cfg.source = argv[i + 1];
    if (k == "--server") cfg.server = argv[i + 1];
    if (k == "--seconds") seconds = std::atoi(argv[i + 1]);
  }
  std::signal(SIGINT, [](int) { g_stop = true; });
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  PrerollRing ring(cfg.rate, static_cast<std::size_t>(cfg.rate) * 60);
  LevelMeter meter;
  PulseCapture cap(cfg, ring, meter, [](const CaptureStatus& s) {
    if (s.state == CaptureState::Lost) std::printf("NOT CAPTURING: %s (retrying)\n", s.message.c_str());
    if (s.state == CaptureState::Running)
      std::printf("CAPTURING %s [%s] volume %d%%%s\n", s.source.c_str(), s.description.c_str(), s.volumePercent,
                  s.muted ? "  *** MUTED ***" : "");
  });
  if (!cap.start()) return 1;
  double maxLat = 0;
  for (int n = 0; !g_stop && (seconds == 0 || n < seconds * 2); ++n) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const auto s = cap.status();
    if (s.state != CaptureState::Running) continue;
    const double db = meter.takePeakDbfs();
    const int bars = db <= -60 ? 0 : static_cast<int>((db + 60) / 2);
    const double lat = double(monotonic() - ring.newestTime()) / 1e6;
    maxLat = std::max(maxLat, lat);
    std::printf("%7.1f dBFS |%-30s| newest audio %5.1f ms ago  gaps %lu%s\n", db, std::string(static_cast<size_t>(bars), '#').c_str(),
                lat, (unsigned long)ring.gaps(),
                meter.zeroRun() > static_cast<std::uint64_t>(cfg.rate) ? "  digital silence: muted at headset?" : "");
  }
  cap.stop();
  std::printf("largest delay %.1f ms, gaps %lu\n", maxLat, (unsigned long)ring.gaps());
}
