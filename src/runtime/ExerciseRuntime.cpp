#include "runtime/ExerciseRuntime.h"

#include <algorithm>
#include <utility>

#include "audio/UtteranceChecks.h"

namespace atc {
namespace {
template <class... F> struct Overload : F... { using F::operator()...; };
template <class... F> Overload(F...) -> Overload<F...>;
}  // namespace

ExerciseRuntime::ExerciseRuntime(RuntimeConfig cfg, ParserRules rules, ISpeechModel& speech, Callbacks cb)
    : cfg_(std::move(cfg)), rules_(std::move(rules)), speech_(speech), cb_(std::move(cb)) {}

ExerciseRuntime::~ExerciseRuntime() {
  if (closer_.joinable()) closer_.join();
}

bool ExerciseRuntime::ownsTx(std::uint64_t tx) const {
  std::lock_guard l(m_);
  return mine_.count(tx) != 0;
}

bool ExerciseRuntime::mine(std::uint64_t tx) const { return ownsTx(tx); }

std::string ExerciseRuntime::start(PreparedExercise ready, ExerciseOptions opt, std::unique_ptr<IEventLog> log) {
  if (!core_) return "no radio";
  if (!ready) return "no checked scenario";
  if (!log) return "no event log";
  if (auto why = modeError(*ready.exercise, opt.mode); !why.empty()) return why;
  if (running() || starting_.exchange(true)) return "an exercise is already running";
  const auto refuse = [this](std::string why) {
    starting_ = false;
    return why;
  };
  // The model is locked first: what is read after that cannot change.
  if (!speech_.lock()) return refuse("the speech model is not ready");
  const auto refuseLocked = [&](std::string why) {
    speech_.unlock();
    return refuse(std::move(why));
  };
  const SttStatus model = speech_.status();
  if (model.modelId != opt.model) return refuseLocked("the loaded speech model is " + model.modelId + ", not " + opt.model);

  auto r = std::make_unique<Run>();
  r->log = std::make_unique<Tee>();
  r->log->file = std::move(log);
  EventLogHeader h;
  h.sessionId = opt.sessionId;
  h.scenarioId = ready.exercise->scenario.id;
  h.scenarioHash = ready.exercise->hash;
  h.seed = opt.seed;
  h.startedMs = opt.startedMs;
  h.mode = opt.mode;
  h.traineeId = opt.traineeId;
  h.modelId = model.modelId;
  h.modelConfig = model.modelConfig;
  std::string error;
  if (!r->log->begin(h, error)) return refuseLocked("event log: " + error);
  r->modelId = model.modelId;
  r->modelConfig = model.modelConfig;
  r->engine = std::make_unique<LoggedEngine>(SequenceEngine(ready.exercise, opt.seed, cfg_.engine), *r->log);
  r->takes.seed(opt.seed ^ 0xD1B54A32D192ED03ull);
  r->ready = std::move(ready);
  r->opt = std::move(opt);
  // RadioCore's queue needs copyable functions.
  auto shared = std::make_shared<std::unique_ptr<Run>>(std::move(r));
  core_->call([this, shared] { begin(std::move(*shared)); });
  return {};
}

void ExerciseRuntime::begin(std::unique_ptr<Run> r) {
  const bool missing = core_->pttLink() != PttLink::Ok;
  std::string why;
  if (!core_->free()) why = "the radio is in use";
  else if (missing && !r->opt.pttFallback) why = "the PTT is missing";
  else if (!core_->setRules(r->ready.exercise->scenario.pressDuringPilot, r->ready.exercise->scenario.repeatInterruptedReply)) why = "the radio is in use";
  starting_ = false;
  if (!why.empty()) {
    r->log->end();
    speech_.unlock();
    if (cb_.started) cb_.started(false, why);
    return;
  }
  r->fallback = missing;
  run_ = std::move(r);
  phase_ = ExercisePhase::Running;
  if (cb_.started) cb_.started(true, {});
  apply(core_->now(), in::Started{});
}

void ExerciseRuntime::stop() {
  core_->call([this] { if (running()) apply(core_->now(), in::InstructorStop{}); });
}

void ExerciseRuntime::programClosed() {
  if (running()) apply(core_->now(), in::ProgramClosed{});
}

void ExerciseRuntime::resume() {
  core_->call([this] { if (running()) apply(core_->now(), in::InstructorContinue{}); });
}

void ExerciseRuntime::useFallback() {
  core_->call([this] {
    if (!running() || run_->fallback || core_->pttLink() == PttLink::Ok) return;
    run_->fallback = true;
    apply(core_->now(), in::PttPresence{true});
  });
}

void ExerciseRuntime::apply(Ns t, const EngineInput& i) {
  if (cb_.input) cb_.input(t, i);
  for (const auto& o : run_->engine->apply(t, i)) take(t, o);
  if (run_ && !run_->lostReported && run_->log->lost()) {
    run_->lostReported = true;
    fault("the event log is incomplete: entries could not be written");
  }
  // Last: the outputs of the ending input are complete.
  if (run_ && run_->engine->engine().status() == SequenceEngine::Status::Ended && running()) finish();
}

void ExerciseRuntime::take(Ns t, const EngineOutput& o) {
  std::visit(Overload{[&](const out::TimerSet& x) { run_->timers[x.timer] = {x.generation, t + x.delay}; },
                      [&](const out::TimerCancelled& x) { run_->timers.erase(x.timer); },
                      [&](const out::PilotRequested& x) { say(x.utterance); },
                      [&](const out::NoReply& x) { answer(x.tx); },
                      [&](const out::Ended& x) { run_->reason = x.reason; },
                      [](const auto&) {}},
             o);
  if (cb_.output) cb_.output(t, o);
}

// The sound is made when it is asked for, so that it is in memory when the pilot starts.
void ExerciseRuntime::say(const PilotUtterance& u) {
  std::vector<std::int16_t> pcm;
  std::string error;
  if (PhraseAssembler(*run_->ready.bank, cfg_.assembler).assemble(u.clipIds, run_->takes, pcm, error))
    pcm = applyRadioEffect(pcm, VoiceBank::kRate, run_->opt.radio, run_->opt.seed + u.id);
  else
    fault("pilot reply " + u.replyId + ": " + error);
  Reply r;
  r.text = u.text;
  r.delay = u.delay;
  r.expected = speechLength(pcm.size());
  r.id = u.id;
  r.first = u.first;
  r.owed = u.owed;
  run_->sounds[u.id] = std::move(pcm);
  if (u.tx) answer(u.tx, std::move(r));
  else core_->requestPilot(std::move(r));
}

void ExerciseRuntime::answer(std::uint64_t tx, std::optional<Reply> r) {
  if (waiting_.erase(tx)) core_->processed(tx, std::move(r));
}

void ExerciseRuntime::notRecognised(Ns t, std::uint64_t tx, const std::string& why) {
  if (!running()) return answer(tx);
  in::Recognised r;
  r.instruction.tx = tx;
  r.instruction.modelId = run_->modelId;
  r.instruction.modelConfig = run_->modelConfig;
  r.instruction.reasons.push_back(why.empty() ? "no speech to recognise" : why);
  r.sttFault = why;
  apply(t, r);
}

bool ExerciseRuntime::recognised(std::uint64_t tx, const std::string& modelId, const std::string& modelConfig, const SttResult& heard, Ns releaseToText) {
  if (!mine(tx)) return false;
  ParsedInstruction p = parseInstruction(heard, rules_);  // not on the radio's thread
  p.tx = tx;
  p.modelId = modelId;
  p.modelConfig = modelConfig;
  core_->call([this, tx, p = std::move(p), releaseToText] {
    if (!waiting_.count(tx)) return;  // too late, or the exercise ended
    if (!running()) return answer(tx);
    in::Recognised r;
    r.instruction = p;
    r.releaseToText = releaseToText;
    apply(core_->now(), r);
  });
  return true;
}

void ExerciseRuntime::presence(bool present) {
  if (!running()) return;
  if (present) run_->fallback = false;
  else if (run_->fallback) return;  // already accepted without it
  apply(core_->now(), in::PttPresence{present});
}

void ExerciseRuntime::finish() {
  phase_ = ExercisePhase::Ended;
  // Completed comes from the end of the last reply: nothing is left, and the radio is in the middle of it. The other
  // two come from its queue or its tick, never from inside the half-duplex logic.
  if (run_->reason != out::EndReason::Completed) core_->dropPilot();
  const auto open = std::exchange(waiting_, {});
  for (const auto& [tx, limit] : open) core_->processed(tx);
  run_->log->end();
  speech_.unlock();
  auto r = std::move(run_);
  const ExerciseResult result = buildResult(r->ready.exercise->scenario, r->log->events);
  if (r->log->lost() && !r->lostReported) fault("the event log is incomplete: entries could not be written");
  if (cb_.ended) cb_.ended(result);
  // The log's file is closed here, after the last write: not on the radio's thread.
  if (closer_.joinable()) closer_.join();
  closer_ = std::thread([r = std::move(r)]() mutable { r.reset(); });
}

void ExerciseRuntime::tick(Ns now) {
  while (running()) {
    auto next = run_->timers.end();
    for (auto i = run_->timers.begin(); i != run_->timers.end(); ++i)
      if (i->second.at <= now && (next == run_->timers.end() || i->second.at < next->second.at)) next = i;
    if (next == run_->timers.end()) break;
    const in::TimerFired f{next->first, next->second.generation};
    run_->timers.erase(next);
    apply(now, f);
  }
  std::vector<std::uint64_t> late;
  for (const auto& [tx, limit] : waiting_)
    if (now >= limit) late.push_back(tx);
  for (const auto tx : late) {
    fault("no text from the speech model for transmission " + std::to_string(tx));
    notRecognised(now, tx, "no result in time");
  }
}

RadioCore::Events ExerciseRuntime::wrap(RadioCore::Events lab) {
  auto l = std::make_shared<const RadioCore::Events>(std::move(lab));
  RadioCore::Events e = *l;
  e.note = [this, l](const PttNote& n) {
    if (n.kind == PttNote::Kind::Down) {
      downTx_ = n.tx;
      downSource_ = n.source;
    } else if (n.kind == PttNote::Kind::Lost) {
      presence(false);
    } else if (n.kind == PttNote::Kind::Ready) {
      presence(true);
    }
    if (l->note) l->note(n);
  };
  e.sideTone = [this, l] {
    fromQueue_ = true;
    if (l->sideTone) l->sideTone();
  };
  e.txOpened = [this, l](std::uint64_t id, Ns press, bool recognising) {
    const bool queued = std::exchange(fromQueue_, false);
    if (running()) {
      {
        std::lock_guard g(m_);
        mine_.insert(id);
      }
      apply(core_->now(), in::TxOpened{id, press, queued});
      // D-311: on-screen or keyboard PTT because the device is missing.
      if (running() && downTx_ == id && downSource_ != core_->pttSource() && core_->pttLink() != PttLink::Ok)
        apply(core_->now(), in::FallbackPttUsed{id, downSource_});
    }
    if (l->txOpened) l->txOpened(id, press, recognising);
  };
  e.txClosed = [this, l](RadioTx&& t) {
    if (!mine(t.id)) {
      if (l->txClosed) l->txClosed(std::move(t));
      return;
    }
    const Ns now = core_->now();
    waiting_[t.id] = now + cfg_.recogniseLimit;
    if (!running()) return answer(t.id);
    in::TxClosed c;
    c.tx = t.id;
    c.release = t.release;
    c.end = t.end;
    c.report = checkUtterance(t.audio, t.preRoll, {.rate = core_->rate()});
    apply(now, c);
    if (!t.recognising) notRecognised(now, t.id, t.hadModel ? "" : "no speech model");
  };
  e.txCancelled = [this, l](std::uint64_t id, bool pulse) {
    if (mine(id) && running()) apply(core_->now(), in::TxCancelled{id});
    if (l->txCancelled) l->txCancelled(id, pulse);
  };
  e.pressNotOpened = [this, l](in::PressResult r, Ns t) {
    if (running()) apply(core_->now(), in::PressNotOpened{r});
    if (l->pressNotOpened) l->pressNotOpened(r, t);
  };
  e.pilotSound = [this, l](const Reply& r) -> PilotSound {
    if (running()) {
      const auto i = run_->sounds.find(r.id);
      return i == run_->sounds.end() ? PilotSound{} : PilotSound{i->second, VoiceBank::kRate};
    }
    return l->pilotSound ? l->pilotSound(r) : PilotSound{};
  };
  e.pilotStarted = [this, l](const Reply& r) {
    if (running()) apply(core_->now(), in::PilotStarted{r.id});
    if (l->pilotStarted) l->pilotStarted(r);
  };
  e.pilotEnded = [this, l](const Reply& r, bool heardToEnd) {
    if (running()) {
      run_->sounds.erase(r.id);
      apply(core_->now(), in::PilotEnded{r.id, heardToEnd ? in::PilotEnd::Completed : in::PilotEnd::TimedOut});
    }
    if (l->pilotEnded) l->pilotEnded(r, heardToEnd);
  };
  e.pilotStopped = [this, l](const Reply& r, PilotStop why) {
    if (running() && (why == PilotStop::Timeout || why == PilotStop::Superseded)) run_->sounds.erase(r.id);
    if (running() && why != PilotStop::Withdrawn)
      apply(core_->now(), in::PilotEnded{r.id, why == PilotStop::Interrupted      ? in::PilotEnd::Interrupted
                                               : why == PilotStop::PressCameFirst ? in::PilotEnd::PressCameFirst
                                               : why == PilotStop::Superseded     ? in::PilotEnd::Superseded
                                                                                  : in::PilotEnd::TimedOut});
    if (l->pilotStopped) l->pilotStopped(r, why);
  };
  e.tick = [this, l](Ns now) {
    tick(now);
    if (l->tick) l->tick(now);
  };
  return e;
}

}  // namespace atc
