// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-perscreen-demo. Acceptance harness for the Phosphor.Shell
// PerScreen helper (Phase 1.5).
//
// Wires the QGuiApplication-backed DefaultScreenProvider through
// ScreenModel and exposes the model to QML as a context property. The
// QML side declares a `PerScreen` with a small Window delegate that
// shows each screen's name, index, and primary flag. Hot-plug a
// monitor → a new Window appears; unplug → its Window disappears.
// Delegate identity survives the hot-plug for monitors that didn't
// change (the reuse contract pinned by the PerScreen tests).

#include <PhosphorLayer/defaults/DefaultScreenProvider.h>
#include <PhosphorShell/ScreenModel.h>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Phosphor"));
    QCoreApplication::setApplicationName(QStringLiteral("phosphor-perscreen-demo"));

    // Use the Basic style so the demo doesn't pull a platform theme's
    // visual chrome — keeps the per-monitor window minimal.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // DefaultScreenProvider mirrors QGuiApplication::screens() and
    // emits the screensChanged / primaryChanged notifier signals
    // ScreenModel listens on. Production shells inject a different
    // provider (virtual-screen-aware); for a standalone demo the
    // default is exactly right.
    PhosphorLayer::DefaultScreenProvider provider;
    PhosphorShell::ScreenModel screensModel(&provider);

    QQmlApplicationEngine engine;
    // Expose the screen model as `screensModel` context property.
    // Production shells expose it through `PhosphorShell.screens` via
    // ShellGlobal, but spinning up a ShellGlobal + ShellEngine is
    // overkill for a five-monitor sanity demo. PerScreen accepts any
    // QAbstractItemModel-shaped source so the context-property route
    // is equivalent at the consumer's call site.
    engine.rootContext()->setContextProperty(QStringLiteral("screensModel"), &screensModel);

    engine.loadFromModule(QStringLiteral("Phosphor.PerScreenDemo"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        qWarning("phosphor-perscreen-demo: failed to load Phosphor.PerScreenDemo/Main.qml");
        return 1;
    }

    // Tear PerScreen's delegates down BEFORE the engine starts
    // destruction. PerScreen creates each Window with no QObject
    // parent (the JS-Map holds the reference) so the Wayland
    // compositor commits each as a true top-level surface; without
    // this aboutToQuit hook the JS GC would destroy those Windows
    // during the engine destructor and race the Wayland platform
    // cleanup, SIGSEGV-ing at exit.
    QObject* root = engine.rootObjects().first();
    QObject::connect(&app, &QGuiApplication::aboutToQuit, root, [root]() {
        QMetaObject::invokeMethod(root, "shutdownDelegates");
    });

    return app.exec();
}
