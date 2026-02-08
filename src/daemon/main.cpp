// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon.h"
#include "../core/logging.h"
#include "rendering/zoneshaderitem.h"
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQml/qqml.h>
#include <KAboutData>
#include <KLocalizedString>
#include <KDBusService>
#include <signal.h>

// Import static QML module for shared components
Q_IMPORT_QML_PLUGIN(org_plasmazones_commonPlugin)

using namespace PlasmaZones;

static Daemon* g_daemon = nullptr;

void signalHandler(int /*signal*/)
{
    if (g_daemon) {
        g_daemon->stop();
    }
    QCoreApplication::quit();
}

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);

    // Daemon must survive monitor power-off (DP disconnect destroys all overlay
    // windows; without this, Qt sees zero windows and calls quit()).
    app.setQuitOnLastWindowClosed(false);

    // Register ZoneShaderItem for QML
    // This enables RenderNodeOverlay.qml to use the GPU-accelerated shader item
    qmlRegisterType<PlasmaZones::ZoneShaderItem>("PlasmaZones", 1, 0, "ZoneShaderItem");

    // Set translation domain BEFORE any i18n() calls
    KLocalizedString::setApplicationDomain("plasmazonesd");

    // Set up application metadata
    KAboutData aboutData(QStringLiteral("plasmazonesd"), i18n("PlasmaZones Daemon"), QStringLiteral("1.2.0"),
                         i18n("FancyZones-style window snapping for KDE Plasma"), KAboutLicense::GPL_V3,
                         i18n("© 2026 fuddlesworth"));
    aboutData.addAuthor(i18n("fuddlesworth"));
    aboutData.setHomepage(QStringLiteral("https://github.com/plasmazones/plasmazones"));
    aboutData.setBugAddress(QByteArrayLiteral("https://github.com/plasmazones/plasmazones/issues"));
    aboutData.setDesktopFileName(QStringLiteral("org.plasmazones.daemon"));

    KAboutData::setApplicationData(aboutData);

    // Command line options
    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);

    QCommandLineOption replaceOption(QStringList{QStringLiteral("r"), QStringLiteral("replace")},
                                     i18n("Replace existing daemon instance"));
    parser.addOption(replaceOption);

    parser.process(app);
    aboutData.processCommandLine(&parser);

    // Ensure single instance
    KDBusService::StartupOptions options = KDBusService::Unique;
    if (parser.isSet(replaceOption)) {
        options |= KDBusService::Replace;
    }

    KDBusService service(options);

    // Set up signal handling for clean shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGHUP, signalHandler);

    // Create and start daemon
    Daemon daemon;
    g_daemon = &daemon;

    if (!daemon.init()) {
        qCCritical(PlasmaZones::lcDaemon) << "Failed to initialize daemon";
        return 1;
    }

    qCInfo(PlasmaZones::lcDaemon) << "Started successfully";
    daemon.start();

    // Handle activation requests (e.g., user launches plasmazonesd when already running)
    // Log the activation but don't take action - overlay activation is via drag+modifier
    QObject::connect(&service, &KDBusService::activateRequested, &daemon, []() {
        qCDebug(PlasmaZones::lcDaemon) << "Already running - activation request ignored";
    });

    int result = app.exec();

    daemon.stop();
    g_daemon = nullptr;

    return result;
}
