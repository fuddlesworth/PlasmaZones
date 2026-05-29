// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceUPower/QmlRegistration.h>

#include <PhosphorServiceUPower/UPowerDevice.h>
#include <PhosphorServiceUPower/UPowerDeviceModel.h>
#include <PhosphorServiceUPower/UPowerHost.h>

#include <QQmlEngine>

namespace PhosphorServiceUPower {

namespace {
constexpr int kModuleVersionMajor = 1;
constexpr int kModuleVersionMinor = 0;
constexpr const char* kModule = "Phosphor.Service.UPower";
} // namespace

void registerQmlTypes()
{
    // Instantiable types. UPowerHost is the main entry point — QML
    // constructs one, hands its pointer to a model, and binds the
    // model to a Repeater (or reads `displayDevice` for a single
    // aggregate battery indicator).
    qmlRegisterType<UPowerHost>(kModule, kModuleVersionMajor, kModuleVersionMinor, "UPowerHost");
    qmlRegisterType<UPowerDeviceModel>(kModule, kModuleVersionMajor, kModuleVersionMinor, "UPowerDeviceModel");

    // Pointer-receivable types — exposed as Q_PROPERTY values from the
    // host / model, never directly constructed in QML, but their
    // metatype needs to be known so QML can read percentage / state /
    // iconName off them.
    qmlRegisterUncreatableType<UPowerDevice>(
        kModule, kModuleVersionMajor, kModuleVersionMinor, "UPowerDevice",
        QStringLiteral("UPowerDevice is owned by UPowerHost — bind via the host or model"));
}

} // namespace PhosphorServiceUPower
