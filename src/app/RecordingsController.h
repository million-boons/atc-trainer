#pragma once
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "app/AccessController.h"
#include "app/LabController.h"
#include "app/RowModel.h"
#include "app/SettingsController.h"
#include "storage/RecordingStore.h"

// The recording store as functions, so that the mock and the tests need no files. They return at once;
// the callbacks may come on another thread.
struct RecordingBackend {
  std::function<std::string(std::int64_t startedMs, int tx)> makeKey;
  std::function<void(atc::StoredRecording, std::vector<std::int16_t>)> save;
  std::function<void(const std::string& key, atc::StoredResult)> addResult;
  std::function<void(std::size_t max, std::function<void(std::vector<atc::StoredRecording>, int unreadable)>)> list;
  std::function<void(const std::string& key, const std::string& role, std::function<void(const std::string& why)>)> remove;
};

// Decides whether a transmission is saved, and shows what is stored (04 section 5b). Never affects PTT.
// GUI thread only. LiveBackend calls offer() and addResult() and knows nothing about consent.
class RecordingsController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("created in C++")
  Q_PROPERTY(QString speakerText READ speakerText NOTIFY changed)
  Q_PROPERTY(int lineLevel READ lineLevel NOTIFY changed)   // LabController.Level
  Q_PROPERTY(QString lineText READ lineText NOTIFY changed)
  Q_PROPERTY(bool canDelete READ canDelete NOTIFY changed)
  Q_PROPERTY(QString message READ message NOTIFY changed)   // of the list or a deletion
  Q_PROPERTY(QAbstractItemModel* stored READ stored CONSTANT)  // newest first
 public:
  RecordingsController(LabController* lab, AccessController* access, SettingsController* settings, QObject* parent = nullptr);
  ~RecordingsController() override;
  void setBackend(RecordingBackend b) { b_ = std::move(b); }
  // For the store: may be called on any thread.
  std::function<void(const std::string& key, bool ok, const std::string& why)> savedCallback();

  // Leave key and speaker empty. The audio is copied only if it is saved.
  void offer(atc::StoredRecording rec, const std::vector<std::int16_t>& audio);
  void addResult(int tx, atc::StoredResult r);  // ignored for a transmission that was not saved

  QString speakerText() const;
  int lineLevel() const;
  QString lineText() const;
  bool canDelete() const { return access_ && atc::mayDeleteRecordings(access_->role()); }
  QString message() const { return message_; }
  QAbstractItemModel* stored() { return &stored_; }

  Q_INVOKABLE void refreshStored();
  Q_INVOKABLE void deleteStored(int row);  // the screen has asked for confirmation

 signals:
  void changed();

 private:
  atc::SaveRule rule() const;
  void saved(const std::string& key, bool ok, const std::string& why);
  void removed(const std::string& key, const std::string& why);

  QPointer<LabController> lab_;
  QPointer<AccessController> access_;
  QPointer<SettingsController> settings_;
  std::map<int, std::string> keys_;  // saved transmissions of this run
  QString error_, message_;
  RowModel stored_;
  RecordingBackend b_;  // last: the store finishes its work before the rest goes
};
