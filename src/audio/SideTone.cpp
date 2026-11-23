#include "audio/SideTone.h"

#include <algorithm>
#include <cmath>

namespace atc {

std::vector<std::int16_t> makeSideTone(const SideToneConfig& cfg) {
  const int n = cfg.rate * std::clamp(cfg.lengthMs, 5, 100) / 1000;
  const int ramp = std::max(1, cfg.rate / 500);  // 2 ms, so the edges do not splatter
  const double peak = 32767.0 * std::pow(10.0, std::min(cfg.dbfs, 0.0) / 20.0);
  std::vector<std::int16_t> out(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    const double edge = std::min({1.0, double(i) / ramp, double(n - 1 - i) / ramp});
    out[static_cast<std::size_t>(i)] =
        static_cast<std::int16_t>(std::lround(peak * edge * std::sin(2 * M_PI * cfg.hz * i / cfg.rate)));
  }
  return out;
}

}  // namespace atc
