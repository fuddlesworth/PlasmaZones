// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include "core/platform/logging.h"
#include "core/types/constants.h"
#include <PhosphorProtocol/ClientHelpers.h>
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
///
/// The call itself lives in PhosphorProtocol::ClientHelpers so the editor —
/// a separate app that cannot include this GPL settings-app header — spells
/// the same wire contract without a second hand-rolled copy. It picks the
/// async form there: it has no guard to order against the reply.
inline void notifyReload()
{
    PhosphorProtocol::ClientHelpers::reloadDaemonSettingsBlocking();
}

/// Ask the daemon to re-read rules.json.
///
/// reloadSettings() does NOT cover this: the daemon's rule store is borrowed by
/// the settings surface it serves, and a borrowed store is never reloaded by the
/// settings reload path (the owner drives reloads). An import rewrites rules.json
/// underneath the daemon, so without this call the daemon keeps serving its
/// pre-import rule set and the settings app's next revert or Apply fetches those
/// stale rules back over the imported ones.
///
/// Synchronous for the same reason notifyReload is: the caller reloads its own
/// in-memory state (and re-fetches rules from the daemon) immediately after, so
/// the daemon has to have adopted the new file before that fetch is dispatched.
inline void notifyRulesReload()
{
    callDaemon(QString(PhosphorProtocol::Service::Interface::Rules), QStringLiteral("reloadRules"));
}

/// Decode a daemon reply whose single argument is a JSON array string.
///
/// The settings app has several of these call sites and they used to disagree on
/// whether a parse failure was reported, whether the element loop guarded on
/// isObject, and whether the result list reserved. @p context names the caller in
/// the warning so a malformed payload is diagnosable from the log alone.
///
/// Returns an empty array for a D-Bus error, an empty payload, a parse failure,
/// or a document that is not an array. Only the last two warn: a transport error
/// is the caller's to report (the callers word it per feature) and an empty
/// payload is a normal "nothing to report" answer.
inline QJsonArray replyJsonArray(const QDBusMessage& reply, QLatin1String context)
{
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
        return {};
    }
    const QString json = reply.arguments().at(0).toString();
    if (json.isEmpty()) {
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcCore) << context << "reply is not valid JSON:" << parseError.errorString();
        return {};
    }
    if (!doc.isArray()) {
        qCWarning(lcCore) << context << "reply JSON is not an array";
        return {};
    }
    return doc.array();
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
