#ifndef IPCSERVERMANAGER_H
#define IPCSERVERMANAGER_H

// MIT © ProjectMobius contributors
#pragma once
#include <QObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>
#include <QSet>
#include <QHash>
#include "MessageProcessor.h"

// Simple local IPC server replacing WebSocketManager.
// - Listens on a QLocalServer name (e.g., "MobiusIpc")
// - Speaks length-prefixed frames: [u32_le size][UTF-8 JSON]
class ChartTableModel;
class AxisSettings;
class ChartSettings;

class IpcServerManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY connectionStatusChanged)
public:
    explicit IpcServerManager(const QString& endpointName,
                              const QString& unrealEngineID,
                              ChartTableModel* model,
                              AxisSettings* axisSettings,
                              ChartSettings* chartSettings,
                              QObject* parent = nullptr);

    ~IpcServerManager();

    QString connectionStatus() const { return m_status; }

public slots:
    // Keep your existing polling semantics (10 ms) until UE flips to push-only
    //void sendPoll();

signals:
    void connectionStatusChanged();

private slots:
    void onNewConnection();
    void onReadyRead(QLocalSocket* s);
    void onDisconnected(QLocalSocket* s);

private:
    void startListening();
    void sendFrame(QLocalSocket* s, const QByteArray& payload);
    void broadcast(const QByteArray& payload);
    static QByteArray MakeJson(const QJsonObject& obj);

private:
    QString        m_endpoint;
    QString        m_unrealEngineID;
    QLocalServer   m_server;
    QSet<QLocalSocket*> m_clients;
    QHash<QLocalSocket*, QByteArray> m_recvBuf; // per-socket reassembly buffer
    // QTimer         m_pollTimer;
    QString        m_status{"Initializing…"};
    // QByteArray     m_pollPayload;

    // Worker thread + message processor stays as in WebSocketManager
    QThread*        m_workerThread{nullptr};
    MessageProcessor* m_processor{nullptr};
};


#endif // IPCSERVERMANAGER_H
