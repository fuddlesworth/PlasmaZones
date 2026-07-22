// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon.h"

#include "config/configdefaults.h"
#include "config/configmigration.h"
#include "core/types/constants.h"
#include "core/platform/logging.h"
#include "core/utils/translationloader.h"
#include "phosphor_i18n.h"
#include "rendering/surfaceshaderitem.h"
#include "rendering/zoneshaderitem.h"
#include "version.h"
#include "rendering/vulkansupport.h"
#include "waylandsessioncheck.h"

#include <PhosphorProtocol/Registration.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorWayland/LayerShellPluginLoader.h>
#include <PhosphorWayland/LayerSurface.h>

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <QtQml/qqml.h>
#include <QtQml/qqmlextensionplugin.h>

#include <cerrno>
#include <cstdio>
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
    // Exit cleanly (code 0) if there is no usable Wayland display — avoids a
    // SIGABRT → Restart=on-failure loop. When systemd respawns us during the
    // logout → SDDM handoff (or autostarts us in a session that has no live
    // wl_display), Qt's wayland QPA can't create a wl_display, the xcb fallback
    // also fails, and QGuiApplication's constructor calls qFatal() → abort.
    // This is a Wayland-only daemon, so "no wayland socket" means "nothing to
    // do". Resolving the socket path handles the empty/unset WAYLAND_DISPLAY
    // case too (Qt's default "wayland-0"), which previously bypassed this guard
    // and let Qt abort. See queryPlasmaWorkspaceState() in daemon.cpp for the
    // full phantom-session analysis.
    //
    // Skip the probe entirely when WAYLAND_SOCKET is set: libwayland (and thus
    // Qt's wayland QPA) connects via that inherited fd and ignores
    // WAYLAND_DISPLAY, so there is a live connection even though no socket path
    // resolves. Probing a path in that case would make us exit on a perfectly
    // usable session.
    if (qEnvironmentVariableIsEmpty("WAYLAND_SOCKET")) {
        const QByteArray waylandDisplay = qgetenv("WAYLAND_DISPLAY");
        const QByteArray runtimeDir = qgetenv("XDG_RUNTIME_DIR");
        const QString socketPath = resolveWaylandSocketPath(waylandDisplay, runtimeDir);
        if (socketPath.isEmpty() || !QFile::exists(socketPath)) {
            std::fprintf(stderr,
                         "plasmazones: no usable Wayland display (WAYLAND_DISPLAY=\"%s\", "
                         "resolved socket \"%s\") — wayland session not available, "
                         "exiting cleanly to avoid restart loop\n",
                         waylandDisplay.constData(),
                         socketPath.isEmpty() ? "<none>" : socketPath.toLocal8Bit().constData());
            return 0;
        }
    }

    // Opt out of MangoHud's implicit Vulkan layer injection. MangoHud's
    // implicit_layer manifest attaches whenever MANGOHUD=1 is in the
    // environment (e.g. set globally for games), and its NVIDIA stat-polling
    // thread costs ~30% CPU continuously inside this daemon — we are a
    // background service, not a game client. Both env vars are cleared:
    // MANGOHUD=0 alone is not enough on all manifest versions; the explicit
    // DISABLE_MANGOHUD opt-out is honored regardless of MANGOHUD's value.
    // Must run before QVulkanInstance::create() in vulkansupport.cpp.
    qunsetenv("MANGOHUD");
    qputenv("DISABLE_MANGOHUD", "1");

    // Use the simple animation driver (Qt 6.5+). With the threaded scene-
    // graph render loop and TWO OR MORE QQuickWindows, Qt 6's default
    // animation driver explicitly "falls back to the system timer based
    // approach" — and crucially, every render-thread sync phase blocks the
    // GUI thread for the duration of that window's polishAndSync. Animation
    // ticks driven by GUI-thread QTimers (which is how SurfaceAnimator's
    // m_driverTimer works) miss frames whenever the GUI thread is parked
    // in a sibling window's sync — the visible symptom is one popup's
    // shader fly-in pausing partway through while another popup mounts and
    // animates, then jumping forward when the GUI thread resumes.
    //
    // QSG_USE_SIMPLE_ANIMATION_DRIVER=1 swaps the multi-window-fallback
    // timer driver for QElapsedTimer-based timing, decoupling the
    // animation tick cadence from the per-window vsync / sync handshake.
    // Documented in the Qt Quick Scene Graph manual page as the supported
    // way to address exactly this multi-window stutter pattern. Must be
    // set before QGuiApplication is constructed.
    qputenv("QSG_USE_SIMPLE_ANIMATION_DRIVER", "1");

    // Register our layer-shell QPA plugin before QGuiApplication
    PhosphorWayland::registerLayerShellPlugin();

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
                << "Vulkan unavailable (instance creation failed or no enumerable GPU) — falling back to OpenGL."
                << "Check that Vulkan drivers are installed and match the running kernel module"
                << "(a GPU driver upgrade without a reboot leaves a userspace/kernel version skew that"
                << "breaks device enumeration).";
            useVulkan = false;
        }
    }
#endif
    PlasmaZones::loadTranslations(&app);

    // Register D-Bus struct types for typed signal/method exchange
    PhosphorProtocol::registerWireTypes();

    // Register metatype for QVariant storage (LayerSurface stores itself
    // as a QWindow dynamic property via QVariant::fromValue).
    qRegisterMetaType<PhosphorWayland::LayerSurface*>();

    // Verify the layer-shell QPA plugin loaded successfully. If not, overlays will
    // be created as xdg_toplevel (wrong stacking/anchoring) — warn loudly.
    if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && !PhosphorWayland::LayerSurface::isSupported()) {
        qCCritical(lcDaemon) << "Layer-shell QPA plugin did not initialize —"
                             << "overlays will use xdg_toplevel (wrong stacking/anchoring)."
                             << "Zone overlays will appear as regular windows (visible in taskbar,"
                             << "wrong z-order, no keyboard grab). This compositor may not support"
                             << "zwlr_layer_shell_v1 (e.g. GNOME/Mutter)."
                             << "Check that phosphorwayland-qpa.so is installed to Qt's"
                             << "wayland-shell-integration plugin directory.";
    }

    // Daemon must survive monitor power-off (DP disconnect destroys all overlay
    // windows; without this, Qt sees zero windows and calls quit()).
    app.setQuitOnLastWindowClosed(false);

    // Register ZoneShaderItem for QML
    // This enables RenderNodeOverlay.qml to use the GPU-accelerated shader item
    // (the item's ctor registers the ZoneLabelTexture metatype + QImage converter).
    qmlRegisterType<PlasmaZones::ZoneShaderItem>("PlasmaZones", 1, 0, "ZoneShaderItem");

    // Register SurfaceShaderItem (per-surface decoration layer) for QML.
    // Same module URI/version as ZoneShaderItem. The on-screen host is
    // SurfaceDecoration.qml, driven by OverlayService::applyDecoration on the
    // OSD / popup surfaces (Stage d); the per-application-window host lives in
    // the kwin-effect (renderSurfaceChainComposite), not in this process.
    qmlRegisterType<PlasmaZones::SurfaceShaderItem>("PlasmaZones", 1, 0, "SurfaceShaderItem");

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
    parser.setApplicationDescription(PhosphorI18n::tr("Window tiling and zone management"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption replaceOption(QStringList{QStringLiteral("r"), QStringLiteral("replace")},
                                     PhosphorI18n::tr("Replace existing daemon instance"));
    parser.addOption(replaceOption);

    QCommandLineOption debugOption(QStringList{QStringLiteral("d"), QStringLiteral("debug")},
                                   PhosphorI18n::tr("Enable debug logging for all PlasmaZones categories"));
    parser.addOption(debugOption);

    QCommandLineOption logFileOption(QStringList{QStringLiteral("l"), QStringLiteral("log-file")},
                                     PhosphorI18n::tr("Write log output to <file> instead of stderr"),
                                     PhosphorI18n::tr("file"));
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
    const QString serviceName = QString(PhosphorProtocol::Service::Name);
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
