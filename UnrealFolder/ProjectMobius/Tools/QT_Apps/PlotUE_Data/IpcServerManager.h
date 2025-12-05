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

class ChartTableModel;
class AxisSettings;
class ChartSettings;

/**
 * @brief Local socket server that feeds Unreal telemetry into the Qt UI.
 *
 * IpcServerManager replaces the WebSocket path with a simpler QLocalServer.
 * Each connected client sends length-prefixed JSON frames of the form
 * [u32_le size][UTF-8 JSON]. Messages are forwarded to MessageProcessor on a
 * worker thread so parsing does not block the GUI event loop.
 */
class IpcServerManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY connectionStatusChanged)
public:
    /**
     * @brief Construct an IPC server bound to a named endpoint.
     * @param endpointName Name passed to QLocalServer::listen (e.g., "MobiusIpc").
     * @param unrealEngineID Identifier forwarded back to Unreal when registering.
     * @param model Target chart model receiving point data.
     * @param axisSettings Axis metadata bound in QML.
     * @param chartSettings Chart UI state (title, playbar, status text).
     * @param parent Owning QObject.
     */
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
    /**
     * @brief Accept a pending client and send the initial registration message.
     */
    void onNewConnection();
    /**
     * @brief Read and reassemble length-prefixed frames from a socket.
     * @param s Socket that has data available.
     *
     * Buffers partial frames until a full [len][payload] pair is present, then
     * forwards the UTF-8 JSON payload to MessageProcessor on the worker thread.
     */
    void onReadyRead(QLocalSocket* s);
    /**
     * @brief Remove socket bookkeeping when a client disconnects.
     */
    void onDisconnected(QLocalSocket* s);

private:
    void startListening();
    /**
     * @brief Send a single length-prefixed JSON payload to a client.
     * @param s Connected socket.
     * @param payload Raw JSON bytes to deliver.
     */
    void sendFrame(QLocalSocket* s, const QByteArray& payload);
    /**
     * @brief Send the same payload to all connected clients.
     */
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
