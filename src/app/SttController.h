#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <optional>

#include "stt/SttService.h"

// Speech-model selector of the Speech Lab (D-26). Wraps atc::SttService: its callbacks
// arrive on the service's thread and are queued to the GUI thread here.
class SttController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("created in C++")
  Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)  // {id, name, note, installed}
  Q_PROPERTY(int selected READ selected NOTIFY changed)
  Q_PROPERTY(QString selectedName READ selectedName NOTIFY changed)
  Q_PROPERTY(QString loadedName READ loadedName NOTIFY changed)  // empty if none
  Q_PROPERTY(QString stateText READ stateText NOTIFY changed)
  Q_PROPERTY(int level READ level NOTIFY changed)                // LabController.Level
  Q_PROPERTY(QString message READ message NOTIFY changed)        // failure or refusal reason
  Q_PROPERTY(bool loadEnabled READ loadEnabled NOTIFY changed)
  Q_PROPERTY(bool locked READ locked NOTIFY changed)             // exercise running (D-39)
 public:
  explicit SttController(QObject* parent = nullptr) : QObject(parent) {}

  void start(std::vector<atc::SttModel> models, atc::SttService::Factory factory, atc::SttService::Describe describe,
             std::function<long()> freeMb = {});
  void setCatalogError(const QString& why);
  atc::SttService* service() { return service_.get(); }
  // A transmission is open, in its tail, or its text is pending.
  void setTransmissionActive(bool on);
  // Mock cases: show this status instead of the service's. Empty: the service again.
  void showScripted(std::optional<atc::SttStatus> s);
  QString nameOf(const std::string& id) const;

  QVariantList models() const { return list_; }
  int selected() const { return selected_; }
  QString selectedName() const;
  QString loadedName() const;
  QString stateText() const;
  int level() const;
  QString message() const;
  bool loadEnabled() const;
  bool locked() const { return shown().locked; }

  Q_INVOKABLE void select(int row);
  Q_INVOKABLE void load();

 signals:
  void modelsChanged();
  void changed();
  void result(quint64 tx, const QString& modelName, const QString& modelConfig, const QString& text, const QStringList& words,
              const QList<double>& conf, double releaseToTextSeconds);

 private:
  const atc::SttStatus& shown() const { return scripted_ ? *scripted_ : status_; }
  QVariantList list_;
  int selected_ = -1;
  bool txActive_ = false;
  QString catalogError_;
  atc::SttStatus status_;
  std::optional<atc::SttStatus> scripted_;
  std::unique_ptr<atc::SttService> service_;  // last: joins its thread before the rest goes
};
