// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorServiceIconTheme/QmlRegistration.h>
#include <PhosphorServiceMpris/QmlRegistration.h>
#include <PhosphorServicePipeWire/QmlRegistration.h>
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
    //   PipeWire  Phosphor.Service.PipeWire 1.0  (PipeWireHost, node models)
    // One call per lib here at startup is sufficient. The wrapper
    // functions are idempotent (each lib guards its registration with
    // std::call_once internally), so a future hot-reload hook that
    // re-invokes them per fresh QQmlEngine is also safe. The bare Qt
    // primitive qmlRegisterType is NOT idempotent (Qt warns and
    // overwrites on second registration), which is the reason the
    // per-lib guard exists.
    PhosphorServiceSni::registerQmlTypes();
    PhosphorServiceIconTheme::registerQmlTypes();
    PhosphorServiceUPower::registerQmlTypes();
    PhosphorServiceMpris::registerQmlTypes();
    PhosphorServicePipeWire::registerQmlTypes();

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
        const QString message = QStringLiteral(
                                    "No shell.qml found.\n"
                                    "  Searched:    %1\n"
                                    "               and ${XDG_DATA_DIRS}/phosphor-shell/\n\n"
                                    "  To get started, copy the bundled example:\n"
                                    "    mkdir -p %1\n"
                                    "    cp -r /usr/share/phosphor-shell/* %1")
                                    .arg(configDir);
        qCCritical(lcShell).noquote() << message;
        return 1;
    }

    qCInfo(lcShell) << "Loading shell from:" << shellUrl.toLocalFile();

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
