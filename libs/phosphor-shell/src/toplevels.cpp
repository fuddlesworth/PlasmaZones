// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/Toplevels.h>

#include <QCoreApplication>
#include <QQmlEngine>
#include <QThread>

namespace PhosphorShell {

using PhosphorWayland::ForeignToplevel;
using PhosphorWayland::ForeignToplevelManager;

ForeignToplevelManager* Toplevels::sharedManager()
{
    // GUI-thread-only contract: ForeignToplevelManager binds Wayland
    // protocol globals through QPA, which lives on the main thread.
    // The function-static singleton is not protected by a mutex, so a
    // worker-thread caller would race on the unguarded check. Assert
    // rather than try to support multi-thread access — the underlying
    // protocol bindings would also misbehave.
    Q_ASSERT_X(qApp && QThread::currentThread() == qApp->thread(), "Toplevels::sharedManager",
               "must be called from the GUI thread");

    // Process-wide singleton. Parent to qApp so the manager is destroyed
    // exactly once, at QCoreApplication teardown — never during a hot-
    // reload's engine swap. Two managers cannot coexist because the
    // wlroots protocol's `add_listener` call is one-time per proxy and
    // double-binding produces duplicate event streams.
    static ForeignToplevelManager* instance = nullptr;
    if (!instance && qApp) {
        instance = new ForeignToplevelManager(qApp);
    }
    return instance;
}

Toplevels::Toplevels(QObject* parent)
    : QObject(parent)
{
    auto* mgr = sharedManager();
    if (mgr) {
        connect(mgr, &ForeignToplevelManager::toplevelAdded, this, &Toplevels::toplevelsChanged);
        connect(mgr, &ForeignToplevelManager::toplevelRemoved, this, &Toplevels::toplevelsChanged);
    }
}

Toplevels::~Toplevels() = default;

Toplevels* Toplevels::create(QQmlEngine* engine, QJSEngine* scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)
    // Per-engine wrapper around the shared manager. The QML registry
    // deletes the wrapper on engine teardown — the underlying manager
    // (parented to qApp) survives, so a hot-reload that recreates the
    // engine does NOT rebind the protocol global.
    return new Toplevels();
}

QList<ForeignToplevel*> Toplevels::toplevels() const
{
    auto* mgr = sharedManager();
    return mgr ? mgr->toplevels() : QList<ForeignToplevel*>{};
}

bool Toplevels::isSupported() const
{
    return ForeignToplevelManager::isSupported();
}

} // namespace PhosphorShell
