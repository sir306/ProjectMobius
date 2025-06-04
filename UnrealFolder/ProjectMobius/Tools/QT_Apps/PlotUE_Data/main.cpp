#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QUrlQuery>
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


    // URL for websocket
    QUrl wsUrl(QStringLiteral("ws://127.0.0.1:9090"));


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
