#pragma once
#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "app/LabController.h"

// Min/max waveform of the selected recording with pre-roll and tail shading.
class WaveformItem : public QQuickPaintedItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(LabController* lab READ lab WRITE setLab NOTIFY changed)
  Q_PROPERTY(QColor color MEMBER color_ NOTIFY changed)
  Q_PROPERTY(QColor shade MEMBER shade_ NOTIFY changed)
  Q_PROPERTY(QColor marker MEMBER marker_ NOTIFY changed)
  Q_PROPERTY(QColor clipColor MEMBER clip_ NOTIFY changed)
 public:
  explicit WaveformItem(QQuickItem* parent = nullptr);
  LabController* lab() const { return lab_; }
  void setLab(LabController* l);
  void paint(QPainter* p) override;
 signals:
  void changed();

 private:
  QPointer<LabController> lab_;
  QColor color_, shade_, marker_, clip_;
};

// Line icon from a path of "M x y L x y ..." commands in a square viewBox.
class IconItem : public QQuickPaintedItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QString path MEMBER path_ NOTIFY changed)
  Q_PROPERTY(qreal box MEMBER box_ NOTIFY changed)
  Q_PROPERTY(qreal stroke MEMBER stroke_ NOTIFY changed)
  Q_PROPERTY(QColor color MEMBER color_ NOTIFY changed)
 public:
  explicit IconItem(QQuickItem* parent = nullptr);
  void paint(QPainter* p) override;
 signals:
  void changed();

 private:
  QString path_;
  qreal box_ = 20, stroke_ = 1.75;
  QColor color_;
};

// Waveform of a Studio take from peaks (min, max, min, max ... in -1..1).
class PeaksItem : public QQuickPaintedItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QVariantList peaks MEMBER peaks_ NOTIFY changed)
  Q_PROPERTY(QColor color MEMBER color_ NOTIFY changed)
 public:
  explicit PeaksItem(QQuickItem* parent = nullptr);
  void paint(QPainter* p) override;
 signals:
  void changed();

 private:
  QVariantList peaks_;
  QColor color_;
};

// {slots} of a reply in the accent colour and the mono font; the rest stays as the text field has it.
class SlotHighlighter : public QSyntaxHighlighter {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QQuickTextDocument* target READ target WRITE setTarget NOTIFY changed)
  Q_PROPERTY(QColor color MEMBER color_ NOTIFY changed)
  Q_PROPERTY(QString family MEMBER family_ NOTIFY changed)
 public:
  explicit SlotHighlighter(QObject* parent = nullptr);
  QQuickTextDocument* target() const { return target_; }
  void setTarget(QQuickTextDocument* t);
 signals:
  void changed();

 protected:
  void highlightBlock(const QString& text) override;

 private:
  QPointer<QQuickTextDocument> target_;
  QColor color_;
  QString family_;
};

// Rounded frame, dashed: marks a dynamic segment without colour.
class DashFrame : public QQuickPaintedItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QColor color MEMBER color_ NOTIFY changed)
  Q_PROPERTY(qreal line MEMBER line_ NOTIFY changed)
  Q_PROPERTY(qreal radius MEMBER radius_ NOTIFY changed)
 public:
  explicit DashFrame(QQuickItem* parent = nullptr);
  void paint(QPainter* p) override;
 signals:
  void changed();

 private:
  QColor color_;
  qreal line_ = 1, radius_ = 4;
};
