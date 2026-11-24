#pragma once
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "app/StatusFeed.h"
#include "app/SttController.h"

// View state of the Dashboard. Counts and reasons are computed from the tiles, never typed (D-103).
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
  Q_PROPERTY(int alertLevel READ alertLevel NOTIFY changed)  // LabController.Level, for the alert lane
  Q_PROPERTY(QString alert READ alert NOTIFY changed)
 public:
  explicit DashboardController(QObject* parent = nullptr) : QObject(parent) {}

  StatusFeed* feed() const { return feed_; }
  void setFeed(StatusFeed* f);
  SttController* stt() const { return stt_; }
  void setStt(SttController* s);
  QString exerciseModel() const { return model_; }
  void setExerciseModel(const QString& id);

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

  int alertLevel() const { return alertLevel_; }
  QString alert() const { return alert_; }

  // Button of tile i (atc::ReadyTile order). True: QML opens the list of missing clips.
  Q_INVOKABLE bool tileAction(int i);
  Q_INVOKABLE void cancelTests();
  Q_INVOKABLE void stopExercise();

 signals:
  void changed();

 private:
  void refresh();
  atc::ModelReady modelState() const;

  QPointer<StatusFeed> feed_;
  QPointer<SttController> stt_;
  QString model_ = QStringLiteral("whisper-tiny-genprompt");  // D-414
  QVariantList tiles_;
  QString readyText_, blockText_;
  QStringList reasons_;
  bool startEnabled_ = false;
  int alertLevel_ = 0;
  QString alert_;
};
