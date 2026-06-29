// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-bar-canvas-demo entry point.
//
// QGuiApplication + QQmlApplicationEngine. Constructs a BarDemoController
// (which owns the PopoutController + the demo's SocketPopoutTransport),
// exposes it to QML as a context property, and loads Main.qml. The bar's
// socket animation binds to the controller's popout open-state.

#include "BarDemoController.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QString>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Phosphor"));
    QCoreApplication::setApplicationName(QStringLiteral("phosphor-bar-canvas-demo"));

    // Basic style keeps QtQuick.Controls chrome out of the token-driven
    // retint path, matching the other Phosphor demos.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Declared before the engine so reverse-order destruction tears the
    // engine down first, while the controller is still alive for any
    // binding re-evaluation during teardown.
    PhosphorBarCanvasDemo::BarDemoController barController;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("barController"), &barController);

    // Drain popouts before the engine destroys QML objects. DirectConnection
    // so it runs synchronously inside aboutToQuit, before the event loop
    // returns and teardown begins.
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &barController,
                     &PhosphorBarCanvasDemo::BarDemoController::shutdown, Qt::DirectConnection);

    engine.loadFromModule(QStringLiteral("Phosphor.BarCanvasDemo"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}
