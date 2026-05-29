// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePipeWire/QmlRegistration.h>

#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PipeWireHost.h>
#include <PhosphorServicePipeWire/PwNode.h>
#include <PhosphorServicePipeWire/PwNodeModel.h>

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QQmlEngine>

#include <memory>
#include <mutex>

Q_LOGGING_CATEGORY(lcPipeWireQml, "phosphor.service.pipewire.qml")

namespace PhosphorServicePipeWire {

namespace {
constexpr int kModuleVersionMajor = 1;
constexpr int kModuleVersionMinor = 0;
constexpr const char* kModule = "Phosphor.Service.PipeWire";
} // namespace

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
        qmlRegisterUncreatableType<PipeWireConnection>(kModule, kModuleVersionMajor, kModuleVersionMinor,
                                                       "PipeWireConnection",
                                                       QStringLiteral("PipeWireConnection is vended by PipeWireHost"));
        // PwNode is also uncreatable — vended by the models, never by
        // QML directly. Registering the type makes QML aware of its
        // properties and signals so bindings like `model.node.volumes`
        // work.
        qmlRegisterUncreatableType<PwNode>(kModule, kModuleVersionMajor, kModuleVersionMinor, "PwNode",
                                           QStringLiteral("PwNode is vended by PwNodeModel"));
        // The base PwNodeModel is uncreatable from QML; QML uses the
        // pre-filtered subclasses below. Keeping the base type
        // registered makes role enums + helpers accessible from QML
        // for callers that need to peek beyond the filtered views.
        // (PwNodeModel is concrete and instantiable from C++ — the
        // tests use it directly — but we still steer QML toward the
        // pinned subclasses for clarity.)
        qmlRegisterUncreatableType<PwNodeModel>(
            kModule, kModuleVersionMajor, kModuleVersionMinor, "PwNodeModel",
            QStringLiteral("PwNodeModel is registered for role-enum access; instantiate PwSinkModel, "
                           "PwSourceModel, or PwStreamModel from QML"));
        qmlRegisterType<PwSinkModel>(kModule, kModuleVersionMajor, kModuleVersionMinor, "PwSinkModel");
        qmlRegisterType<PwSourceModel>(kModule, kModuleVersionMajor, kModuleVersionMinor, "PwSourceModel");
        qmlRegisterType<PwStreamModel>(kModule, kModuleVersionMajor, kModuleVersionMinor, "PwStreamModel");
        // PipeWireHost — singleton. The header (PipeWireHost.h) is
        // explicit that the host's purpose is process-wide loop-thread
        // sharing: spinning one extra thread per process, not one per
        // QML engine. A hot-reloading shell builds a fresh QQmlEngine
        // on every reload; if the factory created a new host per
        // engine, each reload would start a fresh handshake and tear
        // the previous one down — defeating the singleton contract.
        //
        // Lifetime: a function-static unique_ptr owns the host for the
        // duration of the process. QML treats the pointer as borrowed
        // (we deliberately do NOT setParent on the engine, since the
        // static owns lifetime). The QObject is constructed on first
        // factory call, which happens after QCoreApplication exists
        // (guarded at the top of registerQmlTypes), so QObject's
        // thread-affinity rule is satisfied.
        qmlRegisterSingletonType<PipeWireHost>(
            kModule, kModuleVersionMajor, kModuleVersionMinor, "PipeWireHost", [](QQmlEngine*, QJSEngine*) -> QObject* {
                static std::unique_ptr<PipeWireHost> host = std::make_unique<PipeWireHost>();
                // C++ owns the
                // lifetime via the
                // function-static
                // unique_ptr; tell
                // QML not to GC it.
                QQmlEngine::setObjectOwnership(host.get(), QQmlEngine::CppOwnership);
                return host.get();
            });
    });
}

} // namespace PhosphorServicePipeWire
