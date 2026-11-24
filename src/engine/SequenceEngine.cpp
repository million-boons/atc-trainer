#include "engine/SequenceEngine.h"

#include <algorithm>

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

SequenceEngine::SequenceEngine(std::shared_ptr<const Exercise> exercise, std::uint64_t seed, EngineConfig cfg)
    : exercise_(std::move(exercise)), cfg_(cfg), rng_(seed ^ 0x9E3779B97F4A7C15ull) {
  // loadExercise has checked that a combination exists.
  generateParams(exercise_->scenario, exercise_->airfield, seed, ctx_.params);
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
}

void SequenceEngine::on(Ns, const in::TxOpened& x, Outputs&) { openTx_[x.tx] = {}; }

void SequenceEngine::on(Ns, const in::TxClosed& x, Outputs&) {
  const auto it = openTx_.find(x.tx);
  if (it != openTx_.end()) it->second = {true, x.end, x.report};
}

void SequenceEngine::on(Ns, const in::TxCancelled& x, Outputs&) { openTx_.erase(x.tx); }

void SequenceEngine::on(Ns, const in::PressNotOpened&, Outputs&) {}  // in the log for the result only

void SequenceEngine::on(Ns t, const in::Recognised& x, Outputs& o) {
  const std::uint64_t tx = x.instruction.tx;
  const auto it = openTx_.find(tx);
  if (!running() || it == openTx_.end() || step().final) {
    if (it != openTx_.end()) openTx_.erase(it);
    o.push_back(out::NoReply{tx});
    return;
  }
  const TxFacts facts = it->second;
  openTx_.erase(it);
  const ScenarioStep& st = step();
  const Verdict v = validate(st, ctx_, x.instruction, facts, x.sttFault, cfg_.rules);
  ctx_.history.push_back({t, false, tx, st.id, x.instruction.rawText, v.outcome});
  // The trainee is there: the pilot's calls start again from the beginning.
  pilotCalls_ = 0;
  callsExhausted_ = false;

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
  if (r != st.responses.end() && r->second.say)
    say(r->second.reply, v.outcome, tx, false, o);
  else
    o.push_back(out::NoReply{tx});
  if (v.outcome == Outcome::Ambiguous || v.outcome == Outcome::Unintelligible)
    if (++sayAgain_ == st.maxSayAgain)
      o.push_back(out::InstructorAlert{st.id + ": not understood " + std::to_string(sayAgain_) + " times"});
  takeBranch(v.outcome, o);
}

void SequenceEngine::on(Ns t, const in::PilotStarted& x, Outputs&) {
  const auto it = outstanding_.find(x.utterance);
  if (it != outstanding_.end() && running()) ctx_.history.push_back({t, true, x.utterance, step().id, it->second, std::nullopt});
}

void SequenceEngine::on(Ns, const in::PilotEnded& x, Outputs&) {
  // The transmission controller keeps the reply in these two cases and will start it again.
  if (x.end == in::PilotEnd::PressCameFirst) return;
  if (x.end == in::PilotEnd::Interrupted && exercise_->scenario.repeatInterruptedReply) return;
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

void SequenceEngine::on(Ns, const in::InstructorStop&, Outputs& o) {
  if (running()) finish(out::EndReason::InstructorStop, o);
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
  cancelTimer(in::Timer::OnEnter, o);
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

void SequenceEngine::say(const std::string& replyId, Outcome cause, std::uint64_t tx, bool unprompted, Outputs& o) {
  PilotUtterance u;
  u.id = nextUtterance_++;
  u.replyId = replyId;
  const auto it = exercise_->replies.find(replyId);
  if (it != exercise_->replies.end()) u.text = fillSlots(it->second.text, ctx_.params);
  if (!unprompted) {
    const auto span = static_cast<std::uint64_t>(std::max<Ns>(0, cfg_.replyDelayMax - cfg_.replyDelayMin) / ms(1)) + 1;
    u.delay = cfg_.replyDelayMin + ms(static_cast<std::int64_t>(rng_() % span));
  }
  u.unprompted = unprompted;
  u.cause = cause;
  u.tx = tx;
  outstanding_[u.id] = u.text;
  o.push_back(out::PilotRequested{std::move(u)});
}

void SequenceEngine::stepTimedOut(Outputs& o) {
  const ScenarioStep& st = step();
  out::Judged j;
  j.step = st.id;
  j.outcome = Outcome::Timeout;
  o.push_back(std::move(j));
  if (pilotCalls_ < st.maxPilotCalls) {
    ++pilotCalls_;
    const auto r = st.responses.find(Outcome::Timeout);
    if (r != st.responses.end() && r->second.say) say(r->second.reply, Outcome::Timeout, 0, true, o);
    return;
  }
  callsExhausted_ = true;
  o.push_back(out::InstructorAlert{st.id + ": no ATC call after " + std::to_string(pilotCalls_) + " pilot calls"});
  takeBranch(Outcome::Timeout, o);
}

void SequenceEngine::finish(out::EndReason why, Outputs& o) {
  for (const auto k : {in::Timer::StepTimeout, in::Timer::OnEnter, in::Timer::ExerciseLimit}) cancelTimer(k, o);
  status_ = Status::Ended;
  o.push_back(out::Ended{why});
}

// Brings timers and the end of the exercise in line with the state, after every input.
void SequenceEngine::settle(Ns t, Outputs& o) {
  if (!running()) return;
  const ScenarioStep& st = step();
  if (st.final && outstanding_.empty() && !onEnterPending_) return finish(out::EndReason::Completed, o);
  const bool quiet = outstanding_.empty() && openTx_.empty();
  // The pilot's own call waits for a quiet channel; the trainee may be quicker.
  if (onEnterPending_ && quiet) {
    if (!timer(in::Timer::OnEnter).active) setTimer(in::Timer::OnEnter, st.onEnter->delay, t, o);
  } else {
    cancelTimer(in::Timer::OnEnter, o);
  }
  if (quiet && !onEnterPending_ && !st.final && st.timeout > 0 && !callsExhausted_) {
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
