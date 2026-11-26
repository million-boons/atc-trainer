#pragma once
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "app/AccessController.h"
#include "app/LabController.h"
#include "app/PlannerBackend.h"

// Exercise Planner (04 section 5g, D-142 to D-144): the list of plans, the plan being edited, its checks, and choosing
// a plan for the Dashboard. GUI thread only; it never touches PTT. Only the instructor edits, the backend checks again.
class PlannerController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("created in C++")
  Q_PROPERTY(bool instructor READ instructor NOTIFY listChanged)
  Q_PROPERTY(bool ready READ ready NOTIFY listChanged)            // loaded, instructor logged in
  // Left: plans
  Q_PROPERTY(QVariantList plans READ plans NOTIFY listChanged)    // {id, name, who, runs, state, level, selected}
  Q_PROPERTY(int planCount READ planCount NOTIFY listChanged)
  Q_PROPERTY(QString filter READ filter NOTIFY listChanged)       // "all", "anyone" or a person
  Q_PROPERTY(QStringList people READ people NOTIFY listChanged)
  // Centre: the plan
  Q_PROPERTY(bool hasPlan READ hasPlan NOTIFY planChanged)
  Q_PROPERTY(QVariantMap plan READ plan NOTIFY planChanged)
  Q_PROPERTY(QVariantList scenarios READ scenarios NOTIFY listChanged)  // {id, name, note, off}
  Q_PROPERTY(QVariantList aircraft READ aircraft NOTIFY planChanged)    // callsign and type: {id, note}
  Q_PROPERTY(QVariantList runways READ runways NOTIFY planChanged)
  Q_PROPERTY(QVariantList taxiways READ taxiways NOTIFY planChanged)
  Q_PROPERTY(QVariantList presets READ presets NOTIFY planChanged)      // Easy, Normal, Hard: {name, from, to, radio}
  Q_PROPERTY(bool dirty READ dirty NOTIFY planChanged)
  Q_PROPERTY(bool canSave READ canSave NOTIFY planChanged)
  Q_PROPERTY(QString saveNote READ saveNote NOTIFY planChanged)         // why Save is off, or what it will do
  Q_PROPERTY(bool busy READ busy NOTIFY planChanged)
  Q_PROPERTY(bool trainerBusy MEMBER trainerBusy_ WRITE setTrainerBusy NOTIFY planChanged)  // an exercise is active
  // Right: preview and checks
  Q_PROPERTY(QVariantMap preview READ preview NOTIFY planChanged)       // the card, as the Dashboard would show it
  Q_PROPERTY(QVariantList checks READ checks NOTIFY planChanged)        // {level, text}
  Q_PROPERTY(bool canPut READ canPut NOTIFY planChanged)
  // Dashboard: the plans the person who is logged in may choose
  Q_PROPERTY(QVariantList choices READ choices NOTIFY listChanged)      // {id, text, note, off}
  Q_PROPERTY(int messageLevel READ messageLevel NOTIFY messageChanged)  // LabController.Level
  Q_PROPERTY(QString message READ message NOTIFY messageChanged)
 public:
  static constexpr int kBriefing = 200, kName = 40;        // characters (D-143, D-147)
  static constexpr int kDelayMin = 300, kDelayMax = 5000;  // ms (D-147)

  explicit PlannerController(AccessController* access = nullptr, QObject* parent = nullptr);
  void setAccess(AccessController* a);
  void setBackend(PlannerBackend b);
  bool connected() const { return bool(b_.load); }
  // Keys for the Dashboard's card, added to the scenario's: plan, planNote, planLine, briefing (and name).
  static QVariantMap card(const PlanChoice& c);

  bool instructor() const { return access_ && access_->instructor(); }
  bool ready() const { return loaded_ && instructor(); }
  QVariantList plans() const;
  int planCount() const { return int(content_.plans.size()); }
  QString filter() const { return filter_; }
  QStringList people() const;
  bool hasPlan() const { return open_; }
  QVariantMap plan() const;
  QVariantList scenarios() const;
  QVariantList aircraft() const;
  QVariantList runways() const;
  QVariantList taxiways() const;
  QVariantList presets() const;
  bool dirty() const;
  bool canSave() const { return hasPlan() && !busy() && !trainerBusy_ && changes() && saveRefusal().isEmpty(); }
  QString saveNote() const;
  bool busy() const { return waiting_ > 0; }
  void setTrainerBusy(bool on);
  QVariantMap preview() const;
  QVariantList checks() const;
  bool canPut() const;
  QVariantList choices() const;
  int messageLevel() const { return messageLevel_; }
  QString message() const { return message_; }

  Q_INVOKABLE void enter();                  // a screen opens: read everything again
  Q_INVOKABLE void search(const QString& words);
  Q_INVOKABLE void setFilter(const QString& f);
  Q_INVOKABLE void select(const QString& id);
  Q_INVOKABLE void newPlan();
  Q_INVOKABLE void copyPlan();
  Q_INVOKABLE void setName(const QString& t);
  Q_INVOKABLE void setScenario(const QString& id);
  Q_INVOKABLE void setTrainee(const QString& id);  // empty: anyone
  Q_INVOKABLE void setAssessed(bool on);
  Q_INVOKABLE void setBriefing(const QString& t);
  Q_INVOKABLE void setFixed(bool on);
  Q_INVOKABLE void setAircraft(const QString& id);
  Q_INVOKABLE void setRunway(const QString& id);
  Q_INVOKABLE void setTaxiway(const QString& id);
  Q_INVOKABLE void setPreset(int preset);          // PlanPreset
  Q_INVOKABLE void useNewPreset();                 // a plan whose preset has changed: the preset's values of today
  Q_INVOKABLE void stepReply(bool from, int steps);  // of 0.1 s; then the plan is Custom
  Q_INVOKABLE void setRadio(const QString& r);
  Q_INVOKABLE void save();
  Q_INVOKABLE void undo();
  Q_INVOKABLE void deletePlan();                   // the screen has asked for confirmation
  // "Presets...": changed values are kept here until savePresets().
  Q_INVOKABLE void editPresets();
  Q_INVOKABLE void cancelPresets();
  Q_INVOKABLE void stepPreset(int preset, bool from, int steps);
  Q_INVOKABLE void setPresetRadio(int preset, const QString& r);
  Q_INVOKABLE void startingPresets();
  Q_INVOKABLE void savePresets();
  Q_INVOKABLE void put();                          // the selected plan on the Dashboard
  Q_INVOKABLE void choose(const QString& id);      // from the Dashboard
  Q_INVOKABLE void clearMessage() { say(0, {}); }

 signals:
  void listChanged();
  void planChanged();
  void planLoaded();      // name and briefing were replaced: the fields take them over
  void messageChanged();
  void presetsSaved();
  void chosen();          // the plan is on the Dashboard

 private:
  enum class Field { None, Aircraft, Runway, Taxiway };
  enum class Fit { Incomplete, Checking, Yes, No };
  struct FitAnswer {         // the backend's fits() for the plan being edited
    std::string scenario{};
    PlanValues values{};
    bool answered = false, ok = false;
    std::string why{};
  };
  struct State {
    int level = 0;
    QString text{};
    bool start = true;    // it may be put on the Dashboard
  };
  const PlanScenario* scenarioOf(const std::string& id) const;
  const Plan* find(const std::string& id) const;
  const PlanDifficulty* presetOf(PlanPreset p) const;
  State stateOf(const Plan& p) const;
  bool fits(const PlanValues& v, const PlanScenario& s) const;  // by the list; with tooMany unknown
  Fit draftFit() const;
  QString fitWhy() const;                    // the backend's reason, if the values of the edit do not fit
  void askFits();
  bool misfit(Field f) const;
  bool known(const std::string& trainee) const;
  bool mayChoose(const Plan& p) const;
  bool presetChanged(const Plan& p) const;
  QVariantList options(Field f) const;
  bool changes() const;                      // Save would change something
  QString saveRefusal() const;
  bool allowed();                            // instructor and backend; otherwise the message says so
  bool edit();                               // allowed, and a plan is open
  bool leaveEdit();                          // an edit is never dropped silently
  bool say(int level, const QString& text);  // returns false
  PlannerBackend::Done answer(std::function<void()> ok, const QString& doneText = {});
  void queued(std::function<void()> f);
  void reload(bool reopen = false);
  void open(const Plan& p);
  void touched();
  void check();
  void ask(const std::string& id);
  void accessChanged();
  void allChanged();

  QPointer<AccessController> access_;
  PlannerContent content_;
  Plan draft_;
  std::vector<PlanDifficulty> editPresets_;
  std::string draftRevision_, want_;         // want_: the plan to open after the next load
  PlanCoverage coverage_;
  FitAnswer fit_;
  QString search_, filter_ = QStringLiteral("all"), message_;
  Field last_ = Field::None;                 // the value chosen last is never the one that is marked
  bool loaded_ = false, open_ = false, wasInstructor_ = false, trainerBusy_ = false, choosing_ = false;
  int waiting_ = 0, messageLevel_ = 0;
  unsigned loadNo_ = 0, coverageNo_ = 0, chooseNo_ = 0, fitNo_ = 0;  // answers to older questions are ignored
  PlannerBackend b_;                         // last: it finishes its work before the rest goes
};
