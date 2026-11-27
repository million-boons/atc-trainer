#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <memory>
#include <string>

#include "app/AccessController.h"
#include "app/ExerciseController.h"
#include "app/ExerciseView.h"
#include "app/LabController.h"
#include "app/SettingsController.h"
#include "app/StatusFeed.h"
#include "app/SttController.h"
#include "app/EngineHeaders.h"
#include "app/PlannerService.h"
#include "storage/ExerciseAudio.h"

struct LiveExerciseConfig {
  std::string data, voices;  // folders
  std::string logs;          // event logs, one file per exercise
  std::string scenarioFile;
  std::string model = "whisper-tiny-genprompt";  // D-414
  atc::RadioIntensity radio = atc::RadioIntensity::Light;
  bool pttStandIn = false;       // D-435: the PTT device is the mouse stand-in (--ptt-device stand-in)
  bool pttMissingBlocks = true;  // configuration; the instructor's option comes from SettingsController
  std::function<std::unique_ptr<atc::IEventLog>()> makeLog;  // empty: JSON Lines in `logs`
  atc::VoiceBank::MayUse mayUse;             // the speaker's answer today (D-133); called on the check's thread
  atc::ExerciseCheck::Folder phraseology;    // the folder in use (D-318); empty: data/phraseology
  std::function<QVariantMap(const atc::PlanSelection&)> planCard;  // the Planner's keys for the Dashboard card
  std::shared_ptr<atc::ExerciseAudio> audio;  // empty: exercise audio is never kept
  std::function<atc::CheckResult(const std::string&)> check;  // tests: the result at once; empty: ExerciseCheck
};

// ExerciseRuntime, as LiveBackend holds it. Called on the GUI thread.
struct ExerciseRadio {
  std::function<std::string(atc::PreparedExercise, atc::ExerciseOptions, std::unique_ptr<atc::IEventLog>)> start;
  std::function<void()> stop, resume, useFallback;
};
// StatusController. Called on the GUI thread.
struct ExerciseStatus {
  std::function<void(atc::BankReady, const QStringList&)> bank;
  std::function<void(bool)> running;
  std::function<void(const QVariantMap&)> scenario;
};

// The live exercise: Dashboard Start and Stop, the coverage check, and the Exercise and Result screens, on
// ExerciseRuntime (package 3b). Exercise audio is not stored, and nothing here claims it (D-121, D-314).
class LiveExercise : public QObject {
  Q_OBJECT
 public:
  LiveExercise(LiveExerciseConfig cfg, SttController* stt, LabController* lab, AccessController* access, SettingsController* settings, QObject* parent = nullptr);
  ~LiveExercise() override;

  ExerciseController* controller() { return &c_; }
  // For ExerciseRuntime: they come on RadioCore's thread and are queued to the GUI thread. RadioCore must have
  // stopped before this object goes.
  atc::ExerciseRuntime::Callbacks callbacks();
  // Last. Then the coverage check runs (D-115).
  void connectRadio(ExerciseRadio radio, ExerciseStatus status, StatusFeed* feed);
  // Wording, clips or a consent changed: check again. While an exercise or its Start dialog is open, as soon as it is
  // over; nothing of this can change during an exercise.
  void recheck();
  bool active() const;  // the Start dialog or an exercise
  // D-334: the Planner. A chosen plan runs its scenario with its exact values; none: the scenario of ui.json, values
  // by seed. GUI thread.
  void setPlanner(atc::PlannerService* p) { planner_ = p; }
  void usePlan(std::optional<atc::PlanSelection> p);
  // 5d: the scenario of the Dashboard, for this run of the program (C-3); replaces a plan.
  void useScenario(const std::string& file);

 private:
  template <class F> void ui(F f) { QMetaObject::invokeMethod(this, std::move(f), Qt::QueuedConnection); }
  void checked(const atc::CheckResult& r);
  void newSeed();
  void showPlan();
  const std::string& scenarioFile() const { return plan_ ? plan_->scenarioFile : !scenario_.empty() ? scenario_ : cfg_.scenarioFile; }
  void prepare();
  void start(bool assessed);
  void leave();
  void ended(const atc::ExerciseResult& r);

  const LiveExerciseConfig cfg_;
  QPointer<SttController> stt_;
  QPointer<LabController> lab_;
  QPointer<AccessController> access_;
  QPointer<SettingsController> settings_;
  ExerciseController c_;
  ExerciseView view_{&c_};
  ExerciseRadio radio_;
  ExerciseStatus status_;
  QTimer second_;
  atc::PreparedExercise ready_;
  std::uint64_t seed_ = 1;
  bool starting_ = false, assessed_ = false, recheck_ = false;
  atc::PlannerService* planner_ = nullptr;
  std::optional<atc::PlanSelection> plan_;
  std::string scenario_;  // chosen on the screen; empty: the one of ui.json
  std::string session_;  // of the latest start
  QDateTime startedAt_;
  std::unique_ptr<atc::ExerciseCheck> check_;  // last: joins its thread before the rest goes
};
