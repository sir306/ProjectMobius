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
#include "ChartSettings.h"
#include "ChartTableModel.h"
#include "AxisSettings.h"
#include "WebSocketManager.h"
#include <QWindow.h>
#include <QTimer>

// …

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    ChartTableModel model(&engine);
    AxisSettings    axisSettings(&engine);
    ChartSettings   chartSettings(&engine);

    // Assign the .h and .cpp files for the QML logic
    engine.rootContext()->setContextProperty("chartModel",   &model);
    engine.rootContext()->setContextProperty("axisSettings", &axisSettings);
    engine.rootContext()->setContextProperty("chartSettings",&chartSettings);


    // ───────────────────────────────────────────────
    //  Parse --pairId so we know which Unreal instance
    // ───────────────────────────────────────────────
    QCommandLineParser parser;
    parser.addOption({{"p","pairId"}, "Mobius App ID", "pairId"});
    parser.process(app);
    const QString pairId = parser.value("pairId");

    // Read websocket port from Tools/NodeJS/config.json relative to the
    // executable location. When running from the 'executable' folder the file is
    // located at '../../../NodeJS/config.json'.
    int port = 9090;
    const QString configPath =
        QCoreApplication::applicationDirPath() +
        QStringLiteral("/../../../NodeJS/config.json");
    QFile configFile(configPath);
    if (configFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(configFile.readAll());
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            port = obj.value(QStringLiteral("port")).toInt(port);
        }
    }

    // URL for websocket using the loaded port
    QUrl wsUrl(QStringLiteral("ws://127.0.0.1:%1").arg(port));


    WebSocketManager wsMgr(
        wsUrl,
        pairId,
        &model,
        &axisSettings,
        &chartSettings,
        &engine
        );
    engine.rootContext()->setContextProperty("wsMgr", &wsMgr);

    engine.loadFromModule("PlotUE_Data", "AppWindow");

    // Use QTimer::singleShot to defer execution until after show
    QTimer::singleShot(0, &app, [&engine]() {
        const QObjectList rootObjs = engine.rootObjects();
        if (!rootObjs.isEmpty()) {
            QWindow* window = qobject_cast<QWindow*>(rootObjs.first());
            if (window) {
                window->setFlag(Qt::WindowStaysOnTopHint, true);
                window->raise();
                window->requestActivate();
            }
        }
    });

    return app.exec();
}
