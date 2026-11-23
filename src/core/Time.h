#pragma once
#include <time.h>

#include <cstdint>

namespace atc {
// CLOCK_MONOTONIC nanoseconds, shared by pedal and audio (P-06).
using Ns = std::int64_t;
constexpr Ns ms(std::int64_t v) { return v * 1'000'000; }
inline Ns monotonicNow() {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return Ns(t.tv_sec) * 1'000'000'000 + t.tv_nsec;
}
}  // namespace atc
