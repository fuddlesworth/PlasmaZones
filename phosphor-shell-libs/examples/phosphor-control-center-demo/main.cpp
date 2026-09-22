// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-control-center-demo entry point.
//
// QGuiApplication + QQmlApplicationEngine + a ControlCenterController
// owning a Registry<IControlCenterTileFactory> with the built-in tiles,
// exposed to QML as the ControlCenter provider.
//
// The idle ladder is constructed HERE rather than inside IdleTile and
// handed down as an initial property: a tile-owned IdleService would arm a
// second, independent ladder, and an inhibition taken on one would not
// hold the other open.

#include "ControlCenterController.h"

#include <PhosphorServiceBluetooth/QmlRegistration.h>
#include <PhosphorServiceBrightness/QmlRegistration.h>
#include <PhosphorServiceIdle/IdleService.h>
#include <PhosphorServiceIdle/QmlRegistration.h>
#include <PhosphorServiceNetwork/QmlRegistration.h>
#include <PhosphorServicePipeWire/QmlRegistration.h>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStringLiteral>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Phosphor"));
    QCoreApplication::setApplicationName(QStringLiteral("phosphor-control-center-demo"));

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // The Phosphor.Service.* modules the tiles import are registered by
    // hand rather than by a QML plugin registrar, so they must be
    // registered BEFORE any QML that imports them loads. Each is idempotent
    // and infallible by design (std::call_once around each body), which is
    // why none of these return a status. Same block, and the same
    // reasoning, as src/shell/main.cpp.
    PhosphorServicePipeWire::registerQmlTypes();
    PhosphorServiceNetwork::registerQmlTypes();
    PhosphorServiceBluetooth::registerQmlTypes();
    PhosphorServiceBrightness::registerQmlTypes();
    PhosphorServiceIdle::registerQmlTypes();

    PhosphorServiceIdle::IdleService idleService;
    PhosphorControlCenterDemo::ControlCenterController controlCenterController(&idleService);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("controlCenterController"), &controlCenterController);

    engine.loadFromModule(QStringLiteral("Phosphor.ControlCenterDemo"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}
