#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <cstdint>
#include <vector>

#include "app/RowModel.h"
#include "core/TxDisplay.h"

// What a finished recording shows in the Speech Lab.
struct LabRecording {
  int id = 0;
  QString text;
  QString resultInfo;  // "model  0.34 s"; empty until recognised
  QString modelConfig; // exact model and settings (D-39)
  std::vector<std::int16_t> audio;  // pre-roll + keyed speech + tail
  double pressFrac = 0, endFrac = 1;
  QString startLabel = "PRESS", endLabel, tailLabel;
  QVariantList checks;  // 4 x {level, title, detail}
};

// View state of the PTT and Speech Lab. A backend (mock or real) calls the setters on
// the GUI thread; QML only reads properties and calls the invokables.
class LabController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("created in C++")
  Q_PROPERTY(Tone txTone READ txTone NOTIFY txChanged)
  Q_PROPERTY(QString txLabel READ txLabel NOTIFY txChanged)
  Q_PROPERTY(QString txSub READ txSub NOTIFY txChanged)
  Q_PROPERTY(Level txSubLevel READ txSubLevel NOTIFY txChanged)
  Q_PROPERTY(QString txTime READ txTime NOTIFY txTimeChanged)
  Q_PROPERTY(bool rxActive READ rxActive NOTIFY rxChanged)
  Q_PROPERTY(QString rxSub READ rxSub NOTIFY rxChanged)
  Q_PROPERTY(double micDbfs READ micDbfs NOTIFY micChanged)
  Q_PROPERTY(double micLow MEMBER micLow CONSTANT)
  Q_PROPERTY(double micHigh MEMBER micHigh CONSTANT)
  Q_PROPERTY(double micTarget MEMBER micTarget CONSTANT)
  Q_PROPERTY(QString textTag MEMBER textTag CONSTANT)
  Q_PROPERTY(QString pttSource READ pttSource NOTIFY pttSourceChanged)
  Q_PROPERTY(bool rightDown READ rightDown NOTIFY buttonsChanged)
  Q_PROPERTY(bool leftDown READ leftDown NOTIFY buttonsChanged)
  Q_PROPERTY(QString text READ text NOTIFY textChanged)
  Q_PROPERTY(QString textState READ textState NOTIFY textChanged)
  Q_PROPERTY(QString resultInfo READ resultInfo NOTIFY textChanged)  // model and release-to-text
  Q_PROPERTY(bool textPartial READ textPartial NOTIFY textChanged)
  Q_PROPERTY(bool textPlaceholder READ textPlaceholder NOTIFY textChanged)
  Q_PROPERTY(Level noticeLevel READ noticeLevel NOTIFY noticeChanged)
  Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
  Q_PROPERTY(bool pilotCut READ pilotCut NOTIFY noticeChanged)
  Q_PROPERTY(Level alertLevel READ alertLevel NOTIFY noticeChanged)
  Q_PROPERTY(QString alert READ alert NOTIFY noticeChanged)
  Q_PROPERTY(bool hasRecording READ hasRecording NOTIFY recordingChanged)
  Q_PROPERTY(QString recordingInfo READ recordingInfo NOTIFY recordingChanged)
  Q_PROPERTY(QVariantList checks READ checks NOTIFY recordingChanged)
  Q_PROPERTY(double pressFrac READ pressFrac NOTIFY recordingChanged)
  Q_PROPERTY(double endFrac READ endFrac NOTIFY recordingChanged)
  Q_PROPERTY(QString startLabel READ startLabel NOTIFY recordingChanged)
  Q_PROPERTY(QString endLabel READ endLabel NOTIFY recordingChanged)
  Q_PROPERTY(QString tailLabel READ tailLabel NOTIFY recordingChanged)
  Q_PROPERTY(int selectedRow READ selectedRow NOTIFY recordingChanged)
  Q_PROPERTY(QAbstractItemModel* events READ events CONSTANT)
  Q_PROPERTY(QAbstractItemModel* words READ words CONSTANT)
  Q_PROPERTY(QAbstractItemModel* recordings READ recordings CONSTANT)

 public:
  enum Level { None, Ok, Caution, Fault };
  Q_ENUM(Level)
  enum Tone { Neutral, Active, Busy, Bad };
  Q_ENUM(Tone)

  explicit LabController(QObject* parent = nullptr);

  // Configurable starting values (D-13, D-22). Set before QML loads.
  double micLow = -20, micHigh = -6, micTarget = -12;
  QString textTag = "PLACEHOLDER";              // tag beside recognised text
  QString idleHint = "Hold RIGHT to talk";

  // Setters for the backend.
  void setTx(atc::TxDisplay s, int txId);
  void setTxSeconds(double s);  // < 0 hides the timer
  void setRx(bool active);
  void setPilotCut(bool on);
  void setMic(double dbfs);
  void setPttSource(const QString& s);
  void setButtons(bool right, bool left);
  void setText(const QString& t, const QString& state, bool partial, const QString& resultInfo, bool placeholder);
  void setNotice(Level l, const QString& text);
  void setAlert(Level l, const QString& text);
  void addEvent(double t, const QString& src, const QString& edge, const QString& detail, bool warn);
  void setWords(const QStringList& words, const QList<double>& conf);
  void addRecording(LabRecording rec, const QString& time);
  void setResult(int id, const QString& text, const QString& resultInfo, const QString& modelConfig);
  void clear();
  void showRecording(int row);  // -1: none

  const std::vector<std::int16_t>* waveform() const;

  Tone txTone() const;
  QString txLabel() const;
  QString txSub() const;
  Level txSubLevel() const { return tx_ == atc::TxDisplay::Queued ? Caution : None; }
  QString txTime() const { return txTime_; }
  bool rxActive() const { return rx_; }
  QString rxSub() const;
  double micDbfs() const { return mic_; }
  QString pttSource() const { return pttSource_; }
  bool rightDown() const { return right_; }
  bool leftDown() const { return left_; }
  QString text() const { return text_; }
  QString textState() const { return textState_; }
  QString resultInfo() const { return resultInfo_; }
  bool textPartial() const { return partial_; }
  bool textPlaceholder() const { return placeholder_; }
  Level noticeLevel() const { return noticeLevel_; }
  QString notice() const { return notice_; }
  bool pilotCut() const { return pilotCut_; }
  Level alertLevel() const { return alertLevel_; }
  QString alert() const { return alert_; }
  bool hasRecording() const { return sel_ >= 0; }
  QString recordingInfo() const;
  QVariantList checks() const;
  double pressFrac() const { return sel_ < 0 ? 0 : recs_[sel_].pressFrac; }
  double endFrac() const { return sel_ < 0 ? 1 : recs_[sel_].endFrac; }
  QString startLabel() const { return sel_ < 0 ? QString() : recs_[sel_].startLabel; }
  QString endLabel() const { return sel_ < 0 ? QString() : recs_[sel_].endLabel; }
  QString tailLabel() const { return sel_ < 0 ? QString() : recs_[sel_].tailLabel; }
  int selectedRow() const { return sel_; }
  QAbstractItemModel* events() { return &events_; }
  QAbstractItemModel* words() { return &words_; }
  QAbstractItemModel* recordings() { return &recModel_; }

  Q_INVOKABLE void selectRecording(int row) { showRecording(row); }
  // Requests to the backend.
 signals:
  void pressRequested(const QString& source);
  void releaseRequested(const QString& source);
  void playRequested(bool keyedPartOnly);
  void exportRequested();

  void txChanged();
  void txTimeChanged();
  void rxChanged();
  void micChanged();
  void pttSourceChanged();
  void buttonsChanged();
  void textChanged();
  void noticeChanged();
  void recordingChanged();

 private:
  atc::TxDisplay tx_ = atc::TxDisplay::Idle;
  int txId_ = 0;
  QString txTime_;
  bool rx_ = false, pilotCut_ = false;
  double mic_ = -120;
  QString pttSource_;
  bool right_ = false, left_ = false;
  QString text_, textState_, resultInfo_;
  bool partial_ = false, placeholder_ = false;
  Level noticeLevel_ = None, alertLevel_ = Ok;
  QString notice_, alert_;
  RowModel events_, words_, recModel_;
  std::vector<LabRecording> recs_;  // same order as recModel_
  int sel_ = -1;
};
