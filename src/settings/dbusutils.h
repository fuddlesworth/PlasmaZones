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

/// Call a daemon method synchronously and return the reply.
///
/// Bounded with `PhosphorProtocol::Service::SyncCallTimeoutMs` (500 ms) — the
/// shared cap for blocking daemon calls. Daemon settings handlers are
/// in-memory hash lookups, so 500 ms is "definitely something is wrong"
/// rather than an expected latency.
inline QDBusMessage callDaemon(const QString& interface, const QString& method, const QVariantList& args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath), interface, method);
    if (!args.isEmpty()) {
        msg.setArguments(args);
    }
    return QDBusConnection::sessionBus().call(msg, QDBus::Block, PhosphorProtocol::Service::SyncCallTimeoutMs);
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
    QDBusConnection::sessionBus().call(msg, QDBus::Block, PhosphorProtocol::Service::SyncCallTimeoutMs);
}

// The three settings-WRITE helpers that used to live here (setDaemonSettings,
// setPerScreenDaemonSetting, clearPerScreenDaemonSettings) are gone. They had no callers:
// the settings app writes config.json in-process and calls reloadSettings(), which is what
// notifyReload above is for. Nothing in the tree writes a setting over D-Bus.
//
// That matters beyond dead-code hygiene. SettingsAdaptor's SETTER registry is
// hand-maintained exactly like its getter registry, and unlike the getter registry it has no
// tripwire: a key registered with a getter but no setter makes setSetting return false and
// setSettings drop it with a debug line, silently. Keeping an unused write path around is
// keeping a loaded gun for whoever wires the first page to it.

} // namespace PlasmaZones::DaemonDBus
