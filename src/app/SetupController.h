#pragma once
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <string>
#include <vector>

#include "app/AccessController.h"
#include "app/LabController.h"
#include "app/SetupBackend.h"

// The full Settings (04 section 5f, D-122): values with their ranges, devices, learning a pedal, the PIN, guest
// results. GUI thread only. Nothing here transmits or blocks PTT. A refused value shows its range and changes nothing.
class SetupController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("created in C++")
  Q_PROPERTY(bool ready READ ready NOTIFY changed)
  Q_PROPERTY(bool instructor READ instructor NOTIFY changed)
  Q_PROPERTY(QVariantMap numbers READ numbers NOTIFY changed)   // page: [{key, title, text, byUser, applies, range}]
  Q_PROPERTY(QVariantMap choices READ choices NOTIFY changed)   // page: [{key, title, text, byUser, applies, may, value, options}]
  Q_PROPERTY(QVariantMap facts READ facts NOTIFY changed)       // page: [{name, value}]
  Q_PROPERTY(QString theme READ theme NOTIFY changed)
  Q_PROPERTY(QString clock READ clock NOTIFY changed)
  Q_PROPERTY(bool reduceMotion READ reduceMotion NOTIFY changed)
  Q_PROPERTY(int guests READ guests NOTIFY changed)
  Q_PROPERTY(bool learning READ learning NOTIFY learnChanged)
  Q_PROPERTY(QVariantList presses READ presses NOTIFY learnChanged)  // {id, name, info, state, level, usable}
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(bool trainerBusy MEMBER trainerBusy_ WRITE setTrainerBusy NOTIFY changed)
  Q_PROPERTY(int messageLevel READ messageLevel NOTIFY messageChanged)
  Q_PROPERTY(QString message READ message NOTIFY messageChanged)
 public:
  static constexpr int kHold = 2000;  // ms a pedal must be held before it can be taken
  explicit SetupController(AccessController* access = nullptr, QObject* parent = nullptr);
  void setAccess(AccessController* a);
  void setBackend(SetupBackend b) { b_ = std::move(b); }

  bool ready() const { return loaded_; }
  bool instructor() const { return access_ && access_->instructor(); }
  QVariantMap numbers() const;
  QVariantMap choices() const;
  QVariantMap facts() const;
  QString theme() const { return value("theme"); }
  QString clock() const { return value("clock"); }
  bool reduceMotion() const { return value("motion") == QLatin1String("reduce"); }
  int guests() const { return content_.guests; }
  bool learning() const { return learning_; }
  QVariantList presses() const;
  bool busy() const { return waiting_ > 0; }
  void setTrainerBusy(bool on);
  int messageLevel() const { return messageLevel_; }
  QString message() const { return message_; }

  Q_INVOKABLE void enter();
  Q_INVOKABLE void leave();      // the screen closes: learning stops
  Q_INVOKABLE void stepNumber(const QString& key, int steps);
  Q_INVOKABLE void choose(const QString& key, const QString& id);
  Q_INVOKABLE void restore(const QString& page);
  Q_INVOKABLE void startLearn();
  Q_INVOKABLE void stopLearn();
  Q_INVOKABLE void takePedal(const QString& id);   // held long enough and released
  Q_INVOKABLE void changePin(const QString& oldPin, const QString& newPin, const QString& again);
  Q_INVOKABLE void testSentence();
  Q_INVOKABLE void deleteGuests(int expected);     // the screen has asked, with this count
  Q_INVOKABLE void clearMessage() { say(0, {}); }

 signals:
  void changed();
  void learnChanged();
  void messageChanged();
  void pinChanged();
  void themeChosen(const QString& mode);   // by somebody, not at the start: the start is the program's (ui.json or settings.json)
  void motionChosen(bool reduce);

 private:
  const SetupChoice* choiceOf(const QString& key) const;
  QString value(const char* key) const;
  bool may(bool everybody);
  bool say(int level, const QString& text);
  void queued(std::function<void()> f);
  void reload();
  SetupBackend::Done answer(std::function<void()> ok, const QString& doneText);
  void accessChanged();

  QPointer<AccessController> access_;
  SetupContent content_;
  std::vector<SetupPress> presses_;
  std::vector<std::pair<std::string, int>> longest_;   // device, longest hold seen
  QString message_;
  bool loaded_ = false, learning_ = false, trainerBusy_ = false, wasInstructor_ = false;
  int waiting_ = 0, messageLevel_ = 0;
  unsigned loadNo_ = 0, learnNo_ = 0;
  SetupBackend b_;  // last
};
