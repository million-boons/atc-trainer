#pragma once
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "core/Time.h"
#include "voice/VoiceBank.h"

namespace atc {

struct AssemblerConfig {
  int wordGapMs = 60, commaGapMs = 180;
  int fadeMs = 8;  // at each clip edge; the overlap when a gap is 0
};

// Joins clips into one buffer, 16 kHz mono. The radio effect comes after this.
class PhraseAssembler {
 public:
  explicit PhraseAssembler(const VoiceBank& bank, AssemblerConfig cfg = {}) : bank_(bank), cfg_(cfg) {}
  // clipIds as in PilotUtterance. The rng picks among takes. False, naming the clip, if one is
  // missing: nothing is ever skipped or synthesised.
  bool assemble(const std::vector<std::string>& clipIds, std::mt19937_64& rng, std::vector<std::int16_t>& out, std::string& error) const;

 private:
  const VoiceBank& bank_;
  AssemblerConfig cfg_;
};

// For Reply::expected and the pilot-speech watchdog (D-301).
constexpr Ns speechLength(std::size_t samples) { return static_cast<Ns>(samples) * 1'000'000'000 / VoiceBank::kRate; }

}  // namespace atc
