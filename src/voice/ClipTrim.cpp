#include "voice/ClipTrim.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace atc {
namespace {

constexpr int kFrameMs = 10;

double toDb(double amplitude) { return amplitude > 0 ? 20 * std::log10(amplitude / 32768.0) : -120; }

double rms(std::span<const std::int16_t> s) {
  if (s.empty()) return 0;
  double sum = 0;
  for (const double x : s) sum += x * x;
  return std::sqrt(sum / static_cast<double>(s.size()));
}

std::int16_t toSample(double x) { return static_cast<std::int16_t>(std::lround(std::clamp(x, -32768.0, 32767.0))); }

// Flat tops: several samples in a row at full scale.
bool clipped(std::span<const std::int16_t> s) {
  int run = 0;
  for (const int x : s) {
    run = (x >= 32767 || x <= -32767) ? run + 1 : 0;
    if (run == 3) return true;
  }
  return false;
}

}  // namespace

bool trimClip(std::span<const std::int16_t> raw, int rate, const TrimConfig& cfg, TrimResult& out, std::string& error) {
  const auto samplesFor = [&](int milliseconds) {
    return static_cast<std::size_t>(std::max(0, milliseconds)) * static_cast<std::size_t>(rate) / 1000;
  };
  if (rate < 1000) {
    error = "bad sample rate";
    return false;
  }
  const std::size_t frame = samplesFor(kFrameMs);
  const double threshold = std::max(cfg.noiseFloorDb + cfg.aboveNoiseDb, cfg.minThresholdDb);
  std::vector<bool> loud;
  for (std::size_t i = 0; i < raw.size(); i += frame)
    loud.push_back(toDb(rms(raw.subspan(i, std::min(frame, raw.size() - i)))) > threshold);

  // First and last frame of any run that is long enough; a single click is not speech.
  const std::size_t need = static_cast<std::size_t>(std::max(1, cfg.sustainMs / kFrameMs));
  std::size_t first = loud.size(), last = 0;
  for (std::size_t i = 0; i < loud.size();) {
    std::size_t j = i;
    while (j < loud.size() && loud[j]) ++j;
    if (j - i >= need) {
      if (first == loud.size()) first = i;
      last = j;  // one past
    }
    i = std::max(j, i + 1);
  }
  if (first == loud.size()) {
    error = "no speech found";
    return false;
  }

  TrimResult r;
  const std::size_t speechStart = first * frame, speechEnd = std::min(last * frame, raw.size());
  if (first == 0) r.warnings.emplace_back("speech at the very start: a word may be cut");
  if (last == loud.size()) r.warnings.emplace_back("speech at the very end: a word may be cut");
  if (speechEnd - speechStart < samplesFor(cfg.minSpeechMs)) r.warnings.emplace_back("very short");
  if (clipped(raw)) r.warnings.emplace_back("clipped");

  // Loudness of the loud frames only, so a pause between words does not count.
  double sum = 0;
  std::size_t n = 0;
  for (std::size_t f = first; f < last; ++f) {
    if (!loud[f]) continue;
    for (std::size_t i = f * frame; i < std::min((f + 1) * frame, raw.size()); ++i, ++n) sum += static_cast<double>(raw[i]) * raw[i];
  }
  r.speechDb = toDb(std::sqrt(sum / static_cast<double>(n)));
  if (r.speechDb < cfg.quietDb) r.warnings.emplace_back("very quiet");

  r.start = speechStart - std::min(speechStart, samplesFor(cfg.padBeforeMs));
  r.end = std::min(raw.size(), speechEnd + samplesFor(cfg.padAfterMs));
  const auto kept = raw.subspan(r.start, r.end - r.start);
  int peak = 0;
  for (const int x : kept) peak = std::max(peak, std::abs(x));
  r.gainDb = std::min(cfg.targetDb - r.speechDb, cfg.peakLimitDb - toDb(peak));
  const double gain = std::pow(10.0, r.gainDb / 20);
  const std::size_t in = std::min(samplesFor(cfg.fadeInMs), kept.size() / 2), tail = std::min(samplesFor(cfg.fadeOutMs), kept.size() / 2);
  r.samples.reserve(kept.size());
  for (std::size_t i = 0; i < kept.size(); ++i) {
    double x = kept[i] * gain;
    if (i < in) x *= static_cast<double>(i) / static_cast<double>(in);
    const std::size_t left = kept.size() - 1 - i;
    if (left < tail) x *= static_cast<double>(left) / static_cast<double>(tail);
    r.samples.push_back(toSample(x));
  }
  out = std::move(r);
  return true;
}

std::vector<std::int16_t> resample(std::span<const std::int16_t> in, int from, int to) {
  if (from <= 0 || to <= 0) return {};
  if (from == to) return {in.begin(), in.end()};
  const std::int64_t g = std::gcd(from, to), up = to / g, down = from / g;
  const std::int64_t size = static_cast<std::int64_t>(in.size());
  // Windowed sinc, just below the lower of the two Nyquist limits.
  const double cut = 0.95 * std::min(1.0, static_cast<double>(to) / from);
  const double half = 32 / cut;
  std::vector<std::int16_t> out;
  out.reserve(static_cast<std::size_t>(size * up / down + 1));
  for (std::int64_t k = 0; k * down < size * up; ++k) {
    const double at = static_cast<double>(k * down) / static_cast<double>(up);
    const auto a = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::ceil(at - half)));
    const auto b = std::min<std::int64_t>(size - 1, static_cast<std::int64_t>(std::floor(at + half)));
    double sum = 0;
    for (std::int64_t i = a; i <= b; ++i) {
      const double d = at - static_cast<double>(i), x = std::numbers::pi * cut * d;
      const double w = (d == 0 ? 1 : std::sin(x) / x) * (0.5 + 0.5 * std::cos(std::numbers::pi * d / half));
      sum += w * in[static_cast<std::size_t>(i)];
    }
    // The weights of the whole window sum to 1/cut; the edges are not lifted.
    out.push_back(toSample(sum * cut));
  }
  return out;
}

}  // namespace atc
