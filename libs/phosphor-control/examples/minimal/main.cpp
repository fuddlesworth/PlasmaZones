// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QApplication>
#include <QDebug>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QVariant>

#include "demoapp.h"

int main(int argc, char* argv[])
{
    // QApplication (not QGuiApplication) — Kirigami.ApplicationWindow +
    // the `org.kde.desktop` style integrate with QWidget-based dialogs
    // (native file pickers, color choosers) via QApplication. The demo
    // doesn't open such dialogs today, but the realistic consumer
    // pattern is QApplication, and using QGuiApplication here would
    // teach reviewers a subtly wrong baseline.
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("phosphor-control-minimal"));
    app.setOrganizationName(QStringLiteral("phosphor"));

    // On systems without qqc2-desktop-style installed the org.kde.desktop
    // style fails to load and Qt silently falls back to the Default style
    // — the demo still runs but looks out-of-place against a KDE-themed
    // sample. KDE-distribution users will already have the style; in any
    // other test rig, install `qqc2-desktop-style` to match the screenshot.
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    PhosphorControlExamplesMinimal::DemoApp controller;

    QQmlApplicationEngine engine;
    // Use setInitialProperties + a `required property` on Main.qml —
    // compile-time-checked vs. setContextProperty's stringly-typed
    // global context. This is the pattern real consumers should
    // copy, so the demo demonstrates it.
    engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
    engine.loadFromModule(QStringLiteral("org.phosphor.control.examples.minimal"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "phosphor-control-minimal: failed to load Main.qml — check QML import paths and module"
                       " registration. Run with QT_LOGGING_RULES='qt.qml.*=true' for verbose diagnostics.";
        return 1;
    }
    return app.exec();
}
