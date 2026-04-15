// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SettingsDbusQueries.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "DbusHelpers.h"

#include <QDBusError>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>

namespace PlasmaZones {
namespace SettingsDbusQueries {

using DbusHelpers::createSettingsInterface;

namespace {

// WARNING: Only safe for settings whose values are never legitimately the
// empty string. getSetting() returns an empty string as the "key not found"
// sentinel, and this helper treats that as "missing" so callers fall back
// to their defaults. A string-typed setting that the user has cleared
// would therefore be indistinguishable from an unknown key and get
// silently dropped. Only used as a fallback by querySettingsBatch() for
// the gap/overlay keys (all int/bool), which makes this safe today — do
// not generalize without addressing the empty-string ambiguity.
QVariantMap querySettingsPerKey_NonStringOnly(const QStringList& keys)
{
    QVariantMap result;
    QDBusInterface iface = createSettingsInterface();
    if (!iface.isValid()) {
        return result;
    }
    iface.setTimeout(DBus::SyncCallTimeoutMs);
    for (const QString& key : keys) {
        if (key.isEmpty()) {
            continue;
        }
        QDBusReply<QDBusVariant> reply = iface.call(QStringLiteral("getSetting"), key);
        if (!reply.isValid()) {
            continue;
        }
        QVariant value = reply.value().variant();
        if (!value.isValid()) {
            continue;
        }
        // Empty-string sentinel for unknown keys — see the warning on this
        // function's comment above. Safe for the int/bool gap/overlay keys
        // this helper is restricted to.
        if (value.typeId() == QMetaType::QString && value.toString().isEmpty()) {
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

    QDBusInterface settingsIface = createSettingsInterface();
    if (!settingsIface.isValid()) {
        return QVariantMap();
    }
    settingsIface.setTimeout(DBus::SyncCallTimeoutMs);

    QDBusReply<QVariantMap> reply = settingsIface.call(QStringLiteral("getSettings"), keys);
    if (reply.isValid()) {
        return reply.value();
    }

    // Stale daemon: this build knows about getSettings() but the daemon
    // doesn't. Fall back to individual getSetting() calls so the editor
    // still sees the user's real configured values instead of defaults.
    // Other error types (timeout, service unreachable) would also fail
    // per-key, so there's no point retrying those — return empty and let
    // callers fall back to hardcoded defaults.
    const QDBusError::ErrorType errType = reply.error().type();
    if (errType == QDBusError::UnknownMethod) {
        qCWarning(lcDbus) << "getSettings() unavailable on daemon — falling back to per-key getSetting()."
                          << "Restart plasmazones-daemon to pick up the batched path.";
        return querySettingsPerKey_NonStringOnly(keys);
    }

    qCWarning(lcDbus) << "getSettings() failed:" << reply.error().message() << "(type" << errType << ")";
    return QVariantMap();
}

int queryIntSetting(const QString& settingKey, int defaultValue)
{
    QDBusInterface settingsIface = createSettingsInterface();
    if (!settingsIface.isValid()) {
        return defaultValue;
    }
    settingsIface.setTimeout(DBus::SyncCallTimeoutMs);

    QDBusReply<QDBusVariant> reply = settingsIface.call(QStringLiteral("getSetting"), settingKey);
    if (!reply.isValid()) {
        return defaultValue;
    }

    bool ok = false;
    const int value = reply.value().variant().toInt(&ok);
    if (!ok || value < 0) {
        return defaultValue;
    }

    return value;
}

bool queryBoolSetting(const QString& settingKey, bool defaultValue)
{
    QDBusInterface settingsIface = createSettingsInterface();
    if (!settingsIface.isValid()) {
        return defaultValue;
    }
    settingsIface.setTimeout(DBus::SyncCallTimeoutMs);

    QDBusReply<QDBusVariant> reply = settingsIface.call(QStringLiteral("getSetting"), settingKey);
    if (!reply.isValid()) {
        return defaultValue;
    }

    return reply.value().variant().toBool();
}

} // namespace SettingsDbusQueries
} // namespace PlasmaZones
