#include "voice/PhraseAssembler.h"

#include <algorithm>

#include "voice/PhraseTemplate.h"

namespace atc {
namespace {

std::size_t samplesFor(int milliseconds) { return static_cast<std::size_t>(std::max(0, milliseconds)) * VoiceBank::kRate / 1000; }

std::int16_t scaled(std::int16_t s, std::size_t i, std::size_t n) {
  return static_cast<std::int16_t>(static_cast<std::int64_t>(s) * static_cast<std::int64_t>(i) / static_cast<std::int64_t>(n));
}

}  // namespace

bool PhraseAssembler::assemble(const std::vector<std::string>& clipIds, std::mt19937_64& rng, std::vector<std::int16_t>& out,
                               std::string& error) const {
  std::vector<std::int16_t> r;
  bool first = true, comma = false;
  for (const auto& id : clipIds) {
    if (id == kCommaPause) {
      comma = true;
      continue;
    }
    const VoiceBank::Clip* clip = bank_.find(id);
    if (!clip || clip->takes.empty()) {
      error = "missing clip " + id;
      return false;
    }
    std::vector<std::int16_t> s = clip->takes[rng() % clip->takes.size()];
    // Fades are short against a clip; a very short clip gets shorter ones.
    const std::size_t fade = std::min(samplesFor(cfg_.fadeMs), s.size() / 2);
    for (std::size_t i = 0; i < fade; ++i) {
      s[i] = scaled(s[i], i, fade);
      s[s.size() - 1 - i] = scaled(s[s.size() - 1 - i], i, fade);
    }
    const std::size_t gap = first ? 0 : samplesFor(comma ? cfg_.commaGapMs : cfg_.wordGapMs);
    std::size_t start = r.size() + gap;
    if (!first && gap == 0) start -= std::min(fade, r.size());  // crossfade
    r.resize(std::max(r.size(), start + s.size()), 0);
    for (std::size_t i = 0; i < s.size(); ++i)
      r[start + i] = static_cast<std::int16_t>(std::clamp(static_cast<int>(r[start + i]) + static_cast<int>(s[i]), -32768, 32767));
    first = comma = false;
  }
  if (first) {
    error = "nothing to say";
    return false;
  }
  out = std::move(r);
  return true;
}

}  // namespace atc
