// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include "../../src/core/constants.h"

namespace PlasmaZones::KCMDBus {

/// Unified D-Bus timeout for KCM <-> daemon calls (milliseconds)
constexpr int TimeoutMs = 3000;

/// Call a daemon method synchronously and return the reply
inline QDBusMessage callDaemon(const QString& interface, const QString& method, const QVariantList& args = {})
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath), interface, method);
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
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                       QString(DBus::Interface::Settings), QStringLiteral("reloadSettings"));
    QDBusConnection::sessionBus().call(msg, QDBus::Block, TimeoutMs);
}

} // namespace PlasmaZones::KCMDBus
