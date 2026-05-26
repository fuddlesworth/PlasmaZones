// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "demoapp.h"

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("phosphor-settings-ui-minimal"));
    app.setOrganizationName(QStringLiteral("phosphor"));

    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    PhosphorSettingsUiExamplesMinimal::DemoApp controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("demoController"), &controller);
    engine.loadFromModule(QStringLiteral("org.phosphor.settings.ui.examples.minimal"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}
