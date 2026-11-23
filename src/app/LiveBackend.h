#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "app/LabController.h"
#include "app/SttController.h"
#include "audio/PulseCapture.h"
#include "ptt/EvdevPttSource.h"
#include "ptt/PttInputManager.h"

struct LiveConfig {
  QString pttDevice;  // stable path, e.g. /dev/input/by-id/...; empty = none
  QString pttName;    // shown in the event log header
  bool pttGrab = false;  // true for a pedal, false for a desk mouse
  std::string micSource, pulseServer;
};

// Real microphone, PTT device and speech models behind the Speech Lab. All PTT, half-duplex
// and audio cutting runs on one core thread; the GUI only receives queued updates.
// Recordings stay in memory and are never written to disk here (D-44).
class LiveBackend : public QObject {
  Q_OBJECT
 public:
  LiveBackend(LabController* lab, SttController* stt, LiveConfig cfg, QObject* parent = nullptr);
  ~LiveBackend() override;
  bool start();

 signals:
  void sideTone();

 private:
  struct Closing {
    std::uint64_t id;
    atc::Ns from, to, press;
    atc::TxEnd end;
    std::int64_t endIndex;
    atc::Ns release, deadline;
  };
  void post(std::function<void()> f);  // to the core thread
  template <class F> void ui(F f) { QMetaObject::invokeMethod(this, std::move(f), Qt::QueuedConnection); }
  void event(atc::Ns t, const QString& src, const QString& edge, const QString& detail, bool warn);
  void notice(LabController::Level l, const QString& n, LabController::Level al, const QString& a);
  // Core thread.
  void run();
  void build();
  void feed(std::int64_t to);
  void closeNow();
  void publish(atc::Ns now);
  void pttState(bool present, const QString& chip, LabController::Level l, const QString& why);

  LabController* lab_;
  SttController* stt_;
  LiveConfig cfg_;
  const atc::TxConfig txCfg_;
  const double micLow_, micHigh_;
  const bool hasPtt_;
  const atc::Ns t0_;

  // GUI thread.
  std::uint64_t guiTx_ = 0;
  bool guiOpen_ = false;

  std::mutex m_;
  std::condition_variable cv_;
  std::vector<std::function<void()>> q_;
  std::atomic<bool> stop_{false};

  // Core thread.
  std::optional<atc::PttInputManager> ptt_;
  std::optional<atc::TransmissionController> tx_;
  std::string owner_;
  atc::Ns pressT_ = 0, openT_ = 0, releaseT_ = 0, lastPublish_ = 0;
  std::optional<atc::Ns> cutUntil_;
  bool held_ = false, stuck_ = false, pulse_ = false, present_ = true, left_ = false, txActive_ = false;
  QString pttChip_;
  std::uint64_t curId_ = 0;
  bool streaming_ = false;
  std::int64_t cursor_ = 0;
  bool lost_ = false;  // audio was overwritten before it could be sent
  std::optional<Closing> closing_;
  atc::CaptureStatus mic_;

  atc::PrerollRing ring_;
  atc::LevelMeter meter_;
  std::unique_ptr<atc::PulseCapture> cap_;
  std::unique_ptr<atc::EvdevPttSource> right_, leftSrc_;
  std::thread thread_;
};
