// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BarController.h"
#include "ControlCenterController.h"
#include "LauncherController.h"
#include "LayerPopoutTransport.h"
#include "RoutingPopoutTransport.h"
#include "SocketPopoutTransport.h"

#include <PhosphorShellLauncher/LauncherModel.h>

#include <PhosphorServiceIdle/IdleService.h>

#include <PhosphorPopout/PopoutController.h>

#include <PhosphorIpc/IpcEngine.h>
#include <PhosphorIpc/IpcRouter.h>

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
#include <QIcon>
#include <QLoggingCategory>
#include <QQmlContext>
#include <QQmlEngine>
#include <QUrl>

#include <memory>

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

    // Guarantee named freedesktop icons resolve (bar widgets use
    // Kirigami.Icon → QIcon::fromTheme). On a desktop session the platform
    // theme usually sets a theme already; setting only the FALLBACK leaves
    // the user's choice intact and just backstops a session that exposes
    // none, so icons never silently come up blank.
    if (QIcon::fallbackThemeName().isEmpty()) {
        QIcon::setFallbackThemeName(QStringLiteral("breeze"));
    }

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

    // The bar's IBarWidgetFactory registry owner. Declared BEFORE the
    // engine so C++ reverse-order destruction tears the engine down first
    // (clearing every QML binding to the BarRegistry context property)
    // before the controller dies. It is process-global, outliving every
    // hot-reload engine rebuild; the engine hook below re-binds it on each
    // fresh QQmlEngine, and createWidgetFor resolves the live engine from
    // each widget's parent so no stale-engine reference is held.
    PhosphorShellApp::BarController barController;

    // The shell's ONE idle ladder, and the control center's tile registry
    // that borrows it.
    //
    // Declared before the engine for the same reverse-destruction reason as
    // barController. The service is constructed here rather than inside
    // IdleTile because a tile-owned IdleService would arm a second,
    // independent ladder: an inhibition taken through the tile would not
    // hold open the ladder anything else observes, and the tile's own
    // "keep awake" state would be invisible to the rest of the shell.
    // ControlCenterController hands it to IdleTile as an initial property.
    //
    // Service order matters: idleService must outlive the controller that
    // hands out pointers to it, which reverse-declaration order gives.
    PhosphorServiceIdle::IdleService idleService;
    PhosphorShellApp::ControlCenterController controlCenterController(&idleService);

    // The launcher's registry owner, process-global for the same reason
    // as the two controllers above: the apps scan, clipboard history and
    // window list it holds must survive every hot-reload engine rebuild.
    // Declared before the engine so reverse destruction tears the engine
    // (and every QML binding to LauncherResults) down first.
    PhosphorShellApp::LauncherController launcherController;

    // The IPC router every IpcTarget in the shell's QML registers with.
    // Declared before the engine for the same reverse-destruction reason as
    // the others: targets unregister themselves on destruction and must find
    // a live router when they do.
    //
    // start() binds the Unix socket; without it every target registers
    // against a router nobody can reach and `phosphorctl call` resolves
    // nothing. $PHOSPHOR_SOCKET mirrors phosphorctl's own resolution
    // (--socket > $PHOSPHOR_SOCKET > $XDG_RUNTIME_DIR/phosphor.sock), which
    // is what lets a nested test session bind a private socket instead of
    // colliding with a shell on the host session. Non-fatal on failure: a
    // shell without its control socket still draws bars and popouts, so warn
    // and continue rather than refusing to start.
    PhosphorIpc::IpcRouter ipcRouter;
    const QString socketOverride = qEnvironmentVariable("PHOSPHOR_SOCKET");
    if (!ipcRouter.start(socketOverride)) {
        qCWarning(lcShell) << "IPC router failed to bind"
                           << (socketOverride.isEmpty() ? QStringLiteral("the default socket") : socketOverride)
                           << "— phosphorctl will not reach this shell";
    } else {
        qCInfo(lcShell) << "IPC socket:" << ipcRouter.socketPath();
    }

    // Popout infrastructure, declared BEFORE the engine for the same
    // reverse-destruction reason as barController: the engine must die (and
    // clear every QML binding to these) before they do.
    //
    // Transport FIRST, controller SECOND. ~PopoutController detaches its
    // dismissed callback by calling back into the transport, so the
    // controller has to be destroyed while the transport is still alive —
    // which reverse-declaration order gives us for free.
    PhosphorShellApp::LayerPopoutTransport popoutTransport(&factory, screenProvider.get());
    // The bar-socket transport and the router in front of both. The
    // controller sees ONE transport, so its arbitration (Modal closes
    // Cooperative, Cooperative refused while a modal is up, closeAll on
    // reload) covers the control center painted into the bar exactly as it
    // covers popouts with surfaces of their own. Routing is by popout id:
    // "control-center" is the one socket-hosted popout today. Declared
    // after the layer transport and before the controller, so reverse
    // destruction tears the controller down first.
    PhosphorShellApp::SocketPopoutTransport socketTransport(&controlCenterController);
    PhosphorShellApp::RoutingPopoutTransport routedTransport(&popoutTransport, &socketTransport,
                                                             {QStringLiteral("control-center")});
    PhosphorPopout::PopoutController popouts(&routedTransport);

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

    // The transport builds popout content from the live engine, so it needs
    // the new one on every hot reload. Paired with the aboutToReload drain
    // below: that drops the outgoing engine's surfaces while its object
    // graph is still valid, and this adopts the replacement.
    engine.addEngineHook([&popoutTransport](QQmlEngine* qmlEngine) {
        popoutTransport.setEngine(qmlEngine);
    });

    // Bar-anchored popouts hang below the bar's reserved band. The popout
    // surface is full-bleed and learns nothing about other surfaces' zones
    // from the compositor, but this engine placed every panel and knows
    // what each reserved. Read live per open, so a reload that changes the
    // bar's thickness is reflected on the next popout.
    popoutTransport.setReservedMarginsProvider([&engine](QScreen* screen) {
        return engine.reservedMarginsFor(screen);
    });

    // IpcTarget resolves its router from a property stashed on the engine,
    // so this has to run for every fresh engine, not once at startup.
    // Without it each target warns and stays inert, and `phosphorctl call`
    // finds nothing.
    engine.addEngineHook([&ipcRouter](QQmlEngine* qmlEngine) {
        PhosphorIpc::IpcEngine::install(qmlEngine, &ipcRouter);
    });

    // A hot reload destroys the QQmlEngine and every delegate built from it.
    // Drain while that is still safe to touch, rather than discovering it
    // afterwards through dangling QPointers.
    // Context object is `popouts`, the SHORTEST-lived of the two captures
    // (it is declared after the transport, so it is destroyed first).
    // Auto-disconnect has to key on whichever capture dies first, or the
    // lambda outlives one of the references it holds.
    QObject::connect(&engine, &PhosphorShell::ShellEngine::aboutToReload, &popouts, [&popoutTransport, &popouts] {
        popouts.closeAll();
        popoutTransport.drain();
    });

    // Surfaces must be gone before the QML engine and the Wayland
    // connection unwind. Without this the transport tears down during
    // static destruction, which is where Qt object graphs misbehave.
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &popouts, [&popoutTransport, &popouts] {
        popouts.closeAll();
        popoutTransport.drain();
    });

    engine.addEngineHook([&popouts](QQmlEngine* qmlEngine) {
        // Context property rather than qmlRegisterSingletonInstance: the
        // Phosphor.Popout URI already belongs to a qt_add_qml_module, and
        // the BarRegistry precedent for re-binding a process-global C++
        // object onto each fresh engine is already proven across reloads.
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("Popouts"), &popouts);
    });

    // Expose the bar widget registry to QML as the BarRegistry context
    // property on every engine the shell builds (startup + each hot reload).
    // Slot.qml mounts each delegate through
    // BarRegistry.createWidgetFor(id, parent).
    engine.addEngineHook([&barController](QQmlEngine* qmlEngine) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("BarRegistry"), &barController);
    });

    // The control center's tile registry, bound the same way and for the
    // same reason: ControlCenter mounts each tile through
    // ControlCenterRegistry.createTile(id, parent) on every engine the
    // shell builds, startup and each hot reload alike.
    engine.addEngineHook([&controlCenterController](QQmlEngine* qmlEngine) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("ControlCenterRegistry"), &controlCenterController);
    });

    // The launcher's results model, bound the same way. shell.qml's
    // launcher popout reads it as `results: LauncherResults`, the same
    // context-property name the launcher demo uses, so the two bind
    // identically. A plain QObject owned by C++, re-installed on every
    // engine the shell builds.
    engine.addEngineHook([&launcherController](QQmlEngine* qmlEngine) {
        qmlEngine->rootContext()->setContextProperty(QStringLiteral("LauncherResults"), launcherController.model());
    });

    if (!engine.load(shellUrl)) {
        return 1;
    }

    return app.exec();
}
