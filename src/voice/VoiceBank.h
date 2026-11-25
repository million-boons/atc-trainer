#pragma once
#include <cstdint>
#include <functional>
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
    std::string words{};  // what the manifest says the clip says; may be empty
  };

  // The speaker's answer today (D-133, D-135). Empty: nobody may be used.
  using MayUse = std::function<bool(const std::string& speaker)>;
  // A take that may not be used, by the manifest's record of the consent or by the answer today.
  struct Withheld {
    std::string clipId, file, speaker;
  };
  // dir/manifest.json and the WAV files it names (D-405). On any error the bank stays empty.
  // Each take is "synthetic": true, or has "speaker" (a code, never a name) and "pilotVoice" {consent, date, version}:
  // the answer it was recorded under (D-132 to D-135, D-319). A take with neither is taken for synthetic in a
  // placeholder bank (made before D-319) and refused in any other.
  bool load(const std::string& dir, std::vector<LoadError>& errors, const MayUse& mayUse = {});
  const std::vector<Withheld>& withheld() const { return withheld_; }
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
  std::vector<Withheld> withheld_;
};

// Every clip the exercise can ask for is in the bank; each missing one is named.
bool checkCoverage(const Exercise& e, const VoiceBank& bank, std::vector<LoadError>& errors);

}  // namespace atc
