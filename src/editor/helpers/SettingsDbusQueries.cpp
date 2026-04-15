// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SettingsDbusQueries.h"
#include "../../core/constants.h"
#include "../../core/logging.h"

#include <QDBusConnection>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>

namespace PlasmaZones {
namespace SettingsDbusQueries {

namespace {

// Returns a QDBusInterface as a prvalue so callers can construct-in-place
// via guaranteed copy elision. QDBusInterface inherits QObject which is
// non-copyable/non-movable, so anything other than a direct prvalue return
// would fail to compile. Callers must call setTimeout() themselves after
// construction if they want a non-default timeout.
QDBusInterface createSettingsInterface()
{
    return QDBusInterface(QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
                          QString::fromLatin1(DBus::Interface::Settings), QDBusConnection::sessionBus());
}

// Fetch requested keys individually via getSetting(). Used as a fallback
// when the batched getSettings() call isn't available on the remote side
// (e.g. the editor is a newer build than the running daemon), so users
// don't silently see defaults for their configured gap/overlay values
// during a post-upgrade / pre-daemon-restart window.
QVariantMap querySettingsPerKey(const QStringList& keys)
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
        // getSetting() returns an empty string sentinel for unknown keys
        // (see settingsadaptor.cpp). Treat that as "not found" so the
        // caller falls back to its default rather than coercing "" to 0.
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
        return querySettingsPerKey(keys);
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

    int value = reply.value().variant().toInt();
    if (value < 0) {
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
