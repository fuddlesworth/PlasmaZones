// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../core/logging.h"
#include "../core/translationloader.h"
#include "settingscontroller.h"
#include "version.h"
#include "pz_i18n.h"
#include "pz_qml_i18n.h"

#include "../core/constants.h"

#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

namespace {

/// Try to forward a --page request to an already-running instance.
/// Returns true if the running instance handled it (caller should exit).
bool activateRunningInstance(const QString& page)
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;

    QDBusInterface iface(PlasmaZones::DBus::SettingsApp::ServiceName, PlasmaZones::DBus::SettingsApp::ObjectPath,
                         PlasmaZones::DBus::SettingsApp::Interface, bus);
    if (!iface.isValid())
        return false;

    iface.setTimeout(3000); // 3s timeout — avoid long hang if existing instance is frozen
    if (!page.isEmpty()) {
        QDBusMessage pageReply = iface.call(QStringLiteral("setActivePage"), page);
        if (pageReply.type() == QDBusMessage::ErrorMessage) {
            qCWarning(PlasmaZones::lcCore) << "Failed to forward page to running instance:" << pageReply.errorMessage();
        }
    }
    QDBusMessage reply = iface.call(QStringLiteral("raise"));
    if (reply.type() == QDBusMessage::ErrorMessage)
        return false;
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

    PlasmaZones::SettingsController controller;

    // Register D-Bus service so future launches can forward to us
    if (!controller.registerDBusService()) {
        qCWarning(PlasmaZones::lcCore) << "Failed to register D-Bus service; single-instance forwarding disabled";
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
