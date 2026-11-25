#include "app/LabController.h"

#include <algorithm>

using atc::TxDisplay;

namespace {
constexpr int kMaxRecordings = 50;
}  // namespace

LabController::LabController(QObject* parent)
    : QObject(parent),
      events_({"time", "source", "edge", "detail", "warn"}, 200, this),
      words_({"word", "conf", "low"}, 200, this),
      recModel_({"recId", "time", "duration", "text", "level", "status", "result"}, kMaxRecordings, this) {
  alert_ = tr("Ready");
}

void LabController::setTx(TxDisplay s, int txId) {
  if (s == tx_ && txId == txId_) return;
  tx_ = s;
  txId_ = txId;
  emit txChanged();
}

LabController::Tone LabController::txTone() const {
  switch (tx_) {
    case TxDisplay::Idle: return Neutral;
    case TxDisplay::Tx: return Active;
    case TxDisplay::Tail:
    case TxDisplay::Queued:
    case TxDisplay::Wait: return Busy;
    default: return Bad;
  }
}

QString LabController::txLabel() const {
  switch (tx_) {
    case TxDisplay::Idle: return tr("IDLE");
    case TxDisplay::Tx: return tr("TX");
    case TxDisplay::Tail: return tr("TAIL");
    case TxDisplay::Queued:
    case TxDisplay::Wait: return tr("WAIT");
    case TxDisplay::Blocked: return tr("BLOCKED");
    case TxDisplay::Stuck: return tr("STUCK");
    case TxDisplay::NoPtt: return tr("NO PTT");
    case TxDisplay::NoHold: return tr("NO HOLD");
  }
  return {};
}

QString LabController::txSub() const {
  switch (tx_) {
    case TxDisplay::Idle: return micOpen_ ? tr("Mic open – not transmitting") : idleHint;
    case TxDisplay::Tx: return tr("Transmitting");
    case TxDisplay::Tail: return tr("Closing transmission");
    case TxDisplay::Wait: return tr("Recognising TX %1").arg(txId_);
    case TxDisplay::Queued: return tr("Queued – pilot speaking");
    case TxDisplay::Blocked: return rx_ ? tr("Press discarded: pilot speaking") : tr("Release, then press again");
    case TxDisplay::Stuck: return tr("Release the pedal to reset");
    case TxDisplay::NoPtt: return tr("PTT not connected");
    case TxDisplay::NoHold: return tr("Pulse-type pedal suspected");
  }
  return {};
}

void LabController::setTxSeconds(double s) {
  const QString t = s < 0 ? QString() : QStringLiteral("%1 s").arg(s, 4, 'f', 1, QLatin1Char('0'));
  if (t == txTime_) return;
  txTime_ = t;
  emit txTimeChanged();
}

void LabController::setRx(bool active) {
  if (rx_ == active) return;
  rx_ = active;
  emit rxChanged();
  emit txChanged();  // BLOCKED sub-text depends on it
}

QString LabController::rxSub() const { return rx_ ? tr("Pilot speaking") : tr("Pilot silent"); }

void LabController::setPilotCut(bool on) {
  if (pilotCut_ == on) return;
  pilotCut_ = on;
  emit noticeChanged();
}

void LabController::setMic(double dbfs) {
  if (qFuzzyCompare(mic_, dbfs)) return;
  mic_ = dbfs;
  emit micChanged();
}

void LabController::setMicOpen(bool open) {
  if (open == micOpen_) return;
  micOpen_ = open;
  emit txChanged();
}

void LabController::setPttSource(const QString& s) {
  if (s == pttSource_) return;
  pttSource_ = s;
  emit pttSourceChanged();
}

void LabController::setButtons(bool right, bool left) {
  if (right == right_ && left == left_) return;
  right_ = right;
  left_ = left;
  emit buttonsChanged();
}

void LabController::setText(const QString& t, const QString& state, bool partial, const QString& resultInfo,
                            bool placeholder) {
  text_ = t;
  textState_ = state;
  partial_ = partial;
  resultInfo_ = resultInfo;
  placeholder_ = placeholder;
  emit textChanged();
}

void LabController::setNotice(Level l, const QString& text) {
  if (l == noticeLevel_ && text == notice_) return;
  noticeLevel_ = l;
  notice_ = text;
  emit noticeChanged();
}

void LabController::setAlert(Level l, const QString& text, bool ptt) {
  if (l == alertLevel_ && text == alert_ && ptt == alertPtt_) return;
  alertPtt_ = ptt;
  alertLevel_ = l;
  alert_ = text;
  emit noticeChanged();
}

void LabController::addEvent(double t, const QString& src, const QString& edge, const QString& detail, bool warn) {
  events_.prepend({QString::number(t, 'f', 3), src, edge, detail, warn});
}

void LabController::setWords(const QStringList& words, const QList<double>& conf) {
  QList<QVariantList> rows;
  for (int i = 0; i < words.size(); ++i) {
    const double c = conf.value(i, 0);
    rows.append({words[i], c, c < 0.7});
  }
  words_.reset(rows);
}

void LabController::addRecording(LabRecording rec, const QString& time) {
  int level = Ok;
  QString status = tr("Clean");
  for (const QVariant& v : rec.checks) {  // worst check names the row
    const QVariantMap m = v.toMap();
    if (m["level"].toInt() > level) {
      level = m["level"].toInt();
      status = m["title"].toString();
    }
  }
  recModel_.prepend({rec.id, time, QStringLiteral("%1 s").arg(rec.audio.size() / 16000.0, 0, 'f', 1),
                     rec.text.isEmpty() ? tr("(no text)") : rec.text, level, status, rec.resultInfo});
  recs_.insert(recs_.begin(), std::move(rec));
  if (recs_.size() > kMaxRecordings) recs_.resize(kMaxRecordings);
  sel_ = 0;
  emit recordingChanged();
}

void LabController::setResult(int id, const QString& text, const QString& resultInfo, const QString& modelConfig) {
  for (int i = 0; i < int(recs_.size()); ++i) {
    if (recs_[i].id != id) continue;
    recs_[i].text = text;
    recs_[i].resultInfo = resultInfo;
    recs_[i].modelConfig = modelConfig;
    recModel_.set(i, 3, text.isEmpty() ? tr("(no text)") : text);
    recModel_.set(i, 6, resultInfo);
    return;
  }
}

void LabController::dropRecording(int id) {
  for (int i = 0; i < int(recs_.size()); ++i) {
    if (recs_[i].id != id) continue;
    recs_.erase(recs_.begin() + i);
    recModel_.remove(i);
    if (sel_ == i) sel_ = -1;
    else if (sel_ > i) --sel_;
    emit recordingChanged();
    return;
  }
}

void LabController::showRecording(int row) {
  if (row >= int(recs_.size())) return;
  sel_ = row;
  emit recordingChanged();
}

void LabController::clear() {
  events_.reset();
  words_.reset();
  recModel_.reset();
  recs_.clear();
  sel_ = -1;
  emit recordingChanged();
  setText({}, tr("Waiting"), false, {}, false);
  setNotice(None, {});
  setAlert(Ok, tr("Ready"));
  setPilotCut(false);
}

const std::vector<std::int16_t>* LabController::waveform() const { return sel_ < 0 ? nullptr : &recs_[sel_].audio; }

QString LabController::recordingInfo() const {
  if (sel_ < 0) return {};
  return QStringLiteral("TX %1  %2 s  16 kHz mono").arg(recs_[sel_].id).arg(recs_[sel_].audio.size() / 16000.0, 0, 'f', 2);
}

QVariantList LabController::checks() const {
  if (sel_ >= 0) return recs_[sel_].checks;
  const QVariantMap none{{"level", int(None)}, {"title", "-"}, {"detail", ""}};
  return {none, none, none, none};
}
