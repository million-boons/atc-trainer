#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace atc {

struct TrimConfig {
  double noiseFloorDb = -90;       // measured room tone; -90 for a synthetic voice
  double aboveNoiseDb = 12, minThresholdDb = -50;
  int sustainMs = 30;              // speech starts and ends with this much above the threshold
  int padBeforeMs = 20, padAfterMs = 60;  // keeps soft onsets and the natural decay
  int fadeInMs = 5, fadeOutMs = 10;
  double targetDb = -20;           // RMS of the speech, dBFS
  double peakLimitDb = -1;         // the gain never lifts a peak above this
  int minSpeechMs = 100;           // shorter: warning
  double quietDb = -40;            // raw speech below this: warning
};

struct TrimResult {
  std::vector<std::int16_t> samples;
  std::size_t start = 0, end = 0;  // kept part of the raw samples
  double speechDb = 0, gainDb = 0;
  std::vector<std::string> warnings;
};

// Cuts the silence, sets the loudness and fades the edges. The raw samples are not changed.
// False if no speech is found.
bool trimClip(std::span<const std::int16_t> raw, int rate, const TrimConfig& cfg, TrimResult& out, std::string& error);

// Band-limited rate change for offline use; empty for a bad rate.
std::vector<std::int16_t> resample(std::span<const std::int16_t> in, int from, int to);

}  // namespace atc
