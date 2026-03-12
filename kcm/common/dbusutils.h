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

/// Send an async reloadSettings notification to the daemon
inline void notifyReload()
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                       QString(DBus::Interface::Settings), QStringLiteral("reloadSettings"));
    QDBusConnection::sessionBus().asyncCall(msg);
}

} // namespace PlasmaZones::KCMDBus
