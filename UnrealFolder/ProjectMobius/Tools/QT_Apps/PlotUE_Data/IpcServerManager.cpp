// MIT © ProjectMobius contributors
#include "IpcServerManager.h"
#include "ChartTableModel.h"
#include "AxisSettings.h"
#include "ChartSettings.h"
#include "Logger.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QDebug>
#include <QThread>

static inline quint32 ReadLE32(const char* p) {
    quint32 v;
    memcpy(&v, p, 4);
    return v;
}

IpcServerManager::IpcServerManager(const QString& endpointName,
                                   const QString& unrealEngineID,
                                   ChartTableModel* model,
                                   AxisSettings* axisSettings,
                                   ChartSettings* chartSettings,
                                   QObject* parent)
    : QObject(parent)
    , m_endpoint(endpointName)
    , m_unrealEngineID(unrealEngineID)
{
    LOG_INFO(QString("IpcServerManager starting on endpoint: %1").arg(endpointName));


    // Spin up worker thread
    m_workerThread = new QThread(this);
    m_processor = new MessageProcessor(model, axisSettings, chartSettings);
    m_processor->moveToThread(m_workerThread);
    m_workerThread->start();

    // Start listening
    startListening();

    LOG_INFO("IpcServerManager initialized (push-only mode)");
}

IpcServerManager::~IpcServerManager()
{
    LOG_INFO("IpcServerManager shutting down");
    // m_pollTimer.stop();  // ❌ Remove this line too

    for (auto* s : m_clients) {
        if (s->state() == QLocalSocket::ConnectedState)
            s->disconnectFromServer();
        s->deleteLater();
    }
    m_clients.clear();

    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
        delete m_processor; m_processor = nullptr;
        delete m_workerThread; m_workerThread = nullptr;
    }
    LOG_INFO("IpcServerManager shutdown complete");
}

void IpcServerManager::startListening()
{
    // Clean stale socket on Unix
    QLocalServer::removeServer(m_endpoint);

    connect(&m_server, &QLocalServer::newConnection, this, &IpcServerManager::onNewConnection);
    if (!m_server.listen(m_endpoint)) {
        qWarning() << "[IPC] Listen failed on" << m_endpoint << ":" << m_server.errorString();
        m_status = QStringLiteral("Listen failed");
        emit connectionStatusChanged();
        return;
    }
    m_status = QStringLiteral("Listening");
    emit connectionStatusChanged();
    qDebug() << "[IPC] Listening on" << m_endpoint;
}

void IpcServerManager::onNewConnection()
{
    while (QLocalSocket* s = m_server.nextPendingConnection()) {
        m_clients.insert(s);
        connect(s, &QLocalSocket::readyRead, this, [this, s]{ onReadyRead(s); });
        connect(s, &QLocalSocket::disconnected, this, [this, s]{ onDisconnected(s); });
        m_status = QStringLiteral("Connected");
        emit connectionStatusChanged();

        QJsonObject reg{ {"type","register"}, {"role","qt"} };
        sendFrame(s, MakeJson(reg));

        LOG_INFO(QString("New IPC client connected. Total clients: %1").arg(m_clients.size()));
    }
}

void IpcServerManager::onDisconnected(QLocalSocket* s)
{
    m_clients.remove(s);
    m_recvBuf.remove(s);
    s->deleteLater();
    m_status = m_clients.isEmpty() ? QStringLiteral("Listening") : QStringLiteral("Connected");
    emit connectionStatusChanged();
}

void IpcServerManager::onReadyRead(QLocalSocket* s)
{
    QByteArray& buf = m_recvBuf[s];
    buf.append(s->readAll());

    for (;;) {
        if (buf.size() < 4) break;
        const quint32 len = ReadLE32(buf.constData());
        if (len > 128u * 1024u * 1024u) {
            LOG_ERROR(QString("Absurd frame length: %1").arg(len));
            buf.clear(); break;
        }
        if (buf.size() < 4 + int(len)) break;

        const QByteArray payload = buf.mid(4, int(len));
        buf.remove(0, 4 + int(len));

        const QString msg = QString::fromUtf8(payload);
        LOG_DEBUG(QString("IPC Received (%1 bytes): %2").arg(len).arg(msg));

        QMetaObject::invokeMethod(m_processor, "handleMessage",
                                  Qt::QueuedConnection, Q_ARG(QString, msg));
    }
}

// void IpcServerManager::sendPoll()
// {
//     if (m_clients.isEmpty()) return;

//     // Only log occasionally to avoid spam
//     static int pollCount = 0;
//     if (++pollCount % 100 == 0) {
//         LOG_DEBUG(QString("Sent %1 polls").arg(pollCount));
//     }

//     broadcast(m_pollPayload);
// }

void IpcServerManager::sendFrame(QLocalSocket* s, const QByteArray& payload)
{
    if (!s || s->state() != QLocalSocket::ConnectedState) return;
    QByteArray frame; frame.resize(4 + payload.size());
    const quint32 len = payload.size();
    memcpy(frame.data(), &len, 4);                  // little-endian on Win/mac
    memcpy(frame.data()+4, payload.data(), len);
    s->write(frame);
    s->flush();
}

void IpcServerManager::broadcast(const QByteArray& payload)
{
    for (auto* s : std::as_const(m_clients)) sendFrame(s, payload);
}

QByteArray IpcServerManager::MakeJson(const QJsonObject& o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
