#include <QApplication>
#include <QFileDialog>
#include <QHttpServer>
#include <QTcpServer>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QTimer>
#include <QJsonObject>
#include <QJsonDocument>
#include <QLineEdit>


// Simple file dialog application
int main(int argc, char *argv[])
{
    // Create application with GUI support
    QApplication app(argc, argv);
    QHttpServer server;

    // Current selected files
    QString selectedFile;
    QString agentFileSelected;
    QString meshFileSelected;
    QString currentDialogType;


    // Variables to store the file dialog state
    bool isDialogOpen = false;
    bool bUserCancelled = false;

    QFileDialog dialogBox = QFileDialog();

    // Read the incoming initial dir
    QCommandLineParser parser;
    parser.setApplicationDescription("Mobius Qt App");
    parser.addHelpOption();

    // Add `--initialDir=<dir>` option
    parser.addOption({{"d", "initialDir"}, "Initial directory to load from", "initialDir"});

    // Process the arguments
    parser.process(app);

    // Read the value
    const QString initialDir = parser.value("initialDir");

    dialogBox.setDirectory(initialDir);

    // file filters
    QString agentFileFilter =
        "All Supported Data Files (*.json);;"
        "JSON Files (*.json)";
    // QString meshFileFilter =
    //     "All Mesh Files (*.udatasmith *.fbx *.obj);;"
    //     "UDatasmith Files (*.udatasmith);;"
    //     "FBX Files (*.fbx);;"
    //     "OBJ Files (*.obj)";

    // No Datasmith allowed in this filter to prevent users who haven't configured the project correctly for Datasmith use
    QString meshFileFilter =
        "All Mesh Files (*.fbx *.obj *.wkt);;"
        "FBX Files (*.fbx);;"
        "OBJ Files (*.obj);;"
        "WKT Files (*.wkt)";

    // Connect signals
    QObject::connect(&dialogBox, &QFileDialog::fileSelected, [&](const QString& file) {
        selectedFile = file;
        qInfo() << "[Qt] Selected file:" << selectedFile;
        isDialogOpen = false;
        bUserCancelled = false;
    });

    QObject::connect(&dialogBox, &QFileDialog::rejected, [&]() {
        qInfo() << "[Qt] Dialog cancelled";
        isDialogOpen = false;
        bUserCancelled = true;
    });


    // Setup focus-loss pulse flash (cross-platform)
    QObject::connect(&app, &QApplication::focusChanged, &dialogBox, [&](QWidget* /*old*/, QWidget* now) {
        if (!dialogBox.isActiveWindow() && now != &dialogBox && isDialogOpen) {
            // Simulate pulse flash by changing background color temporarily
            dialogBox.setStyleSheet("QWidget { background-color: rgb(255, 200, 200); }");
            QTimer::singleShot(150, &dialogBox, [&dialogBox]() {
                dialogBox.setStyleSheet(""); // reset after flash
            });
            // Refocus and bring to front
            QTimer::singleShot(50, &dialogBox, [&dialogBox]() {
                dialogBox.raise();
                dialogBox.activateWindow();
                dialogBox.setFocus(Qt::ActiveWindowFocusReason);
            });
        }
    });

    // signal for assigning correct selected files
    QObject::connect(&dialogBox, &QFileDialog::fileSelected, [&](const QString& file) {
        selectedFile = file;
        if (currentDialogType == "agent") {
            agentFileSelected = file;
        } else if (currentDialogType == "mesh") {
            meshFileSelected = file;
        }
        qInfo() << "[Qt] Selected file:" << selectedFile;
        isDialogOpen = false;
    });


    // Ensure app doesn't close too soon
    app.setQuitOnLastWindowClosed(false);

    // method to configure dialog box
    auto ShowDialogWithFilter = [&](const QString& title, const QString& nameFilter) {
        selectedFile.clear(); // clear any selected file

        dialogBox.setWindowTitle(title);
        dialogBox.setFileMode(QFileDialog::ExistingFile);
        dialogBox.setNameFilter(nameFilter);

        dialogBox.setViewMode(QFileDialog::Detail);

        dialogBox.setOptions(QFileDialog::DontUseNativeDialog);
        dialogBox.setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint);
        dialogBox.setWindowModality(Qt::ApplicationModal);

        QString currentDir = dialogBox.directory().absolutePath();
        dialogBox.setDirectory(currentDir);     // reapply same dir

        // force clear existing files -> prevents agent file being applied to the mesh file and vice versa
        QList<QLineEdit*> edits = dialogBox.findChildren<QLineEdit*>();
        for (QLineEdit* edit : std::as_const(edits)) {
            if (edit->parent()->inherits("QFileDialog")) {
                edit->clear();
            }
        }

        dialogBox.raise();
        dialogBox.activateWindow();
        dialogBox.setFocus(Qt::ActiveWindowFocusReason);
        dialogBox.show();
    };

    // When GET /openAgentFile is called, launch the file dialog
    server.route("/openAgentFile", [&]() -> QHttpServerResponse {
        if (isDialogOpen) {
            dialogBox.close(); // will only work on non-static dialogs
        }

        isDialogOpen = true;
        currentDialogType = "agent";

        QTimer::singleShot(10, &app, [&]() {
            ShowDialogWithFilter("Select a Pedestrian Vector File", agentFileFilter);
        });


        return QHttpServerResponse("File dialog opened");
    });

    // When GET /openMeshFile is called, launch the file dialog
    server.route("/openMeshFile", [&]() -> QHttpServerResponse {
        if (isDialogOpen) {
            dialogBox.close(); // will only work on non-static dialogs
        }

        isDialogOpen = true;
        currentDialogType = "mesh";

        QTimer::singleShot(10, &app, [&]() {
            ShowDialogWithFilter("Select a Geometry Model File", meshFileFilter);
        });


        return QHttpServerResponse("File dialog opened");
    });

    // When GET /getFileResult is called, return the current selected file
    server.route("/getFileResult", [&]() -> QHttpServerResponse {
        if (isDialogOpen) {
            return QHttpServerResponse("SELECTION_PENDING");
        }

        if (bUserCancelled) {
            bUserCancelled = false;
            return QHttpServerResponse("NO_SELECTION");
        }

        QJsonObject result;
        result["agentFile"] = agentFileSelected;
        result["meshFile"] = meshFileSelected;

        QJsonDocument doc(result);
        return QHttpServerResponse(doc.toJson());
    });

    // When POST /resetselection is called, clear the current selection
    server.route("/resetselection", QHttpServerRequest::Method::Post, [&](const QHttpServerRequest&) {
        selectedFile.clear();
        return QHttpServerResponse("Selection reset");
    });

    // POST /quit will shut down the app
    server.route("/quit", QHttpServerRequest::Method::Post, [&](const QHttpServerRequest&) {
        qInfo() << "[Qt] Shutting down Qt dialog server.";
        QTimer::singleShot(200, &app, &QCoreApplication::quit);
        return QHttpServerResponse("Shutting down");
    });

    // Setup a heartbeat timer to keep the app responsive
    QTimer* heartbeatTimer = new QTimer(&app);
    QObject::connect(heartbeatTimer, &QTimer::timeout, [&]() {
        // Just pump events to keep the app active
        QCoreApplication::processEvents();
    });
    heartbeatTimer->start(1000); // Check every second

    // Setup TCP listener for HTTP server
    const quint16 port = 8080;
    QTcpServer* tcpServer = new QTcpServer();
    if (!tcpServer->listen(QHostAddress::Any, port) || !server.bind(tcpServer)) {
        qCritical() << "[Qt] Failed to bind server to port" << port;
        delete tcpServer;
        return -1;
    }

    qInfo() << "[Qt] File-dialog HTTP server running on port" << port;
    qInfo() << "  → Open dialog: http://127.0.0.1:" << port << "/openAgentFile";
    qInfo() << "  → Open dialog: http://127.0.0.1:" << port << "/openMeshFile";
    qInfo() << "  → Get result: http://127.0.0.1:" << port << "/getFileResult";
    qInfo() << "  → Reset selection: POST http://127.0.0.1:" << port << "/resetselection";
    qInfo() << "  → Quit server: POST http://127.0.0.1:" << port << "/quit";

    // Start the application's event loop
    return app.exec();
}
