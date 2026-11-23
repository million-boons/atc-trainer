#pragma once
#include <cstdint>
#include <optional>
#include <span>

namespace atc {

struct CheckConfig {
  int rate = 16000;
  double speechDbfs = -40.0;  // frame level counted as speech; tune on the panel headset
  int frameMs = 20;
  int edgeFrames = 3;         // frames that must be speech at a cut edge to flag it
  int clipRun = 4;            // consecutive full-scale samples counted as overload
};

struct UtteranceReport {
  bool empty = true;              // no speech at or after key-down
  bool overload = false;          // microphone clipping
  bool spokeBeforeKeying = false; // speech runs across the key-down point
  bool releasedEarly = false;     // speech still present at the end of the tail
  double peakDbfs = -120.0;
  std::optional<int> keyToSpeechMs;  // unset if empty or speech began before keying
};

// audio = pre-roll + keyed speech + tail, exactly as cut from the ring.
UtteranceReport checkUtterance(std::span<const std::int16_t> audio, int preRollSamples,
                               const CheckConfig& cfg = {});

}  // namespace atc
