// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-launcher-demo entry point.
//
// QGuiApplication + QQmlApplicationEngine + a LauncherController owning a
// Registry<ILauncherProviderFactory> with the built-in providers, whose
// LauncherModel is exposed to QML as LauncherResults — the same context
// property name the shell uses, so Main.qml and the shell's launcher
// popout bind identically.

#include "LauncherController.h"

#include <PhosphorShellLauncher/LauncherModel.h>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStringLiteral>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Phosphor"));
    QCoreApplication::setApplicationName(QStringLiteral("phosphor-launcher-demo"));

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    PhosphorLauncherDemo::LauncherController controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("LauncherResults"), controller.model());

    engine.loadFromModule(QStringLiteral("Phosphor.LauncherDemo"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}
