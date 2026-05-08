// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorShell/ShellEngine.h>
#include <PhosphorShell/ShellLoader.h>
#include <PhosphorWayland/LayerShellPluginLoader.h>

#include <PhosphorLayer/SurfaceFactory.h>
#include <PhosphorLayer/defaults/DefaultScreenProvider.h>
#include <PhosphorLayer/defaults/PhosphorWaylandTransport.h>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(PhosphorShellPlugin)

Q_LOGGING_CATEGORY(lcShell, "phosphorshell")

int main(int argc, char* argv[])
{
    PhosphorWayland::registerLayerShellPlugin();

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("phosphor-shell"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setQuitOnLastWindowClosed(false);

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
        qCCritical(lcShell) << "No shell.qml found. Searched:" << loader.shellConfigDir();
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
