#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "scenario/Scenario.h"

namespace atc {

// Pilot clips, all decoded into memory at load. Immutable afterwards.
class VoiceBank {
 public:
  static constexpr int kRate = 16000;
  struct Clip {
    std::vector<std::vector<std::int16_t>> takes;  // primary first, then alternates; rejected ones are not loaded
  };

  // dir/manifest.json and the WAV files it names (D-405). On any error the bank stays empty.
  bool load(const std::string& dir, std::vector<LoadError>& errors);
  const Clip* find(const std::string& clipId) const;
  const std::string& voice() const { return voice_; }
  const std::string& manifestFile() const { return manifest_; }
  bool placeholder() const { return placeholder_; }  // e.g. Piper clips (D-413)
  std::size_t clipCount() const { return clips_.size(); }
  std::size_t bytes() const;

 private:
  std::string voice_, manifest_;
  bool placeholder_ = true;
  std::map<std::string, Clip> clips_;
};

// Every clip the exercise can ask for is in the bank; each missing one is named.
bool checkCoverage(const Exercise& e, const VoiceBank& bank, std::vector<LoadError>& errors);

}  // namespace atc
