#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "core/Time.h"
#include "ptt/PttInputManager.h"

namespace atc {

// What a press does while the pilot is speaking. Set per scenario (D-12, D-20).
//   Interrupt: the pilot is cut and a transmission opens.
//   Block:     the press is discarded; nothing opens until PTT is released and pressed again.
//   Queue:     the press waits; a transmission opens when the pilot finishes if PTT is still held.
enum class PressRule { Interrupt, Block, Queue };
// Scenario JSON value; empty for anything unknown, so the loader can reject it.
std::optional<PressRule> pressRuleFromString(std::string_view s);

struct TxConfig {
  Ns minDuration = ms(250);
  Ns maxDuration = ms(30'000);
  Ns preRoll = ms(300);
  Ns tail = ms(200);
  bool repeatInterruptedReply = true;
  PressRule pressDuringPilot = PressRule::Interrupt;
};

enum class TxEnd { Normal, Stuck, DeviceLost };

struct Reply {
  std::string text;
  Ns delay = 0;  // chosen by the caller's seeded RNG
};

// Half-duplex arbitration. Pure logic: time is passed in, output through callbacks.
// Invariants: pilot speech never starts while PTT is down, and no transmission is ever
// open while the pilot speaks.
class TransmissionController {
 public:
  enum class State { Idle, TxOpen, TxTail, Recognising, PilotDelay, PilotSpeaking };
  struct Callbacks {
    std::function<void(std::uint64_t id, Ns audioFrom)> txBegin;
    std::function<void(std::uint64_t id, Ns audioFrom, Ns audioTo, TxEnd)> txEnd;
    std::function<void(std::uint64_t id)> txCancelled;  // short tap
    std::function<void(const Reply&)> pilotStart;
    std::function<void(const Reply&)> pilotInterrupted;
    std::function<void(Ns)> pressBlocked{};
    std::function<void(Ns)> pressQueued{};
    std::function<void(Ns)> queuedOpened{};   // just before txBegin: side-tone click (D-24)
    std::function<void(Ns)> queuedDropped{};  // released or lost before the pilot finished
  };

  TransmissionController(TxConfig cfg, Callbacks cb) : cfg_(cfg), cb_(std::move(cb)) {}

  void onPress(Ns t);
  void onRelease(Ns t, ReleaseReason reason);
  // Recognition and validation finished for a transmission. Applied in transmission
  // order even if results arrive out of order.
  void onProcessed(std::uint64_t id, std::optional<Reply> reply, Ns now);
  // Pilot speech not caused by a transmission, e.g. the pilot calling again.
  void requestPilot(Reply reply, Ns now);
  void onPlaybackDone(Ns now);
  void tick(Ns now);

  State state() const;
  bool pedalDown() const { return pedalDown_; }
  bool pressBlocked() const { return press_ == Press::Blocked; }  // until release
  bool pressQueued() const { return press_ == Press::Queued; }

 private:
  void openTx(Ns t);
  void closeTx(Ns audioTo, TxEnd end);
  void maybeStartPilot(Ns now);

  TxConfig cfg_;
  Callbacks cb_;
  bool pedalDown_ = false;
  enum class Press { Normal, Blocked, Queued } press_ = Press::Normal;
  enum class Tx { None, Open, Tail } tx_ = Tx::None;
  std::uint64_t id_ = 0;
  Ns pressT_ = 0, releaseT_ = 0;
  TxEnd endReason_ = TxEnd::Normal;
  // Unresolved transmissions in order; value set once processed.
  std::map<std::uint64_t, std::optional<std::optional<Reply>>> open_;
  std::deque<Reply> replies_;
  std::optional<Reply> speaking_;
  std::optional<Ns> eligibleSince_;
};

}  // namespace atc
