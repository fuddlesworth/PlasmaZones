// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSession/QmlRegistration.h>

#include <PhosphorServiceSession/SessionHost.h>

#include <QQmlEngine>

#include <mutex>

namespace PhosphorServiceSession {

namespace {
constexpr int kModuleVersionMajor = 1;
constexpr int kModuleVersionMinor = 0;
constexpr const char* kModule = "Phosphor.Service.Session";
} // namespace

void registerQmlTypes()
{
    // qmlRegister* is process-global, not per-engine. Repeat calls overwrite
    // the previous registration and Qt logs a duplicate-registration warning.
    // A hot-reloading shell builds a fresh QQmlEngine per reload and would call
    // this every time; the call_once guard makes it safe to invoke from every
    // engine setup, matching the sibling service-lib pattern.
    static std::once_flag once;
    std::call_once(once, [] {
        // Instantiable entry point. SessionHost is constructed in QML; it is a
        // plain type, NOT a singleton (the project discourages singletons
        // holding mutable global state).
        qmlRegisterType<SessionHost>(kModule, kModuleVersionMajor, kModuleVersionMinor, "SessionHost");
    });
}

} // namespace PhosphorServiceSession
