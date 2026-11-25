#include "app/ExerciseController.h"

#include <algorithm>
#include <utility>

#include "app/LabController.h"

ExerciseController::ExerciseController(QObject* parent)
    : QObject(parent),
      transcript_({"kind", "time", "text", "intent", "chips", "cls", "level", "delay"}, 200, this),
      timeline_({"time", "who", "text", "outcome", "level"}, 2000, this) {}

void ExerciseController::begin(ExerciseContext c, bool assessed, bool consent, const QString& preview) {
  ctx_ = std::move(c);
  assessed_ = assessed;
  consent_ = consent;
  preview_ = preview;
  phase_ = Idle;
  waiting_.clear();
  clearances_.clear();
  missed_.clear();
  steps_.clear();
  done_ = 0;
  notice_ = alert_ = {};
  choice_ = false;
  result_.clear();
  transcript_.reset();
  timeline_.reset();
  emit phaseChanged();
  emit changed();
  emit waitingChanged();
  emit resultChanged();
}

void ExerciseController::setPhase(Phase p) {
  if (phase_ == p) return;
  phase_ = p;
  emit phaseChanged();
  emit changed();  // Assessed shows more in the result
  emit resultChanged();
}

void ExerciseController::setSteps(QList<ExerciseStep> s, int done) {
  steps_ = std::move(s);
  done_ = std::clamp(done, 0, int(steps_.size()));
  emit changed();
}

void ExerciseController::setMissed(QStringList names) {
  missed_ = std::move(names);
  emit changed();
}

void ExerciseController::setClearances(QStringList c) {
  clearances_ = std::move(c);
  emit changed();
}

void ExerciseController::setWaiting(const QString& t) {
  if (waiting_ == t) return;
  waiting_ = t;
  emit waitingChanged();
}

void ExerciseController::setNotice(int level, const QString& t, bool verdict) {
  notice_ = {level, t, verdict};
  emit changed();
}

void ExerciseController::setAlert(int level, const QString& t, bool verdict) {
  alert_ = {level, t, verdict};
  emit changed();
}

void ExerciseController::setChoice(const QString& step, int pilotCalls) {
  choice_ = true;
  choiceStep_ = step;
  choiceCalls_ = pilotCalls;
  notice_ = {LabController::Caution, tr("Instructor notified")};
  alert_ = {LabController::Caution, tr("Instructor notified")};
  emit changed();
}

void ExerciseController::clearChoice() {
  if (!choice_) return;
  choice_ = false;
  notice_ = {};
  alert_ = {LabController::Ok, tr("Exercise running")};
  emit changed();
}

QString ExerciseController::choiceText() const {
  return hide() ? tr("No ATC call.") : tr("%1: %2 pilot calls without an ATC call.").arg(choiceStep_).arg(choiceCalls_);
}

int ExerciseController::alertLevel() const { return hide() && alert_.verdict ? int(LabController::Ok) : alert_.level; }

QString ExerciseController::alert() const { return hide() && alert_.verdict ? tr("Exercise running") : alert_.text; }

void ExerciseController::addAtc(const QString& time, const QString& text, const QString& intent, const QVariantList& chips,
                                const QString& cls, bool recognised, const QString& delay) {
  transcript_.prepend({int(Atc), time, text, intent, chips, cls, int(recognised ? LabController::Ok : LabController::Caution), delay});
}

void ExerciseController::addPilot(const QString& time, const QString& text) {
  transcript_.prepend({int(Pilot), time, text, QString(), QVariantList(), QString(), 0, QString()});
}

void ExerciseController::addNote(int level, const QString& text) {
  transcript_.prepend({int(Note), QString(), text, QString(), QVariantList(), QString(), level, QString()});
}

void ExerciseController::setResult(QVariantMap summary, QList<QVariantList> timeline) {
  result_ = std::move(summary);
  timeline_.reset(std::move(timeline));
  emit resultChanged();
}

QVariantMap ExerciseController::context() const {
  return {{"scenario", ctx_.scenario}, {"callsign", ctx_.callsign}, {"runway", ctx_.runway}, {"taxiway", ctx_.taxiway},
          {"type", ctx_.type}, {"rule", ctx_.rule}, {"model", ctx_.model}};
}

QString ExerciseController::assessedNote() const {
  return ctx_.placeholder ? tr("Scenario text is PLACEHOLDER. The verdict would be hidden until the debrief.")
                          : tr("The verdict is hidden until the debrief.");
}

QVariantList ExerciseController::steps() const {
  QVariantList out;
  for (int i = 0; i < int(steps_.size()); ++i) {
    const StepState s = hide() ? Hidden : i < done_ ? Done : i == done_ ? Now : Later;
    QString note = s == Done ? tr("Done") : s == Now ? tr("Now") : s == Later ? tr("Later") : QString();
    if (s != Hidden && steps_[i].attempts > 0) note = tr("%1   attempts %2").arg(note).arg(steps_[i].attempts);
    out.append(QVariantMap{{"name", tr("%1 %2").arg(i + 1).arg(steps_[i].name)}, {"state", int(s)}, {"note", note}});
  }
  return out;
}

QString ExerciseController::progressText() const {
  return hide() ? tr("progress: in the debrief") : tr("%1 of %2 done").arg(done_).arg(steps_.size());
}

void ExerciseController::start(bool assessed) {
  if (phase_ == Starting && (!assessed || assessedAvailable())) emit startRequested(assessed);
}

void ExerciseController::stop() {
  if (pttLive()) emit stopRequested();
}

void ExerciseController::continueExercise() {
  if (choice()) emit continueRequested();
}

void ExerciseController::showResult() {
  if (phase_ == Complete) emit resultRequested();
}

void ExerciseController::useScreenPtt() {
  if (phase_ == Paused) emit screenPttRequested();
}

void ExerciseController::play(int row) {
  if (audioSaved() && row >= 0 && row < timeline_.rowCount()) emit playRequested(row);
}
