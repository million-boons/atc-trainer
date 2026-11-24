#include "app/DashboardController.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "app/StatusWords.h"

using namespace atc;

namespace {
// Start is connected in package 3b. Until then it is never enabled.
constexpr bool kStartBuilt = false;

QString wordOf(StartEffect e) {
  return e == StartEffect::Ready ? DashboardController::tr("READY")
         : e == StartEffect::Warns ? DashboardController::tr("WARNS") : DashboardController::tr("BLOCKS START");
}
}  // namespace

void DashboardController::setFeed(StatusFeed* f) {
  if (feed_ == f) return;
  if (feed_) disconnect(feed_.data(), nullptr, this, nullptr);
  feed_ = f;
  if (f) connect(f, &StatusFeed::changed, this, &DashboardController::refresh);
  refresh();
}

void DashboardController::setStt(SttController* s) {
  if (stt_ == s) return;
  if (stt_) disconnect(stt_.data(), nullptr, this, nullptr);
  stt_ = s;
  if (s) {
    connect(s, &SttController::changed, this, &DashboardController::refresh);
    connect(s, &SttController::modelsChanged, this, &DashboardController::refresh);
  }
  refresh();
}

void DashboardController::setLab(LabController* l) {
  if (lab_ == l) return;
  if (lab_) disconnect(lab_.data(), nullptr, this, nullptr);
  lab_ = l;
  if (l) connect(l, &LabController::noticeChanged, this, &DashboardController::refreshAlert);
  emit alertChanged();
  refreshAlert();
}

void DashboardController::setExercise(ExerciseController* e) {
  if (exercise_ == e) return;
  if (exercise_) disconnect(exercise_.data(), nullptr, this, nullptr);
  exercise_ = e;
  if (e) connect(e, &ExerciseController::changed, this, &DashboardController::refreshAlert);
  emit alertChanged();
  refreshAlert();
}

void DashboardController::setExerciseModel(const QString& id) {
  if (model_ == id) return;
  model_ = id;
  refresh();
}

QString DashboardController::modelName() const { return stt_ ? stt_->nameOf(model_.toStdString()) : model_; }

ModelReady DashboardController::modelState() const {
  if (!stt_) return ModelReady::NotLoaded;
  if (!stt_->catalogError().isEmpty()) return ModelReady::Failed;
  const SttStatus& s = stt_->status();
  switch (s.state) {
    case SttState::NotLoaded: return ModelReady::NotLoaded;
    case SttState::Loading: return ModelReady::Loading;
    case SttState::Failed: return ModelReady::Failed;
    case SttState::Ready:
    case SttState::Busy: break;
  }
  return s.modelId == model_.toStdString() ? ModelReady::Ready : ModelReady::OtherLoaded;
}

void DashboardController::refresh() {
  tiles_.clear();
  reasons_.clear();
  readyText_.clear();
  blockText_.clear();
  startEnabled_ = false;
  readyLevel_ = LabController::None;
  readyAlert_.clear();
  if (!feed_) {
    chips_.clear();
    emit changed();
    emit chipsChanged();
    return refreshAlert();
  }
  const StatusInputs& in = feed_->inputs();
  const ModelReady model = modelState();
  const Readiness r = readiness({.mic = in.mic, .speaker = in.speaker, .ptt = in.ptt, .model = model, .bank = in.bank,
                                 .storage = in.storage, .pttMissingBlocks = in.pttMissingBlocks});
  QStringList blockers;
  const auto add = [&](ReadyTile t, const QString& title, const QString& state, const QString& line1, const QString& line2,
                       const QString& action, bool actionEnabled, const QString& action2 = {}, bool action2Enabled = false) {
    const StartEffect e = r.of(t);
    tiles_.append(QVariantMap{{"title", title}, {"level", int(status_words::level(e))}, {"state", state}, {"line1", line1}, {"line2", line2},
                              {"effect", wordOf(e)}, {"action", action}, {"actionEnabled", actionEnabled},
                              {"action2", action2}, {"action2Enabled", action2Enabled}});
    if (e != StartEffect::Blocks) return;
    reasons_.append(tr("%1: %2").arg(title, state.toLower()));
    blockers.append(tr("%1 %2").arg(title, state.toLower()));
  };
  const bool idle = !in.running;

  const QString dbfs = tr("%1 dBFS").arg(std::lround(in.micDbfs));
  add(ReadyTile::Mic, tr("Microphone"), status_words::word(in.mic), in.micName,
      (in.micTestLeft > 0 ? tr("%1  test %2 s").arg(dbfs).arg(in.micTestLeft) : dbfs) + (in.micOpenIdle ? tr("\nMic open – not transmitting") : QString()), tr("Test"),
      idle && in.micTestLeft == 0 && in.mic != MicReady::Lost);

  add(ReadyTile::Speaker, tr("Speaker"), status_words::word(in.speaker), in.speakerName, in.speakerNote, tr("Test tone"), idle,
      tr("I heard it"), idle && in.toneAwaitingAnswer);

  const QString edge = in.ptt == PttReady::Missing ? QString() : in.pttDown ? tr("RIGHT: DOWN") : tr("RIGHT: UP");
  add(ReadyTile::Ptt, tr("PTT"), status_words::word(in.ptt), in.pttName, in.pttNote.isEmpty() ? edge : in.pttNote, tr("Test"),
      idle && !in.pttTesting && in.ptt != PttReady::Missing);

  static const char* const mod[] = {QT_TR_NOOP("READY"), QT_TR_NOOP("NOT LOADED"), QT_TR_NOOP("LOADING"), QT_TR_NOOP("FAILED"), QT_TR_NOOP("OTHER MODEL")};
  const bool installed = stt_ && stt_->installed(model_);
  QString note;
  if (stt_ && model == ModelReady::Failed) note = stt_->message();
  else if (stt_ && model == ModelReady::OtherLoaded) note = tr("Loaded: %1").arg(stt_->loadedName());
  else if (!installed) note = tr("not installed");
  else if (stt_ && stt_->locked()) note = tr("locked");
  const bool busy = model == ModelReady::Ready && stt_->status().state == SttState::Busy;
  add(ReadyTile::Model, tr("Speech model"), busy ? tr("BUSY") : tr(mod[int(model)]), modelName(), note, tr("Load exercise model"),
      idle && stt_ && stt_->canLoad(model_));

  QString bank;
  switch (in.bank) {
    case BankReady::CoverageOk: bank = tr("COVERAGE OK"); break;
    case BankReady::Loaded: bank = tr("LOADED"); break;
    case BankReady::NotLoaded: bank = tr("NOT LOADED"); break;
    case BankReady::MissingClips: bank = tr("MISSING CLIPS (%1)").arg(in.missingClips.size()); break;
    case BankReady::Failed: bank = tr("FAILED"); break;
  }
  add(ReadyTile::Bank, tr("Voice bank"), bank, in.bank == BankReady::Loaded ? tr("Coverage not checked") : QString(), {}, tr("List"),
      !in.missingClips.isEmpty());

  add(ReadyTile::Storage, tr("Storage"), in.storage == StorageReady::Ok ? tr("OK") : tr("LOW"), in.storageFree, {}, {}, false);

  readyText_ = tr("%1 of %2 ready").arg(r.count(StartEffect::Ready)).arg(int(r.tiles.size()));
  blockText_ = tr("%1 block Start").arg(r.count(StartEffect::Blocks));
  QVariantList chips;
  const QString names[] = {tr("Mic"), tr("Speaker"), tr("PTT"), tr("STT"), tr("VOICE BANK")};  // ReadyTile order (D-27)
  for (int i = 0; i < int(std::size(names)); ++i) {
    const QVariantMap m = tiles_[i].toMap();
    chips.append(QVariantMap{{"name", names[i]}, {"value", m["state"]}, {"level", m["level"]}});
  }
  const bool newChips = chips != chips_;  // the level meter changes the tiles, not the chips
  chips_ = chips;

  const int blocks = r.count(StartEffect::Blocks), warns = r.count(StartEffect::Warns);
  readyLevel_ = status_words::level(blocks ? StartEffect::Blocks : warns ? StartEffect::Warns : StartEffect::Ready);
  const QString more = warns ? tr("; %1 warn").arg(warns) : QString();
  if (in.running)
    readyAlert_ = blocks ? tr("Exercise running: %1%2").arg(blockers.join(", "), more)
                  : warns ? tr("Exercise running; %1 warn").arg(warns) : tr("Exercise running");
  else
    readyAlert_ = blocks ? tr("Not ready to start: %1%2").arg(blockers.join(", "), more)
                  : warns ? tr("%1 warn, none block Start").arg(warns) : tr("All systems ready");
  startEnabled_ = kStartBuilt && idle && !r.blocked();
  if (!kStartBuilt) reasons_.append(tr("Exercise not built yet (package 3b)"));
  emit changed();
  if (newChips) emit chipsChanged();
  refreshAlert();
}

void DashboardController::refreshAlert() {
  QStringList parts;
  int level = readyLevel_;
  const auto add = [&](int l, const QString& text) {
    if (l < LabController::Caution) return;
    level = std::max(level, l);
    parts.append(text);
  };
  if (lab_) add(lab_->alertLevel(), lab_->alert());
  const bool exercise = exercise_ && exercise_->phase() != ExerciseController::Idle;
  if (exercise) add(exercise_->alertLevel(), exercise_->alert());
  if (readyLevel_ >= LabController::Caution) parts.append(readyAlert_);
  const QString text = !parts.isEmpty() ? parts.join("  |  ") : exercise ? exercise_->alert() : readyAlert_;
  if (level == alertLevel_ && text == alert_) return;
  alertLevel_ = level;
  alert_ = text;
  emit alertChanged();
}

bool DashboardController::tileAction(int i) {
  if (!feed_) return false;
  switch (ReadyTile(i)) {
    case ReadyTile::Mic: emit feed_->micTestRequested(); break;
    case ReadyTile::Speaker: emit feed_->toneRequested(); break;
    case ReadyTile::Ptt: emit feed_->pttTestRequested(); break;
    case ReadyTile::Model:
      if (stt_) stt_->loadModel(model_);
      break;
    case ReadyTile::Bank: return !missingClips().isEmpty();
    case ReadyTile::Storage: break;
  }
  return false;
}

void DashboardController::toneHeard() {
  if (feed_ && feed_->inputs().toneAwaitingAnswer && !running()) emit feed_->toneHeardRequested();
}

void DashboardController::cancelTests() {
  if (feed_) emit feed_->cancelTestsRequested();
}

void DashboardController::stopExercise() {
  if (running()) emit feed_->stopExerciseRequested();
}
