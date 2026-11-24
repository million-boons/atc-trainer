#pragma once
#include <QAbstractListModel>
#include <QList>
#include <QVariant>

// Small list model: named roles, newest row first when prepended.
class RowModel : public QAbstractListModel {
 public:
  explicit RowModel(QList<QByteArray> roles, int cap, QObject* parent = nullptr)
      : QAbstractListModel(parent), roles_(std::move(roles)), cap_(cap) {}

  int rowCount(const QModelIndex& p = {}) const override { return p.isValid() ? 0 : int(rows_.size()); }
  QVariant data(const QModelIndex& i, int role) const override {
    const int c = role - Qt::UserRole;
    if (!i.isValid() || i.row() >= rows_.size() || c < 0 || c >= roles_.size()) return {};
    return rows_[i.row()].value(c);
  }
  QHash<int, QByteArray> roleNames() const override {
    QHash<int, QByteArray> h;
    for (int i = 0; i < roles_.size(); ++i) h[Qt::UserRole + i] = roles_[i];
    return h;
  }
  void prepend(QVariantList row) {
    beginInsertRows({}, 0, 0);
    rows_.prepend(std::move(row));
    endInsertRows();
    if (rows_.size() > cap_) {
      beginRemoveRows({}, cap_, int(rows_.size()) - 1);
      rows_.resize(cap_);
      endRemoveRows();
    }
  }
  void set(int row, int col, QVariant v) {
    if (row < 0 || row >= rows_.size()) return;
    rows_[row][col] = std::move(v);
    emit dataChanged(index(row), index(row));
  }
  void remove(int row) {
    if (row < 0 || row >= rows_.size()) return;
    beginRemoveRows({}, row, row);
    rows_.removeAt(row);
    endRemoveRows();
  }
  const QVariantList& row(int i) const { return rows_[i]; }
  void reset(QList<QVariantList> rows = {}) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
  }

 private:
  QList<QByteArray> roles_;
  QList<QVariantList> rows_;
  int cap_;
};
