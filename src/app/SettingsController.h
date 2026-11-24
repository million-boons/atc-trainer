#pragma once
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "app/AccessController.h"

// Settings kept between runs, in settings.json of the writable folder. Only the instructor may change them
// (D-118, D-116). An unreadable file gives the defaults and is not overwritten until a setting changes.
class SettingsController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("created in C++")
  Q_PROPERTY(bool saveRecordings READ saveRecordings NOTIFY changed)
  Q_PROPERTY(bool pttFallback READ pttFallback NOTIFY changed)  // a missing PTT only warns
  Q_PROPERTY(QString message READ message NOTIFY changed)
 public:
  // file empty: nothing is stored (mock).
  SettingsController(AccessController* access, QString file, QObject* parent = nullptr);
  bool saveRecordings() const { return save_; }
  bool pttFallback() const { return fallback_; }
  QString message() const { return message_; }
  Q_INVOKABLE void setSaveRecordings(bool on);
  Q_INVOKABLE void setPttFallback(bool on);

 signals:
  void changed();

 private:
  void change(bool& field, bool on);
  bool store() const;
  QPointer<AccessController> access_;
  const QString file_;
  bool save_ = true, fallback_ = false;
  QString message_;
};
