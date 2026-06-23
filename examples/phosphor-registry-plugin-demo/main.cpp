// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-registry-plugin-demo entry point. Toy bar with two
// built-in widgets plus a third loaded from a .so + manifest.json
// via PluginLoader.
//
// The --plugin-root flag (default: ${GenericDataLocation}/phosphor/plugins/)
// lets developers point the loader at a per-build directory the
// CPU-meter plugin's CMakeLists writes the .so into. CI runs the
// demo with --plugin-root=${CMAKE_BINARY_DIR}/registry-plugin-demo-plugins.

#include "DemoController.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QString>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Phosphor"));
    QCoreApplication::setApplicationName(QStringLiteral("phosphor-registry-plugin-demo"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("phosphor-registry plugin loader demo"));
    parser.addHelpOption();
    QCommandLineOption pluginRootOption(
        QStringList{QStringLiteral("plugin-root")},
        QStringLiteral("Directory the plugin loader scans for .so + manifest.json pairs. Defaults to the XDG data "
                       "location for phosphor plugins."),
        QStringLiteral("path"));
    parser.addOption(pluginRootOption);
    parser.process(app);

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Resolve the plugin root in priority order:
    //   1. --plugin-root from the CLI (CI + manual testing override)
    //   2. PHOSPHOR_REGISTRY_PLUGIN_DEMO_DEFAULT_ROOT injected at
    //      build time, pointing at ${CMAKE_BINARY_DIR}/registry-plugin-demo-plugins/.
    //      Lets `./bin/phosphor-registry-plugin-demo` work out of the
    //      build tree without arguments.
    //   3. Empty string — PluginLoader then falls back to
    //      ${GenericDataLocation}/phosphor/plugins/ (the installed-
    //      deployment default).
    QString pluginRoot = parser.value(pluginRootOption);
#ifdef PHOSPHOR_REGISTRY_PLUGIN_DEMO_DEFAULT_ROOT
    if (pluginRoot.isEmpty()) {
        pluginRoot = QStringLiteral(PHOSPHOR_REGISTRY_PLUGIN_DEMO_DEFAULT_ROOT);
    }
#endif

    // demoController is declared BEFORE the engine so C++ reverse-
    // order destruction tears the engine down first. Mirrors
    // phosphor-popout-demo's precedent — see the in-process registry
    // demo for the longer-form rationale.
    PhosphorRegistryPluginDemo::DemoController demoController(pluginRoot);

    QQmlApplicationEngine engine;
    demoController.setEngine(&engine);
    engine.rootContext()->setContextProperty(QStringLiteral("demoController"), &demoController);

    engine.loadFromModule(QStringLiteral("Phosphor.RegistryPluginDemo"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return app.exec();
}
