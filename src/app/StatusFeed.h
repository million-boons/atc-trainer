#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <utility>

#include "core/Readiness.h"

// Subsystem states for the Dashboard, except the speech model (SttController has it).
struct StatusInputs {
  atc::MicReady mic = atc::MicReady::Ok;
  QString micName;
  double micDbfs = -120;
  bool micOpenIdle = false;  // the microphone runs and no transmission is open (D-312)
  int micTestLeft = 0;    // seconds; 0 = no test running
  atc::SpeakerReady speaker = atc::SpeakerReady::NotChecked;
  QString speakerName, speakerNote;
  bool toneAwaitingAnswer = false;  // the tone played to its end: "I heard it" may be pressed (D-306)
  atc::PttReady ptt = atc::PttReady::Missing;
  bool pttMissingBlocks = true;
  bool pttDown = false;
  QString pttName, pttNote;
  bool pttTesting = false;
  atc::BankReady bank = atc::BankReady::NotLoaded;
  QStringList missingClips;
  atc::StorageReady storage = atc::StorageReady::Ok;
  QString storageFree;
  bool running = false;   // an exercise is running
  QVariantMap scenario;   // name, summary, callsign, type, runway, taxiway, rule, steps, sop, placeholder
};

// A backend (the mock now, StatusController after 1a) calls set() on the GUI thread and
// answers the requests. None of the tests may record or open a transmission.
class StatusFeed : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("created in C++")
 public:
  explicit StatusFeed(QObject* parent = nullptr) : QObject(parent) {}
  const StatusInputs& inputs() const { return in_; }
  void set(StatusInputs in) {
    in_ = std::move(in);
    emit changed();
  }

 signals:
  void changed();
  void micTestRequested();
  void toneRequested();
  void toneHeardRequested();  // the only way to SpeakerReady::Ok
  void pttTestRequested();
  void cancelTestsRequested();
  void stopExerciseRequested();

 private:
  StatusInputs in_;
};
