// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon.h"
#include "../config/configdefaults.h"
#include "../core/logging.h"
#include "../core/qpa/layershellpluginloader.h"
#include "../core/layersurface.h"
#include "../core/translationloader.h"
#include "version.h"
#include "rendering/zoneshaderitem.h"
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QIcon>
#include "vulkan_metatype.h"
#include <QQuickWindow>
#include <QSettings>
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
        QSettings cfg(PlasmaZones::ConfigDefaults::configFilePath(), QSettings::IniFormat);
        cfg.beginGroup(QStringLiteral("Shaders"));
        const QString backend = PlasmaZones::ConfigDefaults::normalizeRenderingBackend(
            cfg.value(QStringLiteral("RenderingBackend"), PlasmaZones::ConfigDefaults::renderingBackend()).toString());
        cfg.endGroup();

        if (backend == QLatin1String("vulkan")) {
#if QT_CONFIG(vulkan)
            // Probe Vulkan availability before committing to the API.
            // Note: QVulkanInstance::create() before QGuiApplication means Vulkan
            // validation layers set via VK_INSTANCE_LAYERS may not be picked up
            // (Qt reads some env settings after app construction). This is
            // intentional — setGraphicsApi() must be called before the app exists.
            vulkanInstance.setApiVersion(QVersionNumber(1, 1));
            if (vulkanInstance.create()) {
                QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
                useVulkan = true;
            } else {
                qCCritical(PlasmaZones::lcDaemon)
                    << "Failed to create Vulkan instance — falling back to OpenGL."
                    << "Check that Vulkan drivers are installed (vulkan-icd-loader, mesa-vulkan-drivers, etc.)";
                QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
            }
#else
            qCWarning(PlasmaZones::lcDaemon)
                << "Vulkan backend requested but Qt was built without Vulkan support — falling back to OpenGL.";
            QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#endif
        } else if (backend == QLatin1String("opengl")) {
            QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        }
        // "auto" → let Qt choose (default behavior)
        const char* detail = useVulkan             ? "(Vulkan active)"
            : (backend == QLatin1String("vulkan")) ? "(Vulkan failed, OpenGL fallback)"
            : (backend == QLatin1String("auto"))   ? "(Qt default)"
                                                   : "(OpenGL)";
        qCInfo(PlasmaZones::lcDaemon) << "Rendering backend:" << backend << detail;
    }

    QGuiApplication app(argc, argv);

    // Store instance pointer as a dynamic property so OverlayService::createQmlWindow()
    // can retrieve it and call setVulkanInstance() on each QQuickWindow.
#if QT_CONFIG(vulkan)
    qRegisterMetaType<QVulkanInstance*>();
    if (useVulkan) {
        app.setProperty(PzVulkanInstanceProperty, QVariant::fromValue(&vulkanInstance));
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
