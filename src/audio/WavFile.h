#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace atc {

struct WavData {
  int rate = 0;
  std::vector<std::int16_t> samples;
};

// 16-bit PCM mono only. Written to a temporary name and renamed, so a crash never
// leaves a half-written file under the final name.
bool writeWav(const std::string& path, int rate, std::span<const std::int16_t> samples);
std::optional<WavData> readWav(const std::string& path);

}  // namespace atc
