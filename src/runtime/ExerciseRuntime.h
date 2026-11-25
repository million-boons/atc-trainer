#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "audio/RadioAudioProcessor.h"
#include "core/IEventLog.h"
#include "engine/ExerciseResult.h"
#include "engine/SequenceEngine.h"
#include "parser/Parser.h"
#include "runtime/ExerciseCheck.h"
#include "runtime/RadioCore.h"
#include "stt/SttService.h"
#include "voice/PhraseAssembler.h"

namespace atc {

// The speech model as the runtime sees it. SttService fits (SpeechModel below).
struct ISpeechModel {
  virtual ~ISpeechModel() = default;
  virtual SttStatus status() const = 0;
  virtual bool lock() = 0;
  virtual void unlock() = 0;
};
struct SpeechModel : ISpeechModel {
  explicit SpeechModel(SttService& s) : stt(s) {}
  SttStatus status() const override { return stt.status(); }
  bool lock() override { return stt.lock(); }
  void unlock() override { stt.unlock(); }
  SttService& stt;
};

struct ExerciseOptions {
  std::uint64_t seed = 1;
  ExerciseMode mode = ExerciseMode::Practice;
  std::string sessionId, traineeId;
  std::int64_t startedMs = 0;
  std::string model;         // the configured exercise model; another loaded one is refused (D-414)
  bool pttFallback = false;  // a missing PTT only warns (D-116)
  RadioIntensity radio = RadioIntensity::Light;
};

struct RuntimeConfig {
  EngineConfig engine{};
  AssemblerConfig assembler{};
  Ns recogniseLimit = ms(10'000);  // no text by then: a system fault, not the trainee's; the pilot says again (D-314)
};

enum class ExercisePhase { Idle, Running, Ended };

// One exercise on the radio (06 package 3a): RadioCore -> STT -> parser -> engine -> voice bank -> player, with the
// model locked and every input and output in the log (D-39, D-427). It is a client of RadioCore beside the Speech
// Lab: wrap() puts it in front of the Lab's events, and only what belongs to the exercise is taken.
// Everything runs on RadioCore's thread; the public functions may be called from any thread unless noted.
class ExerciseRuntime {
 public:
  struct Callbacks {                                          // RadioCore's thread
    std::function<void(bool ok, const std::string& why)> started;
    std::function<void(Ns, const EngineInput&)> input;        // for the screen; already logged
    std::function<void(Ns, const EngineOutput&)> output;
    std::function<void(const ExerciseResult&)> ended;
    std::function<void(const std::string&)> fault;            // missing sound, lost log entries, recogniser silent
  };
  ExerciseRuntime(RuntimeConfig cfg, ParserRules rules, ISpeechModel& speech, Callbacks cb);
  ~ExerciseRuntime();

  // Before RadioCore is made: the events to give it. Then attach() it. RadioCore must stop before this object goes.
  RadioCore::Events wrap(RadioCore::Events lab);
  void attach(RadioCore& core) { core_ = &core; }

  // The reason if refused at once; otherwise empty, and started() reports whether the radio was free.
  std::string start(PreparedExercise ready, ExerciseOptions opt, std::unique_ptr<IEventLog> log);
  void stop();                // instructor stop: the pilot is silent at once
  void resume();              // the instructor continues after missed responses (D-424)
  void useFallback();         // on-screen PTT instead of a lost one
  // A speech result. False: not a transmission of an exercise, the Lab's.
  bool recognised(std::uint64_t tx, const std::string& modelId, const std::string& modelConfig, const SttResult& r, Ns releaseToText);
  bool ownsTx(std::uint64_t tx) const;
  ExercisePhase phase() const { return phase_; }

 private:
  struct Due { std::uint64_t generation = 0; Ns at = 0; };
  struct Tee : IEventLog {
    std::unique_ptr<IEventLog> file;
    std::vector<EngineEvent> events;
    bool begin(const EventLogHeader& h, std::string& e) override { return file->begin(h, e); }
    void append(const EngineEvent& e) override { events.push_back(e); file->append(e); }
    void end() override { file->end(); }
    std::uint64_t lost() const override { return file->lost(); }
  };
  struct Run {  // one exercise
    PreparedExercise ready;
    ExerciseOptions opt;
    std::unique_ptr<Tee> log;
    std::unique_ptr<LoggedEngine> engine;
    std::mt19937_64 takes;
    std::map<in::Timer, Due> timers;
    std::map<std::uint64_t, std::vector<std::int16_t>> sounds;  // by utterance, until it was heard
    bool fallback = false;
    bool lostReported = false;
    std::string modelId, modelConfig;                           // locked
    out::EndReason reason = out::EndReason::Completed;
  };
  bool running() const { return phase_ == ExercisePhase::Running; }
  bool mine(std::uint64_t tx) const;
  void begin(std::unique_ptr<Run> r);
  void apply(Ns t, const EngineInput& i);
  void take(Ns t, const EngineOutput& o);
  void say(const PilotUtterance& u);
  void answer(std::uint64_t tx, std::optional<Reply> r = {});
  void notRecognised(Ns t, std::uint64_t tx, const std::string& fault);
  void presence(bool present);
  void finish();
  void tick(Ns now);
  void fault(const std::string& why) { if (cb_.fault) cb_.fault(why); }

  const RuntimeConfig cfg_;
  const ParserRules rules_;
  ISpeechModel& speech_;
  const Callbacks cb_;
  RadioCore* core_ = nullptr;
  std::atomic<ExercisePhase> phase_{ExercisePhase::Idle};
  std::atomic<bool> starting_{false};
  mutable std::mutex m_;
  std::set<std::uint64_t> mine_;  // under m_: transmissions opened in an exercise

  // RadioCore's thread.
  std::unique_ptr<Run> run_;
  std::map<std::uint64_t, Ns> waiting_;  // closed and not answered: when to give up
  std::uint64_t downTx_ = 0;
  std::string downSource_;
  bool fromQueue_ = false;
  std::thread closer_;  // closes the last log's file
};

}  // namespace atc
