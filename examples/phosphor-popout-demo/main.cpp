// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-popout-demo entry point.
//
// QGuiApplication plus QQmlApplicationEngine. Constructs a
// DemoController, which owns the PopoutController and the
// InAppPopoutTransport, exposes it to QML as a context property,
// and loads Main.qml.

#include "DemoController.h"

#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QString>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Phosphor"));
    QCoreApplication::setApplicationName(QStringLiteral("phosphor-popout-demo"));

    // Same Breeze workaround the phosphor-theme-demo uses. Basic style
    // keeps the controls out of our token-driven retint path.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // demoController is declared BEFORE the engine so C++ reverse-order
    // destruction tears the engine down first. The engine teardown
    // re-evaluates QML bindings. If demoController died first, those
    // re-evaluations would dereference a dangling context-property
    // pointer and log "Cannot read property X of null" errors.
    PhosphorPopoutDemo::DemoController demoController;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("demoController"), &demoController);

    // Drain popouts before the engine starts destroying QML objects.
    // aboutToQuit fires before the event loop returns, so the close
    // storm runs while every QML binding target is still alive. Doing
    // this from a destructor would happen too late, after the engine
    // has already half-destroyed the QML object tree. The connection
    // is explicitly DirectConnection because shutdown MUST complete
    // synchronously inside aboutToQuit: a queued connection would
    // defer the slot past the event-loop return and the engine's
    // teardown would run first.
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &demoController,
                     &PhosphorPopoutDemo::DemoController::shutdown, Qt::DirectConnection);

    engine.loadFromModule(QStringLiteral("Phosphor.PopoutDemo"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}
