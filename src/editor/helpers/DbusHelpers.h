// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorProtocol/ServiceConstants.h>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QString>

namespace PlasmaZones {
namespace DbusHelpers {

/**
 * @brief Construct a QDBusInterface bound to the daemon's Settings service.
 *
 * Returns the interface as a prvalue because QDBusInterface inherits
 * QObject and is therefore non-copyable and non-movable — it cannot be
 * returned from a named local. C++17 guaranteed copy elision handles the
 * prvalue return into the caller's storage correctly.
 *
 * ⚠ CRITICAL: every caller MUST invoke
 *     iface.setTimeout(PhosphorProtocol::Service::SyncCallTimeoutMs);
 * before dispatching a synchronous .call(). Omitting it reintroduces the
 * editor-startup-freeze failure mode this PR was written to fix (Qt's
 * default is 25 seconds). The timeout cannot be baked into this helper
 * because QDBusInterface's non-movability forbids "construct, configure,
 * return" from a named local — only direct prvalue construction works.
 *
 * Shared by SettingsDbusQueries.cpp and ShaderDbusQueries.cpp because
 * both target the exact same service/object/interface, and duplicating
 * the three fromLatin1() conversions at every call site invites drift.
 */
inline QDBusInterface createSettingsInterface()
{
    return QDBusInterface(QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
                          QString(PhosphorProtocol::Service::Interface::Settings), QDBusConnection::sessionBus());
}

} // namespace DbusHelpers
} // namespace PlasmaZones
