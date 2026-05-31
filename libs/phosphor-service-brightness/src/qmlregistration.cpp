// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceBrightness/QmlRegistration.h>

#include <PhosphorServiceBrightness/BrightnessDevice.h>
#include <PhosphorServiceBrightness/BrightnessDeviceModel.h>
#include <PhosphorServiceBrightness/BrightnessHost.h>

#include <QQmlEngine>

#include <mutex>

namespace PhosphorServiceBrightness {

namespace {
constexpr int kModuleVersionMajor = 1;
constexpr int kModuleVersionMinor = 0;
constexpr const char* kModule = "Phosphor.Service.Brightness";
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
        // Instantiable entry points. BrightnessHost is constructed in QML and
        // handed to the model; it is a plain type, NOT a singleton (the project
        // discourages singletons holding mutable global state).
        qmlRegisterType<BrightnessHost>(kModule, kModuleVersionMajor, kModuleVersionMinor, "BrightnessHost");
        qmlRegisterType<BrightnessDeviceModel>(kModule, kModuleVersionMajor, kModuleVersionMinor,
                                               "BrightnessDeviceModel");

        // Pointer-receivable type. Exposed as a Q_PROPERTY / role value from
        // the host or model, never directly constructed in QML; registering it
        // uncreatable makes its metatype known so QML can read its properties
        // and call its Q_INVOKABLE setBrightness / setPercentage.
        qmlRegisterUncreatableType<BrightnessDevice>(
            kModule, kModuleVersionMajor, kModuleVersionMinor, "BrightnessDevice",
            QStringLiteral("BrightnessDevice is owned by BrightnessHost; bind via the host or model"));
    });
}

} // namespace PhosphorServiceBrightness
