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

/**
 * @brief Handles the WebSocket transport used to feed Unreal data into QML.
 *
 * WebSocketManager owns the socket connection plus a worker-threaded
 * MessageProcessor so heavy JSON parsing happens off the GUI thread. It emits
 * @c connectionStatus changes for QML to display connection diagnostics.
 */
class WebSocketManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString connectionStatus READ connectionStatus NOTIFY connectionStatusChanged)

public:
    /**
     * @brief Create a WebSocket client wired up to the charting objects.
     * @param url Remote endpoint, typically the Unreal editor WebSocket URL.
     * @param unrealEngineID Identifier registered with the Unreal side.
     * @param model Target chart model receiving point data.
     * @param axisSettings Axis metadata bound in QML.
     * @param chartSettings Chart UI state (title, playbar, status text).
     * @param parent Owning QObject.
     */
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
    /**
     * @brief Poll for new data or reconnect if the socket drops.
     *
     * Triggered by a QTimer (100 Hz) to keep the UI in sync with the sim.
     */
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
