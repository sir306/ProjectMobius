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

#include "MessageProcessor.h"
#include "ChartTableModel.h"
#include "AxisSettings.h"
#include "ChartSettings.h"
#include "Logger.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMetaObject>
#include <QDebug>
#include <QThread>

// Constructor
MessageProcessor::MessageProcessor(ChartTableModel* model, AxisSettings* axis, ChartSettings* settings)
    : m_model(model), m_axis(axis), m_settings(settings) {}

void MessageProcessor::registerHandler(QStringView actionName, std::function<void(const QJsonObject&)> handler) {
    m_dynamicHandlers.insert({ actionName.toString(), std::move(handler) });
}

void MessageProcessor::handleBuiltIn(ActionType type, const QJsonObject& obj) {
    switch (type) {
    case ActionType::AppendPoint: {
        double x = obj["x"].toDouble();
        double y = obj["y"].toDouble();
        QMetaObject::invokeMethod(m_model, "appendPoint", Qt::QueuedConnection,
                                  Q_ARG(double, x), Q_ARG(double, y));
        break;
    }

    case ActionType::SetData: {
        QList<QPointF> points;
        const QJsonArray arr = obj["points"].toArray();
        points.reserve(arr.size());
        for (int i = 0; i < arr.size(); ++i) {
            const QJsonObject o = arr.at(i).toObject();
            points.append(QPointF(o["x"].toDouble(), o["y"].toDouble()));
        }

        LOG_INFO(QString("SetData: received %1 points").arg(points.size()));

        QMetaObject::invokeMethod(m_model, "setPoints", Qt::QueuedConnection,
                                  Q_ARG(QList<QPointF>, points));
        break;
    }

    case ActionType::UpdateCurrentTime: {
        // double t = static_cast<double>(obj["time"].toDouble())/10;
        // QMetaObject::invokeMethod(m_settings, "setCurrentTime", Qt::QueuedConnection,
        //                           Q_ARG(qreal, t));
        break;
    }

    case ActionType::UpdateAgentCount: {
        // qint32 t = obj["agentCount"].toInteger();
        // QMetaObject::invokeMethod(m_settings, "setCurrentAgentCount", Qt::QueuedConnection,
        //                           Q_ARG(qint32, t));
        break;
    }
    case ActionType::UpdateLiveData: {
        qint32 t = obj["currentTime"].toInt();
        qint32 c = obj["count"].toInt();

        LOG_DEBUG(QString("UpdateLiveData: time=%1, count=%2").arg(t).arg(c));

        QMetaObject::invokeMethod(m_settings,
                                  "updateLiveData",
                                  Qt::QueuedConnection,
                                  Q_ARG(qint32, t),
                                  Q_ARG(qint32, c));
        break;
    }

    case ActionType::ResetData: {
        QMetaObject::invokeMethod(m_model, "resetData", Qt::QueuedConnection);
        break;
    }

    case ActionType::UpdateAxis: {
        LOG_INFO("UpdateAxis received");
        if (obj.contains("xMin")) QMetaObject::invokeMethod(m_axis, "setXMin", Qt::QueuedConnection,
                                      Q_ARG(qreal, obj["xMin"].toDouble()));
        if (obj.contains("xMax")) QMetaObject::invokeMethod(m_axis, "setXMax", Qt::QueuedConnection,
                                      Q_ARG(qreal, obj["xMax"].toDouble()));
        if (obj.contains("yMin")) QMetaObject::invokeMethod(m_axis, "setYMin", Qt::QueuedConnection,
                                      Q_ARG(qreal, obj["yMin"].toDouble()));
        if (obj.contains("yMax")) QMetaObject::invokeMethod(m_axis, "setYMax", Qt::QueuedConnection,
                                      Q_ARG(qreal, obj["yMax"].toDouble()));

        if (obj.contains("xTitle")) QMetaObject::invokeMethod(m_axis, "setXTitle", Qt::QueuedConnection,
                                      Q_ARG(QString, obj["xTitle"].toString()));
        if (obj.contains("yTitle")) QMetaObject::invokeMethod(m_axis, "setYTitle", Qt::QueuedConnection,
                                      Q_ARG(QString, obj["yTitle"].toString()));

        if (obj.contains("xGridVisible")) QMetaObject::invokeMethod(m_axis, "setXGridVisible", Qt::QueuedConnection,
                                      Q_ARG(bool, obj["xGridVisible"].toBool()));
        if (obj.contains("yGridVisible")) QMetaObject::invokeMethod(m_axis, "setYGridVisible", Qt::QueuedConnection,
                                      Q_ARG(bool, obj["yGridVisible"].toBool()));
        break;
    }

    case ActionType::UpdateChartTitle: {
        if (obj.contains("chartTitle")) {
            QString title = obj["chartTitle"].toString();
            LOG_INFO(QString("UpdateChartTitle: %1").arg(title));
            QMetaObject::invokeMethod(m_settings, "setTitle", Qt::QueuedConnection,
                                      Q_ARG(QString, title));
        }
        break;
    }
    case ActionType::Heartbeat:
        // ✅ Just ignore heartbeats silently
        break;

    case ActionType::Unknown:
    default:
        break;
    }
}


void MessageProcessor::handleMessage(const QString &msg)
{
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(msg.toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError)
    {
        LOG_ERROR(QString("JSON parse error at offset %1: %2").arg(perr.offset).arg(perr.errorString()));
        return;
    }

    if (!doc.isObject())
    {
        LOG_ERROR("Received JSON is not an object");
        return;
    }

    const QJsonObject obj = doc.object();
    const QString actionStr = obj["action"].toString();

    if (actionStr.isEmpty())
    {
        LOG_WARN(QString("Received message with no action field: %1").arg(msg));
        return;
    }

    LOG_DEBUG(QString("Processing action: %1").arg(actionStr));

    const std::string_view actionView(actionStr.toUtf8().constData(), actionStr.size());
    ActionType type = actionFromString(actionView);

    if (type != ActionType::Unknown) {
        handleBuiltIn(type, obj);
        return;
    }

    auto it = m_dynamicHandlers.find(actionStr);
    if (it != m_dynamicHandlers.end()) {
        it->second(obj);
    } else {
        LOG_WARN(QString("Unknown action: %1").arg(actionStr));
    }
}

