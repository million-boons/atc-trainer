#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <optional>
#include <vector>

#include "app/LabChecks.h"
#include "app/LabController.h"
#include "app/StatusFeed.h"
#include "app/SttController.h"
#include "ptt/PttInputManager.h"

// Runs the real PTT, half-duplex and utterance-check logic on a virtual clock with
// synthetic audio. No hardware, no speech recognition: text is scripted PLACEHOLDER.
class MockBackend : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("created in C++")
  Q_PROPERTY(QVariantList scripts READ scripts CONSTANT)
  Q_PROPERTY(QString current READ current NOTIFY currentChanged)
  Q_PROPERTY(StatusFeed* status READ status CONSTANT)
 public:
  MockBackend(LabController* lab, SttController* stt, int pilotCutMs, QObject* parent = nullptr);
  QVariantList scripts() const;
  QString current() const { return current_; }
  StatusFeed* status() { return &status_; }
  // Fast-forwards a scripted case and freezes it there until the next press.
  Q_INVOKABLE void run(const QString& id);
  // Live: the mock pilot starts speaking now under this rule (interrupt, block, queue).
  Q_INVOKABLE void livePilot(const QString& rule);
  // Fake engine for SttService: scripted PLACEHOLDER text, about 1.2 s to load.
  static std::unique_ptr<atc::ISpeechRecognizer> fakeEngine(const atc::SttModel& m, std::string& error);

 signals:
  void sideTone();  // live only
  void currentChanged();

 private:
  void setStatusCase(const QString& id);  // Dashboard values for a scripted case
  void syncStatus();                      // PTT and microphone: tiles and chips from one state
  void statusTick();                      // 100 ms, running tests only
  void testEdge(bool down);
  void endPttTest(const QString& note);
  struct Speech { double startMs = 320, endBeforeReleaseMs = 350, gain = 0.292  /* about -12 dBFS */; };
  void goLive();
  void restart();
  void userEdge(const QString& src, bool down);
  void advanceTo(atc::Ns t);
  void at(double ms, std::function<void()> f);
  void edge(double tMs, const char* src, bool down) { ptt_->rawEdge(atc::ms(0) + atc::Ns(tMs * 1e6), src, down); }
  void publish();
  void showFinal(std::uint64_t id, const QString& text, const QStringList& words, const QList<double>& conf,
                 const QString& info, const QString& config);
  std::uint64_t wire(std::uint64_t id) const { return (epoch_ << 32) | id; }
  void finish(std::uint64_t id, atc::Ns from, atc::Ns to, atc::TxEnd end);
  std::vector<std::int16_t> synth(atc::Ns from, atc::Ns to, atc::Ns press, std::optional<atc::Ns> release) const;
  LabChecks checksFor(const std::vector<std::int16_t>& audio, int preRollSamples, atc::TxEnd end) const;
  double sec() const { return double(now_) / 1e9; }

  LabController* lab_;
  SttController* stt_;
  std::uint64_t epoch_ = 0;   // results from before a restart are ignored
  bool sttOpen_ = false;      // the service accepted this transmission
  int pilotCutMs_;
  QTimer timer_;
  StatusFeed status_;
  QTimer statusTimer_;
  QElapsedTimer testHold_;
  atc::PttReady pttBase_ = atc::PttReady::StandIn;
  int micTestTicks_ = 0, pttTestTicks_ = 0;
  QElapsedTimer wall_;
  atc::Ns wallBase_ = 0, now_ = 0, lastMeter_ = 0;
  bool frozen_ = false;
  QString current_;

  std::optional<atc::PttInputManager> ptt_;
  std::optional<atc::TransmissionController> tx_;
  std::vector<std::pair<atc::Ns, std::function<void()>>> script_;
  Speech speech_;
  std::string owner_;
  atc::Ns pressT_ = 0, openT_ = 0, releaseT_ = 0;
  atc::PlaybackId playback_ = 0;
  bool held_ = false, stuck_ = false, present_ = true, pulse_ = false, muted_ = false, pilotHangs_ = false;
  atc::PressRule rule_ = atc::PressRule::Interrupt;
  std::uint64_t curId_ = 0;
  std::optional<std::pair<std::uint64_t, atc::Ns>> finalAt_;
  std::optional<atc::Ns> pilotEnd_, cutUntil_;
};
