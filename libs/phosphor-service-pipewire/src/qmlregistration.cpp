// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePipeWire/QmlRegistration.h>

#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PipeWireHost.h>
#include <PhosphorServicePipeWire/PwNode.h>
#include <PhosphorServicePipeWire/PwNodeModel.h>

#include <QQmlEngine>

#include <mutex>

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
        // The base PwNodeModel is uncreatable; QML uses the
        // pre-filtered subclasses below. Keeping the base type
        // registered makes role enums + helpers accessible from QML
        // for callers that need to peek beyond the filtered views.
        qmlRegisterUncreatableType<PwNodeModel>(kModule, kModuleVersionMajor, kModuleVersionMinor, "PwNodeModel",
                                                QStringLiteral("PwNodeModel is abstract; use a concrete subclass"));
        qmlRegisterType<PwSinkModel>(kModule, kModuleVersionMajor, kModuleVersionMinor, "PwSinkModel");
        qmlRegisterType<PwSourceModel>(kModule, kModuleVersionMajor, kModuleVersionMinor, "PwSourceModel");
        qmlRegisterType<PwStreamModel>(kModule, kModuleVersionMajor, kModuleVersionMinor, "PwStreamModel");
        // PipeWireHost — singleton, lazily constructed on first QML
        // import. Holds the canonical PipeWireConnection instance and
        // forwards its observable state for direct QML binding.
        qmlRegisterSingletonType<PipeWireHost>(kModule, kModuleVersionMajor, kModuleVersionMinor, "PipeWireHost",
                                               [](QQmlEngine* engine, QJSEngine*) -> QObject* {
                                                   auto* host = new PipeWireHost();
                                                   // The QML engine owns
                                                   // the singleton lifetime
                                                   // — parent it explicitly
                                                   // so it lives as long as
                                                   // the engine and is torn
                                                   // down with it.
                                                   host->setParent(engine);
                                                   return host;
                                               });
    });
}

} // namespace PhosphorServicePipeWire
