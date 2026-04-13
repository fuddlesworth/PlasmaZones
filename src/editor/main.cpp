// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EditorController.h"
#include "../core/constants.h"
#include "../core/logging.h"
#include "../core/qpa/layershellpluginloader.h"
#include "../core/layersurface.h"
#include "../core/single_instance_service.h"
#include "../core/translationloader.h"
#include "../config/configdefaults.h"
#include "version.h"
#include "../daemon/rendering/zoneshaderitem.h"
#include "../daemon/vulkan_support.h"

#include <QFile>
#include <QGuiApplication>
#include <QLibrary>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QCursor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QObject>

#include "pz_i18n.h"
#include "pz_qml_i18n.h"
#include <QtQml/qqml.h>

using namespace PlasmaZones;

namespace {

constexpr PlasmaZones::SingleInstanceIds kEditorIds{DBus::EditorApp::ServiceName, DBus::EditorApp::ObjectPath,
                                                    DBus::EditorApp::Interface};

/// Try to forward a launch request to an already-running editor instance.
/// Returns true if the running instance accepted the request (caller should exit).
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

    // Register our layer-shell QPA plugin before QGuiApplication
    PlasmaZones::registerLayerShellPlugin();

    // Read rendering backend preference and set graphics API BEFORE QGuiApplication.
    // Must match daemon's backend so shader previews render identically.
    bool useVulkan = false;
#if QT_CONFIG(vulkan)
    QVulkanInstance vulkanInstance;
#endif
    {
        const QString backend = PlasmaZones::ConfigDefaults::readRenderingBackendFromDisk();
        useVulkan = PlasmaZones::probeAndSetGraphicsApi(backend);
    }

    QGuiApplication app(argc, argv);
    PlasmaZones::loadTranslations(&app);

    // Create and store QVulkanInstance for shader preview windows (same as daemon)
#if QT_CONFIG(vulkan)
    qRegisterMetaType<QVulkanInstance*>();
    if (useVulkan) {
        if (!PlasmaZones::createAndRegisterVulkanInstance(vulkanInstance, app)) {
            qCCritical(PlasmaZones::lcEditor)
                << "Failed to create Vulkan instance — falling back to OpenGL for shader preview.";
            useVulkan = false;
        }
    }
#endif

    // Register metatype for QVariant storage (LayerSurface stores itself
    // as a QWindow dynamic property via QVariant::fromValue).
    qRegisterMetaType<PlasmaZones::LayerSurface*>();

    // Verify the layer-shell QPA plugin loaded successfully. If not, shader preview
    // overlays will be created as xdg_toplevel (wrong stacking/anchoring).
    if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && !PlasmaZones::LayerSurface::isSupported()) {
        qCCritical(lcEditor) << "Layer-shell QPA plugin did not initialize —"
                             << "shader preview overlays will use xdg_toplevel (wrong stacking)."
                             << "Check that pz-layer-shell.so is installed to Qt's"
                             << "wayland-shell-integration plugin directory.";
    }

    app.setApplicationName(QStringLiteral("plasmazones-editor"));
    app.setApplicationVersion(PlasmaZones::VERSION_STRING);
    app.setOrganizationName(QStringLiteral("plasmazones"));
    app.setOrganizationDomain(QStringLiteral("org.plasmazones"));
    app.setDesktopFileName(QStringLiteral("org.plasmazones.editor"));

    // Command line options
    QCommandLineParser parser;
    parser.setApplicationDescription(PzI18n::tr("Visual layout editor for PlasmaZones"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption layoutIdOption(QStringList{QStringLiteral("l"), QStringLiteral("layout")},
                                      PzI18n::tr("Layout ID to edit"), QStringLiteral("uuid"));
    QCommandLineOption screenOption(QStringList{QStringLiteral("s"), QStringLiteral("screen")},
                                    PzI18n::tr("Target screen name"), QStringLiteral("name"));
    QCommandLineOption newLayoutOption(QStringList{QStringLiteral("n"), QStringLiteral("new")},
                                       PzI18n::tr("Create new layout"));
    QCommandLineOption previewOption(QStringLiteral("preview"), PzI18n::tr("Open in read-only preview mode"));

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
    QString targetScreen;
    if (parser.isSet(screenOption)) {
        targetScreen = parser.value(screenOption);
    } else {
        // Default to the screen under the cursor — more intuitive than primaryScreen()
        // which can be unreliable on Wayland (Qt may not match KDE's configured primary).
        // Query the daemon for the effective screen ID (resolves to virtual screen when
        // configured) so the editor opens with the correct VS context.
        QPoint cursorPos = QCursor::pos();
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QString::fromLatin1(PlasmaZones::DBus::ServiceName), QString::fromLatin1(PlasmaZones::DBus::ObjectPath),
            QString::fromLatin1(PlasmaZones::DBus::Interface::Screen), QStringLiteral("getEffectiveScreenAt"));
        msg << cursorPos.x() << cursorPos.y();
        QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 2000);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            targetScreen = reply.arguments().at(0).toString();
        }
        // Fallback to Qt's physical screen if daemon unavailable
        if (targetScreen.isEmpty()) {
            QScreen* cursorScreen = QGuiApplication::screenAt(cursorPos);
            if (!cursorScreen) {
                cursorScreen = QGuiApplication::primaryScreen();
            }
            if (cursorScreen) {
                targetScreen = cursorScreen->name();
            }
        }
    }

    // Warn about mutually exclusive flags
    if (parser.isSet(previewOption) && parser.isSet(newLayoutOption)) {
        qWarning() << "--preview and --new are mutually exclusive; ignoring --preview";
    }

    const bool createNewLayout = parser.isSet(newLayoutOption);
    const QString layoutIdArg = parser.isSet(layoutIdOption) ? parser.value(layoutIdOption) : QString();
    const bool previewArg = parser.isSet(previewOption) && !createNewLayout;

    // Single-instance: if another editor is already running, forward the launch
    // request and exit. Avoids spawning parallel editor processes when the user
    // hits the shortcut repeatedly while the first editor is still starting up.
    if (activateRunningInstance(targetScreen, layoutIdArg, createNewLayout, previewArg)) {
        return 0;
    }

    // Create controller. This is cheap: it wires up services and loads local
    // KConfig, but does not make any blocking D-Bus calls to the daemon — those
    // happen only when applyLaunchArgs() invokes loadLayout/createNewLayout.
    EditorController controller;

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
    if (!controller.registerDBusService()) {
        qCWarning(lcEditor) << "Editor D-Bus service already owned; forwarding to running instance";
        if (activateRunningInstance(targetScreen, layoutIdArg, createNewLayout, previewArg)) {
            return 0;
        }
        qCCritical(lcEditor) << "Editor D-Bus name" << DBus::EditorApp::ServiceName
                             << "is held by an unreachable instance. The existing editor may be hung —"
                             << "kill the stale plasmazones-editor process and try again.";
        return 1;
    }

    controller.applyLaunchArgs(targetScreen, layoutIdArg, createNewLayout, previewArg);

    // Set up QML engine
    QQmlApplicationEngine engine;

    // Set up i18n for QML (makes i18n() available in QML)
    auto* localizedContext = new PzLocalizedContext(&engine);
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
