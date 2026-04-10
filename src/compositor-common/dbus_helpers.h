// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "dbus_constants.h"
#include "logging.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QObject>
#include <QVariant>
#include <functional>

namespace PlasmaZones {

/**
 * @brief Compositor-agnostic D-Bus helper utilities
 *
 * These functions wrap common D-Bus call patterns used by all compositor plugins
 * when communicating with the PlasmaZones daemon. They use QDBusMessage::createMethodCall
 * (not QDBusInterface) to avoid synchronous D-Bus introspection that blocks the
 * compositor thread.
 */
namespace DBusHelpers {

/**
 * @brief Fire-and-forget async D-Bus call with error logging.
 *
 * @param parent     QObject parent for the watcher (must outlive the call)
 * @param interface  D-Bus interface (e.g., DBus::Interface::Autotile)
 * @param method     D-Bus method name
 * @param args       Method arguments
 * @param logContext Human-readable label for the warning log
 */
inline void fireAndForget(QObject* parent, const QString& interface, const QString& method, const QVariantList& args,
                          const QString& logContext = {})
{
    if (!parent) {
        qCWarning(lcCompositorCommon) << (logContext.isEmpty() ? method : logContext)
                                      << "fireAndForget called with null parent, ignoring";
        return;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, interface, method);
    for (const QVariant& arg : args) {
        msg << arg;
    }
    QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(pending, parent);
    const QString ctx = logContext.isEmpty() ? method : logContext;
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, parent, [ctx](QDBusPendingCallWatcher* w) {
        if (w->isError()) {
            qCWarning(lcCompositorCommon) << ctx << "D-Bus call failed:" << w->error().message();
        }
        w->deleteLater();
    });
}

/**
 * @brief Create an async D-Bus method call and return the pending result.
 *
 * @param interface  D-Bus interface name
 * @param method     D-Bus method name
 * @param args       Method arguments
 * @return QDBusPendingCall for attaching a watcher
 */
inline QDBusPendingCall asyncCall(const QString& interface, const QString& method, const QVariantList& args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, interface, method);
    for (const QVariant& arg : args) {
        msg << arg;
    }
    return QDBusConnection::sessionBus().asyncCall(msg);
}

/**
 * @brief Async helper for loading a single daemon setting.
 *
 * Sends getSetting(name) via raw QDBusMessage, unwraps the QDBusVariant,
 * and calls onValue with the extracted QVariant.
 *
 * @param parent   QObject parent for the watcher
 * @param name     Setting name to load
 * @param onValue  Callback receiving the unwrapped QVariant value (captured by
 *                 move into the lambda — callers must pass a fresh rvalue each call)
 */
template<typename Fn>
void loadSettingAsync(QObject* parent, const QString& name, Fn&& onValue)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(DBus::ServiceName, DBus::ObjectPath, DBus::Interface::Settings,
                                                      QStringLiteral("getSetting"));
    msg << name;
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), parent);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, parent,
                     [name, onValue = std::forward<Fn>(onValue)](QDBusPendingCallWatcher* w) {
                         w->deleteLater();
                         QDBusPendingReply<QVariant> reply = *w;
                         if (reply.isValid()) {
                             QVariant value = reply.value();
                             if (value.canConvert<QDBusVariant>()) {
                                 value = value.value<QDBusVariant>().variant();
                             }
                             onValue(value);
                             qCDebug(lcCompositorCommon) << "Loaded" << name;
                         } else {
                             qCWarning(lcCompositorCommon)
                                 << "loadSettingAsync: failed to load" << name << ":" << reply.error().message();
                         }
                     });
}

} // namespace DBusHelpers
} // namespace PlasmaZones
