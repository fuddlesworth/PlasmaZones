// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon.h"
#include "../config/configdefaults.h"
#include "../config/configmigration.h"
#include "../core/logging.h"
#include "../core/qpa/layershellpluginloader.h"
#include "../core/layersurface.h"
#include "../core/translationloader.h"
#include "version.h"
#include "rendering/zoneshaderitem.h"
#include "vulkan_support.h"
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QIcon>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQml/qqml.h>
#include "pz_i18n.h"
#include <cerrno>
#include <cstring>
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
    // Register our layer-shell QPA plugin before QGuiApplication
    PlasmaZones::registerLayerShellPlugin();

    // Read rendering backend preference and probe Vulkan BEFORE QGuiApplication —
    // QQuickWindow::setGraphicsApi() must be called before the app object exists.
    // Vulkan instance creation is also done here so that fallback to OpenGL can
    // happen before the graphics API is locked in by QGuiApplication construction.
    // vulkanInstance is declared BEFORE app so C++ destruction order guarantees
    // it outlives QGuiApplication (and all QQuickWindows that reference it).
    bool useVulkan = false;
#if QT_CONFIG(vulkan)
    QVulkanInstance vulkanInstance;
#endif
    {
        const QString backend = PlasmaZones::ConfigDefaults::readRenderingBackendFromDisk();
        useVulkan = PlasmaZones::probeAndSetGraphicsApi(backend);
        if (!useVulkan && backend == QLatin1String("vulkan")) {
            qCCritical(PlasmaZones::lcDaemon) << "Vulkan library not found — falling back to OpenGL."
                                              << "Install vulkan-icd-loader or equivalent for your distro.";
        }
        qCInfo(PlasmaZones::lcDaemon) << "Rendering backend:" << backend
                                      << (useVulkan                                ? "(Vulkan)"
                                              : backend == QLatin1String("opengl") ? "(OpenGL)"
                                                                                   : "(Qt default)");
    }

    QGuiApplication app(argc, argv);

    // Store instance pointer as a dynamic property so OverlayService::createQmlWindow()
    // can retrieve it and call setVulkanInstance() on each QQuickWindow.
#if QT_CONFIG(vulkan)
    qRegisterMetaType<QVulkanInstance*>();
    if (useVulkan) {
        if (PlasmaZones::createAndRegisterVulkanInstance(vulkanInstance, app)) {
            qCInfo(PlasmaZones::lcDaemon) << "Vulkan instance created successfully";
        } else {
            qCCritical(PlasmaZones::lcDaemon)
                << "Failed to create Vulkan instance after app init — falling back to OpenGL."
                << "Check that Vulkan drivers are installed (vulkan-icd-loader, mesa-vulkan-drivers, etc.)";
            useVulkan = false;
        }
    }
#endif
    PlasmaZones::loadTranslations(&app);

    // Register metatype for QVariant storage (LayerSurface stores itself
    // as a QWindow dynamic property via QVariant::fromValue).
    qRegisterMetaType<PlasmaZones::LayerSurface*>();

    // Verify the layer-shell QPA plugin loaded successfully. If not, overlays will
    // be created as xdg_toplevel (wrong stacking/anchoring) — warn loudly.
    if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && !PlasmaZones::LayerSurface::isSupported()) {
        qCCritical(lcDaemon) << "Layer-shell QPA plugin did not initialize —"
                             << "overlays will use xdg_toplevel (wrong stacking/anchoring)."
                             << "Zone overlays will appear as regular windows (visible in taskbar,"
                             << "wrong z-order, no keyboard grab). This compositor may not support"
                             << "zwlr_layer_shell_v1 (e.g. GNOME/Mutter)."
                             << "Check that pz-layer-shell.so is installed to Qt's"
                             << "wayland-shell-integration plugin directory.";
    }

    // Daemon must survive monitor power-off (DP disconnect destroys all overlay
    // windows; without this, Qt sees zero windows and calls quit()).
    app.setQuitOnLastWindowClosed(false);

    // Register ZoneShaderItem for QML
    // This enables RenderNodeOverlay.qml to use the GPU-accelerated shader item
    qmlRegisterType<PlasmaZones::ZoneShaderItem>("PlasmaZones", 1, 0, "ZoneShaderItem");

    // Set up application metadata
    // applicationName is the KGlobalAccel component key — must match plasmazonesd.desktop
    app.setApplicationName(QStringLiteral("plasmazonesd"));
    app.setApplicationDisplayName(QStringLiteral("PlasmaZones"));
    app.setApplicationVersion(PlasmaZones::VERSION_STRING);
    app.setOrganizationName(QStringLiteral("plasmazones"));
    app.setOrganizationDomain(QStringLiteral("org.plasmazones"));
    app.setDesktopFileName(QStringLiteral("org.plasmazones.daemon"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("plasmazones")));

    // Command line options
    QCommandLineParser parser;
    parser.setApplicationDescription(PzI18n::tr("Window tiling and zone management"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption replaceOption(QStringList{QStringLiteral("r"), QStringLiteral("replace")},
                                     PzI18n::tr("Replace existing daemon instance"));
    parser.addOption(replaceOption);

    QCommandLineOption debugOption(QStringList{QStringLiteral("d"), QStringLiteral("debug")},
                                   PzI18n::tr("Enable debug logging for all PlasmaZones categories"));
    parser.addOption(debugOption);

    QCommandLineOption logFileOption(QStringList{QStringLiteral("l"), QStringLiteral("log-file")},
                                     PzI18n::tr("Write log output to <file> instead of stderr"), PzI18n::tr("file"));
    parser.addOption(logFileOption);

    parser.process(app);

    // --log-file: redirect Qt message output to a file.
    // Opened here (before --debug filter rules) so the file is ready
    // before any log messages are emitted. The static FILE* and QMutex
    // intentionally leak — they must outlive QCoreApplication for
    // shutdown messages.
    static FILE* logFile = nullptr;
    static QMutex* logMutex = nullptr;
    if (parser.isSet(logFileOption)) {
        const QByteArray pathLocal = parser.value(logFileOption).toLocal8Bit();
        logFile = fopen(pathLocal.constData(), "a");
        if (logFile) {
            logMutex = new QMutex;
            qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
                const QByteArray formatted = qFormatLogMessage(type, ctx, msg).toUtf8();
                QMutexLocker lock(logMutex);
                fprintf(logFile, "%s\n", formatted.constData());
                fflush(logFile);
            });
            fprintf(stderr, "plasmazonesd: logging to %s\n", pathLocal.constData());
        } else {
            qCWarning(PlasmaZones::lcDaemon)
                << "Failed to open log file:" << parser.value(logFileOption) << "-" << strerror(errno);
        }
    }

    if (parser.isSet(debugOption)) {
        QLoggingCategory::setFilterRules(QStringLiteral("plasmazones.*=true"));
    }

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

    // Migrate INI config to JSON if needed (one-time on upgrade).
    // The editor and settings app also call ensureJsonConfig() in case they start
    // before the daemon.  Concurrent calls produce identical JSON from the same INI,
    // and QSaveFile's atomic rename prevents partial writes.  The .bak rename of the
    // old INI may fail for the second caller (non-fatal — logged as a warning).
    PlasmaZones::ConfigMigration::ensureJsonConfig();

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

    // Close all remaining windows before QGuiApplication teardown.
    // The NVIDIA Vulkan ICD (driver 595.x+) crashes in vkDestroyInstance during
    // dlclose when GPU worker threads are still running. Explicitly closing windows
    // here lets the Wayland platform plugin tear down EGL/Vulkan surfaces while the
    // event loop is still partially intact, before ~QGuiApplication triggers the
    // problematic dlclose path. This is an NVIDIA driver bug — this workaround
    // reduces (but may not eliminate) the crash window.
    const auto topLevels = QGuiApplication::topLevelWindows();
    for (QWindow* w : topLevels) {
        w->destroy();
    }

    return result;
}
