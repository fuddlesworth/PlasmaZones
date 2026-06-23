// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceNotifications/QmlRegistration.h>

#include <PhosphorServiceNotifications/Notification.h>
#include <PhosphorServiceNotifications/NotificationModel.h>
#include <PhosphorServiceNotifications/NotificationServer.h>

#include <QQmlEngine>

#include <mutex>

namespace PhosphorServiceNotifications {

namespace {
constexpr int kModuleVersionMajor = 1;
constexpr int kModuleVersionMinor = 0;
constexpr const char* kModule = "Phosphor.Service.Notifications";
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
        // Instantiable entry point. NotificationServer is constructed in QML and
        // owns the bus name; it is a plain type, NOT a singleton (the project
        // discourages singletons holding mutable global state).
        qmlRegisterType<NotificationServer>(kModule, kModuleVersionMajor, kModuleVersionMinor, "NotificationServer");

        // List model over a server's live notifications. Instantiable in QML
        // (bind its `server` property); a plain type, not a singleton.
        qmlRegisterType<NotificationModel>(kModule, kModuleVersionMajor, kModuleVersionMinor, "NotificationModel");

        // Pointer-receivable type. Exposed as a Q_PROPERTY / signal arg / model
        // role from the server, never constructed in QML; registering it as
        // uncreatable makes its metatype (and the Urgency enum) known so QML can
        // read its properties.
        qmlRegisterUncreatableType<Notification>(
            kModule, kModuleVersionMajor, kModuleVersionMinor, "Notification",
            QStringLiteral("Notification is owned by NotificationServer; bind via the server or model"));
    });
}

} // namespace PhosphorServiceNotifications
