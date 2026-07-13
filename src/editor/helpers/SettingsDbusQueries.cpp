// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SettingsDbusQueries.h"
#include "../../core/dbusvariantutils.h"
#include "../../core/logging.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <QDBusError>
#include <QDBusMessage>
#include <QDBusVariant>

namespace PlasmaZones {
namespace SettingsDbusQueries {

namespace {

/// Unwrap a getSetting() reply into a plain QVariant. Returns an invalid
/// QVariant on D-Bus error or if the reply shape is wrong. The daemon
/// answers with an `sv` (variant-of-variant) so we have to step through
/// QDBusVariant once.
QVariant unwrapGetSetting(const QDBusMessage& reply)
{
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    return reply.arguments().constFirst().value<QDBusVariant>().variant();
}

// Fetch each key with its own getSetting() call. The fallback path for a daemon too
// old to have the batched getSettings().
//
// An unknown key answers with a D-Bus ERROR, so unwrapGetSetting hands back an invalid
// QVariant and the isValid() check below drops it — the caller keeps its own default.
// This used to need a warning and a name to match: getSetting once answered an unknown
// key with a valid EMPTY STRING, so "missing" and "the user cleared this string setting"
// were the same reply, and this helper was restricted to int/bool keys because it could
// not tell them apart. The daemon reports the difference properly now, so the
// restriction is gone.
QVariantMap querySettingsPerKey(const QStringList& keys)
{
    QVariantMap result;
    for (const QString& key : keys) {
        if (key.isEmpty()) {
            continue;
        }
        const QVariant value = unwrapGetSetting(PhosphorProtocol::ClientHelpers::syncCall(
            PhosphorProtocol::Service::Interface::Settings, QStringLiteral("getSetting"), {key}));
        // An invalid variant is an unknown key (the daemon answered with an error) or a
        // getter that failed. Either way there is nothing to report, and the caller's
        // default stands. A legitimately EMPTY string is a real value and is kept.
        if (!value.isValid()) {
            continue;
        }
        result.insert(key, value);
    }
    return result;
}

} // namespace

QVariantMap querySettingsBatch(const QStringList& keys)
{
    if (keys.isEmpty()) {
        return QVariantMap();
    }

    const QDBusMessage reply = PhosphorProtocol::ClientHelpers::syncCall(PhosphorProtocol::Service::Interface::Settings,
                                                                         QStringLiteral("getSettings"), {keys});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        // Defensively unwrap any QDBusArgument/QDBusVariant wrappers Qt may
        // leave in the map. For a flat a{sv} of scalar int/bool Qt normally
        // demarshals cleanly into plain types, but nested compound values
        // (or a future extension to this method returning lists/maps) would
        // arrive as QDBusArgument wrappers that toInt()/toBool() can't
        // handle — the result would silently fall through to caller-side
        // defaults. ShaderDbusQueries::queryShaderInfo does the same thing
        // against the same daemon object for the same reason.
        QVariant converted = DBusVariantUtils::convertDbusArgument(reply.arguments().constFirst());
        return converted.toMap();
    }

    // Stale daemon: this build knows about getSettings() but the daemon
    // doesn't. Fall back to individual getSetting() calls so the editor
    // still sees the user's real configured values instead of defaults.
    // Other error types (timeout, service unreachable) would also fail
    // per-key, so there's no point retrying those — return empty and let
    // callers fall back to hardcoded defaults.
    if (reply.type() == QDBusMessage::ErrorMessage) {
        const QDBusError err(reply);
        if (err.type() == QDBusError::UnknownMethod) {
            qCWarning(lcDbus) << "getSettings() unavailable on daemon — falling back to per-key getSetting()."
                              << "Restart plasmazones-daemon to pick up the batched path.";
            return querySettingsPerKey(keys);
        }
        qCWarning(lcDbus) << "getSettings() failed:" << err.message() << "(type" << err.type() << ")";
    }
    return QVariantMap();
}

int queryIntSetting(const QString& settingKey, int defaultValue)
{
    const QVariant value = unwrapGetSetting(PhosphorProtocol::ClientHelpers::syncCall(
        PhosphorProtocol::Service::Interface::Settings, QStringLiteral("getSetting"), {settingKey}));
    if (!value.isValid()) {
        return defaultValue;
    }
    bool ok = false;
    const int result = value.toInt(&ok);
    if (!ok || result < 0) {
        return defaultValue;
    }
    return result;
}

bool queryBoolSetting(const QString& settingKey, bool defaultValue)
{
    const QVariant value = unwrapGetSetting(PhosphorProtocol::ClientHelpers::syncCall(
        PhosphorProtocol::Service::Interface::Settings, QStringLiteral("getSetting"), {settingKey}));
    if (!value.isValid()) {
        return defaultValue;
    }
    return value.toBool();
}

} // namespace SettingsDbusQueries
} // namespace PlasmaZones
