// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/phosphorprotocol_export.h>

#include <PhosphorDBus/Client.h>

#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QObject>
#include <QVariant>

#include <utility>

namespace PhosphorProtocol {

PHOSPHORPROTOCOL_EXPORT const QLoggingCategory& lcPhosphorProtocol();

/**
 * @brief A PhosphorDBus::Client bound to the Phosphor daemon.
 *
 * Returned by value — `Client` is a cheap handle, so this is not a singleton
 * and holds no shared global state. It binds the canonical
 * `org.plasmazones` service / `/PlasmaZones` object on the session bus, with
 * call failures logged under `lcPhosphorProtocol()`.
 *
 * Generic, service-agnostic D-Bus plumbing lives in PhosphorDBus; this is the
 * thin Phosphor-specific binding on top.
 */
inline PhosphorDBus::Client daemonClient()
{
    return PhosphorDBus::Client(QDBusConnection::sessionBus(), Service::Name, Service::ObjectPath,
                                &lcPhosphorProtocol());
}

/**
 * @brief Daemon-bound convenience wrappers around PhosphorDBus::Client.
 *
 * These talk to the canonical Phosphor daemon. Code that needs to address
 * a different service should construct its own PhosphorDBus::Client.
 */
namespace ClientHelpers {

/// Fire-and-forget async call to the daemon, with error logging.
/// @see PhosphorDBus::Client::fireAndForget
inline void fireAndForget(QObject* parent, const QString& interface, const QString& method, const QVariantList& args,
                          const QString& logContext = {})
{
    daemonClient().fireAndForget(parent, interface, method, args, logContext);
}

/// One-way notification to the daemon with no expected reply.
/// @see PhosphorDBus::Client::sendOneWay
inline void sendOneWay(const QString& interface, const QString& method, const QVariantList& args = {})
{
    daemonClient().sendOneWay(interface, method, args);
}

/// Async method call to the daemon; caller attaches its own watcher.
/// @see PhosphorDBus::Client::asyncCall
inline QDBusPendingCall asyncCall(const QString& interface, const QString& method, const QVariantList& args = {})
{
    return daemonClient().asyncCall(interface, method, args);
}

/// Blocking daemon call bounded by `Service::SyncCallTimeoutMs`.
/// Prefer @ref asyncCall whenever the caller can tolerate a callback.
/// @see PhosphorDBus::Client::syncCall
inline QDBusMessage syncCall(const QString& interface, const QString& method, const QVariantList& args = {})
{
    return daemonClient().syncCall(interface, method, args, Service::SyncCallTimeoutMs);
}

/**
 * @brief Async helper for loading a single daemon setting.
 *
 * Sends getSetting(name) to the Settings interface, unwraps the QDBusVariant,
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
    auto* watcher = new QDBusPendingCallWatcher(
        daemonClient().asyncCall(Service::Interface::Settings, QStringLiteral("getSetting"), {name}), parent);
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
                             qCDebug(lcPhosphorProtocol) << "Loaded" << name;
                         } else {
                             qCWarning(lcPhosphorProtocol)
                                 << "loadSettingAsync: failed to load" << name << ":" << reply.error().message();
                         }
                     });
}

} // namespace ClientHelpers

} // namespace PhosphorProtocol
