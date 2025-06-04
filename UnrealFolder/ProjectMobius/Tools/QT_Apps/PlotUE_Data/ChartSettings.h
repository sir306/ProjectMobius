#pragma once

#include <QObject>
#include <qqml.h>

class ChartSettings : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString title MEMBER m_title NOTIFY titleChanged)

    // flag for showing all the scatter points or only the major y points
    Q_PROPERTY(bool showAllPoints READ showAllPoints WRITE setShowAllPoints NOTIFY showAllPointsChanged)

    //
    Q_PROPERTY(QString statusDisplay READ statusDisplay NOTIFY currentLiveDataChanged)

    // property for the  live play bar
    Q_PROPERTY(qreal playbarX MEMBER m_playbarX NOTIFY playbarPosChanged)

public:
    explicit ChartSettings(QObject* parent = nullptr)
        : QObject(parent)
    {}

    bool showAllPoints() const { return m_showAllPoints; }

    // update current playback marker time
    qreal currentTime() const { return m_currentTime; }

    QString statusDisplay() const {
        return m_statusDisplay;
    }

    void buildUpdatedStatusDisplay() {
        double trimmedTime = m_currentIntTime / 10.0;
        m_statusDisplay = QStringLiteral("%1 occupants at time: %2s")
                               .arg(m_currentAgentCount)
                              .arg(QString::number(trimmedTime, 'f', 1));
    }


    qreal playbarX()    const { return m_playbarX; }
    qreal plotAreaX() const    { return m_plotAreaX; }
    qreal plotWidth() const    { return m_plotWidth; }
    qreal axisMin()     const { return m_playbarMin; }
    qreal axisMax()     const { return m_playbarMax; }

public slots:
    void setTitle(const QString& t) {
        if (m_title != t) {
            m_title = t;
            emit titleChanged(t);
        }
    }

    void setShowAllPoints(bool v) {
        if (m_showAllPoints == v) return;
        m_showAllPoints = v;
        emit showAllPointsChanged();
    }
// these should be combined into 1 method over 2 then whenever one of the values changes it is emitted and we dont build string twice and emit twice!!!
    void setCurrentTime(qreal t) {
        if (!qFuzzyCompare(m_currentTime, t)) {
            m_currentTime = t;
            buildUpdatedStatusDisplay();
        }
    }

    void setCurrentAgentCount(qint32 t){
        if(m_currentAgentCount != t){
            m_currentAgentCount = t;
            buildUpdatedStatusDisplay();
        }
    }

    void updateLiveData(qint32 time, qint32 count){
        if(time != m_currentIntTime || count != m_currentAgentCount)
        {
            m_currentIntTime = time;
            qreal t = static_cast<double>(time)/10;
            m_currentTime = t;
            m_currentAgentCount = count;
            buildUpdatedStatusDisplay();
            updatePlaybarX();
            emit currentLiveDataChanged();
        }
    }

    void updatePlaybarX()
    {
        recomputeScale();
        recomputePlaybarX();
    }

    // batch‐update all four metrics whenever ANY of them changes
    Q_INVOKABLE void setPlotMetrics(qreal originX, qreal width, qreal min, qreal max)
    {
        // check if all already equal
        if(m_plotAreaX == originX && m_plotWidth == width && m_playbarMin == min && m_playbarMax == max)
        {
            return;
        }

        m_plotAreaX = originX;
        m_plotWidth   = width;
        m_playbarMin     = min;
        m_playbarMax     = max;

        recomputeScale();
        recomputePlaybarX();
    }


    void setPlotAreaX(qreal x) {
        if (qFuzzyCompare(m_plotAreaX, x)) return;
        m_plotAreaX = x;
        recomputeScale();
        recomputePlaybarX();
        //emit plotAreaXChanged();
    }

    void setPlotWidth(qreal w) {
        if (qFuzzyCompare(m_plotWidth, w)) return;
        m_plotWidth = w;
        recomputeScale();
        recomputePlaybarX();
        //emit plotWidthChanged();
    }

    void setAxisMin(qreal mn) {
        if (qFuzzyCompare(m_playbarMin, mn)) return;
        m_playbarMin = mn;
        recomputeScale();
        recomputePlaybarX();
        //emit axisMinChanged();
    }

    void setAxisMax(qreal mx) {
        if (qFuzzyCompare(m_playbarMax, mx)) return;
        m_playbarMax = mx;
        recomputeScale();
        recomputePlaybarX();
        //emit axisMaxChanged();
    }


signals:
    void titleChanged(const QString&);

    void showAllPointsChanged();

    // notify current time changed
    void currentLiveDataChanged();

    void currentAgentCountChanged();

    void playbarPosChanged();
    //void plotAreaXChanged();
    //void plotWidthChanged();
    //void axisMinChanged();
    //void axisMaxChanged();


private:

    void recomputeScale() {
        qreal range = m_playbarMax - m_playbarMin;
        m_scale = (range>0) ? (m_plotWidth / range) : 0;
    }
    void recomputePlaybarX() {

        qreal oldX = m_playbarX;

        qreal newX = m_plotAreaX
                     + (m_currentTime - m_playbarMin) * m_scale;

        if(!qFuzzyCompare(oldX, newX))
        {
            m_playbarX = newX;
            emit currentLiveDataChanged();
        }




        emit playbarPosChanged();
    }
    QString m_title = "";

    // this will be the current sim time to show users where the current playback data is in relation to the graph
    qreal m_currentTime = 0.0;
    qint32 m_currentIntTime = 0;

    qint32 m_currentAgentCount;

    QString m_statusDisplay = "";

    qreal m_playbarX = 0.0;
    qreal m_playbarMin = 0.0;
    qreal m_playbarMax = 1.0;
    qreal m_playbarRange = 1.0;
    qreal m_plotWidth = 1.0;
    qreal m_plotAreaX = 1.0;
    qreal   m_scale       = 1;   // = plotWidth/(axisMax–axisMin)

    bool m_showAllPoints = false;
};
