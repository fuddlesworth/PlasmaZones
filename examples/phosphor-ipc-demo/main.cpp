// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-ipc-demo. Acceptance harness for libs/phosphor-ipc and
// bin/phosphorctl.
//
// QGuiApplication + IpcRouter bound to a per-build socket path
// (overridable via --socket / $PHOSPHOR_SOCKET). The QML side
// declares three IpcTargets (greet, count, set-value) that
// `phosphorctl call`, `phosphorctl schema`, and
// `phosphorctl subscribe` exercise. Window shows registered
// targets + most recent socket activity.

#include "DemoController.h"

#include <PhosphorIpc/IpcEngine.h>
#include <PhosphorIpc/IpcRouter.h>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QLoggingCategory>
#include <QString>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Phosphor"));
    QCoreApplication::setApplicationName(QStringLiteral("phosphor-ipc-demo"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("phosphor-ipc acceptance harness"));
    parser.addHelpOption();
    QCommandLineOption socketOption(
        QStringList{QStringLiteral("socket"), QStringLiteral("s")},
        QStringLiteral("Socket path to listen on. Defaults to PHOSPHOR_IPC_DEMO_SOCKET (CMake-injected) "
                       "or a per-build path; falls back to $XDG_RUNTIME_DIR/phosphor.sock."),
        QStringLiteral("path"));
    parser.addOption(socketOption);
    parser.process(app);

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // demoController is declared BEFORE the engine so reverse-
    // order destruction tears the engine down first; mirrors the
    // phosphor-popout-demo / phosphor-registry-demo precedent.
    PhosphorIpcDemo::DemoController demoController;

    // Resolve socket path priority: --socket > $PHOSPHOR_SOCKET >
    // PHOSPHOR_IPC_DEMO_SOCKET CMake injection > empty (router
    // falls back to XDG default).
    QString socketPath = parser.value(socketOption);
    if (socketPath.isEmpty()) {
        socketPath = QProcessEnvironment::systemEnvironment().value(QStringLiteral("PHOSPHOR_SOCKET"));
    }
#ifdef PHOSPHOR_IPC_DEMO_SOCKET
    if (socketPath.isEmpty()) {
        socketPath = QStringLiteral(PHOSPHOR_IPC_DEMO_SOCKET);
    }
#endif
    // Qt suppresses qInfo by default; the demo's startup log is
    // user-facing (operators need to see which socket the router
    // bound), so enable the info-and-above categories before any
    // qInfo runs.
    QLoggingCategory::setFilterRules(QStringLiteral("default.info=true"));

    const bool routerOk = demoController.start(socketPath);
    if (routerOk) {
        qInfo("phosphor-ipc-demo: router listening on '%s'", qPrintable(demoController.socketPath()));
    } else {
        qWarning("phosphor-ipc-demo: router failed to start on '%s' — see preceding log",
                 qPrintable(demoController.socketPath()));
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("demoController"), &demoController);
    // Install router on the engine so IpcTarget instances declared
    // in QML can locate it during componentComplete.
    PhosphorIpc::IpcEngine::install(&engine, demoController.router());

    engine.loadFromModule(QStringLiteral("Phosphor.IpcDemo"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        qWarning("phosphor-ipc-demo: failed to load Phosphor.IpcDemo/Main.qml");
        return 1;
    }
    return app.exec();
}
