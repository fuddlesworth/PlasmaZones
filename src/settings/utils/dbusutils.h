// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/platform/logging.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <QDBusMessage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace PlasmaZones::DaemonDBus {

/// Call a daemon method synchronously and return the reply.
///
/// Bounded with `PhosphorProtocol::Service::SyncCallTimeoutMs` (500 ms) — the
/// shared cap for blocking daemon calls. Daemon settings handlers are
/// in-memory hash lookups, so 500 ms is "definitely something is wrong"
/// rather than an expected latency.
///
/// A forward to `ClientHelpers::syncCall`, not a second implementation: the
/// service name, object path and timeout are the protocol library's to spell,
/// and this header's own closing note is about not keeping parallel copies of
/// wire plumbing around. The alias survives only because the settings app's
/// call sites read better naming the daemon.
inline QDBusMessage callDaemon(const QString& interface, const QString& method, const QVariantList& args = {})
{
    return PhosphorProtocol::ClientHelpers::syncCall(interface, method, args);
}

/// Send a synchronous reloadSettings call to the daemon.
/// Must be synchronous so the daemon processes the reload (and emits
/// its settingsChanged D-Bus signal) before SettingsController clears
/// its m_saving guard.  An async call here races: the settingsChanged
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
///
/// Returns false when the call did not reach the daemon (not started, timed
/// out, no such interface). That is the case the caller has to care about: the
/// re-fetch it is about to make will then answer with the daemon's PRE-import
/// rule set and quietly overwrite the rules that were just imported. Failure is
/// warned about here, so a caller that has no recovery of its own can ignore
/// the return and still leave a trace in the log.
///
/// @return true if the daemon acknowledged the reload.
inline bool notifyRulesReload()
{
    const QDBusMessage reply =
        callDaemon(QString(PhosphorProtocol::Service::Interface::Rules), QStringLiteral("reloadRules"));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "notifyRulesReload: the daemon did not reload rules.json —" << reply.errorMessage()
                          << "; its rule set is now stale and a re-fetch will serve pre-import rules";
        return false;
    }
    return true;
}

/// Decode a daemon reply whose single argument is a JSON array string.
///
/// The settings app has several of these call sites and they used to disagree on
/// whether a parse failure was reported, whether the element loop guarded on
/// isObject, and whether the result list reserved. @p context names the caller in
/// the warning so a malformed payload is diagnosable from the log alone.
///
/// Returns an empty array for a D-Bus error, an empty payload, a parse failure,
/// or a document that is not an array. Everything but the empty payload warns —
/// an empty payload is a normal "nothing to report" answer, while a transport
/// error means the settings app is about to render an empty feature as though
/// the daemon had said there was nothing there. The doc here used to defer the
/// transport case to the callers on the grounds that they word it per feature;
/// none of them did, so the failure was silent at every site.
inline QJsonArray replyJsonArray(const QDBusMessage& reply, QLatin1String context)
{
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCInfo(lcCore) << context << "D-Bus call failed:" << reply.errorMessage();
        return {};
    }
    if (reply.arguments().isEmpty()) {
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
