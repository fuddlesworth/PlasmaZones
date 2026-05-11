// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorServices/QmlRegistration.h>
#include <PhosphorShell/ShellEngine.h>
#include <PhosphorShell/ShellLoader.h>
#include <PhosphorWayland/LayerShellPluginLoader.h>

#include <PhosphorLayer/SurfaceFactory.h>
#include <PhosphorLayer/defaults/DefaultScreenProvider.h>
#include <PhosphorLayer/defaults/PhosphorWaylandTransport.h>

#include <QGuiApplication>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcShell, "phosphorshell")

int main(int argc, char* argv[])
{
    PhosphorWayland::registerLayerShellPlugin();

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("phosphor-shell"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setQuitOnLastWindowClosed(false);

    // Register PhosphorServices QML types BEFORE the engine loads
    // shell.qml. The shell's tray Repeater binds StatusNotifierHost +
    // StatusNotifierItemModel which both live under Phosphor.Services
    // 1.0 — without this they'd surface as "not a type" errors at
    // QML load time. Idempotent under repeated calls.
    PhosphorServices::registerQmlTypes();

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
        qCCritical(lcShell).noquote() << "No shell.qml found.\n"
                                      << "  Searched:    " << configDir << "\n"
                                      << "               and ${XDG_DATA_DIRS}/phosphor-shell/\n\n"
                                      << "  To get started, copy the bundled example:\n"
                                      << "    mkdir -p " << configDir << "\n"
                                      << "    cp -r /usr/share/phosphor-shell/* " << configDir;
        return 1;
    }

    qCInfo(lcShell) << "Loading shell from:" << shellUrl.toLocalFile();

    PhosphorShell::ShellEngine engine(
        PhosphorShell::ShellEngine::Deps{
            .surfaceFactory = &factory,
            .screenProvider = screenProvider.get(),
        },
        &app);

    if (!engine.load(shellUrl)) {
        return 1;
    }

    return app.exec();
}
