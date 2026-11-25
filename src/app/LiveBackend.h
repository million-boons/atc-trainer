#pragma once
#include <QObject>
#include <QString>
#include <functional>
#include <map>
#include <memory>
#include <optional>

#include "app/EngineHeaders.h"
#include "app/LabController.h"
#include "app/LiveExercise.h"
#include "app/RecordingsController.h"
#include "app/StatusController.h"
#include "app/SttController.h"
#include "runtime/RadioCore.h"
#include "runtime/RealDevices.h"

struct LiveExerciseSetup {
  atc::ParserRules rules;
  atc::RuntimeConfig runtime;
  atc::ExerciseRuntime::Callbacks callbacks;  // must stay callable until the backend is gone
};

// The Studio's service, if there is one. It must stay until the backend is gone.
namespace atc { class StudioService; }

struct LiveConfig {
  atc::RealDeviceConfig devices;
  atc::RadioConfig radio;
  QString pttName;  // shown in the event log header
  StatusConfig status;
  std::optional<LiveExerciseSetup> exercise;  // empty: this program runs no exercise
  atc::StudioService* studio = nullptr;
};

// The Speech Lab and the status tiles as clients of RadioCore. RadioCore runs on its own
// thread; everything here is queued to the GUI thread. Recordings stay in memory and are
// never written to disk here (D-44): each one is offered to RecordingsController, which alone decides
// whether it is saved (switch, speaker ID, consent). Its audio goes nowhere else.
class LiveBackend : public QObject {
  Q_OBJECT
 public:
  LiveBackend(LabController* lab, SttController* stt, LiveConfig cfg, RecordingsController* recordings = nullptr, QObject* parent = nullptr);
  ~LiveBackend() override;
  bool start();
  StatusFeed* status() { return status_->feed(); }
  // For LiveExercise; empty functions without an exercise set-up.
  ExerciseRadio exerciseRadio();
  ExerciseStatus exerciseStatus();

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
  void notice(LabController::Level l, const QString& n, LabController::Level al, const QString& a, bool ptt = false);
  // Core thread.
  void note(const atc::PttNote& n);
  void closed(atc::RadioTx&& t);
  // GUI thread.
  void snapshot(const atc::RadioSnapshot& s);
  void play(bool keyedPartOnly);
  void result(quint64 tx, const QString& model, const QString& config, const QString& text, const QStringList& words,
              const QList<double>& conf, double seconds);

  LabController* lab_;
  SttController* stt_;
  RecordingsController* recordings_;  // may be null: nothing is offered
  const LiveConfig cfg_;
  const double micLow_, micHigh_;
  const atc::Ns t0_;
  Speech speech_;
  StatusController* status_;

  // Core thread.
  std::map<std::uint64_t, std::string> sources_;  // who keyed the latest transmissions

  // GUI thread.
  std::uint64_t lastClosed_ = 0;
  std::map<std::uint64_t, std::function<void()>> early_;  // results that came before their recording
  std::uint64_t guiTx_ = 0;
  bool guiOpen_ = false;
  atc::PttLink link_ = atc::PttLink::NotSet;

  std::unique_ptr<atc::SpeechModel> model_;
  std::unique_ptr<atc::ExerciseRuntime> runtime_;  // may be null
  std::unique_ptr<atc::RadioCore> core_;  // last: joins its thread before the rest goes
};
