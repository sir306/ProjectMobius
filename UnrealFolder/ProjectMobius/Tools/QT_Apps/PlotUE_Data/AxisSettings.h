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

// AxisSettings.h
#pragma once

#include <QObject>
#include <qqml.h>
#include <cmath>

class AxisSettings : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(qreal xMin        MEMBER m_xMin        NOTIFY xMinChanged)
    Q_PROPERTY(qreal xMax        MEMBER m_xMax        NOTIFY xMaxChanged)
    Q_PROPERTY(qreal xChartMax   MEMBER m_xChartMax   NOTIFY xChartMaxChanged)
    Q_PROPERTY(QString xTitle     MEMBER m_xTitle      NOTIFY xTitleChanged)
    Q_PROPERTY(bool xGridVisible  MEMBER m_xGridVisible NOTIFY xGridVisibleChanged)

    Q_PROPERTY(qreal yMin        MEMBER m_yMin        NOTIFY yMinChanged)
    Q_PROPERTY(qreal yMax        MEMBER m_yMax        NOTIFY yMaxChanged)
    Q_PROPERTY(qreal yChartMax   MEMBER m_yChartMax   NOTIFY yChartMaxChanged)
    Q_PROPERTY(QString yTitle     MEMBER m_yTitle      NOTIFY yTitleChanged)
    Q_PROPERTY(bool   yGridVisible MEMBER m_yGridVisible NOTIFY yGridVisibleChanged)

public:
    explicit AxisSettings(QObject* parent = nullptr) : QObject(parent) {}

    static qreal calcNiceMax(qreal min, qreal max)
    {
        qreal range = max - min;
        if (range <= 0.0)
            range = 1.0;

        qreal step = std::pow(10.0, std::floor(std::log10(range / 5.0)));
        return std::ceil(max / step) * step;
    }

    // ——— setters to use from C++ (and via QMetaObject::invokeMethod)
public slots:
    void setXMin(qreal v)        { if (m_xMin != v)   { m_xMin = v;   emit xMinChanged(v); }}

    void setXMax(qreal v)
    {
        if (m_xMax != v)
        {
            m_xMax = v;
            calcChartXMax();
            emit xMaxChanged(v);
        }
    }

    void setXTitle(const QString &t) { if (m_xTitle != t) { m_xTitle = t; emit xTitleChanged(t);} }
    void setXGridVisible(bool b)  { if (m_xGridVisible != b) { m_xGridVisible = b; emit xGridVisibleChanged(b); }}

    void setYMin(qreal v)        { if (m_yMin != v)   { m_yMin = v;   emit yMinChanged(v);} }
    void setYMax(qreal v)
    {
        if (m_yMax != v)
        {
            m_yMax = v;
            calcChartYMax();
            emit yMaxChanged(v);
        }
    }

    void setYTitle(const QString &t) { if (m_yTitle != t) { m_yTitle = t; emit yTitleChanged(t);} }
    void setYGridVisible(bool b)  { if (m_yGridVisible != b) { m_yGridVisible = b; emit yGridVisibleChanged(b);} }

    void calcChartXMax()
    {

        qreal newMax = calcNiceMax(m_xMin, m_xMax);

        if (!qFuzzyCompare(newMax, m_xChartMax)) {
            m_xChartMax = newMax;
            emit xChartMaxChanged();
        }
    }

    void calcChartYMax()
    {

        qreal newMax = calcNiceMax(m_yMin, m_yMax);

        if (!qFuzzyCompare(newMax, m_yChartMax)) {
            m_yChartMax = newMax;
            emit yChartMaxChanged();
        }
    }

    // getters
    qreal GetYMin() {return m_yMin; }
    qreal GetYMax() {return m_yMax; }

signals:
    void xMinChanged(qreal);
    void xMaxChanged(qreal);
    void xChartMaxChanged();
    void xTitleChanged(const QString&);
    void xGridVisibleChanged(bool);

    void yMinChanged(qreal);
    void yMaxChanged(qreal);
    void yChartMaxChanged();
    void yTitleChanged(const QString&);
    void yGridVisibleChanged(bool);

private:
    qreal  m_xMin         = 0;
    qreal  m_xMax         = 1;
    qreal m_xChartMax     = 1;
    qreal m_yChartMax     = 1;
    QString m_xTitle       = QStringLiteral("Time (s)");
    bool    m_xGridVisible = true;

    qreal  m_yMin         = 0;
    qreal  m_yMax         = 1;
    QString m_yTitle       = QStringLiteral("Agent Count");
    bool    m_yGridVisible = true;
};
