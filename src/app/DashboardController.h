#pragma once
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "app/LabController.h"
#include "app/StatusFeed.h"
#include "app/SttController.h"

// The one state behind the Dashboard tiles, the status-bar chips and the footer (D-110): all three are
// made from the same tiles, so they cannot disagree. Counts and reasons are computed, never typed (D-103).
class DashboardController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(StatusFeed* feed READ feed WRITE setFeed NOTIFY changed)
  Q_PROPERTY(SttController* stt READ stt WRITE setStt NOTIFY changed)
  Q_PROPERTY(QString exerciseModel READ exerciseModel WRITE setExerciseModel NOTIFY changed)  // model id
  // 6 x {title, level, state, line1, line2, effect, action, actionEnabled}
  Q_PROPERTY(QVariantList tiles READ tiles NOTIFY changed)
  Q_PROPERTY(QString readyText READ readyText NOTIFY changed)
  Q_PROPERTY(QString blockText READ blockText NOTIFY changed)
  Q_PROPERTY(QStringList reasons READ reasons NOTIFY changed)  // why Start is disabled
  Q_PROPERTY(bool startEnabled READ startEnabled NOTIFY changed)
  Q_PROPERTY(bool running READ running NOTIFY changed)
  Q_PROPERTY(QString modelName READ modelName NOTIFY changed)
  Q_PROPERTY(QVariantMap scenario READ scenario NOTIFY changed)
  Q_PROPERTY(QStringList missingClips READ missingClips NOTIFY changed)
  Q_PROPERTY(bool pttTesting READ pttTesting NOTIFY changed)
  Q_PROPERTY(LabController* lab READ lab WRITE setLab NOTIFY alertChanged)  // radio alerts; may be null
  Q_PROPERTY(QVariantList chips READ chips NOTIFY chipsChanged)     // 5 x {name, value, level}
  // Footer: the radio alert, then the tiles. Its level is the worse of the two, on every screen.
  Q_PROPERTY(int alertLevel READ alertLevel NOTIFY alertChanged)    // LabController.Level
  Q_PROPERTY(QString alert READ alert NOTIFY alertChanged)
 public:
  explicit DashboardController(QObject* parent = nullptr) : QObject(parent) {}

  StatusFeed* feed() const { return feed_; }
  void setFeed(StatusFeed* f);
  SttController* stt() const { return stt_; }
  void setStt(SttController* s);
  QString exerciseModel() const { return model_; }
  void setExerciseModel(const QString& id);
  LabController* lab() const { return lab_; }
  void setLab(LabController* l);

  QVariantList tiles() const { return tiles_; }
  QString readyText() const { return readyText_; }
  QString blockText() const { return blockText_; }
  QStringList reasons() const { return reasons_; }
  bool startEnabled() const { return startEnabled_; }
  bool running() const { return feed_ && feed_->inputs().running; }
  QString modelName() const;
  QVariantMap scenario() const { return feed_ ? feed_->inputs().scenario : QVariantMap(); }
  QStringList missingClips() const { return feed_ ? feed_->inputs().missingClips : QStringList(); }
  bool pttTesting() const { return feed_ && feed_->inputs().pttTesting; }

  QVariantList chips() const { return chips_; }
  int alertLevel() const { return alertLevel_; }
  QString alert() const { return alert_; }

  // Button of tile i (atc::ReadyTile order). True: QML opens the list of missing clips.
  Q_INVOKABLE bool tileAction(int i);
  Q_INVOKABLE void cancelTests();
  Q_INVOKABLE void stopExercise();

 signals:
  void changed();
  void chipsChanged();
  void alertChanged();

 private:
  void refresh();
  void refreshAlert();
  atc::ModelReady modelState() const;

  QPointer<StatusFeed> feed_;
  QPointer<SttController> stt_;
  QPointer<LabController> lab_;
  QString model_ = QStringLiteral("whisper-tiny-genprompt");  // D-414
  QVariantList tiles_, chips_;
  QString readyText_, blockText_;
  QStringList reasons_;
  bool startEnabled_ = false;
  int readyLevel_ = 0, alertLevel_ = 0;
  QString readyAlert_, alert_;
};
