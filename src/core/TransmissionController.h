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
// "While speaking" is judged by the pedal's own (kernel) time stamps, not by when the event
// was processed: a press stamped at or before the pilot's start came first (D-303).
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
  // A queued press opens this long after the pilot ends, so that a release made before the
  // end is known by then. Must be longer than PttConfig::debounce.
  Ns queueSettle = ms(25);
  // Pilot-speech watchdog (D-301): expected + margin, never more than the cap.
  Ns pilotMargin = ms(1500);
  Ns pilotCap = ms(20'000);
};

enum class TxEnd { Normal, Stuck, DeviceLost };

struct Reply {
  std::string text;
  Ns delay = 0;     // chosen by the caller's seeded RNG
  Ns expected = 0;  // length of the assembled speech; 0 = unknown, only the cap applies
};

using PlaybackId = std::uint64_t;
enum class PilotStop {
  Interrupted,     // rule Interrupt
  PressCameFirst,  // the press was made before the speech began; the reply is kept
  Timeout          // no end reported in time: a fault, to be logged and shown
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
    std::function<void(const Reply&, PlaybackId)> pilotStart;
    // Must silence the output before returning: a transmission may open right after it.
    std::function<void(const Reply&, PlaybackId, PilotStop)> pilotStop;
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
  // Only the current playback counts: late, repeated or unknown ids are ignored.
  // May be called from inside pilotStart.
  void onPlaybackDone(PlaybackId id, Ns now);
  void tick(Ns now);

  State state() const;
  bool pedalDown() const { return pedalDown_; }
  bool pressBlocked() const { return press_ == Press::Blocked; }  // until release
  bool pressQueued() const { return press_ == Press::Queued; }
  Ns openedAt() const { return pressT_; }  // of the latest transmission; valid inside txBegin

 private:
  void openTx(Ns t, Ns audioFrom);
  void advance(Ns now);  // everything that depends on time except starting the pilot
  void stopPilot(PilotStop why, Ns t);
  void pilotEnded(Ns t);
  void closeTx(Ns audioTo, TxEnd end);
  void maybeStartPilot(Ns now);

  TxConfig cfg_;
  Callbacks cb_;
  bool pedalDown_ = false;
  enum class Press { Normal, Blocked, Queued } press_ = Press::Normal;
  enum class Tx { None, Open, Tail } tx_ = Tx::None;
  std::uint64_t id_ = 0;
  Ns pressT_ = 0, releaseT_ = 0, audioFrom_ = 0;
  bool openedFromQueue_ = false;
  TxEnd endReason_ = TxEnd::Normal;
  // Unresolved transmissions in order; value set once processed.
  std::map<std::uint64_t, std::optional<std::optional<Reply>>> open_;
  std::deque<Reply> replies_;
  std::optional<Reply> speaking_;
  PlaybackId playback_ = 0;
  Ns pilotStart_ = 0, pilotDeadline_ = 0;
  std::optional<Ns> pilotEnd_;       // end of the latest speech
  std::optional<Ns> queuedOpenAt_;
  std::optional<Ns> eligibleSince_;
};

}  // namespace atc
