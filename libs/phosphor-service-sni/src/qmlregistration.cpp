// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSni/QmlRegistration.h>

#include <PhosphorServiceSni/DBusMenuModel.h>
#include <PhosphorServiceSni/StatusNotifierHost.h>
#include <PhosphorServiceSni/StatusNotifierItem.h>
#include <PhosphorServiceSni/StatusNotifierItemModel.h>

#include <QQmlEngine>

#include <mutex>

namespace PhosphorServiceSni {

namespace {
constexpr int kModuleVersionMajor = 1;
constexpr int kModuleVersionMinor = 0;
constexpr const char* kModule = "Phosphor.Service.Sni";
} // namespace

void registerQmlTypes()
{
    // qmlRegisterType is process-global, not per-engine. Repeat calls
    // overwrite the previous registration and Qt logs a duplicate-
    // registration warning. A hot-reloading shell builds a fresh
    // QQmlEngine per reload and would call this every time; the
    // call_once guard makes the function safe to invoke from every
    // engine setup.
    static std::once_flag once;
    std::call_once(once, [] {
        // Instantiable types. StatusNotifierHost is the main entry point:
        // QML constructs one, hands its pointer to a model, and binds the
        // model to a Repeater.
        qmlRegisterType<StatusNotifierHost>(kModule, kModuleVersionMajor, kModuleVersionMinor, "StatusNotifierHost");
        qmlRegisterType<StatusNotifierItemModel>(kModule, kModuleVersionMajor, kModuleVersionMinor,
                                                 "StatusNotifierItemModel");
        qmlRegisterType<DBusMenuModel>(kModule, kModuleVersionMajor, kModuleVersionMinor, "DBusMenuModel");

        // Pointer-receivable type: exposed as a Q_PROPERTY value from
        // the host / model, never directly constructed in QML, but the
        // metatype needs to be known so QML can read iconUrl, tooltip,
        // etc. off it.
        qmlRegisterUncreatableType<StatusNotifierItem>(
            kModule, kModuleVersionMajor, kModuleVersionMinor, "StatusNotifierItem",
            QStringLiteral("StatusNotifierItem is owned by StatusNotifierHost: bind via the model"));
    });
}

} // namespace PhosphorServiceSni
