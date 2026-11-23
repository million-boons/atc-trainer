#include "audio/PrerollRing.h"

#include <algorithm>
#include <limits>

namespace atc {
namespace {
constexpr Ns kBucketLength = ms(250);
constexpr Ns kGapThreshold = ms(40);  // a lasting delay larger than this means lost audio
constexpr Ns kNone = std::numeric_limits<Ns>::max();
constexpr Ns kSecond = 1'000'000'000;
}  // namespace

PrerollRing::PrerollRing(int rate, std::size_t capacitySamples)
    : rate_(rate), cap_(capacitySamples), buf_(capacitySamples) {}

// Split so that weeks of samples do not overflow.
Ns PrerollRing::duration(std::uint64_t n) const {
  const auto s = static_cast<Ns>(n);
  return s / rate_ * kSecond + s % rate_ * kSecond / rate_;
}

std::uint64_t PrerollRing::samplesIn(Ns d) const {
  return d <= 0 ? 0 : static_cast<std::uint64_t>(d / kSecond * rate_ + d % kSecond * rate_ / kSecond);
}

void PrerollRing::resetWindow(Ns offset, Ns arrival) {
  minOffset_.fill(kNone);
  bucket_ = 0;
  minOffset_[0] = offset;
  bucketStart_ = arrival;
}

void PrerollRing::fillSilence(std::uint64_t n, std::uint64_t flagFrom) {
  const std::uint64_t w = written_.load(std::memory_order_relaxed);
  reserved_.store(w + n, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  for (std::uint64_t i = n > cap_ ? w + n - cap_ : w; i < w + n; ++i) write(i, 0);
  lastGapBegin_.store(flagFrom, std::memory_order_relaxed);
  lastGapEnd_.store(w + n, std::memory_order_relaxed);
  gapCount_.fetch_add(1, std::memory_order_relaxed);
  written_.store(w + n, std::memory_order_release);
}

void PrerollRing::pushGap(std::uint64_t n) {
  if (n) fillSilence(n, written_.load(std::memory_order_relaxed));
}

void PrerollRing::push(Ns arrival, std::span<const std::int16_t> block) {
  if (block.empty()) return;
  std::uint64_t w = written_.load(std::memory_order_relaxed);
  const auto offsetNow = [&] { return arrival - duration(w + block.size()); };

  if (!started_) {
    started_ = true;
    resetWindow(offsetNow(), arrival);
  } else if (restart_) {
    restart_ = false;
    const std::uint64_t lost = samplesIn(offsetNow() - offset_.load(std::memory_order_relaxed));
    if (lost) fillSilence(lost, w);
    w += lost;
    resetWindow(offsetNow(), arrival);
  } else {
    if (arrival - bucketStart_ >= kBucketLength * kBuckets) {
      // Nothing arrived for the whole window: judge by this block alone.
      resetWindow(offsetNow(), arrival);
    }
    while (arrival - bucketStart_ >= kBucketLength) {
      bucketStart_ += kBucketLength;
      bucket_ = (bucket_ + 1) % kBuckets;
      minOffset_[static_cast<std::size_t>(bucket_)] = kNone;
    }
    Ns& cur = minOffset_[static_cast<std::size_t>(bucket_)];
    cur = std::min(cur, offsetNow());
    const Ns late = *std::min_element(minOffset_.begin(), minOffset_.end()) - offset_.load(std::memory_order_relaxed);
    if (late > kGapThreshold) {  // late for the whole window, so it is lost, not delayed
      const std::uint64_t lost = samplesIn(late);
      const std::uint64_t window = samplesIn(kBucketLength * kBuckets);
      fillSilence(lost, w > window ? w - window : 0);
      w += lost;
      for (Ns& o : minOffset_)
        if (o != kNone) o -= duration(lost);
    }
  }
  offset_.store(*std::min_element(minOffset_.begin(), minOffset_.end()), std::memory_order_release);

  const std::uint64_t end = w + block.size();
  reserved_.store(end, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  for (std::size_t i = 0; i < block.size(); ++i) write(w + i, block[i]);
  written_.store(end, std::memory_order_release);
}

Ns PrerollRing::newestTime() const {
  const std::uint64_t w = written_.load(std::memory_order_acquire);
  return w == 0 ? 0 : offset_.load(std::memory_order_acquire) + duration(w);
}

std::int64_t PrerollRing::indexAt(Ns t) const {
  const Ns d = t - offset_.load(std::memory_order_acquire);
  // Round to the nearest sample; works for negative d too.
  const Ns whole = d / kSecond, frac = d % kSecond;
  return whole * rate_ + (frac * rate_ + (frac >= 0 ? kSecond / 2 : -kSecond / 2)) / kSecond;
}

PrerollRing::Cut PrerollRing::extract(Ns from, Ns to) const {
  if (to <= from) return {};
  const std::int64_t b = indexAt(from);
  // Length from the duration, so a refinement between two calls cannot change it.
  return extractIndex(b, b + static_cast<std::int64_t>(samplesIn(to - from + kSecond / rate_ / 2)));
}

PrerollRing::Cut PrerollRing::extractIndex(std::int64_t b, std::int64_t e) const {
  Cut cut;
  const std::uint64_t w = written_.load(std::memory_order_acquire);
  if (e <= b) return cut;
  const auto total = static_cast<std::size_t>(std::max<std::int64_t>(0, e - b));
  if (w == 0) {
    cut.missingTail = total;
    return cut;
  }
  const std::int64_t oldest = static_cast<std::int64_t>(w > cap_ ? w - cap_ : 0);
  const std::int64_t lo = std::clamp<std::int64_t>(b, oldest, static_cast<std::int64_t>(w));
  const std::int64_t hi = std::clamp<std::int64_t>(e, lo, static_cast<std::int64_t>(w));
  cut.missingHead = std::min(static_cast<std::size_t>(lo - std::min(b, lo)), total);
  cut.missingTail = std::min(static_cast<std::size_t>(std::max<std::int64_t>(e - hi, 0)), total);
  if (hi == lo) {  // nothing available: wholly too old, or wholly in the future
    const bool tooOld = e <= oldest;
    cut.missingHead = tooOld ? total : 0;
    cut.missingTail = tooOld ? 0 : total;
  }
  cut.samples.resize(static_cast<std::size_t>(hi - lo));
  for (std::int64_t i = lo; i < hi; ++i)
    cut.samples[static_cast<std::size_t>(i - lo)] =
        buf_[static_cast<std::uint64_t>(i) % cap_].load(std::memory_order_relaxed);

  // Drop anything the writer may have overwritten while we copied.
  std::atomic_thread_fence(std::memory_order_acquire);
  const std::uint64_t r = reserved_.load(std::memory_order_relaxed);
  const std::int64_t safe = static_cast<std::int64_t>(r > cap_ ? r - cap_ : 0);
  if (safe > lo && hi > lo) {
    const auto drop = static_cast<std::size_t>(std::min(safe, hi) - lo);
    cut.samples.erase(cut.samples.begin(), cut.samples.begin() + static_cast<std::ptrdiff_t>(drop));
    cut.missingHead += drop;
  }
  if (gapCount_.load(std::memory_order_relaxed)) {
    const auto gb = static_cast<std::int64_t>(lastGapBegin_.load(std::memory_order_relaxed));
    const auto ge = static_cast<std::int64_t>(lastGapEnd_.load(std::memory_order_relaxed));
    cut.gap = gb < hi && ge > lo;
  }
  return cut;
}

}  // namespace atc
