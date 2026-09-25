// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceUPower/QmlRegistration.h>

#include <PhosphorServiceUPower/UPowerDevice.h>
#include <PhosphorServiceUPower/UPowerDeviceModel.h>
#include <PhosphorServiceUPower/UPowerHost.h>

#include <QQmlEngine>

#include <mutex>

namespace PhosphorServiceUPower {

namespace {
constexpr int kModuleVersionMajor = 1;
constexpr int kModuleVersionMinor = 0;
constexpr const char* kModule = "Phosphor.Service.UPower";
} // namespace

void registerQmlTypes()
{
    // qmlRegisterType is process-global, not per-engine. Repeat calls
    // overwrite the previous registration and Qt logs a duplicate-
    // registration warning. A hot-reloading shell builds a fresh
    // QQmlEngine per reload and would call this every time; the
    // call_once guard makes the function safe to invoke from every
    // engine setup, matching the sibling phosphor-service-sni pattern.
    static std::once_flag once;
    std::call_once(once, [] {
        // Instantiable types. UPowerHost is the main entry point: QML
        // constructs one, hands its pointer to a model, and binds the
        // model to a Repeater (or reads `displayDevice` for a single
        // aggregate battery indicator).
        qmlRegisterType<UPowerHost>(kModule, kModuleVersionMajor, kModuleVersionMinor, "UPowerHost");
        qmlRegisterType<UPowerDeviceModel>(kModule, kModuleVersionMajor, kModuleVersionMinor, "UPowerDeviceModel");

        // Pointer-receivable types. Exposed as Q_PROPERTY values from the
        // host or model, never directly constructed in QML; their metatype
        // needs to be known so QML can read percentage / state / iconName
        // off them.
        qmlRegisterUncreatableType<UPowerDevice>(
            kModule, kModuleVersionMajor, kModuleVersionMinor, "UPowerDevice",
            QStringLiteral("UPowerDevice is owned by UPowerHost; bind via the host or model"));
    });
}

} // namespace PhosphorServiceUPower
