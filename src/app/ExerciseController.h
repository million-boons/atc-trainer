#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <optional>

#include "app/RowModel.h"

// The plan an exercise was started from (04 section 5g).
struct ExercisePlan {
  QString name{}, trainee{};  // trainee empty: anyone
  bool assessed = false;
  int run = 0;                // of this person; 0: unnumbered (guest)
};

struct ExerciseContext {
  QString scenario, callsign, runway, taxiway, type, rule, model;
  QString trainee;  // ID; empty: guest, without history and audio (D-127)
  bool placeholder = true;  // some text is unvalidated: no Assessed mode (P-11)
  std::optional<ExercisePlan> plan{};
};

struct ExerciseStep {
  QString name;
  int attempts = 0;
};

// View state of the Exercise and Result screens (04 section 5c). A backend (the mock, or ExerciseView) calls the setters on the GUI thread; QML reads properties and calls the invokables. What Assessed
// hides is hidden here, so that no screen can show it (D-107, D-113).
class ExerciseController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("created in C++")
  Q_PROPERTY(Phase phase READ phase NOTIFY phaseChanged)
  Q_PROPERTY(bool active READ active NOTIFY phaseChanged)    // the other screens are not available
  Q_PROPERTY(bool pttLive READ pttLive NOTIFY phaseChanged)  // Space is PTT
  Q_PROPERTY(QVariantMap context READ context NOTIFY changed)  // scenario, callsign, runway, taxiway, type, rule, model
  Q_PROPERTY(bool placeholder READ placeholder NOTIFY changed)
  Q_PROPERTY(QString standInText READ standInText NOTIFY changed)
  Q_PROPERTY(bool assessed READ assessed NOTIFY changed)
  Q_PROPERTY(QString modeText READ modeText NOTIFY changed)
  Q_PROPERTY(QString preview READ preview NOTIFY changed)  // mock: why this is only a preview
  Q_PROPERTY(bool assessedAvailable READ assessedAvailable NOTIFY changed)
  Q_PROPERTY(QString assessedNote READ assessedNote NOTIFY changed)
  Q_PROPERTY(bool practiceAvailable READ practiceAvailable NOTIFY changed)  // a plan fixes the mode
  Q_PROPERTY(QString practiceNote READ practiceNote NOTIFY changed)
  Q_PROPERTY(bool planAssessed READ planAssessed NOTIFY changed)
  Q_PROPERTY(QString planText READ planText NOTIFY changed)        // empty: no plan
  Q_PROPERTY(QString planWarning READ planWarning NOTIFY changed)  // somebody else is logged in (D-142)
  Q_PROPERTY(QString runText READ runText NOTIFY changed)
  Q_PROPERTY(bool audioSaved READ audioSaved NOTIFY changed)
  Q_PROPERTY(QString audioText READ audioText NOTIFY changed)
  Q_PROPERTY(QStringList clearances READ clearances NOTIFY changed)
  Q_PROPERTY(QString clearancesNote READ clearancesNote NOTIFY changed)  // shown instead of an empty list
  Q_PROPERTY(QVariantList steps READ steps NOTIFY changed)  // {name, state, note}
  Q_PROPERTY(QString progressText READ progressText NOTIFY changed)
  Q_PROPERTY(QString missedText READ missedText NOTIFY changed)
  Q_PROPERTY(QString waitingText READ waitingText NOTIFY waitingChanged)  // changes every second
  Q_PROPERTY(int noticeLevel READ noticeLevel NOTIFY changed)
  Q_PROPERTY(QString notice READ notice NOTIFY changed)
  // The pilot's calls are used up and the instructor decides (D-424, D-125).
  Q_PROPERTY(bool choice READ choice NOTIFY changed)
  Q_PROPERTY(QString choiceTitle READ choiceTitle NOTIFY changed)
  Q_PROPERTY(QString choiceText READ choiceText NOTIFY changed)
  Q_PROPERTY(bool pinAsked READ pinAsked NOTIFY changed)  // Assessed: the instructor's PIN is needed to end (D-125, D-129)
  Q_PROPERTY(QVariantMap result READ result NOTIFY resultChanged)
  Q_PROPERTY(QAbstractItemModel* transcript READ transcript CONSTANT)  // newest first
  Q_PROPERTY(QAbstractItemModel* timeline READ timeline CONSTANT)      // oldest first
 public:
  enum Phase { Idle, Starting, Running, Paused, Complete, Result };
  Q_ENUM(Phase)
  enum Kind { Atc, Pilot, Note };
  Q_ENUM(Kind)
  enum StepState { Done, Now, Later, Hidden };
  Q_ENUM(StepState)

  explicit ExerciseController(QObject* parent = nullptr);

  // Setters for the backend. verdict: true for text that tells the trainee how the step was judged.
  void begin(ExerciseContext c, bool assessed, bool consent, const QString& preview = {});  // clears everything else
  // Only from a backend that really stores the exercise audio. Before that the screens never claim it.
  void setAudioConnected() {
    audioConnected_ = true;
    emit changed();
  }
  // The run uses the mouse stand-in (--ptt-device stand-in), not the pedal (D-435); set at each start.
  void setPttStandIn(bool on) {
    if (pttStandIn_ == on) return;
    pttStandIn_ = on;
    emit changed();
  }
  // The Lab's save switch also covers exercise audio (EA-1); set before each Start dialog. Off until told.
  void setAudioSwitch(bool on) {
    if (audioSwitch_ == on) return;
    audioSwitch_ = on;
    emit changed();
  }
  void setPhase(Phase p);
  void setSteps(QList<ExerciseStep> s, int done);
  void setMissed(QStringList names);
  void setClearances(QStringList c);
  void setWaiting(const QString& t);
  void setNotice(int level, const QString& t, bool verdict = false);
  void setAlert(int level, const QString& t, bool verdict = false);
  void setChoice(const QString& title, const QString& text);  // text is hidden in Assessed; also the trainee's "Instructor notified"
  void clearChoice();
  void endConfirmed();  // the instructor's PIN was right; only while it is asked for
  // chips: list of {value, uncertain}. An uncertain value is passed as heard and never corrected.
  void addAtc(const QString& time, const QString& text, const QString& intent, const QVariantList& chips, const QString& cls, bool recognised, const QString& delay);
  void addPilot(const QString& time, const QString& text);
  void addNote(int level, const QString& text);
  void setResult(QVariantMap summary, QList<QVariantList> timeline);  // timeline: time, who, text, outcome, level

  Phase phase() const { return phase_; }
  bool active() const { return phase_ != Idle && phase_ != Result; }
  bool pttLive() const { return phase_ == Running || phase_ == Paused || phase_ == Complete; }
  QVariantMap context() const;
  bool placeholder() const { return ctx_.placeholder; }
  QString standInText() const { return pttStandIn_ ? tr("PTT: mouse stand-in, not the pedal") : QString(); }
  bool assessed() const { return assessed_; }
  QString modeText() const { return assessed_ ? tr("ASSESSED") : tr("PRACTICE"); }
  QString preview() const { return preview_; }
  bool assessedAvailable() const { return !ctx_.placeholder && (!ctx_.plan || (ctx_.plan->assessed && planned())); }
  bool practiceAvailable() const { return !planAssessed(); }
  QString practiceNote() const { return practiceAvailable() ? tr("The verdict is shown during the exercise.") : tr("The plan is for Assessed."); }
  bool planAssessed() const { return ctx_.plan && ctx_.plan->assessed; }
  QString runText() const { return !ctx_.plan ? QString() : ctx_.plan->run > 0 ? tr("plan, run %1").arg(ctx_.plan->run) : tr("plan, run not numbered"); }
  QString planText() const { return ctx_.plan ? tr("Plan: %1   %2").arg(ctx_.plan->name, ctx_.plan->run > 0 ? tr("run %1").arg(ctx_.plan->run) : tr("run not numbered")) : QString(); }
  QString planWarning() const { return !ctx_.plan || planned() || planAssessed() ? QString() : tr("This plan is for %1. The result belongs to %2.").arg(ctx_.plan->trainee, who()); }
  QString assessedNote() const;
  QString who() const { return ctx_.trainee.isEmpty() ? tr("Guest") : ctx_.trainee; }
  bool audioSaved() const { return consent_ && audioSwitch_ && audioConnected_ && !ctx_.trainee.isEmpty(); }
  QString audioText() const {
    return audioSaved() ? tr("Saved on this machine only") : ctx_.trainee.isEmpty() ? tr("Exercise audio not saved: guest") : !audioSwitch_ ? tr("Exercise audio not saved: switched off in Settings") : !consent_ ? tr("Exercise audio not saved: no consent") : tr("Exercise audio not saved: not connected yet");
  }
  QStringList clearances() const { return hide() ? QStringList() : clearances_; }
  QString clearancesNote() const { return hide() ? tr("Shown in the debrief") : clearances_.isEmpty() ? tr("None yet") : QString(); }
  QVariantList steps() const;
  QString progressText() const;
  QString missedText() const { return hide() ? QStringLiteral("-") : missed_.isEmpty() ? tr("none") : missed_.join(", "); }
  QString waitingText() const { return waiting_.isEmpty() ? QStringLiteral("-") : waiting_; }
  int noticeLevel() const { return hide() && notice_.verdict ? 0 : notice_.level; }
  QString notice() const { return hide() && notice_.verdict ? QString() : notice_.text; }
  int alertLevel() const;  // footer
  QString alert() const;
  bool pinAsked() const { return pin_ != Pin::No; }
  bool choice() const { return choice_ && phase_ == Running; }
  QString choiceTitle() const { return choiceTitle_; }
  QString choiceText() const { return hide() ? QString() : choiceText_; }
  QVariantMap result() const { return phase_ == Result ? result_ : QVariantMap(); }
  QAbstractItemModel* transcript() { return &transcript_; }
  QAbstractItemModel* timeline() { return &timeline_; }

  Q_INVOKABLE void start(bool assessed);  // refused if the mode is not available
  Q_INVOKABLE void cancelStart() { emit cancelStartRequested(); }
  Q_INVOKABLE void stop();        // Practice; refused in Assessed: the PIN is needed (D-129)
  Q_INVOKABLE void requestEnd();  // Assessed: ask for the PIN
  Q_INVOKABLE void cancelEnd();
  Q_INVOKABLE void continueExercise();  // the pilot calls again
  Q_INVOKABLE void showResult();
  Q_INVOKABLE void useScreenPtt();
  Q_INVOKABLE void runAgain() { emit runAgainRequested(); }
  Q_INVOKABLE void leaveResult() { emit leaveRequested(); }
  Q_INVOKABLE void play(int row);  // timeline row; refused without saved audio

 signals:
  void changed();
  void phaseChanged();
  void waitingChanged();
  void resultChanged();
  void screenRequested(const QString& id);
  // Requests to the backend.
  void startRequested(bool assessed);
  void cancelStartRequested();
  void stopRequested();
  void continueRequested();
  void resultRequested();
  void screenPttRequested();
  void runAgainRequested();
  void leaveRequested();
  void playRequested(int row);

 private:
  struct Line {
    int level = 0;
    QString text;
    bool verdict = false;
  };
  bool planned() const { return ctx_.plan->trainee.isEmpty() || ctx_.plan->trainee == ctx_.trainee; }  // only with a plan
  bool hide() const { return assessed_ && phase_ != Result; }

  Phase phase_ = Idle;
  ExerciseContext ctx_;
  bool assessed_ = false, consent_ = false, audioConnected_ = false, audioSwitch_ = false, pttStandIn_ = false;
  QString preview_, waiting_;
  QStringList clearances_, missed_;
  QList<ExerciseStep> steps_;
  int done_ = 0;
  Line notice_, alert_;
  bool choice_ = false;
  enum class Pin { No, Stop, Choice };  // Choice: asked from the choice dialog, and ends with it
  Pin pin_ = Pin::No;
  QString choiceTitle_, choiceText_;
  QVariantMap result_;
  RowModel transcript_, timeline_;
};
