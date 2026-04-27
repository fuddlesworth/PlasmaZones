// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../core/animationbootstrap.h"
#include "../core/logging.h"
#include "../core/single_instance_service.h"
#include "../core/translationloader.h"
#include "../config/configmigration.h"
#include "settingscontroller.h"
#include "settingslaunchcontroller.h"
#include "version.h"
#include "pz_i18n.h"
#include "pz_qml_i18n.h"

#include "../core/constants.h"

#include <QGuiApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

namespace {

constexpr PlasmaZones::SingleInstanceIds kSettingsIds{PlasmaZones::DBus::SettingsApp::ServiceName,
                                                      PlasmaZones::DBus::SettingsApp::ObjectPath,
                                                      PlasmaZones::DBus::SettingsApp::Interface};

/// Try to forward a --page request to an already-running instance.
/// Returns true if a running instance exists (caller should exit).
///
/// Forwards only the page switch — does not try to raise the running window.
/// No Wayland workaround reliably convinces KWin to bring an already-mapped
/// xdg_toplevel to the front from a programmatic caller, so the user has to
/// focus the existing window themselves.
bool activateRunningInstance(const QString& page)
{
    if (!PlasmaZones::SingleInstanceService::isRunning(kSettingsIds))
        return false;

    if (!page.isEmpty()) {
        PlasmaZones::SingleInstanceService::forward(kSettingsIds, QStringLiteral("setActivePage"), {page});
    }
    return true;
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    PlasmaZones::loadTranslations(&app);

    app.setApplicationName(QStringLiteral("plasmazones-settings"));
    app.setApplicationVersion(PlasmaZones::VERSION_STRING);
    app.setOrganizationName(QStringLiteral("plasmazones"));
    app.setOrganizationDomain(QStringLiteral("org.plasmazones"));
    app.setDesktopFileName(QStringLiteral("org.plasmazones.settings"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("plasmazones-settings")));

    QCommandLineParser parser;
    parser.setApplicationDescription(PzI18n::tr("PlasmaZones Settings"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption pageOption(QStringList{QStringLiteral("p"), QStringLiteral("page")},
                                  PzI18n::tr("Open a specific settings page"), QStringLiteral("name"));
    parser.addOption(pageOption);
    parser.process(app);

    const QString requestedPage = parser.isSet(pageOption) ? parser.value(pageOption) : QString();

    // Single-instance: if another instance is running, forward the page request and exit
    if (activateRunningInstance(requestedPage)) {
        return 0;
    }

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
        if (desktop.contains(QLatin1String("kde")) || desktop.contains(QLatin1String("plasma"))) {
            QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
        } else {
            QQuickStyle::setStyle(QStringLiteral("Fusion"));
        }
    }

    // Ensure INI→JSON migration has run (the daemon does this too, but the
    // settings app may start before the daemon on first upgrade).
    PlasmaZones::ConfigMigration::ensureJsonConfig();

    // Bootstrap the per-process PhosphorProfileRegistry so QML
    // `PhosphorMotionAnimation { profile: "..." }` lookups resolve against the
    // shipped data/profiles JSONs. Without this the registry stays empty in
    // the settings process and every animation falls back to the library
    // default 150 ms — see AnimationBootstrap docs for the full rationale.
    // Must outlive the QML engine (Behavior bindings keep registry handles).
    PlasmaZones::AnimationBootstrap animationBootstrap;

    PlasmaZones::SettingsController controller;

    // The launch controller owns the D-Bus single-instance lifecycle. Holds a
    // non-owning pointer to `controller`, which must outlive it (guaranteed by
    // reverse destruction order: `controller` is declared first and destroyed
    // last).
    PlasmaZones::SettingsLaunchController launcher(&controller);

    // Register D-Bus service so future launches can forward to us.
    // If registration fails, another instance registered between our
    // activateRunningInstance() check and now — retry forwarding and exit.
    if (!launcher.registerDBusService()) {
        qCWarning(PlasmaZones::lcCore) << "D-Bus service already owned; forwarding to running instance";
        if (activateRunningInstance(requestedPage)) {
            return 0;
        }
        // D-Bus name is taken but we can't reach the owner — bail out
        // rather than running a second instance without single-instance support.
        qCCritical(PlasmaZones::lcCore) << "Cannot register D-Bus service and cannot reach existing instance; exiting";
        return 1;
    }

    QQmlApplicationEngine engine;

    auto* localizedContext = new PzLocalizedContext(&engine);
    engine.rootContext()->setContextObject(localizedContext);

    engine.rootContext()->setContextProperty(QStringLiteral("settingsController"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"), controller.settings());

    if (!requestedPage.isEmpty()) {
        controller.setActivePage(requestedPage);
    }

    engine.loadFromModule("org.plasmazones.settings", "Main");

    if (engine.rootObjects().isEmpty()) {
        qCCritical(PlasmaZones::lcCore) << "Failed to load settings QML";
        return 1;
    }

    return app.exec();
}
