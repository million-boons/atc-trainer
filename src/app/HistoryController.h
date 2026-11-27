#pragma once
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app/AccessController.h"
#include "app/HistoryBackend.h"
#include "app/LabController.h"

// Assessment and Debrief (04 section 5e, D-122, D-124): the stored exercises, and one of them opened. Facts only:
// no score and no pass mark. A trainee sees their own, the instructor all; guests are in no history (D-127).
// Suspected recognition errors are never added to trainee errors. GUI thread only.
class HistoryController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("created in C++")
  Q_PROPERTY(bool allowed READ allowed NOTIFY listChanged)             // somebody is logged in
  Q_PROPERTY(bool instructor READ instructor NOTIFY listChanged)
  Q_PROPERTY(bool loaded READ loaded NOTIFY listChanged)
  Q_PROPERTY(QVariantList rows READ rows NOTIFY listChanged)           // newest first
  Q_PROPERTY(QString countText READ countText NOTIFY listChanged)
  Q_PROPERTY(QString trainee READ trainee NOTIFY listChanged)          // filter; empty: all
  Q_PROPERTY(QStringList trainees READ trainees NOTIFY listChanged)    // who have results; instructor only
  Q_PROPERTY(QString mode READ mode NOTIFY listChanged)                // all, practice, assessed
  Q_PROPERTY(QString scenario READ scenario NOTIFY listChanged)        // filter; empty: all
  Q_PROPERTY(QStringList scenarios READ scenarios NOTIFY listChanged)
  Q_PROPERTY(int days READ days NOTIFY listChanged)                    // 0: all
  Q_PROPERTY(QString selected READ selected NOTIFY listChanged)
  Q_PROPERTY(QVariantList trend READ trend NOTIFY listChanged)         // {name, values}; never added together
  Q_PROPERTY(QString trendNote READ trendNote NOTIFY listChanged)      // why there is none
  Q_PROPERTY(bool open READ isOpen NOTIFY detailChanged)
  Q_PROPERTY(QVariantMap head READ head NOTIFY detailChanged)
  Q_PROPERTY(QVariantList cards READ cards NOTIFY detailChanged)       // {name, value}
  Q_PROPERTY(QStringList suspected READ suspected NOTIFY detailChanged)
  Q_PROPERTY(QStringList pttNotes READ pttNotes NOTIFY detailChanged)
  Q_PROPERTY(QString entryFilter READ entryFilter NOTIFY detailChanged)  // all, errors, ptt, stt
  Q_PROPERTY(QVariantList entries READ entries NOTIFY detailChanged)     // {no, time, who, text, outcome, level, selected}
  Q_PROPERTY(QVariantMap entry READ entry NOTIFY detailChanged)          // the selected one
  Q_PROPERTY(QVariantList reviews READ reviews NOTIFY detailChanged)     // {verdict, note, by}
  Q_PROPERTY(bool busy READ busy NOTIFY detailChanged)
  Q_PROPERTY(bool playing READ playing NOTIFY detailChanged)
  Q_PROPERTY(int messageLevel READ messageLevel NOTIFY messageChanged)
  Q_PROPERTY(QString message READ message NOTIFY messageChanged)
 public:
  static constexpr int kTrend = 20, kNote = 200;
  static QString verdictWords(const std::string& key);  // "trainee", "stt", "none" as the screen says them
  explicit HistoryController(AccessController* access = nullptr, std::function<std::int64_t()> nowMs = {}, QObject* parent = nullptr);
  void setAccess(AccessController* a);
  void setBackend(HistoryBackend b) { b_ = std::move(b); }

  bool instructor() const { return access_ && access_->instructor(); }
  bool allowed() const { return access_ && (access_->instructor() || access_->trainee()); }
  bool loaded() const { return loaded_; }
  QVariantList rows() const;
  QString countText() const;
  QString trainee() const { return trainee_; }
  QStringList trainees() const;
  QString mode() const { return mode_; }
  QString scenario() const { return scenario_; }
  QStringList scenarios() const;
  int days() const { return days_; }
  QString selected() const { return QString::fromStdString(selected_); }
  QVariantList trend() const;
  QString trendNote() const;
  bool isOpen() const { return detail_.has_value(); }
  QVariantMap head() const;
  QVariantList cards() const;
  QStringList suspected() const;
  QStringList pttNotes() const;
  QString entryFilter() const { return entryFilter_; }
  QVariantList entries() const;
  QVariantMap entry() const;
  QVariantList reviews() const;
  bool busy() const { return waiting_ > 0; }
  bool playing() const { return playing_; }
  int messageLevel() const { return messageLevel_; }
  QString message() const { return message_; }

  Q_INVOKABLE void enter();
  Q_INVOKABLE void leave();                        // the screen closes: playback stops
  Q_INVOKABLE void setTrainee(const QString& id);
  Q_INVOKABLE void setMode(const QString& m);
  Q_INVOKABLE void setScenario(const QString& name);
  Q_INVOKABLE void setDays(int d);
  Q_INVOKABLE void select(const QString& id);
  Q_INVOKABLE void openSelected();
  Q_INVOKABLE void close();                        // back to the list
  Q_INVOKABLE void setEntryFilter(const QString& f);
  Q_INVOKABLE void selectEntry(int no);
  Q_INVOKABLE void play(bool keyedOnly);
  Q_INVOKABLE void stopPlay();
  Q_INVOKABLE void reclassify(const QString& verdict, const QString& note);  // instructor; the original is kept
  Q_INVOKABLE void removeSelected();               // instructor; the screen has asked for confirmation

 signals:
  void listChanged();
  void detailChanged();
  void messageChanged();
  void reclassified();

 private:
  bool mine(const HistoryRow& r) const;            // H-1, D-127
  std::vector<const HistoryRow*> shown() const;
  bool say(int level, const QString& text);
  void queued(std::function<void()> f);
  void reload();
  void load(const std::string& id);
  void drop();
  HistoryBackend::Done answer(std::function<void()> ok, const QString& doneText = {});
  void accessChanged();

  QPointer<AccessController> access_;
  std::function<std::int64_t()> nowMs_;
  std::vector<HistoryRow> all_;
  std::optional<HistoryDetail> detail_;
  std::string selected_;
  QString who_, trainee_, mode_ = QStringLiteral("all"), scenario_, entryFilter_ = QStringLiteral("all"), message_;
  int days_ = 30, entry_ = -1, waiting_ = 0, messageLevel_ = 0;
  bool playing_ = false;
  bool loaded_ = false;
  unsigned loadNo_ = 0, openNo_ = 0;
  HistoryBackend b_;  // last
};
