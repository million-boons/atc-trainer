#pragma once
#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>
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
