// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIpc/IpcEngine.h>

#include <PhosphorIpc/IpcRouter.h>

#include <QHash>
#include <QPointer>
#include <QQmlEngine>
#include <QVariant>

namespace PhosphorIpc::IpcEngine {

namespace {
constexpr auto RouterPropertyName = "phosphorIpcRouter";

// Per-engine handle on the destroyed-watcher install() wires, so
// uninstall() / install()-on-replace can disconnect ONLY our wire
// and not collaterally tear down any other code's connections that
// happen to use `engine` as their context receiver. Without this,
// `QObject::disconnect(router, &destroyed, engine, nullptr)` would
// be a wildcard that drops every destroyed→engine connection in
// the process. Keyed by engine because there's at most one router
// installed per engine; the QObject::Connection is the precise
// disconnect handle Qt's docs require for surgical teardown.
QHash<QQmlEngine*, QMetaObject::Connection>& destroyedWatchers()
{
    static QHash<QQmlEngine*, QMetaObject::Connection> watchers;
    return watchers;
}

// Read the engine's dynamic property and qobject_cast it back to an
// IpcRouter. The router is stored as a raw QObject*; install() wires
// a destroyed-signal connection so the property auto-clears when the
// router dies before uninstall() runs.
IpcRouter* readStoredRouter(QQmlEngine* engine)
{
    return qobject_cast<IpcRouter*>(engine->property(RouterPropertyName).value<QObject*>());
}

void clearDestroyedWatcher(QQmlEngine* engine)
{
    auto& watchers = destroyedWatchers();
    const auto it = watchers.find(engine);
    if (it == watchers.end()) {
        return;
    }
    QObject::disconnect(it.value());
    watchers.erase(it);
}
} // namespace

void install(QQmlEngine* engine, IpcRouter* router)
{
    if (!engine) {
        qWarning("PhosphorIpc::IpcEngine::install: null engine ignored");
        return;
    }
    if (!router) {
        qWarning("PhosphorIpc::IpcEngine::install: null router; call uninstall() to drop the binding explicitly");
        return;
    }
    IpcRouter* const existing = readStoredRouter(engine);
    if (existing == router) {
        return; // idempotent, same router on same engine is fine
    }
    if (existing) {
        // Drop ONLY our prior destroyed-watcher (precise disconnect
        // via stored Connection handle, NOT a wildcard). Otherwise
        // the outgoing router's destruction would clear the engine
        // property out from under the replacement.
        clearDestroyedWatcher(engine);
        qWarning("PhosphorIpc::IpcEngine::install: replacing existing router on engine");
    }
    engine->setProperty(RouterPropertyName, QVariant::fromValue<QObject*>(router));
    // Auto-clear the property if the router is destroyed before
    // uninstall() runs. Without this, a raw QObject* in the QVariant
    // would dangle; routerFor() would qobject_cast a freed pointer
    // (UB). The engine-bound context guarantees the connection is
    // torn down when the engine dies, avoiding leaked lambdas.
    auto& watchers = destroyedWatchers();
    const bool firstInstallOnEngine = !watchers.contains(engine);
    const QMetaObject::Connection conn = QObject::connect(router, &QObject::destroyed, engine, [engine, router]() {
        // Only clear if the engine still holds THIS router. A
        // subsequent install() may have replaced it; the
        // clearDestroyedWatcher-on-replace above prevents that
        // path from entering this lambda, but we double-check
        // defensively because a setProperty from foreign code
        // could leave us here pointing at someone else's data.
        if (engine->property(RouterPropertyName).value<QObject*>() == router) {
            engine->setProperty(RouterPropertyName, QVariant());
        }
        destroyedWatchers().remove(engine);
    });
    // Engine-destroyed path: drop our QHash entry so it doesn't grow
    // unbounded across long-running test runs / processes with many
    // engines created and destroyed. Connected exactly once per
    // engine; repeated install()s on the same engine reuse this
    // same engine-destroyed handler so we don't accumulate copies.
    if (firstInstallOnEngine) {
        QObject::connect(engine, &QObject::destroyed, [engine]() {
            destroyedWatchers().remove(engine);
        });
    }
    watchers.insert(engine, conn);
}

void uninstall(QQmlEngine* engine)
{
    if (!engine) {
        return;
    }
    // Drop our destroyed-watcher first so an explicit uninstall
    // can't get retro-fired by the outgoing router's later
    // destruction.
    clearDestroyedWatcher(engine);
    engine->setProperty(RouterPropertyName, QVariant());
}

IpcRouter* routerFor(QQmlEngine* engine)
{
    if (!engine) {
        return nullptr;
    }
    return readStoredRouter(engine);
}

} // namespace PhosphorIpc::IpcEngine
