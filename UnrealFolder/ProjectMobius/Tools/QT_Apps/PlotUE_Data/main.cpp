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

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QUrlQuery>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDebug>
#include "ChartSettings.h"
#include "ChartTableModel.h"
#include "AxisSettings.h"
// #include "WebSocketManager.h"
#include "IpcServerManager.h"
#include <QWindow>
#include <QTimer>

// …

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    ChartTableModel model(&engine);
    AxisSettings    axisSettings(&engine);
    ChartSettings   chartSettings(&engine);

    engine.rootContext()->setContextProperty("chartModel",   &model);
    engine.rootContext()->setContextProperty("axisSettings", &axisSettings);
    engine.rootContext()->setContextProperty("chartSettings",&chartSettings);

    // Parse command line
    QCommandLineParser parser;
    parser.addOption({{"p","pairId"},  "Mobius App ID (unused for IPC)", "pairId", ""}); // ✅ Default to empty
    parser.addOption({{"e","endpoint"},"QLocalServer name", "endpoint", "MobiusIpc"});
    parser.process(app);

    const QString pairId  = parser.value("pairId");  // Will be empty, that's fine
    const QString endpoint = parser.value("endpoint");

    // IPC server - pairId is ignored now
    IpcServerManager ipcMgr(
        endpoint,
        pairId,      // ✅ Can be empty string, not used anymore
        &model,
        &axisSettings,
        &chartSettings,
        &engine
        );
    engine.rootContext()->setContextProperty("wsMgr", &ipcMgr);

    engine.loadFromModule("PlotUE_Data", "AppWindow");

    QTimer::singleShot(0, &app, [&engine]() {
        const QObjectList rootObjs = engine.rootObjects();
        if (!rootObjs.isEmpty()) {
            if (QWindow* window = qobject_cast<QWindow*>(rootObjs.first())) {
                window->setFlag(Qt::WindowStaysOnTopHint, true);
                window->raise();
                window->requestActivate();
            }
        }
    });

    return app.exec();
}
