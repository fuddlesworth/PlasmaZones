// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceIdle/QmlRegistration.h>

#include <PhosphorServiceIdle/IdleService.h>

#include <QQmlEngine>

#include <mutex>

namespace PhosphorServiceIdle {

namespace {
constexpr int kModuleVersionMajor = 1;
constexpr int kModuleVersionMinor = 0;
constexpr const char* kModule = "Phosphor.Service.Idle";
} // namespace

void registerQmlTypes()
{
    // qmlRegister* is process-global, not per-engine. Repeat calls overwrite the
    // previous registration and Qt logs a duplicate-registration warning. A
    // hot-reloading shell builds a fresh QQmlEngine per reload and would call
    // this every time; the call_once guard makes the function safe to invoke
    // from every engine setup, matching the sibling service-lib pattern.
    static std::once_flag once;
    std::call_once(once, [] {
        // Instantiable host. The shell constructs the service in QML and
        // configures its idle stages; a plain type, NOT a singleton.
        qmlRegisterType<IdleService>(kModule, kModuleVersionMajor, kModuleVersionMinor, "IdleService");
    });
}

} // namespace PhosphorServiceIdle
