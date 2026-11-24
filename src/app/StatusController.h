#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <functional>

#include "app/LabController.h"
#include "app/StatusFeed.h"
#include "runtime/RadioCore.h"

struct StatusConfig {
  QString pttName;
  bool pttStandIn = true;       // a desk mouse in place of the pedal
  bool pttMissingBlocks = true;
  QString storagePath;          // folder whose free space is shown
  qint64 storageWarnMb = 1000;  // proposed; 1c decides the recordings limit (D-102)
};

// Turns the radio's state into Dashboard tiles and status-bar chips, from one state so that
// they cannot disagree, and runs the three tests. GUI thread only. No test records or opens
// a transmission.
class StatusController : public QObject {
  Q_OBJECT
 public:
  struct Radio {
    std::function<void(bool)> setPttTest;
    std::function<void()> playTone, stopLocal;
  };
  StatusController(LabController* lab, StatusConfig cfg, Radio radio, QObject* parent = nullptr);

  StatusFeed* feed() { return &feed_; }
  void onSnapshot(const atc::RadioSnapshot& s);
  void onOutput(const atc::OutputStatus& s);
  void onTone(atc::LocalEnd e);
  // "I heard it" (D-306): counts only after the tone played to its end.
  void toneHeard();
  bool toneAwaitingAnswer() const { return toneDone_; }
  void onPttTest(const atc::PttTest& t);
  void setBank(atc::BankReady b, const QStringList& missing = {});

 private:
  void requestTone();
  void endPttTest(const QString& note);
  void tick();       // 100 ms, while a test runs
  void storage();
  void apply();      // feed and chips, only if something changed
  void speakerNote(const QString& note);

  LabController* lab_;
  const StatusConfig cfg_;
  const Radio radio_;
  StatusFeed feed_;
  StatusInputs in_, sent_;
  bool first_ = true;
  atc::OutputStatus out_;
  atc::RadioSnapshot radioState_;
  double peak_ = -120, testPeak_ = -120;
  bool toneDone_ = false;     // played to the end, not yet confirmed
  enum class MicCheck { None, Good, Outside };  // result of the last 5 s test of this microphone (D-306)
  MicCheck micCheck_ = MicCheck::None;
  QString micSource_;
  QString testNote_;          // result of the last PTT test
  bool pressSeen_ = false;
  QElapsedTimer level_, micTest_, pttTest_;
  QTimer tick_, storageTimer_;
};
