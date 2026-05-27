// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-registry-demo entry point.
//
// Toy QQuickWindow that drives Registry<IBarWidgetFactory> end-to-end.
// Two built-in widget factories register at startup; the bar QML
// enumerates the registry and parents one widget per factory id.

#include "DemoController.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QString>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Phosphor"));
    QCoreApplication::setApplicationName(QStringLiteral("phosphor-registry-demo"));

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;

    // Constructed AFTER the engine so its forward connections to
    // the engine's QQmlComponent are valid. Owned by `app`'s root
    // so it outlives the engine? No — the engine is destroyed first
    // (declared before this on the stack in reverse-order
    // destruction). The same precedent as phosphor-popout-demo:
    // create controller AFTER the engine, but bind the controller's
    // lifetime to a stack ahead of the engine via reverse-order
    // destruction.
    PhosphorRegistryDemo::DemoController demoController(&engine);
    engine.rootContext()->setContextProperty(QStringLiteral("demoController"), &demoController);

    engine.loadFromModule(QStringLiteral("Phosphor.RegistryDemo"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}
