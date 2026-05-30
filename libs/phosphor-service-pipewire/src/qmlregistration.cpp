// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePipeWire/QmlRegistration.h>

#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PipeWireHost.h>
#include <PhosphorServicePipeWire/PwNode.h>
#include <PhosphorServicePipeWire/PwNodeModel.h>

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QPointer>
#include <QQmlEngine>
#include <QThread>

#include <mutex>

Q_LOGGING_CATEGORY(lcPipeWireQml, "phosphor.service.pipewire.qml")

namespace PhosphorServicePipeWire {

// QML type-name maintenance contract:
//
// Every qmlRegister* call below takes a C++ type as a template argument
// AND a string literal QML name. The two must stay in lock-step:
//
//   * If you rename a C++ type below (PipeWireConnection, PwNode,
//     PwNodeModel, PwSinkModel, PwSourceModel, PwStreamModel,
//     PipeWireHost), also rename the corresponding string-literal QML
//     name argument here. The compiler will NOT catch a stale QML name
//     — the template argument and the literal are independent.
//
//   * When adding or removing a registered type, audit ALL qmlRegister*
//     calls in this function (uncreatable, type, singleton) to keep
//     the module surface consistent and to spot any sibling registration
//     that has drifted out of sync.
//
// A constexpr table mapping `cppName` to QML metadata would mechanise
// this, but for 7 types it's over-engineering — this comment plus
// localised review is the chosen contract.
void registerQmlTypes()
{
    // qmlRegisterType is process-global, not per-engine. Repeat calls
    // overwrite the previous registration and Qt logs a duplicate-
    // registration warning. A hot-reloading shell builds a fresh
    // QQmlEngine per reload and would call this every time; the
    // call_once guard makes the function safe to invoke from every
    // engine setup, matching the sibling phosphor-service-* pattern.
    //
    // Mirror the sibling phosphor-service-icontheme guard: refuse to
    // run before QCoreApplication exists. qmlRegisterType uses Qt's
    // meta-type registry, and calling it pre-app produces a hard-to-
    // diagnose ordering failure rather than the obvious warning here.
    if (!QCoreApplication::instance()) {
        qCWarning(lcPipeWireQml) << "registerQmlTypes called before QCoreApplication; refusing to register";
        return;
    }
    static std::once_flag once;
    std::call_once(once, [] {
        // PipeWireConnection is uncreatable from QML: shells either
        // pull it through `PipeWireHost.connection` (the common path)
        // or construct one in C++ and expose it manually. Direct QML
        // instantiation would mean each scope spawns its own loop
        // thread + handshake, which we want to discourage.
        qmlRegisterUncreatableType<PipeWireConnection>(kQmlModuleName, kQmlModuleMajor, kQmlModuleMinor,
                                                       "PipeWireConnection",
                                                       QStringLiteral("PipeWireConnection is vended by PipeWireHost"));
        // PwNode is also uncreatable — vended by the models, never by
        // QML directly. Registering the type makes QML aware of its
        // properties and signals so bindings like `model.node.volumes`
        // work.
        qmlRegisterUncreatableType<PwNode>(kQmlModuleName, kQmlModuleMajor, kQmlModuleMinor, "PwNode",
                                           QStringLiteral("PwNode is vended by PwNodeModel"));
        // The base PwNodeModel is uncreatable from QML; QML uses the
        // pre-filtered subclasses below. Keeping the base type
        // registered makes role enums + helpers accessible from QML
        // for callers that need to peek beyond the filtered views.
        // (PwNodeModel is concrete and instantiable from C++ — the
        // tests use it directly — but we still steer QML toward the
        // pinned subclasses for clarity.)
        qmlRegisterUncreatableType<PwNodeModel>(
            kQmlModuleName, kQmlModuleMajor, kQmlModuleMinor, "PwNodeModel",
            QStringLiteral("PwNodeModel is registered for role-enum access; instantiate PwSinkModel, "
                           "PwSourceModel, or PwStreamModel from QML"));
        qmlRegisterType<PwSinkModel>(kQmlModuleName, kQmlModuleMajor, kQmlModuleMinor, "PwSinkModel");
        qmlRegisterType<PwSourceModel>(kQmlModuleName, kQmlModuleMajor, kQmlModuleMinor, "PwSourceModel");
        qmlRegisterType<PwStreamModel>(kQmlModuleName, kQmlModuleMajor, kQmlModuleMinor, "PwStreamModel");
        // PipeWireHost — singleton. The header (PipeWireHost.h) is
        // explicit that the host's purpose is process-wide loop-thread
        // sharing: spinning one extra thread per process, not one per
        // QML engine. A hot-reloading shell builds a fresh QQmlEngine
        // on every reload; if the factory created a new host per
        // engine, each reload would start a fresh handshake and tear
        // the previous one down — defeating the singleton contract.
        //
        // Lifetime: parented to QCoreApplication so the host is
        // destroyed during app shutdown (children are deleted before
        // QCoreApplication's own destructor runs), NOT at static-
        // cleanup time after QGuiApplication has gone. The previous
        // function-static `unique_ptr<PipeWireHost>` destructed at
        // process exit, and `~PipeWireConnection` reaches into Qt
        // internals (signal disconnects, possibly object-deletion
        // bookkeeping) that have already been torn down by then —
        // risking a crash on shutdown. Parenting to the app lets Qt
        // destroy the host on the right side of the shutdown boundary.
        //
        // The function-static `hostPtr` caches the raw pointer so
        // call_once still guarantees one-time construction and the
        // factory returns the same instance on every call. QML treats
        // the pointer as borrowed (CppOwnership) — the parent app owns
        // lifetime.
        qmlRegisterSingletonType<PipeWireHost>(
            kQmlModuleName, kQmlModuleMajor, kQmlModuleMinor, "PipeWireHost", [](QQmlEngine*, QJSEngine*) -> QObject* {
                // Re-check QCoreApplication inside the factory: the
                // outer guard at registerQmlTypes() runs once at type
                // registration, but the factory itself is invoked
                // lazily on first QML access — which may happen after
                // the app instance has gone (release-build teardown
                // ordering, hot-reload tearing the app down between
                // registration and first use). Without this guard, the
                // next `QCoreApplication::instance()->thread()`
                // dereferences a nullptr.
                auto* app = QCoreApplication::instance();
                if (!app) {
                    qCWarning(lcPipeWireQml)
                        << "PipeWireHost factory: no QCoreApplication instance — skipping registration";
                    return static_cast<QObject*>(nullptr);
                }
                // The QObject inherits the calling thread's affinity
                // on first construction; we assume the QQmlEngine that
                // triggers this factory lives on the main thread
                // (which is the documented Phosphor shell contract —
                // engines are constructed in PhosphorShell::ShellEngine
                // on the GUI thread).
                if (QThread::currentThread() != app->thread()) {
                    qCWarning(lcPipeWireQml) << "PipeWireHost factory invoked off the GUI thread; refusing to register";
                    return static_cast<QObject*>(nullptr);
                }
                // QPointer auto-nulls when the tracked QObject is destroyed.
                // The previous raw-pointer cache UB'd on app re-creation:
                // when the prior QCoreApplication was destroyed, the cached
                // host (its child) was deleted, and the raw pointer dangled.
                // Reading hostPtr->parent() to detect the mismatch then
                // dereferenced freed memory. QPointer decays to false in
                // bool context once the tracked object is gone, so the
                // mismatch test below is safe across app-recreation.
                static QPointer<PipeWireHost> hostPtr;
                if (!hostPtr || hostPtr->parent() != app) {
                    // Parent to the app inline so the host is never
                    // briefly parentless between `new` and a follow-up
                    // setParent — the QObject ctor wires the
                    // parent-child link atomically.
                    hostPtr = new PipeWireHost(app);
                }
                // QPointer's implicit conversion to PipeWireHost* is what
                // the setObjectOwnership and return statements rely on;
                // the parent-check above already proved hostPtr is
                // non-null so the conversion can't decay to nullptr.
                // setObjectOwnership lives OUTSIDE the construction
                // guard: it is a per-engine policy registered against
                // the engine-private QML metadata table, not against
                // the QObject itself. The first engine's one-shot
                // pinning would leave any subsequent engine (think
                // hot-reload spawning a fresh QQmlEngine) treating the
                // shared host as JavaScriptOwnership — which would
                // schedule a delete on engine teardown and yank the
                // process-wide singleton out from under every other
                // engine still alive. Repeating per-call is the
                // documented Qt pattern for shared C++-owned QObjects.
                QQmlEngine::setObjectOwnership(hostPtr, QQmlEngine::CppOwnership);
                return hostPtr;
            });
    });
}

} // namespace PhosphorServicePipeWire
