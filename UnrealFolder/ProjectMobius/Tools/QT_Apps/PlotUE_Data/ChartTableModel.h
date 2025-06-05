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
