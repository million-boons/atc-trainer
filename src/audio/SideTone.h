#pragma once
#include <cstdint>
#include <vector>

namespace atc {

// Short click played to the trainee when a queued press opens TX (D-24). Never mixed into
// the recording or the pilot audio.
struct SideToneConfig {
  int rate = 48000;
  int lengthMs = 20;
  double hz = 1000;
  double dbfs = -18;  // peak
};
std::vector<std::int16_t> makeSideTone(const SideToneConfig& cfg = {});

}  // namespace atc
