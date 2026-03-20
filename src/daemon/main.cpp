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
#include <KAboutData>
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
    KAboutData aboutData(QStringLiteral("plasmazonesd"), PzI18n::tr("PlasmaZones Daemon"), PlasmaZones::VERSION_STRING,
                         PzI18n::tr("Window tiling and zone management for KDE Plasma"), KAboutLicense::GPL_V3,
                         PzI18n::tr("© 2026 fuddlesworth"));
    aboutData.addAuthor(PzI18n::tr("fuddlesworth"));
    aboutData.setHomepage(QStringLiteral("https://github.com/plasmazones/plasmazones"));
    aboutData.setBugAddress(QByteArrayLiteral("https://github.com/plasmazones/plasmazones/issues"));
    aboutData.setDesktopFileName(QStringLiteral("org.plasmazones.daemon"));

    KAboutData::setApplicationData(aboutData);

    // Command line options
    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);

    QCommandLineOption replaceOption(QStringList{QStringLiteral("r"), QStringLiteral("replace")},
                                     PzI18n::tr("Replace existing daemon instance"));
    parser.addOption(replaceOption);

    parser.process(app);
    aboutData.processCommandLine(&parser);

    // Ensure single instance via D-Bus service name registration
    const QString serviceName = QStringLiteral("org.plasmazones.daemon");
    QDBusConnection bus = QDBusConnection::sessionBus();

    if (!bus.registerService(serviceName)) {
        if (parser.isSet(replaceOption)) {
            // Ask existing instance to quit, then take over
            QDBusInterface existing(serviceName, QStringLiteral("/Daemon"), serviceName);
            if (existing.isValid()) {
                existing.call(QStringLiteral("Quit"));
                QThread::msleep(500);
            }
            if (!bus.registerService(serviceName)) {
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
