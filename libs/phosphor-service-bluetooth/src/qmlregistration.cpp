// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceBluetooth/QmlRegistration.h>

#include <PhosphorServiceBluetooth/BluetoothAdapter.h>
#include <PhosphorServiceBluetooth/BluetoothAdapterModel.h>
#include <PhosphorServiceBluetooth/BluetoothAgent.h>
#include <PhosphorServiceBluetooth/BluetoothDevice.h>
#include <PhosphorServiceBluetooth/BluetoothDeviceModel.h>
#include <PhosphorServiceBluetooth/BluetoothHost.h>

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
        // Instantiable entry points. BluetoothHost is constructed in QML and
        // handed to the models; it is a plain type, NOT a singleton (the
        // project discourages singletons holding mutable global state).
        qmlRegisterType<BluetoothHost>(kModule, kModuleVersionMajor, kModuleVersionMinor, "BluetoothHost");
        qmlRegisterType<BluetoothAdapterModel>(kModule, kModuleVersionMajor, kModuleVersionMinor,
                                               "BluetoothAdapterModel");
        qmlRegisterType<BluetoothDeviceModel>(kModule, kModuleVersionMajor, kModuleVersionMinor,
                                              "BluetoothDeviceModel");

        // Pointer-receivable types. Exposed as Q_PROPERTY / role values from
        // the host or models, never directly constructed in QML; registering
        // them as uncreatable makes their metatype known so QML can read their
        // properties and (for adapter/device) call their Q_INVOKABLE writes.
        qmlRegisterUncreatableType<BluetoothAdapter>(
            kModule, kModuleVersionMajor, kModuleVersionMinor, "BluetoothAdapter",
            QStringLiteral("BluetoothAdapter is owned by BluetoothHost; bind via the host or model"));
        qmlRegisterUncreatableType<BluetoothDevice>(
            kModule, kModuleVersionMajor, kModuleVersionMinor, "BluetoothDevice",
            QStringLiteral("BluetoothDevice is owned by BluetoothHost; bind via the host or model"));
        qmlRegisterUncreatableType<BluetoothAgent>(
            kModule, kModuleVersionMajor, kModuleVersionMinor, "BluetoothAgent",
            QStringLiteral("BluetoothAgent is owned by BluetoothHost; access it via host.agent"));
    });
}

} // namespace PhosphorServiceBluetooth
