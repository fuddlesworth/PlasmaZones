// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-popout-demo entry point.
//
// QGuiApplication + QQmlApplicationEngine. Constructs a DemoController
// (which owns the PopoutController + InAppPopoutTransport), exposes it
// to QML as a context property, and loads Main.qml.

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

    QQmlApplicationEngine engine;

    PhosphorPopoutDemo::DemoController demoController;
    engine.rootContext()->setContextProperty(QStringLiteral("demoController"), &demoController);

    engine.loadFromModule(QStringLiteral("Phosphor.PopoutDemo"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}
