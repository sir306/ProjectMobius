#pragma once

#include <QObject>
#include <QUrl>
#include <QAbstractSocket>
#include <QThread>
#include "MessageProcessor.h"

class ChartTableModel;
class AxisSettings;
class ChartSettings;     // ← forward
class QWebSocket;
class QTimer;

class WebSocketManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY connectionStatusChanged)

public:
    explicit WebSocketManager(const QUrl &url,
                              const QString& unrealEngineID,
                              ChartTableModel *model,
                              AxisSettings    *axisSettings,
                              ChartSettings   *chartSettings,
                              QObject        *parent = nullptr);

    // using the destructor we can make sure to kill the sockets and threads
    ~WebSocketManager();

    QString connectionStatus() const { return m_status; }

public slots:
    void sendPoll();


signals:
    void connectionStatusChanged();

private slots:
    void onStateChanged(QAbstractSocket::SocketState newState);
    void onError(QAbstractSocket::SocketError error);
    void onTextMessage(const QString &msg);
    void onBinaryMessage(const QByteArray &data);

private:
    QUrl               m_url;
    QWebSocket        *m_socket;
    ChartTableModel   *m_model;
    AxisSettings      *m_axisSettings;
    ChartSettings     *m_chartSettings;
    QTimer            *m_pollTimer;
    QString            m_status;
    QByteArray         m_pollPayload;
    QString            m_unrealEngineID;
    QThread* m_workerThread;
    MessageProcessor* m_processor;
};
