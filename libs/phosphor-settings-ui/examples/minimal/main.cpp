// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QDebug>
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
    // Demo uses setContextProperty for brevity. Real applications should
    // prefer engine.setInitialProperties({{"controller", ...}}) plus a
    // `required property` on the QML root — compile-time-checked and
    // moc-friendly. Context properties are kept here only so the demo
    // stays a single-file example.
    engine.rootContext()->setContextProperty(QStringLiteral("demoController"), &controller);
    engine.loadFromModule(QStringLiteral("org.phosphor.settings.ui.examples.minimal"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "phosphor-settings-ui-minimal: failed to load Main.qml — check QML import paths and module"
                       " registration. Run with QT_LOGGING_RULES='qt.qml.*=true' for verbose diagnostics.";
        return 1;
    }
    return app.exec();
}
