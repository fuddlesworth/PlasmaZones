// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePipeWire/QmlRegistration.h>

#include <PhosphorServicePipeWire/PipeWireConnection.h>

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
        // PipeWireConnection is the host-level lifecycle owner. QML
        // constructs one, hands its `connected` / `daemonAvailable`
        // signals to status indicators, and (in later milestones) binds
        // the sink / source / stream models that hang off it.
        qmlRegisterType<PipeWireConnection>(kModule, kModuleVersionMajor, kModuleVersionMinor, "PipeWireConnection");
    });
}

} // namespace PhosphorServicePipeWire
