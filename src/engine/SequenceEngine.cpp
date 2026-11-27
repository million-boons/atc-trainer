#include "engine/SequenceEngine.h"

#include <algorithm>

#include "voice/PhraseTemplate.h"

namespace atc {

std::string fillSlots(const std::string& text, const ParamValues& values) {
  std::string r;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const auto end = text[i] == '{' ? text.find('}', i) : std::string::npos;
    if (end == std::string::npos) {
      r += text[i];
      continue;
    }
    const auto v = values.find(text.substr(i + 1, end - i - 1));
    if (v != values.end()) r += v->second;
    i = end;
  }
  return r;
}

SequenceEngine::SequenceEngine(std::shared_ptr<const Exercise> exercise, std::uint64_t seed, EngineConfig cfg, std::optional<ParamValues> values)
    : exercise_(std::move(exercise)), cfg_(cfg), rng_(seed ^ 0x9E3779B97F4A7C15ull) {
  // loadExercise has checked that a combination exists.
  if (values) ctx_.params = std::move(*values);
  else generateParams(exercise_->scenario, exercise_->airfield, seed, ctx_.params);
}

SequenceEngine::Outputs SequenceEngine::handle(Ns t, const EngineInput& input) {
  Outputs o;
  std::visit([&](const auto& x) { on(t, x, o); }, input);
  settle(t, o);
  return o;
}

void SequenceEngine::on(Ns t, const in::Started&, Outputs& o) {
  if (status_ != Status::Ready) return;
  status_ = Status::Running;
  if (exercise_->scenario.maxDuration > 0) setTimer(in::Timer::ExerciseLimit, exercise_->scenario.maxDuration, t, o);
  enter(exercise_->scenario.initialStep, o);
  if (!step().onEnter) return;
  first_ = First::Waiting;
  firstReply_ = step().onEnter->reply;
  onEnterPending_ = false;
  setTimer(in::Timer::OnEnter, step().onEnter->delay, t, o);
}

void SequenceEngine::on(Ns, const in::TxOpened& x, Outputs&) {
  openTx_[x.tx] = {};
  lastTx_ = std::max(lastTx_, x.tx);
  if (running() && (first_ == First::Waiting || first_ == First::Requested)) early_.insert(x.tx);
}

void SequenceEngine::on(Ns, const in::TxClosed& x, Outputs& o) {
  const auto it = openTx_.find(x.tx);
  if (it == openTx_.end()) return;
  it->second = {true, x.end, x.report};
  if (running() && early_.count(x.tx)) o.push_back(out::AtcBeforePilot{x.tx});
}

void SequenceEngine::on(Ns, const in::TxCancelled& x, Outputs&) {
  openTx_.erase(x.tx);
  early_.erase(x.tx);
  std::erase_if(cut_, [&](const auto& c) { return c.second < x.tx; });  // nothing was said: it is said again
}

void SequenceEngine::on(Ns, const in::PressNotOpened&, Outputs&) {}  // in the log for the result only

void SequenceEngine::on(Ns t, const in::Recognised& x, Outputs& o) {
  const std::uint64_t tx = x.instruction.tx;
  const auto it = openTx_.find(tx);
  // D-325: a new instruction was given, so what it cut is not said again (the transmission controller does the same).
  if (it != openTx_.end())
    std::erase_if(cut_, [&](const auto& c) {
      if (c.second >= tx) return false;
      outstanding_.erase(c.first);
      return true;
    });
  const bool early = early_.erase(tx) > 0;
  if (!running() || it == openTx_.end() || step().final) {
    if (it != openTx_.end()) openTx_.erase(it);
    o.push_back(out::NoReply{tx});
    return;
  }
  const TxFacts facts = it->second;
  openTx_.erase(it);
  const ScenarioStep& st = step();
  const auto& rc = exercise_->scenario.readbackCheck;
  // D-430: a challenge of a readback is judged as if the step expected it, whatever the step is.
  const bool challenge = rc && !x.instruction.intent.empty() && x.instruction.intent == rc->challenge.intent;
  Verdict v;
  if (challenge) {
    ScenarioStep cs = st;
    cs.expects = {rc->challenge};
    v = validate(cs, ctx_, x.instruction, facts, x.sttFault, cfg_.rules);
  } else {
    v = validate(st, ctx_, x.instruction, facts, x.sttFault, cfg_.rules);
  }
  ctx_.history.push_back({t, false, tx, st.id, x.instruction.rawText, v.outcome});
  // The trainee is there: the pilot's calls start again from the beginning.
  pilotCalls_ = 0;
  callsExhausted_ = false;
  if (challenge && v.outcome == Outcome::Correct) return answerChallenge(tx, o);
  // An instruction that was understood, given while a challenge was due: the chance is gone.
  // Unclear speech ("say again") does not use it up (assistant, D-431).
  if (due_ && !challenge && v.outcome != Outcome::Ambiguous && v.outcome != Outcome::Unintelligible) {
    o.push_back(out::ReadbackCheck{due_->step, tx, due_->utterance, out::ReadbackResult::Missed});
    due_.reset();
  }

  out::Judged j;
  j.step = st.id;
  j.tx = tx;
  j.outcome = v.outcome;
  j.problems = v.problems;
  j.advanced = v.outcome == Outcome::Correct;
  j.suspectedStt = v.suspectedStt;
  o.push_back(std::move(j));

  if (v.outcome == Outcome::Correct) {
    Clearance c{st.id, x.instruction.intent, {}};
    for (const auto& [name, f] : x.instruction.fields) c.fields[name] = f.value;
    ctx_.clearances.push_back(std::move(c));
  }
  const auto r = st.responses.find(v.outcome);
  if (r != st.responses.end() && r->second.say) {
    const StepResponse& resp = r->second;
    std::string reply = resp.reply;
    bool withApplied = false;
    for (const auto& [field, with] : resp.withField)
      if (const auto f = x.instruction.fields.find(field); f != x.instruction.fields.end() && !f->second.value.empty()) {
        reply = with;
        withApplied = true;
      }
    // D-430: which readback the pilot gives. The RNG is used only by a step that has a choice, so
    // the replies of scenarios without one are as before.
    bool incomplete = false;
    if (v.outcome == Outcome::Correct && !withApplied) {
      if (!resp.alternates.empty()) {
        const std::size_t k = static_cast<std::size_t>(rng_() % (resp.alternates.size() + 1));
        if (k > 0) reply = resp.alternates[k - 1];
      } else if (!resp.incompleteReply.empty() && rc && rc->incompleteRate > 0) {
        const double u = static_cast<double>(rng_() >> 11) * 0x1.0p-53;  // [0, 1)
        if (u < rc->incompleteRate) {
          reply = resp.incompleteReply;
          incomplete = true;
        }
      }
    }
    const std::uint64_t said = say(reply, v.outcome, tx, false, o).id;
    if (v.outcome == Outcome::Correct) {
      if (incomplete)
        due_ = Readback{st.id, resp.reply, said};
      else
        lastReadback_ = Readback{st.id, withApplied ? reply : resp.reply, said};
    }
  } else {
    o.push_back(out::NoReply{tx});
  }
  if (!early && (v.outcome == Outcome::Ambiguous || v.outcome == Outcome::Unintelligible))
    if (++sayAgain_ == st.maxSayAgain)
      o.push_back(out::InstructorAlert{st.id + ": not understood " + std::to_string(sayAgain_) + " times"});
  if (!challenge) takeBranch(v.outcome, o);  // a failed challenge takes no branch of the step
}

// D-430: the pilot gives the full readback. Without an incomplete one due, the challenge was
// unnecessary (a warning only) and the latest readback is said again.
void SequenceEngine::answerChallenge(std::uint64_t tx, Outputs& o) {
  if (due_) {
    const Readback d = *due_;
    due_.reset();
    o.push_back(out::ReadbackCheck{d.step, tx, d.utterance, out::ReadbackResult::Caught});
    lastReadback_ = Readback{d.step, d.fullReply, say(d.fullReply, Outcome::Correct, tx, false, o).id};
    return;
  }
  if (lastReadback_) {
    o.push_back(out::ReadbackCheck{lastReadback_->step, tx, lastReadback_->utterance, out::ReadbackResult::Unnecessary});
    lastReadback_->utterance = say(lastReadback_->fullReply, Outcome::Correct, tx, false, o).id;
    return;
  }
  o.push_back(out::ReadbackCheck{step().id, tx, 0, out::ReadbackResult::Unnecessary});
  o.push_back(out::NoReply{tx});
}

void SequenceEngine::on(Ns t, const in::PilotStarted& x, Outputs&) {
  if (x.utterance == firstCall_ && first_ == First::Requested) first_ = First::Begun;
  cut_.erase(x.utterance);  // said again
  const auto it = outstanding_.find(x.utterance);
  if (it != outstanding_.end() && running()) ctx_.history.push_back({t, true, x.utterance, step().id, it->second.text, std::nullopt});
}

void SequenceEngine::on(Ns, const in::PilotEnded& x, Outputs&) {
  // The transmission controller keeps the reply in these two cases. One that is not owed is kept only
  // until the next instruction (D-325).
  if (x.end == in::PilotEnd::PressCameFirst) return;
  if (x.end == in::PilotEnd::Interrupted && exercise_->scenario.repeatInterruptedReply) {
    if (const auto it = outstanding_.find(x.utterance); it != outstanding_.end() && !it->second.owed) cut_[x.utterance] = lastTx_;
    return;
  }
  outstanding_.erase(x.utterance);
}

void SequenceEngine::on(Ns, const in::TimerFired& x, Outputs& o) {
  TimerState& s = timer(x.timer);
  if (!running() || !s.active || s.paused || s.generation != x.generation) return;
  s.active = false;
  switch (x.timer) {
    case in::Timer::StepTimeout:
      stepTimedOut(o);
      break;
    case in::Timer::OnEnter:
      if (first_ == First::Waiting) {
        first_ = First::Requested;
        PilotUtterance& u = say(firstReply_, Outcome::Correct, 0, true, o);
        u.first = true;
        firstCall_ = u.id;
        break;
      }
      onEnterPending_ = false;
      if (step().onEnter) say(step().onEnter->reply, Outcome::Correct, 0, true, o);
      break;
    case in::Timer::ExerciseLimit:
      finish(out::EndReason::TimeLimit, o);
      break;
  }
}

void SequenceEngine::on(Ns t, const in::PttPresence& x, Outputs& o) {
  if (x.present == pttPresent_) return;
  pttPresent_ = x.present;
  for (const auto k : {in::Timer::StepTimeout, in::Timer::OnEnter, in::Timer::ExerciseLimit}) {
    TimerState& s = timer(k);
    if (!s.active) continue;
    if (!x.present) {
      s.remaining = std::max<Ns>(0, s.deadline - t);
      s.paused = true;
      o.push_back(out::TimerCancelled{k});
    } else {
      setTimer(k, s.remaining, t, o);
    }
  }
}

void SequenceEngine::on(Ns, const in::FallbackPttUsed&, Outputs&) {}  // in the log for the result only

void SequenceEngine::on(Ns, const in::InstructorStop&, Outputs& o) {
  if (running()) finish(out::EndReason::InstructorStop, o);
}

void SequenceEngine::on(Ns, const in::ProgramClosed&, Outputs& o) {
  if (running()) finish(out::EndReason::ProgramClosed, o);
}

// The pilot's calls start again; the step is unchanged.
void SequenceEngine::on(Ns, const in::InstructorContinue&, Outputs&) {
  if (!running() || !callsExhausted_) return;
  callsExhausted_ = false;
  pilotCalls_ = 0;
}

void SequenceEngine::setTimer(in::Timer k, Ns delay, Ns t, Outputs& o) {
  TimerState& s = timer(k);
  s.active = true;
  s.remaining = delay;
  s.deadline = t + delay;
  s.paused = !pttPresent_;
  if (s.paused) return;  // starts when PTT is back
  o.push_back(out::TimerSet{k, ++s.generation, delay});
}

void SequenceEngine::cancelTimer(in::Timer k, Outputs& o) {
  TimerState& s = timer(k);
  if (!s.active) return;
  if (!s.paused) o.push_back(out::TimerCancelled{k});
  s.active = s.paused = false;
}

void SequenceEngine::enter(const std::string& id, Outputs& o) {
  const auto& steps = exercise_->scenario.steps;
  for (std::size_t i = 0; i < steps.size(); ++i)
    if (steps[i].id == id) step_ = i;
  cancelTimer(in::Timer::StepTimeout, o);
  if (first_ != First::Waiting) cancelTimer(in::Timer::OnEnter, o);
  onEnterPending_ = step().onEnter.has_value();
  callsExhausted_ = false;
  pilotCalls_ = sayAgain_ = 0;
  o.push_back(out::StepEntered{id});
}

void SequenceEngine::takeBranch(Outcome outcome, Outputs& o) {
  for (const auto& tr : step().transitions) {
    if (tr.on != outcome) continue;
    enter(tr.to, o);
    return;
  }
}

PilotUtterance& SequenceEngine::say(const std::string& replyId, Outcome cause, std::uint64_t tx, bool unprompted, Outputs& o) {
  PilotUtterance u;
  u.id = nextUtterance_++;
  u.replyId = replyId;
  const auto it = exercise_->replies.find(replyId);
  if (it != exercise_->replies.end()) {
    std::string error;  // cannot occur once checkCoverage has passed
    u.text = fillSlots(it->second.text, ctx_.params);
    u.clipIds = clipSequence(it->second.text, exercise_->scenario, ctx_.params, error);
  }
  if (!unprompted) {
    const auto span = static_cast<std::uint64_t>(std::max<Ns>(0, cfg_.replyDelayMax - cfg_.replyDelayMin) / ms(1)) + 1;
    u.delay = cfg_.replyDelayMin + ms(static_cast<std::int64_t>(rng_() % span));
  }
  u.unprompted = unprompted;
  u.cause = cause;
  u.tx = tx;
  u.owed = !unprompted && cause == Outcome::Correct;
  outstanding_[u.id] = {u.text, u.owed};
  o.push_back(out::PilotRequested{std::move(u)});
  return std::get<out::PilotRequested>(o.back()).utterance;
}

void SequenceEngine::stepTimedOut(Outputs& o) {
  const ScenarioStep& st = step();
  out::Judged j;
  j.step = st.id;
  j.outcome = Outcome::Timeout;
  j.waited = st.timeout;  // time without PTT does not count: the timer was paused
  o.push_back(std::move(j));
  if (pilotCalls_ < st.maxPilotCalls) {
    ++pilotCalls_;
    const auto r = st.responses.find(Outcome::Timeout);
    if (r != st.responses.end() && r->second.say) say(r->second.reply, Outcome::Timeout, 0, true, o);
    return;
  }
  callsExhausted_ = true;
  o.push_back(out::InstructorAlert{st.id + ": no ATC call after " + std::to_string(pilotCalls_) + " pilot calls", true});
  // Never moves on by itself (D-424): the instructor continues or ends.
}

void SequenceEngine::finish(out::EndReason why, Outputs& o) {
  for (const auto k : {in::Timer::StepTimeout, in::Timer::OnEnter, in::Timer::ExerciseLimit}) cancelTimer(k, o);
  if (due_) {  // D-430: ended before the trainee could speak again
    o.push_back(out::ReadbackCheck{due_->step, 0, due_->utterance, out::ReadbackResult::Open});
    due_.reset();
  }
  status_ = Status::Ended;
  o.push_back(out::Ended{why});
}

// Brings timers and the end of the exercise in line with the state, after every input.
void SequenceEngine::settle(Ns t, Outputs& o) {
  if (!running()) return;
  const ScenarioStep& st = step();
  const bool firstDue = first_ == First::Waiting;
  if (st.final && outstanding_.empty() && !onEnterPending_ && !firstDue) return finish(out::EndReason::Completed, o);
  const bool quiet = outstanding_.empty() && openTx_.empty();
  // The pilot's own call waits for a quiet channel; the trainee may be quicker.
  if (firstDue) {
    // Its timer runs on.
  } else if (onEnterPending_ && quiet) {
    if (!timer(in::Timer::OnEnter).active) setTimer(in::Timer::OnEnter, st.onEnter->delay, t, o);
  } else {
    cancelTimer(in::Timer::OnEnter, o);
  }
  if (quiet && !onEnterPending_ && !firstDue && !st.final && st.timeout > 0 && !callsExhausted_) {
    if (!timer(in::Timer::StepTimeout).active) setTimer(in::Timer::StepTimeout, st.timeout, t, o);
  } else {
    cancelTimer(in::Timer::StepTimeout, o);
  }
}

SequenceEngine::Outputs LoggedEngine::apply(Ns t, const EngineInput& input) {
  log_.append(EngineEvent{++seq_, t, input});
  auto o = engine_.handle(t, input);
  for (const auto& x : o) log_.append(EngineEvent{++seq_, t, x});
  return o;
}

}  // namespace atc
