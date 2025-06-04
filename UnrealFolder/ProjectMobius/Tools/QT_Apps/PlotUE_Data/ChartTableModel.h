#pragma once
#include <QAbstractTableModel>
#include <QPointF>

class ChartTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit ChartTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& = {}) const override  { return m_points.count(); }
    int columnCount(const QModelIndex& = {}) const override { return 2; }

    QVariant data(const QModelIndex &idx, int role) const override {
        if (!idx.isValid() || role != Qt::DisplayRole) return {};
        const auto &p = m_points.at(idx.row());
        return idx.column() == 0 ? QVariant(p.x()) : QVariant(p.y());
    }

    Q_INVOKABLE void appendPoint(double x, double y);
    Q_INVOKABLE void setPoints(const QList<QPointF> &pts);
    Q_INVOKABLE void resetData();

private:
    QList<QPointF> m_points;
    QSet<double>   m_seenX;
};
