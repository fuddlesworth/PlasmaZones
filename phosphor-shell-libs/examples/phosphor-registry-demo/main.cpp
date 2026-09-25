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

    // demoController is declared BEFORE the engine so C++ reverse-
    // order destruction tears the engine down first. The engine
    // teardown re-evaluates QML bindings; if demoController died
    // first, those re-evaluations would dereference a dangling
    // context-property pointer and log "Cannot read property X of
    // null" errors. Mirrors phosphor-popout-demo's precedent.
    PhosphorRegistryDemo::DemoController demoController;

    QQmlApplicationEngine engine;
    demoController.setEngine(&engine);
    engine.rootContext()->setContextProperty(QStringLiteral("demoController"), &demoController);

    engine.loadFromModule(QStringLiteral("Phosphor.RegistryDemo"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}
