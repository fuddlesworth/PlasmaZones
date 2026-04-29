// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include "../core/constants.h"
#include <PhosphorProtocol/ServiceConstants.h>

namespace PlasmaZones::DaemonDBus {

/// Unified D-Bus timeout for KCM <-> daemon calls (milliseconds)
constexpr int TimeoutMs = 3000;

/// Call a daemon method synchronously and return the reply
inline QDBusMessage callDaemon(const QString& interface, const QString& method, const QVariantList& args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath), interface, method);
    if (!args.isEmpty()) {
        msg.setArguments(args);
    }
    return QDBusConnection::sessionBus().call(msg, QDBus::Block, TimeoutMs);
}

/// Send a synchronous reloadSettings call to the daemon.
/// Must be synchronous so the daemon processes the reload (and emits
/// its settingsChanged D-Bus signal) before the KCM clears its
/// m_saving guard.  An async call here races: the settingsChanged
/// signal can arrive after m_saving is false, triggering a spurious
/// load() that reverts just-saved assignments.
inline void notifyReload()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::Settings), QStringLiteral("reloadSettings"));
    QDBusConnection::sessionBus().call(msg, QDBus::Block, TimeoutMs);
}

/// Batch-set settings on the daemon. Synchronous call.
inline QDBusMessage setDaemonSettings(const QVariantMap& settings)
{
    return callDaemon(QString(PhosphorProtocol::Service::Interface::Settings), QStringLiteral("setSettings"),
                      {QVariant::fromValue(settings)});
}

/// Set a per-screen setting on the daemon (async — no round-trip wait).
/// Categories: "autotile", "snapping", "zoneSelector".
inline void setPerScreenDaemonSetting(const QString& screenName, const QString& category, const QString& key,
                                      const QVariant& value)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::Settings), QStringLiteral("setPerScreenSetting"));
    msg.setArguments({screenName, category, key, QVariant::fromValue(QDBusVariant(value))});
    QDBusConnection::sessionBus().asyncCall(msg);
}

/// Clear all per-screen settings for a category on the daemon (async).
inline void clearPerScreenDaemonSettings(const QString& screenName, const QString& category)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::Settings), QStringLiteral("clearPerScreenSettings"));
    msg.setArguments({screenName, category});
    QDBusConnection::sessionBus().asyncCall(msg);
}

} // namespace PlasmaZones::DaemonDBus
