// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../core/logging.h"
#include "../core/single_instance_service.h"
#include "../core/translationloader.h"
#include "../config/configmigration.h"
#include "settingscontroller.h"
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
#include <QWindow>

namespace {

constexpr PlasmaZones::SingleInstanceIds kSettingsIds{PlasmaZones::DBus::SettingsApp::ServiceName,
                                                      PlasmaZones::DBus::SettingsApp::ObjectPath,
                                                      PlasmaZones::DBus::SettingsApp::Interface};

/// Try to forward a --page request to an already-running instance.
/// Returns true if the running instance handled it (caller should exit).
bool activateRunningInstance(const QString& page)
{
    // Skip entirely if there's no running instance. Avoids a speculative
    // setActivePage call logging a bogus error when we know no one's home.
    if (!PlasmaZones::SingleInstanceService::isRunning(kSettingsIds))
        return false;

    if (!page.isEmpty()) {
        // Best-effort: page navigation doesn't gate the raise. A failure here
        // still lets us raise the window — the user can navigate manually.
        PlasmaZones::SingleInstanceService::forward(kSettingsIds, QStringLiteral("setActivePage"), {page});
    }
    return PlasmaZones::SingleInstanceService::forward(kSettingsIds, QStringLiteral("raise"), {});
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

    PlasmaZones::SettingsController controller;

    // Register D-Bus service so future launches can forward to us.
    // If registration fails, another instance registered between our
    // activateRunningInstance() check and now — retry forwarding and exit.
    if (!controller.registerDBusService()) {
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

    // Hand the QML root window to the controller so raise() can target it
    // directly instead of scanning QGuiApplication::allWindows().
    for (QObject* obj : engine.rootObjects()) {
        if (auto* window = qobject_cast<QWindow*>(obj)) {
            controller.setPrimaryWindow(window);
            break;
        }
    }

    return app.exec();
}
