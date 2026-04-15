// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../../compositor-common/dbus_constants.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QString>

namespace PlasmaZones {
namespace DbusHelpers {

/**
 * @brief Construct a QDBusInterface bound to the daemon's Settings service.
 *
 * Returns the interface as a prvalue so guaranteed copy elision (C++17)
 * handles the non-copyable/non-movable QDBusInterface correctly. Callers
 * must invoke setTimeout(DBus::SyncCallTimeoutMs) on the returned object
 * before making any synchronous .call() so an unresponsive daemon can't
 * freeze the editor for Qt's 25-second default.
 *
 * This helper is shared by SettingsDbusQueries.cpp and ShaderDbusQueries.cpp
 * — both target the exact same service/object/interface, and duplicating
 * the three fromLatin1() conversions at every call site invites drift.
 */
inline QDBusInterface createSettingsInterface()
{
    return QDBusInterface(QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
                          QString::fromLatin1(DBus::Interface::Settings), QDBusConnection::sessionBus());
}

} // namespace DbusHelpers
} // namespace PlasmaZones
