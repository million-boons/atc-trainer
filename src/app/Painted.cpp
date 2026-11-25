#include "app/Painted.h"

#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <algorithm>

WaveformItem::WaveformItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
  connect(this, &WaveformItem::changed, this, [this] { update(); });
}

void WaveformItem::setLab(LabController* l) {
  if (lab_ == l) return;
  if (lab_) lab_->disconnect(this);
  lab_ = l;
  if (l) connect(l, &LabController::recordingChanged, this, [this] { update(); });
  emit changed();
}

void WaveformItem::paint(QPainter* p) {
  const auto* a = lab_ ? lab_->waveform() : nullptr;
  if (!a || a->empty()) return;
  const int w = int(width()), h = int(height()), mid = h / 2;
  const double n = double(a->size());
  const int px = int(lab_->pressFrac() * w), ex = std::min(w - 1, int(lab_->endFrac() * w));
  p->fillRect(0, 0, px, h, shade_);
  p->fillRect(ex, 0, w - ex, h, shade_);
  const double half = mid * 0.92;
  bool clipped = false;
  p->setPen(QPen(color_, 1));
  for (int x = 0; x < w; ++x) {
    const auto b = std::size_t(x * n / w), e = std::max(b + 1, std::min(a->size(), std::size_t((x + 1) * n / w)));
    if (b >= a->size()) break;
    const auto [lo, hi] = std::minmax_element(a->begin() + b, a->begin() + e);
    clipped |= *hi >= 32700 || *lo <= -32700;
    p->drawLine(x, int(mid - *hi / 32768.0 * half), x, int(mid - *lo / 32768.0 * half));
  }
  if (clipped) {
    p->setPen(QPen(clip_, 1, Qt::DashLine));
    p->drawLine(0, int(mid - half), w, int(mid - half));
    p->drawLine(0, int(mid + half), w, int(mid + half));
  }
  const double lw = std::max(2.0, height() / 60);
  p->setPen(QPen(marker_, lw));
  p->drawLine(px, 0, px, h);
  p->setPen(QPen(marker_, lw, Qt::DashLine));
  p->drawLine(ex, 0, ex, h);
}

IconItem::IconItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
  connect(this, &IconItem::changed, this, [this] { update(); });
}

void IconItem::paint(QPainter* p) {
  p->setRenderHint(QPainter::Antialiasing);
  p->scale(width() / box_, height() / box_);
  QPainterPath path;
  const QStringList t = path_.split(' ', Qt::SkipEmptyParts);
  for (int i = 0; i < t.size();) {
    const QString c = t[i++];
    if (c == "Z") {
      path.closeSubpath();
    } else if ((c == "M" || c == "L") && i + 1 < t.size()) {
      const QPointF pt(t[i].toDouble(), t[i + 1].toDouble());
      i += 2;
      if (c == "M") path.moveTo(pt); else path.lineTo(pt);
    } else {
      return;  // not a supported path
    }
  }
  p->strokePath(path, QPen(color_, stroke_, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
}

PeaksItem::PeaksItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
  connect(this, &PeaksItem::changed, this, [this] { update(); });
}

void PeaksItem::paint(QPainter* p) {
  const int n = int(peaks_.size() / 2), w = int(width()), mid = int(height() / 2);
  if (n == 0) return;
  p->setPen(QPen(color_, 1));
  for (int x = 0; x < w; ++x) {
    const int b = int(qint64(x) * n / w), e = std::max(b + 1, int(qint64(x + 1) * n / w));
    double lo = 1, hi = -1;
    for (int i = b; i < std::min(e, n); ++i) {
      lo = std::min(lo, peaks_[2 * i].toDouble());
      hi = std::max(hi, peaks_[2 * i + 1].toDouble());
    }
    p->drawLine(x, int(mid - hi * mid * 0.92), x, int(mid - lo * mid * 0.92));
  }
}

SlotHighlighter::SlotHighlighter(QObject* parent) : QSyntaxHighlighter(parent) {
  connect(this, &SlotHighlighter::changed, this, [this] { rehighlight(); });
}

void SlotHighlighter::setTarget(QQuickTextDocument* t) {
  if (target_ == t) return;
  target_ = t;
  setDocument(t ? t->textDocument() : nullptr);
  emit changed();
}

void SlotHighlighter::highlightBlock(const QString& text) {
  static const QRegularExpression slot(QStringLiteral("\\{[^}]*\\}?"));
  QTextCharFormat f;
  f.setForeground(color_);
  f.setFontFamilies({family_});
  for (auto it = slot.globalMatch(text); it.hasNext();) {
    const auto m = it.next();
    setFormat(int(m.capturedStart()), int(m.capturedLength()), f);
  }
}

DashFrame::DashFrame(QQuickItem* parent) : QQuickPaintedItem(parent) {
  connect(this, &DashFrame::changed, this, [this] { update(); });
}

void DashFrame::paint(QPainter* p) {
  p->setRenderHint(QPainter::Antialiasing);
  p->setPen(QPen(color_, line_, Qt::DashLine));
  const qreal m = line_ / 2;
  p->drawRoundedRect(QRectF(m, m, width() - line_, height() - line_), radius_, radius_);
}
