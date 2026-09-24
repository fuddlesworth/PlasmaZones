// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-osd-demo entry point.
//
// QGuiApplication + QQmlApplicationEngine + an IpcRouter so the OSD
// overlay can be driven from a terminal (`phosphorctl call osd.show
// --arg kind=volume --arg value=62`). The OSDController owns a
// Registry<IOSDFactory> with the
// four built-in OSDs and is exposed to QML as the OSDHost provider.

#include "OSDController.h"

#include <PhosphorIpc/IpcEngine.h>
#include <PhosphorIpc/IpcRouter.h>

#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QString>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Phosphor"));
    QCoreApplication::setApplicationName(QStringLiteral("phosphor-osd-demo"));

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    PhosphorOsdDemo::OSDController osdController;

    // IPC router so `phosphorctl call osd.show ...` reaches the overlay.
    // Socket: $PHOSPHOR_SOCKET, else a CMake-injected per-build path,
    // else the IpcRouter XDG default.
    PhosphorIpc::IpcRouter router;
    QString socketPath = QProcessEnvironment::systemEnvironment().value(QStringLiteral("PHOSPHOR_SOCKET"));
#ifdef PHOSPHOR_OSD_DEMO_SOCKET
    if (socketPath.isEmpty()) {
        socketPath = QStringLiteral(PHOSPHOR_OSD_DEMO_SOCKET);
    }
#endif
    if (!router.start(socketPath)) {
        qWarning(
            "phosphor-osd-demo: IPC router failed to start on '%s' (see preceding log); "
            "the in-window buttons still work",
            qPrintable(socketPath.isEmpty() ? QStringLiteral("<XDG default>") : socketPath));
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("osdController"), &osdController);
    engine.rootContext()->setContextProperty(QStringLiteral("ipcSocketPath"), router.socketPath());
    PhosphorIpc::IpcEngine::install(&engine, &router);

    engine.loadFromModule(QStringLiteral("Phosphor.OsdDemo"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}
