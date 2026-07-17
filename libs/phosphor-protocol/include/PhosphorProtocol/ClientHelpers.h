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
 * @brief Ask the daemon to reparse config.json from disk.
 *
 * Every settings writer in the tree writes config.json IN PROCESS and then
 * tells the daemon to reparse — nothing writes a setting over the bus. The
 * reload is not a courtesy: a backend sync rewrites the WHOLE document from
 * its in-memory root, so a daemon still holding the pre-write snapshot would
 * put the old values back on its next flush, from any source, at any later
 * time.
 *
 * Async and error-logged. Prefer this: the notification's only consumer is the
 * daemon, so a caller with nothing to order against the reply gains nothing
 * from blocking on it. Blocking does NOT close the clobber window either — the
 * daemon can flush between the write and the reparse whether or not the writer
 * waits — it only makes the writer wait out the round trip.
 *
 * @param parent      Owns the reply watcher; must outlive the round trip.
 * @param logContext  Prefix for the failure warning.
 */
inline void reloadDaemonSettings(QObject* parent, const QString& logContext = {})
{
    fireAndForget(parent, Service::Interface::Settings, QStringLiteral("reloadSettings"), {}, logContext);
}

/**
 * @brief Blocking form of @ref reloadDaemonSettings.
 *
 * Only for a caller with a real ordering requirement against the reply — the
 * KCM clears its `m_saving` guard once this returns, and an async call there
 * races: the daemon's settingsChanged can land after the guard is clear and
 * trigger a spurious load() that reverts the just-saved assignments. A caller
 * that only needs the daemon to catch up eventually wants the async form.
 *
 * Bounded by `Service::SyncCallTimeoutMs`.
 */
inline void reloadDaemonSettingsBlocking()
{
    syncCall(Service::Interface::Settings, QStringLiteral("reloadSettings"));
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
