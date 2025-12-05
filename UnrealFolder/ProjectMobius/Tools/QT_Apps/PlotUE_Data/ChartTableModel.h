/**
 * MIT License
 * Copyright (c) 2025 ProjectMobius contributors
 * Nicholas R. Harding and Peter Thompson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 *      The above copyright notice and this permission notice shall be included in
 *      all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#pragma once
#include <QAbstractTableModel>
#include <QPointF>

/**
 * @brief Minimal table model exposing XY points to QML charts.
 *
 * The model keeps points sorted on the X axis and rejects duplicate X values
 * so charting components can assume monotonic input. It is intentionally small
 * to make it easy for Qt newcomers to trace how data flows from IPC into the
 * view layer.
 */
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

    /**
     * @brief Insert a new XY point while maintaining sorted order.
     * @param x X-coordinate (time).
     * @param y Y-coordinate (agent count).
     *
     * If a point with the same X already exists it is ignored to keep the data
     * set stable for Qt Charts.
     */
    Q_INVOKABLE void appendPoint(double x, double y);

    /**
     * @brief Replace the entire data set with a new sorted list.
     * @param pts Collection of points (unsorted is fine; we sort internally).
     */
    Q_INVOKABLE void setPoints(const QList<QPointF> &pts);

    /**
     * @brief Remove all points and reset the view.
     */
    Q_INVOKABLE void resetData();

private:
    QList<QPointF> m_points;
    QSet<double>   m_seenX;
};
