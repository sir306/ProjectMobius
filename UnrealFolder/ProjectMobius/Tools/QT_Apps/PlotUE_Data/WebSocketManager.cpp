// WebSocketManager.cpp
#include "WebSocketManager.h"
#include "ChartSettings.h"
#include "ChartTableModel.h"
#include "AxisSettings.h"
#include <QtWebSockets/QWebSocket>
#include <QtWebSockets/QWebSocketProtocol>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QDebug>

WebSocketManager::WebSocketManager(const QUrl &url,
                                   const QString &unrealEngineID,
                                   ChartTableModel *model,
                                   AxisSettings    *axisSettings,
                                   ChartSettings* chartSettings,
                                   QObject        *parent)
    : QObject(parent)
    , m_url(url)
    , m_socket(new QWebSocket(QString{}, QWebSocketProtocol::VersionLatest, this))
    , m_model(model)
    , m_axisSettings(axisSettings)
    , m_chartSettings(chartSettings)
    , m_pollTimer(new QTimer(this))
    , m_status(QStringLiteral("Initializing…"))
    , m_unrealEngineID(unrealEngineID)
{
    // Build the JSON poll payload once:
    {
        QJsonObject obj;
        obj["action"] = QStringLiteral("getData");
        m_pollPayload = QJsonDocument(obj)
                            .toJson(QJsonDocument::Compact);
    }

    // Expose initial status to QML:
    QTimer::singleShot(0, this, [this]{ emit connectionStatusChanged(); });

    // onConnected → marshal to main thread
    connect(m_socket, &QWebSocket::connected, this, [this]() {
        QMetaObject::invokeMethod(this, [this] {
            QJsonObject reg {
                {"type", "register"},
                {"role", "qt"},
                {"id",   m_unrealEngineID}
            };
            m_socket->sendTextMessage(QJsonDocument(reg).toJson());
        }, Qt::QueuedConnection);
    });

    // stateChanged → call onStateChanged() on main thread
    connect(m_socket, &QWebSocket::stateChanged, this,
            [this](QAbstractSocket::SocketState s) {
                QMetaObject::invokeMethod(this, [this, s] {
                    onStateChanged(s);
                }, Qt::QueuedConnection);
            });

    // errorOccurred → call onError() on main thread
    connect(m_socket, &QWebSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError e) {
                QMetaObject::invokeMethod(this, [this, e] {
                    onError(e);
                }, Qt::QueuedConnection);
            });

    // textMessageReceived → handle on main thread
    // connect(m_socket, &QWebSocket::textMessageReceived, this,
    //         [this](const QString &msg) {
    //             QMetaObject::invokeMethod(this, [this, msg] {
    //                 onTextMessage(msg);
    //             }, Qt::QueuedConnection);
    //         });

    m_workerThread = new QThread(this);
    m_processor = new MessageProcessor(m_model, m_axisSettings, m_chartSettings);
    m_processor->moveToThread(m_workerThread);
    m_workerThread->start();

    // Modify the signal connection
    connect(m_socket, &QWebSocket::textMessageReceived, this, [this](const QString &msg) {
        QMetaObject::invokeMethod(m_processor, "handleMessage", Qt::QueuedConnection,
                                  Q_ARG(QString, msg));
    });

    // binaryMessageReceived → handle on main thread
    connect(m_socket, &QWebSocket::binaryMessageReceived, this,
            [this](const QByteArray &data) {
                QMetaObject::invokeMethod(this, [this, data] {
                    onBinaryMessage(data);
                }, Qt::QueuedConnection);
            });


    // // Timer: either poll or reconnect every second.
    // m_pollTimer->setInterval(1000);
    // connect(m_pollTimer, &QTimer::timeout,
    //         this, &WebSocketManager::sendPoll);
    // m_pollTimer->start();

    // use poll timer to get the latest playback time
    m_pollTimer->setInterval(10);           // every 10 ms = 100 Hz
    connect(m_pollTimer, &QTimer::timeout,
            this,         &WebSocketManager::sendPoll,
            Qt::QueuedConnection);

    m_pollTimer->start();

    // Kick off the first connect
    m_socket->open(m_url);
}

WebSocketManager::~WebSocketManager()
{
    qDebug() << "[WebSocketManager] Shutting down";

    // Stop the polling timer
    if (m_pollTimer && m_pollTimer->isActive()) {
        m_pollTimer->stop();
    }

    // Close the WebSocket connection
    if (m_socket && m_socket->isValid()) {
        m_socket->close();
    }

    // Cleanly stop and delete the worker thread and processor
    if (m_workerThread) {
        m_workerThread->quit();    // ask thread to stop
        m_workerThread->wait();    // block until it finishes
        delete m_processor;        // safe now that thread has exited
        m_processor = nullptr;

        delete m_workerThread;
        m_workerThread = nullptr;
    }

    qDebug() << "[WebSocketManager] Shutdown complete";
}

void WebSocketManager::onStateChanged(QAbstractSocket::SocketState s)
{
    switch (s) {
    case QAbstractSocket::UnconnectedState: m_status = "Disconnected"; break;
    case QAbstractSocket::ConnectingState:   m_status = "Connecting";   break;
    case QAbstractSocket::ConnectedState:    m_status = "Connected";    break;
    case QAbstractSocket::ClosingState:      m_status = "Closing";      break;
    default:                                 m_status = "Unknown";      break;
    }
    qDebug() << "[WS] State →" << m_status;
    emit connectionStatusChanged();
}

void WebSocketManager::onError(QAbstractSocket::SocketError)
{
    qWarning() << "[WS] Error:" << m_socket->errorString();
    // Abort to immediately go to UnconnectedState
    m_socket->abort();
}

void WebSocketManager::sendPoll()
{
    auto st = m_socket->state();
    //qDebug() << "[WS] State saved →" << m_status;
    //qDebug() << "[WS] Current State →" << st;
    if (st == QAbstractSocket::ConnectedState) {
        //qDebug() << "[WS] Polling…";
        m_socket->sendTextMessage(QString::fromUtf8(m_pollPayload));

    }
    else if (st == QAbstractSocket::UnconnectedState) {
        //qDebug() << "[WS] Reconnecting…";
        m_socket->open(m_url);
    }
    else if (st == QAbstractSocket::UnconnectedState) {
        //qDebug() << "[WS] ConnectingState";
        m_socket->open(m_url);
    }
    // if Connecting or Closing, do nothing until next tick
}


void WebSocketManager::onTextMessage(const QString &msg)
{
    //qDebug() << "[WS] Text received:" << msg;

    // QJsonParseError perr;
    // auto doc = QJsonDocument::fromJson(msg.toUtf8(), &perr);
    // if (perr.error != QJsonParseError::NoError || !doc.isObject())
    //     return;
    // auto obj    = doc.object();
    // auto action = obj.value("action").toString();

    // if (action == QLatin1String("updateCurrentTime") && obj.contains("currentTime")) {
    //     double t = obj.value("currentTime").toDouble();
    //     m_chartSettings->setCurrentTime(t);
    //     return;
    // }

    // else if (action == "appendPoint") {
    //     m_model->appendPoint(obj["x"].toDouble(), obj["y"].toDouble());
    // }
    // else if (action == "setData") {
    //     QList<QPointF> pts;
    //     for (auto v : obj["points"].toArray()) {
    //         if (v.isObject()) {
    //             auto o = v.toObject();
    //             pts.append({ o["x"].toDouble(), o["y"].toDouble() });
    //         }
    //     }
    //     m_model->setPoints(pts);
    // }
    // else if (action == "updateCurrentTime") {
    //     if (obj.contains("currentTime")){

    //         double t = obj["currentTime"].toDouble();
    //         m_chartSettings->setCurrentTime(t);
    //     }
    // }
    // else if (action == "updateAxis") {
    //     // numeric ranges
    //     if (obj.contains("xMin")) m_axisSettings->setXMin(obj["xMin"].toDouble());
    //     if (obj.contains("xMax")) m_axisSettings->setXMax(obj["xMax"].toDouble());
    //     if (obj.contains("yMin")) m_axisSettings->setYMin(obj["yMin"].toDouble());
    //     if (obj.contains("yMax")) m_axisSettings->setYMax(obj["yMax"].toDouble());

    //     // titles
    //     if (obj.contains("xTitle"))
    //         m_axisSettings->setXTitle(obj["xTitle"].toString());
    //     if (obj.contains("yTitle"))
    //         m_axisSettings->setYTitle(obj["yTitle"].toString());

    //     // grid visibility (optional)
    //     if (obj.contains("xGridVisible"))
    //         m_axisSettings->setXGridVisible(obj["xGridVisible"].toBool());
    //     if (obj.contains("yGridVisible"))
    //         m_axisSettings->setYGridVisible(obj["yGridVisible"].toBool());

    // }
    // else if (action == "updateChartTitle") {
    //     // e.g. {"action":"updateChartTitle","chartTitle":"New Title"}
    //     if (obj.contains("chartTitle")) {
    //         m_chartSettings->setTitle(obj["chartTitle"].toString());
    //     }
    // }
    // else if (action == "resetData") {
    //     // e.g. {"action":"resetData"}
    //     m_model->resetData();
    // }
}

void WebSocketManager::onBinaryMessage(const QByteArray &data)
{
    QString msg = QString::fromUtf8(data);
    //qDebug() << "[WS] Binary received:" << msg;
    // funnel into the same handler
    //onTextMessage(msg);
    m_processor->handleMessage(msg);
}
