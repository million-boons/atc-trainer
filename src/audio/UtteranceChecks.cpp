#include "audio/UtteranceChecks.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace atc {
namespace {
double toDb(double v) { return v <= 0.0 ? -120.0 : 20.0 * std::log10(v / 32768.0); }
}  // namespace

UtteranceReport checkUtterance(std::span<const std::int16_t> audio, int preRollSamples,
                               const CheckConfig& cfg) {
  UtteranceReport r;
  int run = 0, peak = 0;
  for (std::int16_t s : audio) {
    const int a = std::abs(static_cast<int>(s));
    peak = std::max(peak, a);
    run = a >= 32700 ? run + 1 : 0;
    if (run >= cfg.clipRun) r.overload = true;
  }
  r.peakDbfs = toDb(peak);

  const std::size_t frame = static_cast<std::size_t>(cfg.rate * cfg.frameMs / 1000);
  std::vector<bool> speech;
  for (std::size_t i = 0; i + frame <= audio.size(); i += frame) {
    double sum = 0;
    for (std::size_t k = 0; k < frame; ++k) sum += double(audio[i + k]) * audio[i + k];
    speech.push_back(toDb(std::sqrt(sum / double(frame))) >= cfg.speechDbfs);
  }
  const int n = static_cast<int>(speech.size());
  const int key = preRollSamples / static_cast<int>(frame);  // frame containing key-down
  // Speech only in the pre-roll is not an utterance.
  const auto after = std::find(speech.begin() + std::min(key, n), speech.end(), true);
  r.empty = after == speech.end();
  if (r.empty) return r;

  const auto all = [&](int from, int to) {
    if (from < 0 || to > n || from >= to) return false;
    return std::all_of(speech.begin() + from, speech.begin() + to, [](bool b) { return b; });
  };
  r.spokeBeforeKeying = all(key - cfg.edgeFrames, key + 1);
  r.releasedEarly = all(n - cfg.edgeFrames, n);
  if (!r.spokeBeforeKeying) r.keyToSpeechMs = static_cast<int>(after - speech.begin() - key) * cfg.frameMs;
  return r;
}

}  // namespace atc
