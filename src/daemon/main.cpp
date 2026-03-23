// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon.h"
#include "../core/logging.h"
#include "../core/translationloader.h"
#include "version.h"
#include "rendering/zoneshaderitem.h"
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QThread>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQml/qqml.h>
#include "pz_i18n.h"
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
    PlasmaZones::loadTranslations(&app);

    // Daemon must survive monitor power-off (DP disconnect destroys all overlay
    // windows; without this, Qt sees zero windows and calls quit()).
    app.setQuitOnLastWindowClosed(false);

    // Register ZoneShaderItem for QML
    // This enables RenderNodeOverlay.qml to use the GPU-accelerated shader item
    qmlRegisterType<PlasmaZones::ZoneShaderItem>("PlasmaZones", 1, 0, "ZoneShaderItem");

    // Set up application metadata
    app.setApplicationName(QStringLiteral("plasmazonesd"));
    app.setApplicationVersion(PlasmaZones::VERSION_STRING);
    app.setOrganizationName(QStringLiteral("plasmazones"));
    app.setOrganizationDomain(QStringLiteral("org.plasmazones"));
    app.setDesktopFileName(QStringLiteral("org.plasmazones.daemon"));

    // Command line options
    QCommandLineParser parser;
    parser.setApplicationDescription(PzI18n::tr("Window tiling and zone management"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption replaceOption(QStringList{QStringLiteral("r"), QStringLiteral("replace")},
                                     PzI18n::tr("Replace existing daemon instance"));
    parser.addOption(replaceOption);

    parser.process(app);

    // Ensure single instance via D-Bus service name registration
    const QString serviceName = QStringLiteral("org.plasmazones.daemon");
    QDBusConnection bus = QDBusConnection::sessionBus();

    if (!bus.registerService(serviceName)) {
        if (parser.isSet(replaceOption)) {
            // Ask existing instance to quit, then retry with exponential backoff
            QDBusInterface existing(serviceName, QStringLiteral("/Daemon"), serviceName);
            if (existing.isValid()) {
                existing.call(QStringLiteral("Quit"));
            }
            bool registered = false;
            for (int attempt = 0; attempt < 10; ++attempt) {
                QThread::msleep(100 * (1 << qMin(attempt, 3))); // 100, 200, 400, 800ms...
                if (bus.registerService(serviceName)) {
                    registered = true;
                    break;
                }
            }
            if (!registered) {
                qCCritical(PlasmaZones::lcDaemon) << "Failed to register D-Bus service after --replace";
                return 1;
            }
        } else {
            qCWarning(PlasmaZones::lcDaemon) << "Already running (D-Bus name taken)";
            return 0;
        }
    }

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

    qCInfo(PlasmaZones::lcDaemon) << "Daemon started";
    daemon.start();

    int result = app.exec();

    daemon.stop();
    g_daemon = nullptr;

    return result;
}
