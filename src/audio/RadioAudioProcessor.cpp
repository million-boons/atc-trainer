#include "audio/RadioAudioProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <random>
#include <utility>

namespace atc {
namespace {

constexpr std::array<std::pair<RadioIntensity, std::string_view>, 4> kNames{{
    {RadioIntensity::Off, "off"}, {RadioIntensity::Light, "light"}, {RadioIntensity::Medium, "medium"}, {RadioIntensity::Heavy, "heavy"}}};

double amplitude(double db) { return std::pow(10.0, db / 20); }

struct Biquad {
  double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
  double run(double x) {
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

Biquad pass(bool high, double hz, double q, int rate) {
  const double w = 2 * std::numbers::pi * std::clamp(hz, 1.0, rate * 0.49) / rate, c = std::cos(w), alpha = std::sin(w) / (2 * q), a0 = 1 + alpha;
  const double b = (high ? 1 + c : 1 - c) / 2 / a0;
  return {b, high ? -2 * b : 2 * b, b, -2 * c / a0, (1 - alpha) / a0};
}

// The same numbers on every machine, unlike std::normal_distribution.
double uniform(std::mt19937_64& rng) { return double(rng() >> 11) / double(1ull << 53) * 2 - 1; }

}  // namespace

std::string_view toString(RadioIntensity i) {
  for (const auto& [v, n] : kNames)
    if (v == i) return n;
  return {};
}

std::optional<RadioIntensity> radioIntensityFromString(std::string_view s) {
  for (const auto& [v, n] : kNames)
    if (n == s) return v;
  return std::nullopt;
}

RadioEffectConfig radioEffect(RadioIntensity i) {
  RadioEffectConfig c;
  if (i == RadioIntensity::Medium) {
    c.lowHz = 350; c.highHz = 2800;
    c.ratio = 4; c.makeupDb = 5;
    c.drive = 2;
    c.hissDbfs = -36;
  } else if (i == RadioIntensity::Heavy) {
    c.lowHz = 400; c.highHz = 2600;
    c.ratio = 6; c.makeupDb = 6;
    c.drive = 3.5;
    c.hissDbfs = -32;
  }
  return c;
}

std::vector<std::int16_t> applyRadioEffect(std::span<const std::int16_t> speech, int rate, const RadioEffectConfig& cfg, std::uint64_t seed) {
  if (speech.empty() || rate <= 0) return {speech.begin(), speech.end()};
  const auto samples = [rate](int msec) { return std::size_t(std::max(0, msec)) * std::size_t(rate) / 1000; };
  const std::size_t lead = samples(cfg.clickMs), tail = samples(cfg.tailMs), n = lead + speech.size() + tail;
  std::vector<double> out(n, 0.0);
  std::mt19937_64 rng(seed);

  // Band, then compressor, then soft clip.
  std::array<Biquad, 4> band{pass(true, cfg.lowHz, 0.5412, rate), pass(true, cfg.lowHz, 1.3066, rate),  // Butterworth, 4th order
                             pass(false, cfg.highHz, 0.5412, rate), pass(false, cfg.highHz, 1.3066, rate)};
  const double up = 1 - std::exp(-1 / (std::max(0.1, cfg.attackMs) * rate / 1000)), down = 1 - std::exp(-1 / (std::max(0.1, cfg.releaseMs) * rate / 1000));
  const double threshold = amplitude(cfg.thresholdDbfs), slope = 1 - 1 / std::max(1.0, cfg.ratio), makeup = amplitude(cfg.makeupDb);
  double level = 0;
  for (std::size_t i = 0; i < speech.size() + tail; ++i) {  // the filters ring into the tail
    double x = i < speech.size() ? speech[i] / 32768.0 : 0.0;
    for (auto& f : band) x = f.run(x);
    const double a = std::abs(x);
    level += (a > level ? up : down) * (a - level);
    if (level > threshold) x *= std::pow(threshold / level, slope);
    x *= makeup;
    out[lead + i] = cfg.drive > 0 ? std::tanh(cfg.drive * x) / cfg.drive : x;
  }

  if (cfg.matchLevel) {
    double before = 0, after = 0;
    for (std::size_t i = 0; i < speech.size(); ++i) {
      before += speech[i] / 32768.0 * (speech[i] / 32768.0);
      after += out[lead + i] * out[lead + i];
    }
    if (before > 0 && after > 0) {
      const double g = std::clamp(std::sqrt(before / after), amplitude(-12), amplitude(12));
      for (double& x : out) x *= g;
    }
  }

  // Hiss as long as the carrier is there: click, speech and tail. Filtered like the speech, then set to its level.
  std::array<Biquad, 4> hissBand{pass(true, cfg.lowHz, 0.5412, rate), pass(true, cfg.lowHz, 1.3066, rate),
                                 pass(false, cfg.highHz, 0.5412, rate), pass(false, cfg.highHz, 1.3066, rate)};
  std::vector<double> hiss(n);
  double power = 0;
  for (double& h : hiss) {
    h = uniform(rng);
    for (auto& f : hissBand) h = f.run(h);
    power += h * h;
  }
  const double gain = power > 0 ? amplitude(cfg.hissDbfs) / std::sqrt(power / double(n)) : 0;
  for (std::size_t i = 0; i < n; ++i) out[i] += gain * hiss[i];

  // Squelch: a short decaying burst when it opens and when it closes.
  const double click = amplitude(cfg.clickDbfs);
  const auto burst = [&](std::size_t from, std::size_t length) {
    for (std::size_t i = 0; i < length && from + i < n; ++i) out[from + i] += click * uniform(rng) * std::exp(-5.0 * double(i) / double(length));
  };
  burst(0, lead);
  if (tail) burst(n - std::min(tail, std::max<std::size_t>(lead, 1)), std::min(tail, lead));

  // No hard edge at either end, outside the speech.
  const std::size_t edge = std::min({samples(2), lead, tail});
  for (std::size_t i = 0; i < edge; ++i) {
    const double g = double(i) / double(edge);
    out[i] *= g;
    out[n - 1 - i] *= g;
  }

  double peak = 0;
  for (const double x : out) peak = std::max(peak, std::abs(x));
  const double ceiling = amplitude(std::min(0.0, cfg.ceilingDbfs)), scale = peak > ceiling ? ceiling / peak : 1;
  std::vector<std::int16_t> pcm(n);
  std::transform(out.begin(), out.end(), pcm.begin(), [scale](double x) { return std::int16_t(std::lround(std::clamp(x * scale * 32767, -32767.0, 32767.0))); });
  return pcm;
}

std::vector<std::int16_t> applyRadioEffect(std::span<const std::int16_t> speech, int rate, RadioIntensity i, std::uint64_t seed) {
  if (i == RadioIntensity::Off) return {speech.begin(), speech.end()};
  return applyRadioEffect(speech, rate, radioEffect(i), seed);
}

}  // namespace atc
