// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIpc/IpcEngine.h>

#include <PhosphorIpc/IpcRouter.h>

#include <QHash>
#include <QPointer>
#include <QQmlEngine>
#include <QSet>
#include <QThread>
#include <QVariant>

namespace PhosphorIpc::IpcEngine {

namespace {
constexpr auto RouterPropertyName = "phosphorIpcRouter";

// Per-engine handle on the router-destroyed watcher install() wires.
// Keyed by engine because there's at most one router installed per
// engine; the QObject::Connection lets uninstall() / install()-on-
// replace disconnect ONLY our wire instead of wildcard-dropping
// every destroyed→engine connection in the process.
QHash<QQmlEngine*, QMetaObject::Connection>& destroyedWatchers()
{
    static QHash<QQmlEngine*, QMetaObject::Connection> watchers;
    return watchers;
}

// Engines for which install() has already wired the engine->destroyed
// cleanup handler. We attach that handler exactly once per engine for
// the engine's entire lifetime, NOT once per install() call.
// Otherwise repeated install/uninstall/install cycles (which clear
// the destroyedWatchers entry between cycles) would each add another
// engine-destroyed lambda, accumulating dead connections until the
// engine dies. The set entry is dropped only by the engine-destroyed
// handler itself.
QSet<QQmlEngine*>& engineDestroyHandlerInstalled()
{
    static QSet<QQmlEngine*> engines;
    return engines;
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
    // The destroyedWatchers / engineDestroyHandlerInstalled globals
    // below are not thread-guarded; the public contract is that
    // install / uninstall / routerFor run on the engine's owning
    // thread (QQmlEngine is itself thread-affine, so this is the
    // natural contract). We assert (catches misuse loudly in debug)
    // AND return (refuses to corrupt unsynchronised state in
    // release) so a misuse can't silently race the global QHash /
    // QSet.
    Q_ASSERT(engine->thread() == QThread::currentThread());
    if (engine->thread() != QThread::currentThread()) {
        qWarning("PhosphorIpc::IpcEngine::install: called off the engine's owning thread; ignored");
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
    // Engine-destroyed path: drop our bookkeeping when the engine
    // dies. The handler is attached exactly ONCE per engine across
    // the engine's whole lifetime (gated by engineDestroyHandlerInstalled
    // — a separate set that survives uninstall()/clearDestroyedWatcher).
    // Otherwise repeated install/uninstall/install cycles would
    // accumulate engine-destroyed lambdas (one per cycle).
    auto& handlerInstalled = engineDestroyHandlerInstalled();
    if (!handlerInstalled.contains(engine)) {
        // Bind the lambda's invocation context to the engine itself
        // (4-arg connect) for symmetry with the router-destroyed
        // wiring above. The engine is also the sender here, but
        // making the context explicit pins the thread affinity and
        // matches the project pattern.
        QObject::connect(engine, &QObject::destroyed, engine, [engine]() {
            destroyedWatchers().remove(engine);
            engineDestroyHandlerInstalled().remove(engine);
        });
        handlerInstalled.insert(engine);
    }
    watchers.insert(engine, conn);
}

void uninstall(QQmlEngine* engine)
{
    if (!engine) {
        return;
    }
    Q_ASSERT(engine->thread() == QThread::currentThread());
    if (engine->thread() != QThread::currentThread()) {
        qWarning("PhosphorIpc::IpcEngine::uninstall: called off the engine's owning thread; ignored");
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
    Q_ASSERT(engine->thread() == QThread::currentThread());
    if (engine->thread() != QThread::currentThread()) {
        qWarning("PhosphorIpc::IpcEngine::routerFor: called off the engine's owning thread; returning nullptr");
        return nullptr;
    }
    return readStoredRouter(engine);
}

} // namespace PhosphorIpc::IpcEngine
