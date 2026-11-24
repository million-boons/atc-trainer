#pragma once
#include <array>
#include <cstddef>

namespace atc {

// Dashboard tiles: does each one allow, warn about or block Start?
// Rules: docs/architecture/04-ui.md section 5a (D-103, D-106).
enum class MicReady { Ok, NotTested, OutsideBand, NoSignal, Muted, Lost };
enum class SpeakerReady { Ok, NotChecked };
enum class PttReady { Ok, StandIn, NoHold, Missing };
enum class ModelReady { Ready, NotLoaded, Loading, Failed, OtherLoaded };
enum class BankReady { CoverageOk, Loaded, Checking, NotLoaded, MissingClips, Failed };  // Loaded: coverage not checked
enum class StorageReady { Ok, Low };

enum class StartEffect { Ready, Warns, Blocks };

struct ReadinessInputs {
  MicReady mic = MicReady::Ok;
  SpeakerReady speaker = SpeakerReady::NotChecked;
  PttReady ptt = PttReady::Missing;
  ModelReady model = ModelReady::NotLoaded;
  BankReady bank = BankReady::NotLoaded;
  StorageReady storage = StorageReady::Ok;
  bool pttMissingBlocks = true;  // configuration: a missing PTT blocks, or only warns
};

constexpr StartEffect effect(MicReady s) {
  return s == MicReady::Ok ? StartEffect::Ready
         : s == MicReady::NotTested || s == MicReady::OutsideBand ? StartEffect::Warns : StartEffect::Blocks;
}
constexpr StartEffect effect(SpeakerReady s) { return s == SpeakerReady::Ok ? StartEffect::Ready : StartEffect::Warns; }
constexpr StartEffect effect(PttReady s, bool missingBlocks) {
  switch (s) {
    case PttReady::Ok: return StartEffect::Ready;
    case PttReady::StandIn: return StartEffect::Warns;
    case PttReady::NoHold: return StartEffect::Blocks;
    case PttReady::Missing: break;
  }
  return missingBlocks ? StartEffect::Blocks : StartEffect::Warns;
}
constexpr StartEffect effect(ModelReady s) { return s == ModelReady::Ready ? StartEffect::Ready : StartEffect::Blocks; }
// Only a checked bank may start; Start waits while the check runs (D-115).
constexpr StartEffect effect(BankReady s) { return s == BankReady::CoverageOk ? StartEffect::Ready : StartEffect::Blocks; }
constexpr StartEffect effect(StorageReady s) { return s == StorageReady::Ok ? StartEffect::Ready : StartEffect::Warns; }

enum class ReadyTile { Mic, Speaker, Ptt, Model, Bank, Storage };

struct Readiness {
  std::array<StartEffect, 6> tiles{};
  constexpr StartEffect of(ReadyTile t) const { return tiles[static_cast<std::size_t>(t)]; }
  constexpr int count(StartEffect e) const {
    int n = 0;
    for (const auto t : tiles) n += t == e;
    return n;
  }
  constexpr bool blocked() const { return count(StartEffect::Blocks) > 0; }
};

constexpr Readiness readiness(const ReadinessInputs& in) {
  return {{effect(in.mic), effect(in.speaker), effect(in.ptt, in.pttMissingBlocks), effect(in.model), effect(in.bank),
           effect(in.storage)}};
}

}  // namespace atc
