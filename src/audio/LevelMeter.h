#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <span>

namespace atc {

// Written by the capture callback, read by any thread (UI at 20-30 Hz).
class LevelMeter {
 public:
  void add(std::span<const std::int16_t> block) {
    int peak = 0;
    for (std::int16_t s : block) {
      const int a = std::abs(static_cast<int>(s));
      peak = std::max(peak, a);
      zeroRun_ = a == 0 ? zeroRun_ + 1 : 0;
    }
    zeros_.store(zeroRun_, std::memory_order_relaxed);
    int old = peak_.load(std::memory_order_relaxed);
    while (peak > old && !peak_.compare_exchange_weak(old, peak, std::memory_order_relaxed)) {
    }
  }
  // Highest level since the previous call.
  double takePeakDbfs() {
    const int p = peak_.exchange(0, std::memory_order_relaxed);
    return p == 0 ? -120.0 : 20.0 * std::log10(p / 32768.0);
  }
  // Consecutive exactly-zero samples: a long run suggests a muted or dead microphone.
  std::uint64_t zeroRun() const { return zeros_.load(std::memory_order_relaxed); }

 private:
  std::uint64_t zeroRun_ = 0;  // writer only
  std::atomic<std::uint64_t> zeros_{0};
  std::atomic<int> peak_{0};
};

}  // namespace atc
