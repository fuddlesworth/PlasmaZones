// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceBluetooth/QmlRegistration.h>

#include <QQmlEngine>

#include <mutex>

namespace PhosphorServiceBluetooth {

namespace {
constexpr int kModuleVersionMajor = 1;
constexpr int kModuleVersionMinor = 0;
constexpr const char* kModule = "Phosphor.Service.Bluetooth";
} // namespace

void registerQmlTypes()
{
    // qmlRegister* is process-global, not per-engine. Repeat calls overwrite
    // the previous registration and Qt logs a duplicate-registration warning.
    // A hot-reloading shell builds a fresh QQmlEngine per reload and would
    // call this every time; the call_once guard makes the function safe to
    // invoke from every engine setup, matching the sibling
    // phosphor-service-network pattern.
    static std::once_flag once;
    std::call_once(once, [] {
        // Declare the module so `import Phosphor.Service.Bluetooth 1.0`
        // resolves. The host / model / domain type registrations live here,
        // next to the module declaration, as those types land.
        qmlRegisterModule(kModule, kModuleVersionMajor, kModuleVersionMinor);
    });
}

} // namespace PhosphorServiceBluetooth
