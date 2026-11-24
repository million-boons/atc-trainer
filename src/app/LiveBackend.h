#pragma once
#include <QObject>
#include <QString>
#include <memory>

#include "app/LabController.h"
#include "app/StatusController.h"
#include "app/SttController.h"
#include "runtime/RadioCore.h"
#include "runtime/RealDevices.h"

struct LiveConfig {
  atc::RealDeviceConfig devices;
  atc::RadioConfig radio;
  QString pttName;  // shown in the event log header
  StatusConfig status;
};

// The Speech Lab and the status tiles as clients of RadioCore. RadioCore runs on its own
// thread; everything here is queued to the GUI thread. Recordings stay in memory and are
// never written to disk here (D-44).
class LiveBackend : public QObject {
  Q_OBJECT
 public:
  LiveBackend(LabController* lab, SttController* stt, LiveConfig cfg, QObject* parent = nullptr);
  ~LiveBackend() override;
  bool start();
  StatusFeed* status() { return status_->feed(); }

 signals:
  void sideTone();

 private:
  struct Speech : atc::ISpeechFeed {
    explicit Speech(SttController* s) : stt(s) {}
    bool begin(std::uint64_t tx) override { return stt->service() && stt->service()->begin(tx); }
    void accept(std::vector<std::int16_t> a) override { stt->service()->accept(std::move(a)); }
    void finish(atc::Ns t) override { stt->service()->finish(t); }
    void cancel() override { stt->service()->cancel(); }
    SttController* stt;
  };
  template <class F> void ui(F f) { QMetaObject::invokeMethod(this, std::move(f), Qt::QueuedConnection); }
  atc::RadioCore::Events events();
  void event(atc::Ns t, const std::string& src, const QString& edge, const QString& detail, bool warn);
  void notice(LabController::Level l, const QString& n, LabController::Level al, const QString& a);
  // Core thread.
  void note(const atc::PttNote& n);
  void closed(atc::RadioTx&& t);
  // GUI thread.
  void snapshot(const atc::RadioSnapshot& s);
  void play(bool keyedPartOnly);

  LabController* lab_;
  SttController* stt_;
  const LiveConfig cfg_;
  const double micLow_, micHigh_;
  const atc::Ns t0_;
  Speech speech_;
  StatusController* status_;

  // GUI thread.
  std::uint64_t guiTx_ = 0;
  bool guiOpen_ = false;
  atc::PttLink link_ = atc::PttLink::NotSet;

  std::unique_ptr<atc::RadioCore> core_;  // last: joins its thread before the rest goes
};
