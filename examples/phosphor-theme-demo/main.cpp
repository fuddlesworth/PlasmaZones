// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-theme-demo, entry point.
//
// Boots a QGuiApplication + QQmlApplicationEngine, points the
// PaletteStore singleton at the default palette JSON path (if one
// exists), and loads Main.qml.

#include <PhosphorTheme/PaletteStore.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QString>

namespace {

// XDG-conformant default. Edit this file (or copy a matugen output to
// it) and the demo retints in <100 ms, that's the acceptance test for
// Phase 1.1 hot-reload.
QString defaultPalettePath()
{
    const auto base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath(QStringLiteral("palettes/current.json"));
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Phosphor"));
    QCoreApplication::setApplicationName(QStringLiteral("phosphor"));

    // Pin QtQuick.Controls to its Basic style. On KDE, the system-wide
    // Breeze QtQuick.Controls style ships an `ApplicationWindow` /
    // `ScrollView` chrome path that null-derefs on `palette` access in
    // some Qt6 versions ("qrc:/qt/qml/org/kde/breeze/impl/ButtonBackground.qml
    // TypeError"). The demo doesn't theme through QtQuick.Controls, every
    // colored surface binds to Theme.* directly, so an unstyled "Basic"
    // base both silences the Breeze noise and keeps the chrome out of
    // our token-driven retint path.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;

    // Touch the singleton through the engine so Theme.qml resolves
    // PaletteStore correctly. This also surfaces load errors at boot.
    // If the user has a palette JSON but it's malformed, the demo's
    // status bar shows the error. The alternative would be silently
    // falling back to defaults.
    auto* store = engine.singletonInstance<PhosphorTheme::PaletteStore*>(QStringLiteral("Phosphor.Theme"),
                                                                         QStringLiteral("PaletteStore"));
    if (store) {
        const auto path = defaultPalettePath();
        if (QFile::exists(path)) {
            store->loadFromFile(path);
        }
        QObject::connect(
            store, &PhosphorTheme::PaletteStore::loadError, &app, [](const QString& path, const QString& reason) {
                qWarning().noquote() << QStringLiteral("phosphor-theme: failed to load %1, %2").arg(path, reason);
            });
    }

    engine.loadFromModule(QStringLiteral("Phosphor.ThemeDemo"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}
