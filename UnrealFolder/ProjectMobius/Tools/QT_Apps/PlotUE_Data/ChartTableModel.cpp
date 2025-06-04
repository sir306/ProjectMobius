#include "ChartTableModel.h"
#include <algorithm>

ChartTableModel::ChartTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    //Debug Points
    // setPoints({
    //     {0.0,0.0},
    //     {1.0,5.0},
    //     {2.0,2.5},
    //     {2.5,10.0},
    //     {3.0,4.0},
    //     {24, 102}
    // });
}

void ChartTableModel::appendPoint(double x, double y)
{
    // 1) skip duplicates
    if (m_seenX.contains(x))
        return;

    // 2) find insertion index to keep m_points sorted by x
    int row = 0;
    while (row < m_points.size() && m_points[row].x() < x)
        ++row;

    // 3) insert at that position
    beginInsertRows(QModelIndex(), row, row);
    m_points.insert(row, QPointF(x, y));
    endInsertRows();

    // 4) mark seen
    m_seenX.insert(x);
}

void ChartTableModel::setPoints(const QList<QPointF> &pts)
{
    // sort incoming pts by x
    QList<QPointF> sorted = pts;
    std::sort(sorted.begin(), sorted.end(),
              [](const QPointF &a, const QPointF &b){
                  return a.x() < b.x();
              });

    beginResetModel();
    m_points = std::move(sorted);
    m_seenX.clear();
    for (const auto &p : m_points)
        m_seenX.insert(p.x());
    endResetModel();
}

void ChartTableModel::resetData()
{
    beginResetModel();
    m_points.clear();
    m_seenX.clear();
    endResetModel();
}
