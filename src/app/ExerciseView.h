#pragma once
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include "app/ExerciseController.h"
#include "core/EngineEvent.h"
#include "app/EngineHeaders.h"

// Turns the inputs and outputs of a running exercise into what the Exercise and Result screens show. It decides
// nothing: every verdict, reply and count is the engine's. GUI thread; no radio, so tests drive it with the Simulator.
class ExerciseView {
 public:
  struct Setup {
    std::shared_ptr<const atc::Exercise> exercise;
    std::uint64_t seed = 1;
    QString model{};       // shown name
    QString trainee{};     // ID; empty: guest
    bool consent = false;  // of the trainee on record
  };
  explicit ExerciseView(ExerciseController* c) : c_(c) {}

  // The scenario card and the Start dialog show the values of this seed.
  static QVariantMap card(const atc::Exercise& e, std::uint64_t seed);

  void prepare(Setup s);                    // Start dialog
  void refused(const QString& why);         // back in the Start dialog, with the reason
  void started(bool assessed, QDateTime at);
  void input(atc::Ns t, const atc::EngineInput& i);
  void output(atc::Ns t, const atc::EngineOutput& o);
  void fault(const QString& why);
  void ended(const atc::ExerciseResult& r, QDateTime at);
  void showResult();
  void tick(atc::Ns now);                   // each second: the waiting time

 private:
  void on(atc::Ns t, const atc::in::Started&);
  void on(atc::Ns t, const atc::in::TxOpened&);
  void on(atc::Ns t, const atc::in::TxCancelled&);
  void on(atc::Ns t, const atc::in::Recognised&);
  void on(atc::Ns t, const atc::in::PilotStarted&);
  void on(atc::Ns t, const atc::in::PilotEnded&);
  void on(atc::Ns t, const atc::in::TimerFired&);
  void on(atc::Ns t, const atc::in::PttPresence&);
  void on(atc::Ns t, const atc::in::InstructorContinue&);
  void on(atc::Ns t, const atc::out::StepEntered&);
  void on(atc::Ns t, const atc::out::Judged&);
  void on(atc::Ns t, const atc::out::PilotRequested&);
  void on(atc::Ns t, const atc::out::AtcBeforePilot&);
  void on(atc::Ns t, const atc::out::NoReply&);
  void on(atc::Ns t, const atc::out::TimerSet&);
  void on(atc::Ns t, const atc::out::TimerCancelled&);
  void on(atc::Ns t, const atc::out::InstructorAlert&);
  template <class T> void on(atc::Ns, const T&) {}  // nothing to show

  struct Heard { QString text; QVariantList chips; QString clearance; };
  QString time(atc::Ns t) const;
  int stepIndex(const std::string& id) const;  // among the steps that are not final; -1 otherwise
  QString label(const std::string& id) const;
  ExerciseContext context() const;
  void choice();
  void steps();
  void waiting(std::optional<atc::Ns> since);

  ExerciseController* c_;
  Setup s_;
  QDateTime at_;
  std::optional<atc::Ns> t0_, waitSince_;
  std::string step_;
  int done_ = 0, calls_ = 0, choiceCalls_ = 0;
  QList<int> attempts_;
  QStringList clearances_;
  std::map<std::uint64_t, atc::Ns> pressed_;
  std::map<std::uint64_t, QString> utterances_;
  std::set<std::uint64_t> early_;                // transmitted before the pilot's first call (D-322)
  std::map<std::uint64_t, int> row_;             // timeline row of a transmission
  std::map<std::string, int> sayAgain_;
  Heard heard_;                                  // latest transmission
  std::uint64_t judged_ = 0;
  QList<double> delays_;
  int faults_ = 0;
  bool faultShown_ = false;
  std::uint64_t faultTx_ = 0;                    // latest transmission without a result: no attempt (D-317)
  int faultRun_ = 0;                             // such transmissions in a row
  QList<QVariantList> timeline_;                 // time, who, text, outcome, level
  QVariantMap result_;
};
