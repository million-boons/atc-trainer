#include "app/ExerciseView.h"

#include <QLocale>
#include <algorithm>
#include <utility>

#include "app/LabController.h"

using namespace atc;

namespace {
using L = LabController;
QString tr(const char* s) { return ExerciseController::tr(s); }
QString q(const std::string& s) { return QString::fromStdString(s); }
QString q(std::string_view s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }
QString seconds(Ns n) { return tr("%1 s").arg(double(n) / 1e9, 0, 'f', 1); }
QString clock(Ns n) {
  const auto s = std::max<Ns>(0, n) / ms(1000);
  return QString("%1:%2").arg(s / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}
QString list(const std::vector<std::string>& v) {
  QStringList l;
  for (const auto& s : v) l << q(s);
  return l.join(", ");
}
QString rule(PressRule r) {
  return r == PressRule::Interrupt ? tr("interrupt") : r == PressRule::Block ? tr("block") : tr("queue");
}
QString named(const std::string& name, const QString& value) {
  if (name == "callsign") return value;
  if (name == "runway") return tr("RWY %1").arg(value);
  if (name == "taxiway") return tr("TWY %1").arg(value);
  return QString("%1 %2").arg(q(name).replace('_', ' ').toUpper(), value);
}
// An uncertain value is shown as heard, never corrected.
QString shown(const std::string& name, const ParsedField& f) {
  return named(name, f.uncertain ? QString("\"%1\" ?").arg(q(f.heard)) : q(f.value));
}
QString value(const ParamValues& p, const char* key) {
  const auto it = p.find(key);
  return it == p.end() ? QString() : q(it->second);
}
QString median(QList<double> v) {
  if (v.isEmpty()) return QStringLiteral("-");
  std::sort(v.begin(), v.end());
  const int m = int(v.size()) / 2;
  return tr("%1 s").arg(v.size() % 2 ? v[m] : (v[m - 1] + v[m]) / 2, 0, 'f', 1);
}
}  // namespace

QVariantMap ExerciseView::card(const Exercise& e, std::uint64_t seed) {
  ParamValues p;
  generateParams(e.scenario, e.airfield, seed, p);
  QStringList names;
  for (const auto& s : e.scenario.steps)
    if (!s.final) names << q(s.label);
  return {{"name", q(e.scenario.name)}, {"summary", names.join(", ")}, {"callsign", value(p, "callsign")},
          {"type", value(p, "aircraftType")}, {"runway", value(p, "runway")}, {"taxiway", value(p, "taxiway")},
          {"rule", rule(e.scenario.pressDuringPilot)}, {"steps", int(names.size())}, {"sop", QString()},
          {"placeholder", e.placeholder}};
}

QString ExerciseView::time(Ns t) const {
  return at_.addMSecs(t0_ ? (t - *t0_) / ms(1) : 0).toString("HH:mm:ss");
}

int ExerciseView::stepIndex(const std::string& id) const {
  int n = 0;
  for (const auto& s : s_.exercise->scenario.steps) {
    if (s.final) continue;
    if (s.id == id) return n;
    ++n;
  }
  return -1;
}

QString ExerciseView::label(const std::string& id) const {
  const auto* s = s_.exercise->scenario.step(id);
  return s ? q(s->label) : q(id);
}

ExerciseContext ExerciseView::context() const {
  const QVariantMap v = card(*s_.exercise, s_.seed);
  return {.scenario = v["name"].toString(), .callsign = v["callsign"].toString(), .runway = v["runway"].toString(),
          .taxiway = v["taxiway"].toString(), .type = v["type"].toString(), .rule = v["rule"].toString(),
          .model = s_.model, .trainee = s_.trainee, .placeholder = s_.exercise->placeholder};
}

// D-125; after system faults in a row D-317.
void ExerciseView::choice() {
  if (choiceCalls_ > 0) c_->setChoice(tr("No ATC call"), tr("%1: %2 pilot calls without an ATC call.").arg(label(step_)).arg(choiceCalls_));
  else c_->setChoice(tr("Speech recognition failed"), tr("%1: no result %2 times in a row. Not the trainee's error.").arg(label(step_)).arg(faultRun_));
}

void ExerciseView::steps() {
  QList<ExerciseStep> l;
  for (const auto& s : s_.exercise->scenario.steps)
    if (!s.final) l.append({q(s.label), attempts_.value(int(l.size()))});
  c_->setSteps(std::move(l), done_);
}

void ExerciseView::waiting(std::optional<Ns> since) {
  waitSince_ = since;
  if (!since) c_->setWaiting({});
}

void ExerciseView::tick(Ns now) {
  if (waitSince_) c_->setWaiting(clock(now - *waitSince_));
}

void ExerciseView::prepare(Setup s) {
  s_ = std::move(s);
  t0_.reset();
  waitSince_.reset();
  step_.clear();
  done_ = calls_ = faults_ = 0;
  faultShown_ = false;
  faultTx_ = 0;
  faultRun_ = choiceCalls_ = 0;
  judged_ = 0;
  attempts_.clear();
  clearances_.clear();
  pressed_.clear();
  utterances_.clear();
  row_.clear();
  sayAgain_.clear();
  heard_ = {};
  delays_.clear();
  timeline_.clear();
  result_.clear();
  for (const auto& st : s_.exercise->scenario.steps)
    if (!st.final) attempts_ << 0;
  c_->begin(context(), false, s_.consent);
  steps();
  c_->setAlert(L::Ok, tr("Exercise not started"));
  c_->setPhase(ExerciseController::Starting);
}

void ExerciseView::refused(const QString& why) {
  c_->setNotice(L::Fault, tr("Exercise not started: %1").arg(why));
  c_->setAlert(L::Fault, tr("Exercise not started: %1").arg(why));
}

void ExerciseView::started(bool assessed, QDateTime at) {
  at_ = std::move(at);
  c_->begin(context(), assessed, s_.consent);
  steps();
  c_->setAlert(L::Ok, tr("Exercise running"));
  c_->setPhase(ExerciseController::Running);
}

void ExerciseView::input(Ns t, const EngineInput& i) {
  if (!s_.exercise) return;
  if (!std::holds_alternative<in::Recognised>(i)) faultShown_ = false;
  std::visit([&](const auto& x) { on(t, x); }, i);
}

void ExerciseView::output(Ns t, const EngineOutput& o) {
  if (s_.exercise) std::visit([&](const auto& x) { on(t, x); }, o);
}

void ExerciseView::on(Ns t, const in::Started&) { t0_ = t; }

void ExerciseView::on(Ns, const in::TxOpened& x) {
  pressed_[x.tx] = x.press;
  if (c_->choice()) return;  // the instructor is still asked; the transmission may end it
  c_->setNotice(L::None, {});
  c_->setAlert(L::Ok, tr("Text appears after release"));
}

void ExerciseView::on(Ns, const in::TxCancelled&) {
  if (!c_->choice()) c_->setAlert(L::Ok, tr("Exercise running"));
}

void ExerciseView::on(Ns t, const in::Recognised& x) {
  c_->clearChoice();
  const ParsedInstruction& p = x.instruction;
  const auto it = pressed_.find(p.tx);
  const QString at = time(it == pressed_.end() ? t : it->second);
  pressed_.erase(p.tx);
  heard_ = {};
  heard_.text = p.rawText.empty() ? tr("(nothing recognised)") : q(p.rawText).trimmed();
  QStringList values;
  for (const auto& [name, f] : p.fields) {
    heard_.chips.append(QVariantMap{{"value", shown(name, f)}, {"uncertain", f.uncertain}});
    if (name != "callsign") values << shown(name, f);
  }
  heard_.clearance = values.join(", ");
  c_->addAtc(at, heard_.text, q(p.intent).replace('_', ' '), heard_.chips, q(toString(p.cls)), p.cls == RecognitionClass::Recognized,
             x.releaseToText > 0 ? seconds(x.releaseToText) : QString());
  if (x.releaseToText > 0) delays_ << double(x.releaseToText) / 1e9;
  row_[p.tx] = int(timeline_.size());
  timeline_.append(QVariantList{at, tr("ATC"), heard_.text, QString(), 0});
  c_->setAlert(L::Ok, tr("Exercise running"));
  if (x.sttFault.empty()) {
    faultRun_ = 0;
    return;
  }
  faultTx_ = p.tx;
  ++faultRun_;
  if (!std::exchange(faultShown_, false)) ++faults_;  // the runtime may have reported it just before
  c_->setNotice(L::Fault, tr("Speech recognition failed: %1. Not your error.").arg(q(x.sttFault)));
  c_->setAlert(L::Fault, tr("Fault logged: speech recognition"));
}

void ExerciseView::on(Ns t, const in::PilotStarted& x) {
  const auto it = utterances_.find(x.utterance);
  if (it == utterances_.end()) return;
  c_->addPilot(time(t), it->second);
  timeline_.append(QVariantList{time(t), tr("PILOT"), it->second, QString(), 0});
}

void ExerciseView::on(Ns, const in::PilotEnded& x) {
  if (x.end == in::PilotEnd::Interrupted) {
    c_->setNotice(L::Caution, s_.exercise->scenario.repeatInterruptedReply ? tr("You keyed over the pilot. The reply will be repeated.")
                                                                           : tr("You keyed over the pilot. The reply is not repeated."));
  } else if (x.end == in::PilotEnd::TimedOut) {
    c_->setNotice(L::Fault, tr("Pilot voice stopped: playback did not finish. The reply is shown as text only."));
    c_->setAlert(L::Fault, tr("Fault logged: pilot speech watchdog"));
  }
}

void ExerciseView::on(Ns, const in::TimerFired& x) {
  if (x.timer == in::Timer::StepTimeout) waiting({});
}

void ExerciseView::on(Ns, const in::PttPresence& x) {
  if (!x.present) {
    c_->setAlert(L::Fault, tr("PTT device lost: exercise paused"));
    c_->setPhase(ExerciseController::Paused);
  } else if (c_->phase() == ExerciseController::Paused) {
    c_->setPhase(ExerciseController::Running);
    if (c_->choice()) choice();
    else c_->setAlert(L::Ok, tr("Exercise running"));
  }
}

void ExerciseView::on(Ns, const in::InstructorContinue&) {
  calls_ = 0;
  c_->clearChoice();
}

void ExerciseView::on(Ns, const out::StepEntered& x) {
  step_ = x.step;
  faultRun_ = 0;
  calls_ = 0;
  const int i = stepIndex(x.step);
  done_ = i < 0 ? int(attempts_.size()) : i;
  steps();
}

void ExerciseView::on(Ns t, const out::Judged& x) {
  if (x.outcome == Outcome::Timeout) {
    c_->addNote(L::Caution, tr("No ATC call for %1 s").arg(x.waited / ms(1000)));
    timeline_.append(QVariantList{time(t), tr("ATC"), tr("No ATC call for %1 s").arg(x.waited / ms(1000)), tr("MISSED RESPONSE"), int(L::Caution)});
    return;
  }
  judged_ = x.tx;
  calls_ = 0;
  if (x.tx && x.tx == faultTx_) {  // the system failed: nothing about the trainee (D-317)
    if (const auto r = row_.find(x.tx); r != row_.end()) {
      timeline_[r->second][3] = tr("SYSTEM FAULT, not counted");
      timeline_[r->second][4] = int(L::Fault);
    }
    return;
  }
  const int i = stepIndex(x.step);
  if (i >= 0) ++attempts_[i];
  const bool good = x.outcome == Outcome::Correct;
  const QString word = q(toString(x.outcome)).replace('_', ' '), why = list(x.problems);
  if (const auto r = row_.find(x.tx); r != row_.end()) {
    timeline_[r->second][3] = x.suspectedStt ? tr("%1, not counted").arg(word) : word;
    timeline_[r->second][4] = int(good ? L::Ok : L::Caution);
  }
  if (good) {
    clearances_ << (heard_.clearance.isEmpty() ? label(x.step) : tr("%1: %2").arg(label(x.step), heard_.clearance));
    c_->setClearances(clearances_);
    c_->setNotice(L::None, {});
  } else if (x.suspectedStt) {
    if (c_->noticeLevel() != L::Fault)
      c_->setNotice(L::Caution, tr("Not clear: %1. Not counted as your error.").arg(why.isEmpty() ? word.toLower() : why), true);
    if (c_->alertLevel() != L::Fault) c_->setAlert(L::Caution, tr("Suspected speech recognition error"), true);
  } else {
    c_->setNotice(L::Caution, why.isEmpty() ? tr("Step not advanced: %1.").arg(word) : tr("Step not advanced: %1 (%2).").arg(word, why), true);
    c_->setAlert(L::Caution, tr("%1: attempt %2 %3").arg(label(x.step)).arg(i < 0 ? 0 : attempts_[i]).arg(word), true);
  }
  steps();
}

void ExerciseView::on(Ns, const out::PilotRequested& x) {
  utterances_[x.utterance.id] = q(x.utterance.text);
  if (x.utterance.unprompted && x.utterance.cause == Outcome::Timeout) {
    const auto* s = s_.exercise->scenario.step(step_);
    ++calls_;
    c_->setNotice(L::Caution, tr("The pilot is calling again (%1 of %2).").arg(calls_).arg(s ? s->maxPilotCalls : calls_));
  } else if (!x.utterance.unprompted && x.utterance.tx != faultTx_ && (x.utterance.cause == Outcome::Ambiguous || x.utterance.cause == Outcome::Unintelligible)) {
    ++sayAgain_[step_];
  }
}

void ExerciseView::on(Ns, const out::NoReply& x) {
  if (x.tx && std::exchange(judged_, 0) == x.tx) c_->addNote(L::Caution, tr("No reply from the pilot"));
}

void ExerciseView::on(Ns t, const out::TimerSet& x) {
  if (x.timer != in::Timer::StepTimeout) return;
  const auto* s = s_.exercise->scenario.step(step_);
  waiting(t - (s ? std::max<Ns>(0, s->timeout - x.delay) : 0));
  tick(t);
}

void ExerciseView::on(Ns, const out::TimerCancelled& x) {
  if (x.timer == in::Timer::StepTimeout) waiting({});
}

void ExerciseView::on(Ns, const out::InstructorAlert& x) {
  if (x.choice) {
    choiceCalls_ = calls_;
    return choice();
  }
  const auto* s = s_.exercise->scenario.step(step_);
  c_->setNotice(L::Caution, tr("Instructor: speech was not understood %1 times in this step.").arg(s ? s->maxSayAgain : 0));
  c_->setAlert(L::Caution, tr("Instructor attention: say-again limit"));
}

void ExerciseView::fault(const QString& why) {
  ++faults_;
  faultShown_ = true;
  c_->addNote(L::Fault, why);
  c_->setNotice(L::Fault, why);
  c_->setAlert(L::Fault, tr("Fault: %1").arg(why));
}

void ExerciseView::ended(const ExerciseResult& r, QDateTime at) {
  waiting({});
  c_->clearChoice();
  const auto row = [](const QString& name, const QString& v) { return QVariantMap{{"name", name}, {"value", v}}; };
  const auto n = [](int i) { return QString::number(i); };

  QVariantList stepRows;
  int done = 0, missed = 0;
  for (const auto& s : r.steps) {
    QStringList errors;
    for (const auto& [o, count] : s.errors) {
      const QString word = q(toString(o)).replace('_', ' ');
      errors << (count > 1 ? tr("%1 x%2").arg(word).arg(count) : word);
    }
    // Missed responses are a metric of their own, never an incorrect instruction (D-127, D-424).
    Ns silence = 0;
    for (const auto& m : s.missed) silence += m.waited;
    missed += int(s.missed.size());
    done += s.completed;
    const auto again = sayAgain_.find(s.id);
    stepRows << QVariantMap{{"name", tr("%1 %2").arg(stepRows.size() + 1).arg(q(s.label))},
                            {"level", int(s.completed ? L::Ok : s.reached ? L::Caution : L::None)},
                            {"outcome", s.completed ? tr("Correct") : s.reached ? tr("Not completed") : tr("Not reached")},
                            {"attempts", s.attempts},
                            {"errors", !s.reached ? QStringLiteral("-") : errors.isEmpty() ? tr("none") : errors.join(", ")},
                            {"missed", s.missed.empty() ? QStringLiteral("0") : tr("%1, %2").arg(s.missed.size()).arg(seconds(silence))},
                            // From the first call left unanswered. Never answered: at least the silence.
                            {"response", s.responseTime ? seconds(*s.responseTime) : silence > 0 ? tr("> %1").arg(seconds(silence)) : QStringLiteral("-")},
                            {"sayAgain", n(again == sayAgain_.end() ? 0 : again->second)}};
  }
  QStringList suspected;
  for (const auto& s : r.suspectedStt) {
    const QString why = list(s.problems);
    suspected << tr("%1: heard \"%2\", classed %3%4.").arg(label(s.step), q(s.heard), q(toString(s.outcome)).replace('_', ' '), why.isEmpty() ? QString() : tr(" (%1)").arg(why));
  }
  const QVariantList ptt{row(tr("Transmissions"), n(r.ptt.transmissions)), row(tr("Spoke before keying"), n(r.ptt.spokeBeforeKeying)),
                         row(tr("Released early"), n(r.ptt.releasedEarly)), row(tr("Short taps"), n(r.ptt.shortTaps)),
                         row(tr("No speech"), n(r.ptt.noSpeech)), row(tr("Keyed over the pilot"), n(r.ptt.keyedOverPilot)),
                         row(tr("Key to speech, median"), r.ptt.keyToSpeechMs ? tr("%1 s").arg(*r.ptt.keyToSpeechMs / 1000.0, 0, 'f', 2) : QStringLiteral("-")),
                         row(tr("Fallback PTT used"), r.fallbackPttUsed() ? tr("yes, %1").arg(r.ptt.fallback) : tr("no"))};
  const QVariantList speech{row(tr("Speech model"), s_.model), row(tr("Release to text, median"), median(delays_)),
                            row(tr("Faults during exercise"), n(r.pilotFaults + faults_))};
  QString title;
  int level = L::Caution;
  if (!r.end) {
    title = tr("LOG INCOMPLETE");
    level = L::Fault;
  } else if (*r.end == out::EndReason::Completed) {
    title = tr("COMPLETED");
    level = L::Ok;
  } else {
    title = *r.end == out::EndReason::InstructorStop ? tr("STOPPED BY INSTRUCTOR") : tr("TIME LIMIT");
  }
  const QVariantMap ctx = c_->context();
  result_ = {{"title", title}, {"level", level},
             {"sub", tr("%1   %2   %3   %4 to %5").arg(c_->who(), ctx["scenario"].toString(), ctx["callsign"].toString(), QLocale::c().toString(at_, "d MMM HH:mm"), at.toString("HH:mm"))},
             {"stepsDone", tr("%1 of %2").arg(done).arg(r.steps.size())}, {"missed", n(missed)}, {"duration", clock(r.duration)}, {"steps", stepRows}, {"ptt", ptt},
             {"suspectedLevel", int(suspected.isEmpty() ? L::None : L::Caution)},
             {"suspectedTitle", suspected.isEmpty() ? tr("None") : tr("%1, not counted against the trainee").arg(suspected.size())},
             {"suspectedText", suspected.join("\n")}, {"speech", speech}};

  if (r.end && *r.end == out::EndReason::Completed) {
    c_->setNotice(L::Ok, tr("Exercise complete."));
    c_->setAlert(L::Ok, tr("Exercise complete"));
    c_->setPhase(ExerciseController::Complete);
  } else {
    showResult();
  }
}

void ExerciseView::showResult() {
  c_->setNotice(L::None, {});
  c_->setAlert(L::Ok, tr("Exercise finished"));
  c_->setResult(result_, timeline_);
  c_->setPhase(ExerciseController::Result);
  emit c_->screenRequested("ass");
}
