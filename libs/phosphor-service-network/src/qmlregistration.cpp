// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceNetwork/QmlRegistration.h>

#include <PhosphorServiceNetwork/AccessPoint.h>
#include <PhosphorServiceNetwork/AccessPointModel.h>
#include <PhosphorServiceNetwork/NetworkConnection.h>
#include <PhosphorServiceNetwork/NetworkConnectionModel.h>
#include <PhosphorServiceNetwork/NetworkDevice.h>
#include <PhosphorServiceNetwork/NetworkDeviceModel.h>
#include <PhosphorServiceNetwork/NetworkHost.h>

#include <QQmlEngine>

#include <mutex>

namespace PhosphorServiceNetwork {

namespace {
constexpr int kModuleVersionMajor = 1;
constexpr int kModuleVersionMinor = 0;
constexpr const char* kModule = "Phosphor.Service.Network";
} // namespace

void registerQmlTypes()
{
    // qmlRegisterType is process-global, not per-engine. Repeat calls
    // overwrite the previous registration and Qt logs a duplicate-
    // registration warning. A hot-reloading shell builds a fresh
    // QQmlEngine per reload and would call this every time; the
    // call_once guard makes the function safe to invoke from every engine
    // setup, matching the sibling phosphor-service-upower pattern.
    static std::once_flag once;
    std::call_once(once, [] {
        // Instantiable types. NetworkHost is the main entry point: QML
        // constructs one, hands its pointer to a model, and binds the
        // model to a Repeater (or reads the scalar properties for a
        // single connectivity indicator).
        qmlRegisterType<NetworkHost>(kModule, kModuleVersionMajor, kModuleVersionMinor, "NetworkHost");
        qmlRegisterType<NetworkDeviceModel>(kModule, kModuleVersionMajor, kModuleVersionMinor, "NetworkDeviceModel");
        qmlRegisterType<AccessPointModel>(kModule, kModuleVersionMajor, kModuleVersionMinor, "AccessPointModel");
        qmlRegisterType<NetworkConnectionModel>(kModule, kModuleVersionMajor, kModuleVersionMinor,
                                                "NetworkConnectionModel");

        // Pointer-receivable types. Exposed as Q_PROPERTY / role values from
        // the host or model, never directly constructed in QML; their
        // metatype needs to be known so QML can read the properties off them.
        qmlRegisterUncreatableType<NetworkDevice>(
            kModule, kModuleVersionMajor, kModuleVersionMinor, "NetworkDevice",
            QStringLiteral("NetworkDevice is owned by NetworkHost; bind via the host or model"));
        qmlRegisterUncreatableType<AccessPoint>(
            kModule, kModuleVersionMajor, kModuleVersionMinor, "AccessPoint",
            QStringLiteral("AccessPoint is owned by AccessPointModel; bind via the model"));
        qmlRegisterUncreatableType<NetworkConnection>(
            kModule, kModuleVersionMajor, kModuleVersionMinor, "NetworkConnection",
            QStringLiteral("NetworkConnection is owned by NetworkConnectionModel; bind via the model"));
    });
}

} // namespace PhosphorServiceNetwork
