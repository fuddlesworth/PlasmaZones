// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EditorController.h"
#include "EditorLaunchController.h"
#include "../core/animationbootstrap.h"
#include "../core/constants.h"
#include "../core/logging.h"
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorAnimation/PhosphorCurve.h>
#include <PhosphorAnimation/QtQuickClockManager.h>
#include <PhosphorWayland/LayerShellPluginLoader.h>
#include <PhosphorWayland/LayerSurface.h>
#include <PhosphorScreens/Resolver.h>
#include "../core/single_instance_service.h"
#include "../core/translationloader.h"
#include "../config/configdefaults.h"
#include "version.h"
#include "../daemon/rendering/zoneshaderitem.h"
#include "../daemon/vulkan_support.h"

#include <QApplication>
#include <QFile>
#include <QLibrary>
#include <QScopeGuard>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QObject>

#include "phosphor_i18n.h"
#include "phosphor_qml_i18n.h"
#include <QtQml/qqml.h>

using namespace PlasmaZones;

namespace {

constexpr PlasmaZones::SingleInstanceIds kEditorIds{PhosphorProtocol::Service::Apps::Editor::ServiceName,
                                                    PhosphorProtocol::Service::Apps::Editor::ObjectPath,
                                                    PhosphorProtocol::Service::Apps::Editor::Interface};

/// Try to forward a launch request to an already-running editor instance.
/// Returns true if the running instance accepted the request (caller should exit).
///
/// The running instance applies the forwarded args (layout / screen / preview)
/// but deliberately does not attempt to raise its window — see the comment on
/// EditorController::handleLaunchRequest for why.
bool activateRunningInstance(const QString& screenId, const QString& layoutId, bool createNew, bool preview)
{
    return PlasmaZones::SingleInstanceService::forward(kEditorIds, QStringLiteral("handleLaunchRequest"),
                                                       {screenId, layoutId, createNew, preview});
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    // Ensure D-Bus session bus is reachable when launched from CLI (e.g. IDE terminal)
    // where DBUS_SESSION_BUS_ADDRESS may be unset. Use systemd default path.
    if (qEnvironmentVariableIsEmpty("DBUS_SESSION_BUS_ADDRESS")) {
        const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
        if (!runtimeDir.isEmpty()) {
            const QString busPath = runtimeDir + QStringLiteral("/bus");
            if (QFile::exists(busPath)) {
                qputenv("DBUS_SESSION_BUS_ADDRESS", QByteArray("unix:path=" + busPath.toUtf8()));
            }
        }
    }

    // Opt out of MangoHud's implicit Vulkan layer injection. MangoHud's
    // implicit_layer manifest attaches whenever MANGOHUD=1 is in the
    // environment (e.g. set globally for games), and its NVIDIA stat-polling
    // thread costs ~30% CPU continuously inside this process — we are a
    // window-manager helper, not a game client. Both env vars are cleared:
    // MANGOHUD=0 alone is not enough on all manifest versions; the explicit
    // DISABLE_MANGOHUD opt-out is honored regardless of MANGOHUD's value.
    // Must run before QVulkanInstance::create() in vulkan_support.cpp.
    qunsetenv("MANGOHUD");
    qputenv("DISABLE_MANGOHUD", "1");

    // Register our layer-shell QPA plugin before QApplication
    PhosphorWayland::registerLayerShellPlugin();

    // Read rendering backend preference and set graphics API BEFORE QApplication.
    // Must match daemon's backend so shader previews render identically.
    bool useVulkan = false;
#if QT_CONFIG(vulkan)
    QVulkanInstance vulkanInstance;
#endif
    {
        const QString backend = PlasmaZones::ConfigDefaults::readRenderingBackendFromDisk();
        useVulkan = PlasmaZones::probeAndSetGraphicsApi(backend);
    }

    // QApplication (not QGuiApplication): the org.kde.desktop QtQuick Controls
    // style (qqc2-desktop-style) renders every control through a QtWidgets
    // QStyle via KQuickStyleItem. That path calls qApp->style(), which requires
    // a QApplication — under a plain QGuiApplication it operates in a degenerate
    // context that fragile third-party QStyle plugins (e.g. Darkly) dereference
    // into a crash on the first paint frame. See discussion #262.
    QApplication app(argc, argv);
    PlasmaZones::loadTranslations(&app);

    // Create and store QVulkanInstance for shader preview windows (same as daemon)
#if QT_CONFIG(vulkan)
    qRegisterMetaType<QVulkanInstance*>();
    if (useVulkan) {
        if (!PlasmaZones::createAndRegisterVulkanInstance(vulkanInstance, app)) {
            qCCritical(PlasmaZones::lcEditor)
                << "Vulkan unavailable (instance creation failed or no enumerable GPU) —"
                << "falling back to OpenGL for shader preview. If a GPU driver was upgraded,"
                << "a reboot may be needed to match the kernel module to the userspace driver.";
            useVulkan = false;
        }
    }
#endif

    // Register metatype for QVariant storage (LayerSurface stores itself
    // as a QWindow dynamic property via QVariant::fromValue).
    qRegisterMetaType<PhosphorWayland::LayerSurface*>();

    // Verify the layer-shell QPA plugin loaded successfully. If not, shader preview
    // overlays will be created as xdg_toplevel (wrong stacking/anchoring).
    if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && !PhosphorWayland::LayerSurface::isSupported()) {
        qCCritical(lcEditor) << "Layer-shell QPA plugin did not initialize —"
                             << "shader preview overlays will use xdg_toplevel (wrong stacking)."
                             << "Check that phosphorwayland-qpa.so is installed to Qt's"
                             << "wayland-shell-integration plugin directory.";
    }

    app.setApplicationName(QStringLiteral("plasmazones-editor"));
    app.setApplicationVersion(PlasmaZones::VERSION_STRING);
    app.setOrganizationName(QStringLiteral("plasmazones"));
    app.setOrganizationDomain(QStringLiteral("org.plasmazones"));
    app.setDesktopFileName(QStringLiteral("org.plasmazones.editor"));

    // Command line options
    QCommandLineParser parser;
    parser.setApplicationDescription(PhosphorI18n::tr("Visual layout editor for PlasmaZones"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption layoutIdOption(QStringList{QStringLiteral("l"), QStringLiteral("layout")},
                                      PhosphorI18n::tr("Layout ID to edit"), QStringLiteral("uuid"));
    QCommandLineOption screenOption(QStringList{QStringLiteral("s"), QStringLiteral("screen")},
                                    PhosphorI18n::tr("Target screen name"), QStringLiteral("name"));
    QCommandLineOption newLayoutOption(QStringList{QStringLiteral("n"), QStringLiteral("new")},
                                       PhosphorI18n::tr("Create new layout"));
    QCommandLineOption previewOption(QStringLiteral("preview"), PhosphorI18n::tr("Open in read-only preview mode"));

    parser.addOptions({layoutIdOption, screenOption, newLayoutOption, previewOption});
    parser.process(app);

    // Use platform style if available, fall back to Fusion for non-KDE environments
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
        if (desktop.contains(QLatin1String("kde")) || desktop.contains(QLatin1String("plasma"))) {
            QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
        } else {
            QQuickStyle::setStyle(QStringLiteral("Fusion"));
        }
    }

    // Register ZoneShaderItem for QML (shader preview in ShaderSettingsDialog)
    qmlRegisterType<PlasmaZones::ZoneShaderItem>("PlasmaZones", 1, 0, "ZoneShaderItem");

    // Resolve target screen and collect launch args up front so we can forward
    // them to an already-running editor instance before doing any heavy setup.
    // ScreenResolver wraps the daemon call + QGuiApplication::screenAt fallback
    // so we don't have to duplicate the virtual-screen-aware lookup here.
    QString targetScreen = parser.isSet(screenOption) ? parser.value(screenOption)
                                                      : PhosphorScreens::ScreenResolver::effectiveScreenAtCursor();

    // Warn about mutually exclusive flags
    if (parser.isSet(previewOption) && parser.isSet(newLayoutOption)) {
        qWarning() << "--preview and --new are mutually exclusive; ignoring --preview";
    }
    if (parser.isSet(newLayoutOption) && parser.isSet(layoutIdOption)) {
        qWarning() << "--new and --layout are mutually exclusive; ignoring --layout";
    }

    const bool createNewLayout = parser.isSet(newLayoutOption);
    const QString layoutIdArg =
        (!createNewLayout && parser.isSet(layoutIdOption)) ? parser.value(layoutIdOption) : QString();
    const bool previewArg = parser.isSet(previewOption) && !createNewLayout;

    // Single-instance: if another editor is already running, forward the launch
    // request and exit. Avoids spawning parallel editor processes when the user
    // hits the shortcut repeatedly while the first editor is still starting up.
    if (activateRunningInstance(targetScreen, layoutIdArg, createNewLayout, previewArg)) {
        return 0;
    }

    // Bootstrap the per-process PhosphorProfileRegistry so QML
    // `PhosphorMotionAnimation { profile: "..." }` lookups resolve. The
    // shipped tree carries no bundled profile JSONs (timings are
    // Settings-UI driven via the daemon's registry publisher); the
    // bootstrap loader is still wired so user-authored JSONs at
    // `~/.local/share/plasmazones/profiles/<path>.json` get picked up
    // and so live-reload watches are armed for fresh installs. Must
    // outlive the QML engine (Behavior bindings keep registry handles).
    PlasmaZones::AnimationBootstrap animationBootstrap;

    // Publish the bootstrap-owned registries + a fresh clock manager as
    // the QML-side defaults. Phase A3 of the architecture refactor
    // retired the prior `PhosphorProfileRegistry::instance()` /
    // `QtQuickClockManager::instance()` Meyers singletons — composition
    // roots own and publish their own.
    PhosphorAnimation::QtQuickClockManager clockManager;
    PhosphorAnimation::PhosphorCurve::setDefaultRegistry(animationBootstrap.curveRegistry());
    PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(animationBootstrap.profileRegistry());
    PhosphorAnimation::QtQuickClockManager::setDefaultManager(&clockManager);
    auto unpublishAnimationDefaults = qScopeGuard([] {
        PhosphorAnimation::PhosphorCurve::setDefaultRegistry(nullptr);
        PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(nullptr);
        PhosphorAnimation::QtQuickClockManager::setDefaultManager(nullptr);
    });

    // Create the editor controller. This is cheap: wires up services and
    // loads local KConfig but does not make any blocking D-Bus calls to
    // the daemon — those happen only when applyLaunchArgs() invokes
    // loadLayout/createNewLayout.
    EditorController controller;

    // The launch controller owns the D-Bus single-instance registration and
    // the CLI-arg translation. It holds a non-owning pointer to `controller`,
    // which must outlive it (guaranteed by the reverse declaration order:
    // `controller` is declared first and destroyed last).
    EditorLaunchController launcher(&controller);

    // Claim the D-Bus well-known name BEFORE applyLaunchArgs(). applyLaunchArgs
    // triggers blocking daemon calls (queryShadersEnabled, queryAvailableShaders,
    // loadLayout) — if we registered after those, a second launch racing during
    // startup would run its own heavy init before discovering the conflict,
    // redundantly contending on the daemon's event loop. Registering first means
    // rapid-fire launches forward cleanly the moment they check the bus.
    //
    // If registration fails, another instance claimed the name between our
    // earlier activateRunningInstance() check and now — retry forwarding and
    // exit. If the other instance is unreachable (hung or crashed mid-shutdown),
    // surface the error so the user knows the shortcut silently failed.
    if (!launcher.registerDBusService()) {
        qCWarning(lcEditor) << "Editor D-Bus service already owned; forwarding to running instance";
        if (activateRunningInstance(targetScreen, layoutIdArg, createNewLayout, previewArg)) {
            return 0;
        }
        qCCritical(lcEditor) << "Editor D-Bus name" << PhosphorProtocol::Service::Apps::Editor::ServiceName
                             << "is held by an unreachable instance. The existing editor may be hung —"
                             << "kill the stale plasmazones-editor process and try again.";
        return 1;
    }

    launcher.applyLaunchArgs(targetScreen, layoutIdArg, createNewLayout, previewArg);

    // Set up QML engine
    QQmlApplicationEngine engine;

    // Set up i18n for QML (makes i18n() available in QML)
    auto* localizedContext = new PhosphorLocalizedContext(&engine);
    engine.rootContext()->setContextObject(localizedContext);

    // Expose controller to QML
    engine.rootContext()->setContextProperty(QStringLiteral("editorController"), &controller);

    // Screen list: editorController.screenModel provides VS-aware entries.
    // Legacy "availableScreens" context property removed — QML uses screenModel directly.

    // Load main QML (Window starts with visible:false — QML calls
    // editorController.showFullScreenOnTargetScreen() which sets screen from C++)
    engine.loadFromModule("org.plasmazones.editor", "EditorWindow");

    if (engine.rootObjects().isEmpty()) {
        qCCritical(PlasmaZones::lcEditor) << "Editor: Failed to load EditorWindow.qml";
        return -1;
    }

    // Ensure QML objects are destroyed cleanly before the engine goes out of scope.
    // Without this, the C++ destruction order (engine before controller) can race
    // with deferred-delete processing of QML items that have KDE style helpers,
    // causing a crash in QQmlData::destroyed() (QTBUG-style lifecycle issue).
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &engine, [&engine]() {
        // Process any pending deferred deletes before tearing down the engine
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        // Explicitly delete root QML objects while the engine is still alive
        const auto roots = engine.rootObjects();
        for (QObject* obj : roots) {
            delete obj;
        }
    });

    return app.exec();
}
