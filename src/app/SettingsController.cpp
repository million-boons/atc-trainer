#include "app/SettingsController.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <utility>

SettingsController::SettingsController(AccessController* access, QString file, QObject* parent)
    : QObject(parent), access_(access), file_(std::move(file)) {
  QFile f(file_);
  if (file_.isEmpty() || !f.open(QIODevice::ReadOnly)) return;
  QJsonParseError e;
  const QJsonObject o = QJsonDocument::fromJson(f.readAll(), &e).object();
  if (e.error != QJsonParseError::NoError) {
    message_ = tr("The settings could not be read. Defaults are used.");
    return;
  }
  save_ = o["saveRecordings"].toBool(true);
  fallback_ = o["pttFallback"].toBool(false);
}

void SettingsController::setSaveRecordings(bool on) { change(save_, on); }
void SettingsController::setPttFallback(bool on) { change(fallback_, on); }

void SettingsController::change(bool& field, bool on) {
  if (field == on) return;
  if (!access_ || !access_->instructor()) {
    message_ = tr("Only the instructor may change settings.");
  } else {
    field = on;
    message_ = store() ? QString() : tr("The setting holds until the program ends: it could not be stored.");
  }
  emit changed();
}

bool SettingsController::store() const {
  if (file_.isEmpty()) return true;
  if (!QDir().mkpath(QFileInfo(file_).path())) return false;
  QSaveFile f(file_);
  if (!f.open(QIODevice::WriteOnly)) return false;
  f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
  f.write(QJsonDocument(QJsonObject{{"saveRecordings", save_}, {"pttFallback", fallback_}}).toJson());
  return f.commit();
}
