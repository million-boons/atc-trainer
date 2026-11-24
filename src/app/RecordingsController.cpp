#include "app/RecordingsController.h"

#include <QDateTime>
#include <QMetaObject>
#include <utility>

using namespace atc;
using L = LabController;

namespace {
constexpr std::size_t kMaxListed = 200;

std::pair<int, QString> verdict(const UtteranceReport& r) {
  if (r.empty) return {L::Caution, RecordingsController::tr("No speech")};
  if (r.overload) return {L::Fault, RecordingsController::tr("Too loud")};
  if (r.spokeBeforeKeying) return {L::Caution, RecordingsController::tr("Spoke before keying")};
  if (r.releasedEarly) return {L::Caution, RecordingsController::tr("Released early")};
  return {L::Ok, RecordingsController::tr("Clean")};
}
}  // namespace

RecordingsController::RecordingsController(LabController* lab, AccessController* access, SettingsController* settings, QObject* parent)
    : QObject(parent), lab_(lab), access_(access), settings_(settings),
      stored_({"key", "tx", "time", "speaker", "duration", "text", "level", "status"}, int(kMaxListed), this) {
  const auto reset = [this] {
    error_.clear();
    emit changed();
  };
  connect(access, &AccessController::changed, this, reset);
  connect(settings, &SettingsController::changed, this, reset);
}

RecordingsController::~RecordingsController() = default;

std::function<void(const std::string&, bool, const std::string&)> RecordingsController::savedCallback() {
  return [this](const std::string& key, bool ok, const std::string& why) {
    QMetaObject::invokeMethod(this, [this, key, ok, why] { saved(key, ok, why); }, Qt::QueuedConnection);
  };
}

SaveRule RecordingsController::rule() const {
  if (!access_ || !settings_) return SaveRule::Off;
  return saveRule(settings_->saveRecordings(), access_->role(), access_->consent());
}

QString RecordingsController::speakerText() const {
  return access_ && access_->trainee() ? tr("Speaker %1").arg(access_->personId()) : tr("No speaker");
}

int RecordingsController::lineLevel() const { return rule() == SaveRule::Save && error_.isEmpty() && b_.save ? L::Ok : L::Caution; }

QString RecordingsController::lineText() const {
  switch (rule()) {
    case SaveRule::Off: return tr("Saving is off");
    case SaveRule::NotLoggedIn: return tr("Not saved: log in as a trainee");
    case SaveRule::NoConsent: return tr("Not saved: no consent");
    case SaveRule::Save: break;
  }
  if (!b_.save) return tr("Not saved: no recording store");
  return error_.isEmpty() ? tr("Saved on this machine only") : tr("Not saved: %1").arg(error_);
}

void RecordingsController::offer(StoredRecording rec, const std::vector<std::int16_t>& audio) {
  keys_.erase(rec.tx);  // a number used again must not point at an older recording
  if (rule() != SaveRule::Save || !b_.save || !b_.makeKey) return;
  rec.speaker = access_->personId().toStdString();
  rec.key = b_.makeKey(rec.startedMs, rec.tx);
  keys_[rec.tx] = rec.key;
  b_.save(std::move(rec), audio);
}

void RecordingsController::addResult(int tx, StoredResult r) {
  const auto it = keys_.find(tx);
  if (it != keys_.end() && b_.addResult) b_.addResult(it->second, std::move(r));
}

void RecordingsController::saved(const std::string& key, bool ok, const std::string& why) {
  if (ok) {
    error_.clear();
  } else {
    error_ = QString::fromStdString(why);
    std::erase_if(keys_, [&](const auto& k) { return k.second == key; });
  }
  emit changed();
}

void RecordingsController::refreshStored() {
  if (!b_.list) {
    stored_.reset();
    return;
  }
  b_.list(kMaxListed, [this](std::vector<StoredRecording> all, int bad) {
    QMetaObject::invokeMethod(this, [this, all = std::move(all), bad] {
      QList<QVariantList> rows;
      for (const StoredRecording& r : all) {
        const auto [level, status] = verdict(r.report);
        const QString text = r.results.empty() ? QString() : QString::fromStdString(r.results.back().text);
        rows.append(QVariantList{QString::fromStdString(r.key), r.tx, QDateTime::fromMSecsSinceEpoch(r.startedMs).toString("dd MMM HH:mm:ss"),
                                 QString::fromStdString(r.speaker), tr("%1 s").arg(r.rate > 0 ? double(r.samples) / r.rate : 0, 0, 'f', 1),
                                 text.isEmpty() ? tr("(no text)") : text, level, status});
      }
      stored_.reset(std::move(rows));
      message_ = bad ? tr("%1 files could not be read").arg(bad) : QString();
      emit changed();
    }, Qt::QueuedConnection);
  });
}

void RecordingsController::deleteStored(int row) {
  if (!canDelete() || !b_.remove || row < 0 || row >= stored_.rowCount()) return;
  const std::string key = stored_.row(row)[0].toString().toStdString();
  b_.remove(key, std::string(roleName(access_->role())), [this, key](const std::string& why) {
    QMetaObject::invokeMethod(this, [this, key, why] { removed(key, why); }, Qt::QueuedConnection);
  });
}

void RecordingsController::removed(const std::string& key, const std::string& why) {
  message_ = why.empty() ? QString() : tr("Not deleted: %1").arg(QString::fromStdString(why));
  if (why.empty()) {
    for (int i = 0; i < stored_.rowCount(); ++i)
      if (stored_.row(i)[0].toString().toStdString() == key) {
        stored_.remove(i);
        break;
      }
    for (auto it = keys_.begin(); it != keys_.end(); ++it)
      if (it->second == key) {  // also from memory
        if (lab_) lab_->dropRecording(it->first);
        keys_.erase(it);
        break;
      }
  }
  emit changed();
}
