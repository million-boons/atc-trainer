#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

#include "core/Time.h"

namespace atc {

// Always-running capture buffer. One writer (audio callback: no locks, no allocation)
// and any number of readers that cut audio by monotonic time.
//
// Time mapping: sample n was captured at offset + n / rate. Audio can be delivered late
// but never early, so offset is the smallest (arrival - samples / rate) seen in the last
// two seconds. This does not rely on the sound server's latency estimate, which was
// measured to be wrong by hundreds of ms for several seconds after start-up.
// Lost audio is filled with silence so the mapping stays linear.
class PrerollRing {
 public:
  PrerollRing(int rate, std::size_t capacitySamples);

  // Writer only. arrival = time the block was delivered (its last sample is not newer).
  void push(Ns arrival, std::span<const std::int16_t> block);
  // Writer only. The sound server reported this many samples as lost.
  void pushGap(std::uint64_t samples);
  // Writer only. The stream was reopened: fill from the old end to the next block.
  void restart() { restart_ = started_; }

  struct Cut {
    std::vector<std::int16_t> samples;
    std::size_t missingHead = 0;  // requested but already overwritten or before start
    std::size_t missingTail = 0;  // requested but not captured yet
    bool gap = false;             // lost audio lies inside the cut
  };
  Cut extract(Ns from, Ns to) const;
  // Sample-exact reading, so that successive reads never repeat or skip audio even if
  // the time mapping is refined between them.
  std::int64_t indexAt(Ns t) const;
  Cut extractIndex(std::int64_t begin, std::int64_t end) const;
  std::int64_t newestIndex() const { return static_cast<std::int64_t>(written_.load(std::memory_order_acquire)); }

  // Time just after the newest captured sample; 0 before the first block.
  Ns newestTime() const;
  std::uint64_t gaps() const { return gapCount_.load(std::memory_order_relaxed); }

 private:
  static constexpr int kBuckets = 8;
  Ns duration(std::uint64_t samples) const;
  std::uint64_t samplesIn(Ns d) const;
  void fillSilence(std::uint64_t n, std::uint64_t flagFrom);
  void write(std::uint64_t idx, std::int16_t v) { buf_[idx % cap_].store(v, std::memory_order_relaxed); }
  void resetWindow(Ns offset, Ns arrival);

  int rate_;
  std::size_t cap_;
  std::vector<std::atomic<std::int16_t>> buf_;
  std::atomic<std::uint64_t> written_{0};
  std::atomic<std::uint64_t> reserved_{0};  // samples up to here may be being overwritten
  std::atomic<Ns> offset_{0};
  std::atomic<std::uint64_t> gapCount_{0};
  std::atomic<std::uint64_t> lastGapBegin_{0}, lastGapEnd_{0};
  // Writer only.
  bool started_ = false, restart_ = false;
  std::array<Ns, kBuckets> minOffset_{};
  int bucket_ = 0;
  Ns bucketStart_ = 0;
};

}  // namespace atc
