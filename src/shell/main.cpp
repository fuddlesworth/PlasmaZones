// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorServiceBluetooth/QmlRegistration.h>
#include <PhosphorServiceBrightness/QmlRegistration.h>
#include <PhosphorServiceClipboard/QmlRegistration.h>
#include <PhosphorServiceIconTheme/QmlRegistration.h>
#include <PhosphorServiceIdle/QmlRegistration.h>
#include <PhosphorServiceLock/QmlRegistration.h>
#include <PhosphorServiceMpris/QmlRegistration.h>
#include <PhosphorServiceNetwork/QmlRegistration.h>
#include <PhosphorServiceNotifications/QmlRegistration.h>
#include <PhosphorServicePipeWire/QmlRegistration.h>
#include <PhosphorServicePolkit/QmlRegistration.h>
#include <PhosphorServiceSession/QmlRegistration.h>
#include <PhosphorServiceSni/QmlRegistration.h>
#include <PhosphorServiceUPower/QmlRegistration.h>
#include <PhosphorShell/ShellEngine.h>
#include <PhosphorShell/ShellLoader.h>
#include <PhosphorWayland/LayerShellPluginLoader.h>

#include <PhosphorLayer/SurfaceFactory.h>
#include <PhosphorLayer/defaults/DefaultScreenProvider.h>
#include <PhosphorLayer/defaults/PhosphorWaylandTransport.h>

#include <QGuiApplication>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcShell, "phosphorshell.main")

int main(int argc, char* argv[])
{
    // MUST run before QGuiApplication is constructed: selects the
    // phosphorwayland Wayland shell-integration plugin (via the
    // QT_WAYLAND_SHELL_INTEGRATION env var that Qt Wayland's
    // QWaylandIntegration consults during platform init). Inserting any
    // QGuiApplication-touching call between this line and the
    // QGuiApplication ctor would let Qt pick the default xdg-shell
    // integration without the layer-shell hooks, silently disabling
    // overlay positioning across the whole shell.
    PhosphorWayland::registerLayerShellPlugin();

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("phosphor-shell"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setQuitOnLastWindowClosed(false);

    // Register every Phosphor.Service.* QML type BEFORE the engine
    // loads shell.qml. Post Phase 2.0 the umbrella is gone; each
    // service lib owns its own module URI:
    //   SNI       Phosphor.Service.Sni 1.0       (StatusNotifierHost, models, items)
    //   IconTheme Phosphor.Service.IconTheme 1.0 (IconThemeResolver singleton)
    //   UPower    Phosphor.Service.UPower 1.0    (UPowerHost, devices, model)
    //   Mpris     Phosphor.Service.Mpris 1.0     (MprisHost, players, model)
    //   PipeWire  Phosphor.Service.PipeWire 1.0  (PipeWireHost, node
    //                                            models PwSinkModel /
    //                                            PwSourceModel /
    //                                            PwStreamModel, plus
    //                                            PwNode and
    //                                            PipeWireConnection
    //                                            registered as
    //                                            uncreatable for type
    //                                            visibility)
    //   Network   Phosphor.Service.Network 1.0   (NetworkHost +
    //                                            NetworkDeviceModel /
    //                                            AccessPointModel /
    //                                            NetworkConnectionModel,
    //                                            plus NetworkDevice /
    //                                            AccessPoint /
    //                                            NetworkConnection
    //                                            registered as
    //                                            uncreatable for type
    //                                            visibility)
    //   Bluetooth Phosphor.Service.Bluetooth 1.0 (BluetoothHost +
    //                                            BluetoothAdapterModel /
    //                                            BluetoothDeviceModel,
    //                                            plus BluetoothAdapter /
    //                                            BluetoothDevice /
    //                                            BluetoothAgent
    //                                            registered as
    //                                            uncreatable for type
    //                                            visibility)
    //   Brightness Phosphor.Service.Brightness 1.0 (BrightnessHost +
    //                                            BrightnessDeviceModel,
    //                                            plus BrightnessDevice
    //                                            registered as
    //                                            uncreatable for type
    //                                            visibility)
    // One call per lib here at startup is sufficient. The wrapper
    // functions are idempotent (each lib guards its registration with
    // std::call_once internally), so a future hot-reload hook that
    // re-invokes them per fresh QQmlEngine is also safe. The bare Qt
    // primitive qmlRegisterType is NOT idempotent (Qt warns and
    // overwrites on second registration), which is the reason the
    // per-lib guard exists.
    //
    // Each registerQmlTypes() returns void on purpose: by current design
    // every service's registration is idempotent AND infallible —
    // std::call_once gates each body to a one-shot. Services that
    // register a QML singleton (e.g. IconTheme's IconThemeResolver and
    // PipeWire's PipeWireHost) additionally guard on
    // QCoreApplication::instance() and return silently (logging a warning
    // instead of throwing) if a pre-condition fails; the others register
    // only types and have nothing to fail on. There is intentionally no
    // failure surface to inspect, which is why this loop doesn't check
    // return values. If any future service grows environment-dependent
    // registration logic (e.g. needs to fail hard when a required platform
    // feature is absent, or needs to surface a registration error to the
    // shell launcher), this loop must change accordingly: the wrappers will
    // need to start returning success status and the loop will need to
    // handle a partial-init scenario.
    PhosphorServiceSni::registerQmlTypes();
    PhosphorServiceIconTheme::registerQmlTypes();
    PhosphorServiceUPower::registerQmlTypes();
    PhosphorServiceMpris::registerQmlTypes();
    PhosphorServicePipeWire::registerQmlTypes();
    PhosphorServiceNetwork::registerQmlTypes();
    PhosphorServiceBluetooth::registerQmlTypes();
    PhosphorServiceBrightness::registerQmlTypes();
    PhosphorServiceNotifications::registerQmlTypes();
    PhosphorServicePolkit::registerQmlTypes();
    PhosphorServiceIdle::registerQmlTypes();
    PhosphorServiceClipboard::registerQmlTypes();
    PhosphorServiceLock::registerQmlTypes();
    PhosphorServiceSession::registerQmlTypes();

    auto screenProvider = std::make_unique<PhosphorLayer::DefaultScreenProvider>();
    auto transport = std::make_unique<PhosphorLayer::PhosphorWaylandTransport>();

    PhosphorLayer::SurfaceFactory::Deps factoryDeps{
        .transport = transport.get(),
        .screens = screenProvider.get(),
        .engineProvider = nullptr,
        .animator = nullptr,
        .loggingCategory = QStringLiteral("phosphorshell.surface"),
    };
    PhosphorLayer::SurfaceFactory factory(factoryDeps, &app);

    PhosphorShell::ShellLoader loader;
    const QUrl shellUrl = loader.resolve();
    if (shellUrl.isEmpty()) {
        const QString configDir = loader.shellConfigDir();
        // Build the full diagnostic in one QString. Chaining many
        // `<<` operands through QDebug.noquote() inserts a space
        // separator between each operand even with .noquote(), which
        // produced "  Searched:     /home/..." (double space) and a
        // trailing space at every line break. Cosmetic on stderr but
        // visible in log-scraping tools that key on the layout.
        //
        // The path list mirrors what ShellLoader::resolve() actually
        // probes (GenericConfigLocation + GenericDataLocation), not a
        // simplified subset — a user with their shell installed under
        // XDG_DATA_HOME or XDG_CONFIG_DIRS needs to see those classes
        // listed too. The hard-coded /usr/share path in the recovery
        // hint matches the default CMake prefix; packagers building
        // against a non-default CMAKE_INSTALL_PREFIX should patch the
        // hint in their downstream tree (or we plumb the prefix through
        // a generated header in a follow-up — see ShellLoader::resolve).
        const QString message = QStringLiteral(
                                    "No shell.qml found.\n"
                                    "  Searched:    %1\n"
                                    "               and ${XDG_CONFIG_DIRS}/phosphor-shell/\n"
                                    "               and ${XDG_DATA_HOME}/phosphor-shell/\n"
                                    "               and ${XDG_DATA_DIRS}/phosphor-shell/\n\n"
                                    "  To get started, copy the bundled example:\n"
                                    "    mkdir -p %1\n"
                                    "    cp -r ${CMAKE_INSTALL_PREFIX}/share/phosphor-shell/* %1\n"
                                    "  (replace ${CMAKE_INSTALL_PREFIX} with the prefix used at install; "
                                    "/usr or /usr/local for distro packages.)")
                                    .arg(configDir);
        qCCritical(lcShell).noquote() << message;
        return 1;
    }

    // Use toString() rather than toLocalFile(): toLocalFile() returns
    // an empty string for non-file URLs (qrc:, http:, etc.), which
    // would produce an uninformative "Loading shell from: " log line
    // when ShellLoader resolves to a bundled qrc resource. toString()
    // always yields the full URL form, so the log entry is meaningful
    // regardless of the URL scheme.
    qCInfo(lcShell) << "Loading shell from:" << shellUrl.toString();

    PhosphorShell::ShellEngine engine(
        PhosphorShell::ShellEngine::Deps{
            .surfaceFactory = &factory,
            .screenProvider = screenProvider.get(),
        },
        &app);

    // Mount the icon image provider on every QQmlEngine the shell
    // constructs (startup + every hot-reload). Without this the
    // tray Image.source URLs published by StatusNotifierItemModel
    // fall through to "image provider not found" and panel icons
    // render as broken-image placeholders. The provider lives in
    // phosphor-service-icontheme post Phase 2.0; phosphor-service-sni
    // publishes raw IconPixmap blobs through its static registry via
    // PhosphorServiceIconTheme::IconImageProvider::setImage.
    engine.addEngineHook([](QQmlEngine* qmlEngine) {
        PhosphorServiceIconTheme::installImageProvider(qmlEngine);
    });

    if (!engine.load(shellUrl)) {
        return 1;
    }

    return app.exec();
}
