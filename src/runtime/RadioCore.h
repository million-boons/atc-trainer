#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "core/EngineEvent.h"
#include "core/TransmissionController.h"
#include "core/TxDisplay.h"
#include "ptt/PttInputManager.h"
#include "runtime/RadioDevices.h"

namespace atc {

enum class PttLink { NotSet, Opening, Ok, NotOpen, Lost };

struct RadioSnapshot {
  TransmissionController::State state = TransmissionController::State::Idle;
  TxDisplay display = TxDisplay::Idle;
  std::uint64_t tx = 0;    // latest transmission
  double txSeconds = -1;   // < 0: no timer
  bool active = false;     // open, in its tail, being cut or recognised
  bool pilotCut = false;   // the pilot was interrupted less than pilotCutNotice ago
  bool held = false;
  std::string owner;       // source of the latest press
  bool otherDown = false;
  bool testing = false;    // PTT test: presses do not transmit
  PttLink ptt = PttLink::NotSet;
  std::string pttError;
  bool pulse = false;      // pulse-type device suspected
  CaptureStatus mic;
  bool micSilent = false;  // exactly zero, or no audio delivered, for a second
  double micDbfs = -120;   // peak since the previous snapshot
  bool pilotSpeaking() const { return state == TransmissionController::State::PilotSpeaking; }
};

struct PttNote {
  enum class Kind { Down, Up, Lost, Ignored, Pulse, Ready, HeldAtOpen, OtherDown, OtherUp, Stuck, Cancelled };
  Kind kind;
  Ns t = 0;
  std::string source{};
  Ns held = 0;
  std::string owner{};   // Ignored: who holds the transmission
  std::uint64_t tx = 0;  // Down, Stuck, Cancelled
};

// A closed transmission with its audio. Answer every one with exactly one processed().
struct RadioTx {
  std::uint64_t id = 0;
  Ns from = 0, to = 0, press = 0, release = 0;  // release: the limit if Stuck
  TxEnd end = TxEnd::Normal;
  std::vector<std::int16_t> audio{};  // pre-roll + keyed speech + tail
  int preRoll = 0;                  // samples
  bool gap = false;                 // audio was lost
  bool noSpeech = false;
  bool hadModel = false;
  bool recognising = false;         // a speech result will come
};

enum class LocalSound { Recording, Tone };
enum class LocalEnd { Started, Completed, Interrupted, Refused, Failed };

struct PttTest {
  enum class Kind { Started, Refused, Down, Up, Ended };
  Kind kind;
  std::string source{};
  Ns held = 0;
  ReleaseReason reason = ReleaseReason::Normal;
};

struct PilotSound {
  std::vector<std::int16_t> samples;
  int rate = 0;
};

struct RadioConfig {
  TxConfig tx;
  PttConfig ptt;
  int rate = 16000;
  int ringSeconds = 60;
  Ns publishEvery = ms(40);
  Ns lastAudioWait = ms(1000);  // for the tail to be captured
  Ns pilotCutNotice = ms(2000);
  std::string pttSource = "RIGHT", otherSource = "LEFT";
};

// The one owner of PTT, microphone, half-duplex rules, speech feed and all sound the trainee
// hears (06 section 1). Speech Lab and Exercise are clients. Everything runs on one core
// thread; public functions may be called from any thread and return at once, except
// stop() and the destructor. Nothing is written to disk here (D-44).
class RadioCore {
 public:
  // Called on the core thread; must return quickly and may call the public functions.
  struct Events {
    std::function<void(const RadioSnapshot&)> snapshot;
    std::function<void(const PttNote&)> note;
    std::function<void()> pressed;  // before anything opens
    std::function<void(std::uint64_t id, Ns press, bool recognising)> txOpened;
    std::function<void(RadioTx&&)> txClosed;
    std::function<void(std::uint64_t id, bool pulse)> txCancelled;
    std::function<void()> sideTone;  // a queued press opened (D-24)
    std::function<void(in::PressResult, Ns)> pressNotOpened;  // a press during pilot speech that opens nothing yet
    std::function<void(const Reply&)> pilotStarted;
    std::function<void(const Reply&, bool heardToEnd)> pilotEnded;  // not called when stopped
    std::function<void(const Reply&, PilotStop)> pilotStopped;
    std::function<void(const std::string&)> outputFault;
    std::function<void(LocalSound, LocalEnd)> local;
    std::function<void(const PttTest&)> pttTest;
    std::function<void(const OutputStatus&)> output;
    std::function<PilotSound(const Reply&)> pilotSound;  // from memory, at once
    std::function<void(Ns now)> tick;  // every step, after what was queued
  };

  RadioCore(RadioConfig cfg, RadioDevices dev, Events ev, std::function<Ns()> clock = monotonicNow);
  ~RadioCore();
  RadioCore(const RadioCore&) = delete;
  RadioCore& operator=(const RadioCore&) = delete;

  // Opens the devices and starts the core thread. False if the microphone did not start.
  bool start();
  void stop();

  void press(const std::string& source);    // on-screen PTT and keyboard
  void release(const std::string& source);
  void processed(std::uint64_t tx, std::optional<Reply> reply = {});
  void requestPilot(Reply reply);
  // Recording or test tone. Refused unless the radio is idle; a press silences it.
  void playLocal(LocalSound what, std::vector<std::int16_t> samples, int rate);
  void stopLocal();
  // While on, a press is measured and reported and never opens a transmission.
  void setPttTest(bool on);

  // Runs f on the core thread, in order with everything else that is queued.
  void call(std::function<void()> f) { post(std::move(f)); }

  // Core thread only (inside an event or call()).
  Ns now() const { return clock_(); }
  int rate() const { return cfg_.rate; }
  const std::string& pttSource() const { return cfg_.pttSource; }
  PttLink pttLink() const { return link_; }
  bool free() const { return idle() && !testing_; }  // nothing open, held, waiting or speaking; no PTT test
  // Both also end Lab playback.
  bool setRules(PressRule pressDuringPilot, bool repeatInterruptedReply);
  void dropPilot();

  // For tests: open the devices without a thread, then run the core by hand.
  bool open();
  void step();

 private:
  struct Closing {
    std::uint64_t id;
    Ns from, to, press;
    TxEnd end;
    std::int64_t endIndex;
    Ns release, deadline;
  };
  enum class Sound { None, Pilot, Local };
  void post(std::function<void()> f);
  void build();
  void run();
  void feed(std::int64_t to);
  void closeNow();
  void publish(Ns now);
  void note(PttNote n) { if (ev_.note) ev_.note(n); }
  bool idle() const;
  void silence();
  void endLocal(LocalEnd why);
  void startPilot(const Reply& r, PlaybackId id);
  void playerFinished(PlayId id, bool completed);
  void flushPilot();

  const RadioConfig cfg_;
  RadioDevices dev_;
  const Events ev_;
  const std::function<Ns()> clock_;

  std::mutex m_;
  std::condition_variable cv_;
  std::vector<std::function<void()>> q_;
  std::atomic<bool> stop_{false};

  // Core thread.
  std::optional<PttInputManager> ptt_;
  std::optional<TransmissionController> tx_;
  std::string owner_, pttError_;
  Ns pressT_ = 0, openT_ = 0, releaseT_ = 0, lastPublish_ = 0;
  std::optional<Ns> cutUntil_;
  bool held_ = false, stuck_ = false, pulse_ = false, other_ = false;
  PttLink link_ = PttLink::NotSet;
  bool testing_ = false;
  bool heldAtOpen_ = false;        // the device was opened with its key down: ready when it is seen released
  bool swallowed_ = false;         // the held press belongs to a test
  std::vector<Reply> deferred_;    // pilot calls asked for during a test
  std::uint64_t curId_ = 0;
  bool streaming_ = false;
  std::int64_t cursor_ = 0;
  bool lost_ = false;              // audio was overwritten before it could be sent
  std::optional<Closing> closing_;
  CaptureStatus mic_;
  std::int64_t micIndex_ = 0;
  Ns micAt_ = 0;                   // when the newest audio was first seen
  Sound sound_ = Sound::None;
  LocalSound local_ = LocalSound::Recording;
  PlayId play_ = 0;
  PlaybackId pilotPlayback_ = 0;
  Reply pilot_;

  PrerollRing ring_;
  LevelMeter meter_;
  std::unique_ptr<IRadioPlayer> player_;
  std::unique_ptr<IRadioMic> cap_;
  std::unique_ptr<IRadioPtt> pttDev_, otherDev_;
  std::thread thread_;
};

}  // namespace atc
