#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace atc {

// Radio effect on an assembled pilot reply (03 section 4). Never used on trainee audio.
enum class RadioIntensity { Off, Light, Medium, Heavy };
std::string_view toString(RadioIntensity i);
std::optional<RadioIntensity> radioIntensityFromString(std::string_view s);

struct RadioEffectConfig {
  double lowHz = 300, highHz = 3000;          // two high-pass and two low-pass biquads
  double thresholdDbfs = -24, ratio = 3;      // compressor
  double attackMs = 5, releaseMs = 80, makeupDb = 4;
  double drive = 1.2;                         // soft clip; 0 = none
  bool matchLevel = true;                     // speech as loud (RMS) as without the effect, by at most 12 dB
  double hissDbfs = -40;                      // RMS, only while the transmission lasts
  int clickMs = 12, tailMs = 60;              // squelch; speech lies between them, whole
  double clickDbfs = -24;                     // peak
  double ceilingDbfs = -1;                    // no output sample is above it
};
RadioEffectConfig radioEffect(RadioIntensity i);  // Light is the table in 03 section 4

// One reply in, one buffer out: click, speech, tail. Off returns the input unchanged. Stateless and
// repeatable: the same input, rate and seed give the same samples.
std::vector<std::int16_t> applyRadioEffect(std::span<const std::int16_t> speech, int rate, const RadioEffectConfig& cfg, std::uint64_t seed);
std::vector<std::int16_t> applyRadioEffect(std::span<const std::int16_t> speech, int rate, RadioIntensity i, std::uint64_t seed);

}  // namespace atc
