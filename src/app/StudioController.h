#pragma once
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "app/AccessController.h"
#include "app/LabController.h"
#include "app/StudioBackend.h"

// Phraseology Studio (04 section 5): what is selected, edited and recorded, and why something is refused.
// GUI thread only. It opens no transmission and never touches PTT. Only the instructor changes anything; the
// backend checks role and consent again. An undecided take stays pending and is never used (S-1, D-123).
class StudioController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("created in C++")
  Q_PROPERTY(bool ready READ ready NOTIFY listChanged)            // loaded, instructor logged in
  Q_PROPERTY(bool instructor READ instructor NOTIFY listChanged)
  // Left: list
  Q_PROPERTY(bool replies READ replies NOTIFY listChanged)        // tab: pilot replies, or ATC intents
  Q_PROPERTY(int replyCount READ replyCount NOTIFY listChanged)
  Q_PROPERTY(int intentCount READ intentCount NOTIFY listChanged)
  Q_PROPERTY(QVariantList entries READ entries NOTIFY listChanged)  // {id, status, coverage}; filtered by the search
  Q_PROPERTY(QString notice READ notice NOTIFY listChanged)
  Q_PROPERTY(bool changedHere READ changedHere NOTIFY listChanged)
  // Centre: editor
  Q_PROPERTY(QString entryId READ entryId NOTIFY entryChanged)
  Q_PROPERTY(bool validated READ validated NOTIFY entryChanged)
  Q_PROPERTY(QString statusNote READ statusNote NOTIFY entryChanged)
  Q_PROPERTY(QString usedBy READ usedBy NOTIFY entryChanged)
  Q_PROPERTY(QString text READ text NOTIFY entryChanged)
  Q_PROPERTY(QString sopRef READ sopRef NOTIFY entryChanged)
  Q_PROPERTY(int example READ example NOTIFY entryChanged)
  Q_PROPERTY(int exampleCount READ exampleCount NOTIFY entryChanged)
  Q_PROPERTY(bool dirty READ dirty NOTIFY entryChanged)
  Q_PROPERTY(bool canSave READ canSave NOTIFY entryChanged)
  Q_PROPERTY(QVariantList segments READ segments NOTIFY stripChanged)  // {text, dynamic, position, comma, state}
  Q_PROPERTY(int segment READ segment NOTIFY stripChanged)
  Q_PROPERTY(QStringList lint READ lint NOTIFY stripChanged)
  Q_PROPERTY(QString changeText READ changeText NOTIFY stripChanged)
  Q_PROPERTY(QStringList changeRows READ changeRows NOTIFY stripChanged)  // clips that become missing or unused
  Q_PROPERTY(QString poolTitle READ poolTitle NOTIFY stripChanged)
  Q_PROPERTY(QVariantList poolClips READ poolClips NOTIFY stripChanged)   // {id, value, state, selected}
  // Right: recorder
  Q_PROPERTY(bool hasClip READ hasClip NOTIFY clipChanged)
  Q_PROPERTY(QVariantMap clip READ clip NOTIFY clipChanged)       // {id, words, used, before, target, after, differs}
  Q_PROPERTY(QVariantList takes READ takes NOTIFY clipChanged)    // {number, who, state, level, info, selected}
  Q_PROPERTY(bool hasTake READ hasTake NOTIFY clipChanged)
  Q_PROPERTY(QVariantMap take READ take NOTIFY clipChanged)       // {start, end, peaks, warnings, withdrawn, removable, decidable}
  Q_PROPERTY(bool session READ session NOTIFY sessionChanged)
  Q_PROPERTY(QString speaker READ speaker NOTIFY sessionChanged)
  Q_PROPERTY(QVariantList speakers READ speakers NOTIFY sessionChanged)  // {id, may, why}
  Q_PROPERTY(bool recording READ recording NOTIFY sessionChanged)
  Q_PROPERTY(bool canRecord READ canRecord NOTIFY sessionChanged)
  Q_PROPERTY(bool pedal READ pedal NOTIFY sessionChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY sessionChanged)           // an answer is awaited
  Q_PROPERTY(QString elapsed READ elapsed NOTIFY levelChanged)
  Q_PROPERTY(double level READ level NOTIFY levelChanged)
  Q_PROPERTY(bool trainerBusy MEMBER trainerBusy_ WRITE setTrainerBusy NOTIFY sessionChanged)  // an exercise is active
  // Bottom
  Q_PROPERTY(QStringList scenarios READ scenarios NOTIFY listChanged)
  Q_PROPERTY(QString scenario READ scenario NOTIFY coverageChanged)
  Q_PROPERTY(QString coverage READ coverage NOTIFY coverageChanged)
  Q_PROPERTY(int missingCount READ missingCount NOTIFY coverageChanged)
  Q_PROPERTY(QStringList unused READ unused NOTIFY coverageChanged)
  Q_PROPERTY(QString voice READ voice NOTIFY coverageChanged)
  Q_PROPERTY(bool placeholderVoice READ placeholderVoice NOTIFY coverageChanged)
  Q_PROPERTY(QString filter READ filter NOTIFY coverageChanged)
  // Why the last request was refused, or what was done.
  Q_PROPERTY(int messageLevel READ messageLevel NOTIFY messageChanged)  // LabController.Level
  Q_PROPERTY(QString message READ message NOTIFY messageChanged)
 public:
  explicit StudioController(AccessController* access = nullptr, QObject* parent = nullptr);
  void setAccess(AccessController* a);
  void setBackend(StudioBackend b);
  bool connected() const { return bool(b_.load); }

  bool ready() const { return loaded_ && instructor(); }
  bool instructor() const { return access_ && access_->instructor(); }
  bool replies() const { return replies_; }
  int replyCount() const { return count(true); }
  int intentCount() const { return count(false); }
  QVariantList entries() const;
  QString notice() const { return QString::fromStdString(content_.notice); }
  bool changedHere() const { return content_.changedHere; }
  QString entryId() const { return QString::fromStdString(draft_.id); }
  bool validated() const { return draft_.validated; }
  QString statusNote() const;
  QString usedBy() const;
  QString text() const;
  QString sopRef() const { return QString::fromStdString(draft_.sopRef); }
  int example() const { return example_; }
  int exampleCount() const { return int(draft_.texts.size()); }
  bool dirty() const;
  bool canSave() const;
  QVariantList segments() const;
  int segment() const { return segment_; }
  QStringList lint() const;
  QString changeText() const;
  QStringList changeRows() const;
  QString poolTitle() const;
  QVariantList poolClips() const;
  bool hasClip() const { return find(clipId_) != nullptr; }
  QVariantMap clip() const;
  QVariantList takes() const;
  bool hasTake() const { return current() != nullptr; }
  QVariantMap take() const;
  bool session() const { return session_; }
  QString speaker() const { return speaker_; }
  QVariantList speakers() const;
  bool recording() const { return recording_; }
  bool canRecord() const { return session_ && hasClip() && replies_ && !busy(); }
  bool pedal() const { return pedal_; }
  bool busy() const { return waiting_ > 0; }
  QString elapsed() const;
  double level() const { return level_; }
  void setTrainerBusy(bool on);
  QStringList scenarios() const;
  QString scenario() const { return QString::fromStdString(scenario_); }
  QString coverage() const;
  int missingCount() const { return int(missing().size()); }
  QStringList unused() const;
  QString voice() const { return QString::fromStdString(content_.voice); }
  bool placeholderVoice() const { return content_.placeholderVoice; }
  QString filter() const { return filter_; }
  int messageLevel() const { return messageLevel_; }
  QString message() const { return message_; }

  Q_INVOKABLE void enter();               // the screen opens: read everything again
  Q_INVOKABLE void leave();               // the screen closes: the session ends, an edit is kept
  Q_INVOKABLE void showReplies(bool on);
  Q_INVOKABLE void search(const QString& words);
  Q_INVOKABLE void select(const QString& id);
  Q_INVOKABLE void selectExample(int i);
  Q_INVOKABLE void addExample();
  Q_INVOKABLE void setText(const QString& t);
  Q_INVOKABLE void setSopRef(const QString& t);
  Q_INVOKABLE void setValidated(bool on);
  Q_INVOKABLE void save();                // the screen has shown changeRows() first
  Q_INVOKABLE void undo();
  Q_INVOKABLE void addReply(const QString& id, const QString& text);
  Q_INVOKABLE void selectSegment(int i);
  Q_INVOKABLE void selectClip(const QString& id);
  Q_INVOKABLE void selectTake(int row);
  Q_INVOKABLE void startSession(const QString& speaker);
  Q_INVOKABLE void endSession();
  Q_INVOKABLE void setPedal(bool on);
  Q_INVOKABLE void toggleRecord();
  Q_INVOKABLE void setTrim(double start, double end);
  Q_INVOKABLE void play(const QString& mode);
  Q_INVOKABLE void stopPlay();
  Q_INVOKABLE void accept(bool primary);
  Q_INVOKABLE void reject();
  Q_INVOKABLE void deleteTake();          // the screen has asked for confirmation
  Q_INVOKABLE void setScenario(const QString& id);
  Q_INVOKABLE void setFilter(const QString& f);
  Q_INVOKABLE void stepMissing(int direction);
  Q_INVOKABLE void exportChanges(const QString& file);
  Q_INVOKABLE void clearMessage() { say(0, {}); }

 signals:
  void listChanged();
  void entryChanged();
  void entryLoaded();    // text and SOP reference were replaced: the fields take them over
  void stripChanged();
  void clipChanged();
  void sessionChanged();
  void levelChanged();
  void coverageChanged();
  void messageChanged();
  void saved();
  void added();

 private:
  enum class ClipState { Missing, Primary, Alternate };
  static ClipState stateOf(const StudioClip& c);
  static QString letter(ClipState s);
  int count(bool reply) const;
  const StudioEntry* savedEntry() const;
  const StudioClip* find(const std::string& id) const;
  const StudioTake* current() const;
  std::vector<const StudioClip*> poolOf(const StudioSegment& s) const;
  std::vector<const StudioClip*> missing() const;   // under the filter
  QString refusal(const QString& id) const;         // why this person may not record
  bool mayUse(const QString& id) const;
  bool allowed();                                   // instructor and backend; otherwise the message says so
  bool say(int level, const QString& text);         // returns false
  StudioBackend::Done answer(std::function<void()> ok, const QString& doneText = {});
  void queued(std::function<void()> f);
  void reload(bool reopen = false);  // reopen: also replaces an edit
  int segmentOf(const std::string& clip) const;
  bool matches(const StudioClip& c) const;  // the filter
  void loadClips();
  void open(const StudioEntry& e);
  void preview();
  void accessChanged();
  void dropSession(const QString& why, bool tellBackend);
  void allChanged();

  QPointer<AccessController> access_;
  StudioContent content_;
  std::vector<StudioClip> clips_;
  bool loaded_ = false, replies_ = true, wasInstructor_ = false;
  QString search_, filter_ = QStringLiteral("all"), message_, speaker_;
  std::string scenario_, clipId_, takeId_;
  StudioEntry draft_;          // what is on the screen
  std::optional<StudioPreview> preview_;
  int example_ = 0, segment_ = -1, waiting_ = 0, elapsedMs_ = 0, messageLevel_ = 0;
  unsigned startNo_ = 0;       // a session start that was left is not taken over
  unsigned previewNo_ = 0, loadNo_ = 0, clipsNo_ = 0;  // answers to older questions are ignored
  bool session_ = false, starting_ = false, recording_ = false, pedal_ = false, trainerBusy_ = false;
  double level_ = -120;
  std::string draftRevision_;  // of the data the edit started from
  StudioBackend b_;            // last: it finishes its work before the rest goes
};
