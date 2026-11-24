#include "app/StatusController.h"

#include <QDir>
#include <QStorageInfo>
#include <algorithm>
#include <cmath>
#include <utility>

#include "app/StatusWords.h"

using namespace atc;
using L = LabController;

namespace {
constexpr int kMicTestMs = 5000, kPressWaitMs = 10'000, kLevelMs = 500, kLevelTestMs = 100, kStorageMs = 10'000;
constexpr double kHoldSeconds = 2;

bool same(const StatusInputs& a, const StatusInputs& b) {
  return a.mic == b.mic && a.micName == b.micName && a.micDbfs == b.micDbfs && a.micTestLeft == b.micTestLeft &&
         a.speaker == b.speaker && a.speakerName == b.speakerName && a.speakerNote == b.speakerNote && a.ptt == b.ptt &&
         a.pttMissingBlocks == b.pttMissingBlocks && a.pttDown == b.pttDown && a.pttName == b.pttName && a.pttNote == b.pttNote &&
         a.pttTesting == b.pttTesting && a.bank == b.bank && a.missingClips == b.missingClips && a.storage == b.storage &&
         a.storageFree == b.storageFree && a.running == b.running && a.scenario == b.scenario;
}
}  // namespace

StatusController::StatusController(LabController* lab, StatusConfig cfg, Radio radio, QObject* parent)
    : QObject(parent), lab_(lab), cfg_(std::move(cfg)), radio_(std::move(radio)) {
  in_.mic = MicReady::Lost;  // nothing has been reported yet
  in_.ptt = PttReady::Missing;
  in_.pttMissingBlocks = cfg_.pttMissingBlocks;
  in_.speakerName = tr("no output");

  connect(&feed_, &StatusFeed::micTestRequested, this, [this] {
    testPeak_ = -120;
    micTest_.start();
    tick_.start();
    tick();
  });
  connect(&feed_, &StatusFeed::toneRequested, this, &StatusController::requestTone);
  connect(&feed_, &StatusFeed::pttTestRequested, this, [this] { radio_.setPttTest(true); });
  connect(&feed_, &StatusFeed::cancelTestsRequested, this, [this] {
    micTest_.invalidate();
    radio_.stopLocal();  // also ends Lab playback: the screen it belongs to has closed
    radio_.setPttTest(false);
    in_.micTestLeft = 0;
    apply();
  });
  tick_.setInterval(100);
  connect(&tick_, &QTimer::timeout, this, &StatusController::tick);
  storageTimer_.setInterval(kStorageMs);
  connect(&storageTimer_, &QTimer::timeout, this, [this] {
    storage();
    apply();
  });
  storageTimer_.start();
  storage();
  level_.start();
  setBank(BankReady::NotLoaded);
}

void StatusController::storage() {
  // The folder may not exist yet: ask about the nearest one that does.
  QString path = cfg_.storagePath;
  while (!path.isEmpty() && path != "/" && !QDir(path).exists()) path = path.section('/', 0, -2);
  const QStorageInfo s(path.isEmpty() ? "/" : path);
  const qint64 mb = s.isValid() ? s.bytesAvailable() / 1'000'000 : -1;
  in_.storage = mb < cfg_.storageWarnMb ? StorageReady::Low : StorageReady::Ok;
  in_.storageFree = mb < 0 ? tr("free space unknown") : mb < 10'000 ? tr("%1 GB free").arg(mb / 1000.0, 0, 'f', 1) : tr("%1 GB free").arg(mb / 1000);
}

void StatusController::setBank(BankReady b, const QStringList& missing) {
  in_.bank = b;
  in_.missingClips = missing;
  switch (b) {
    case BankReady::Loaded: lab_->setVoiceBank(L::Bank::Loaded); break;
    case BankReady::CoverageOk: lab_->setVoiceBank(L::Bank::CoverageOk); break;
    case BankReady::MissingClips: lab_->setVoiceBank(L::Bank::MissingClips, int(missing.size())); break;
    case BankReady::Failed: lab_->setVoiceBank(L::Bank::Failed); break;
    case BankReady::NotLoaded: lab_->setVoiceBank(L::Bank::NotLoaded); break;
  }
  apply();
}

void StatusController::onSnapshot(const RadioSnapshot& s) {
  radioState_ = s;
  peak_ = std::max(peak_, s.micDbfs);
  const QString source = QString::fromStdString(s.mic.source);
  if (source != micSource_) {  // another microphone has not been tested
    micSource_ = source;
    outside_ = false;
  }
  if (micTest_.isValid()) testPeak_ = std::max(testPeak_, s.micDbfs);
  in_.mic = s.mic.state != CaptureState::Running ? MicReady::Lost
            : s.mic.muted ? MicReady::Muted
            : s.micSilent ? MicReady::NoSignal
            : outside_ ? MicReady::OutsideBand : MicReady::Ok;
  in_.micName = s.mic.description.empty() ? source : QString::fromStdString(s.mic.description);
  // Slowly, and in whole decibels: each change redraws the tiles.
  if (level_.elapsed() >= (micTest_.isValid() ? kLevelTestMs : kLevelMs)) {
    level_.restart();
    in_.micDbfs = std::round(std::exchange(peak_, -120));
  }
  in_.ptt = s.ptt != PttLink::Ok ? PttReady::Missing : s.pulse ? PttReady::NoHold : cfg_.pttStandIn ? PttReady::StandIn : PttReady::Ok;
  in_.pttName = s.ptt == PttLink::NotSet ? tr("no device set") : cfg_.pttName;
  in_.pttDown = s.held && s.owner == "RIGHT";
  QString note = testNote_;
  if (s.ptt == PttLink::Opening) note = tr("opening");
  else if (s.ptt == PttLink::Lost) note = tr("disconnected");
  else if (s.ptt == PttLink::NotOpen) note = QString::fromStdString(s.pttError);
  in_.pttNote = note;
  apply();
}

void StatusController::onOutput(const OutputStatus& s) {
  const bool other = s.available != out_.available || s.name != out_.name;
  out_ = s;
  in_.speakerName = s.available ? QString::fromStdString(s.description) : tr("no output");
  // A check holds only for the output it was made on, and while that can be heard.
  if (other || s.muted || s.volumePercent == 0) {
    in_.speaker = SpeakerReady::NotChecked;
    in_.speakerNote = s.available && s.muted ? tr("muted") : s.available && s.volumePercent == 0 ? tr("volume is 0") : QString();
  } else if (in_.speaker == SpeakerReady::NotChecked && (in_.speakerNote == tr("muted") || in_.speakerNote == tr("volume is 0"))) {
    in_.speakerNote.clear();
  }
  apply();
}

void StatusController::speakerNote(const QString& note) {
  in_.speaker = SpeakerReady::NotChecked;
  in_.speakerNote = note;
  apply();
}

void StatusController::requestTone() {
  if (!out_.available) return speakerNote(tr("no audio output"));
  if (out_.muted) return speakerNote(tr("muted"));
  if (out_.volumePercent == 0) return speakerNote(tr("volume is 0"));
  radio_.playTone();
}

void StatusController::onTone(LocalEnd e) {
  switch (e) {
    case LocalEnd::Started: return speakerNote(tr("playing"));
    case LocalEnd::Refused: return speakerNote(tr("radio busy"));
    case LocalEnd::Failed: return speakerNote(tr("tone failed"));
    case LocalEnd::Interrupted: return speakerNote(tr("tone stopped"));
    case LocalEnd::Completed: break;
  }
  if (!out_.available || out_.muted || out_.volumePercent == 0) return speakerNote(tr("muted or lost"));
  in_.speaker = SpeakerReady::Ok;
  in_.speakerNote = tr("tone played");
  apply();
}

void StatusController::onPttTest(const PttTest& t) {
  using K = PttTest::Kind;
  const bool device = t.source == "RIGHT";
  switch (t.kind) {
    case K::Started:
      pressSeen_ = false;
      in_.pttTesting = true;
      in_.pttNote = testNote_ = tr("Hold for 2 s");
      pttTest_.start();
      tick_.start();
      break;
    case K::Refused: in_.pttNote = testNote_ = tr("radio busy"); break;
    case K::Down:
      if (!in_.pttTesting) break;
      pressSeen_ = device;
      in_.pttNote = testNote_ = device ? tr("RIGHT: DOWN, keep holding") : tr("%1 is not the PTT device").arg(QString::fromStdString(t.source));
      break;
    case K::Up: {
      if (!in_.pttTesting || !device) break;
      const double held = double(t.held) / 1e9;
      if (t.reason == ReleaseReason::DeviceLost) return endPttTest(tr("disconnected"));
      return endPttTest(held >= kHoldSeconds ? tr("Held %1 s: OK").arg(held, 0, 'f', 2) : tr("Held %1 s: too short").arg(held, 0, 'f', 2));
    }
    case K::Ended:
      if (in_.pttTesting) in_.pttNote = testNote_ = QString();  // cancelled: no result
      in_.pttTesting = false;
      pttTest_.invalidate();
      break;
  }
  apply();
}

void StatusController::endPttTest(const QString& note) {
  in_.pttTesting = false;  // before Ended arrives, so that the result stays
  in_.pttNote = testNote_ = note;
  pttTest_.invalidate();
  radio_.setPttTest(false);
  apply();
}

void StatusController::tick() {
  if (micTest_.isValid()) {
    const qint64 left = kMicTestMs - micTest_.elapsed();
    in_.micTestLeft = int(std::max<qint64>(0, (left + 999) / 1000));
    if (left <= 0) {
      micTest_.invalidate();
      outside_ = testPeak_ < lab_->micLow || testPeak_ > lab_->micHigh;
      onSnapshot(radioState_);
    }
  }
  if (pttTest_.isValid() && !pressSeen_ && pttTest_.elapsed() >= kPressWaitMs) endPttTest(tr("No press seen"));
  if (!micTest_.isValid() && !pttTest_.isValid()) tick_.stop();
  apply();
}

void StatusController::apply() {
  if (!first_ && same(in_, sent_)) return;
  first_ = false;
  sent_ = in_;
  lab_->setChip(L::Mic, status_words::word(in_.mic), status_words::level(effect(in_.mic)));
  lab_->setChip(L::Speaker, status_words::word(in_.speaker), status_words::level(effect(in_.speaker)));
  lab_->setChip(L::Ptt, status_words::word(in_.ptt), status_words::level(effect(in_.ptt, in_.pttMissingBlocks)));
  feed_.set(in_);
}
