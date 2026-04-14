// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SettingsDbusQueries.h"
#include "../../core/constants.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>

namespace PlasmaZones {
namespace SettingsDbusQueries {

namespace {

// Hard cap on blocking settings calls. Qt's default D-Bus timeout is 25
// seconds, which is long enough to freeze the editor if the daemon is
// unresponsive. The daemon's getSetting handler is an in-memory hash lookup
// (src/dbus/settingsadaptor.cpp:608), so a healthy response is well under
// a millisecond — 500 ms is generous while still degrading to defaults
// quickly when the daemon event loop is blocked.
constexpr int SettingsCallTimeoutMs = 500;

} // namespace

QVariantMap querySettingsBatch(const QStringList& keys)
{
    if (keys.isEmpty()) {
        return QVariantMap();
    }

    QDBusInterface settingsIface(QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
                                 QString::fromLatin1(DBus::Interface::Settings), QDBusConnection::sessionBus());

    if (!settingsIface.isValid()) {
        return QVariantMap();
    }

    settingsIface.setTimeout(SettingsCallTimeoutMs);
    QDBusReply<QVariantMap> reply = settingsIface.call(QStringLiteral("getSettings"), keys);
    if (!reply.isValid()) {
        return QVariantMap();
    }
    return reply.value();
}

int queryIntSetting(const QString& settingKey, int defaultValue)
{
    QDBusInterface settingsIface(QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
                                 QString::fromLatin1(DBus::Interface::Settings), QDBusConnection::sessionBus());

    if (!settingsIface.isValid()) {
        return defaultValue;
    }

    settingsIface.setTimeout(SettingsCallTimeoutMs);
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

int queryGlobalZonePadding()
{
    return queryIntSetting(QStringLiteral("zonePadding"), Defaults::ZonePadding);
}

int queryGlobalOuterGap()
{
    return queryIntSetting(QStringLiteral("outerGap"), Defaults::OuterGap);
}

bool queryBoolSetting(const QString& settingKey, bool defaultValue)
{
    QDBusInterface settingsIface(QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
                                 QString::fromLatin1(DBus::Interface::Settings), QDBusConnection::sessionBus());

    if (!settingsIface.isValid()) {
        return defaultValue;
    }

    settingsIface.setTimeout(SettingsCallTimeoutMs);
    QDBusReply<QDBusVariant> reply = settingsIface.call(QStringLiteral("getSetting"), settingKey);
    if (!reply.isValid()) {
        return defaultValue;
    }

    return reply.value().variant().toBool();
}

bool queryGlobalUsePerSideOuterGap()
{
    return queryBoolSetting(QStringLiteral("usePerSideOuterGap"), false);
}

int queryGlobalOuterGapTop()
{
    return queryIntSetting(QStringLiteral("outerGapTop"), Defaults::OuterGap);
}

int queryGlobalOuterGapBottom()
{
    return queryIntSetting(QStringLiteral("outerGapBottom"), Defaults::OuterGap);
}

int queryGlobalOuterGapLeft()
{
    return queryIntSetting(QStringLiteral("outerGapLeft"), Defaults::OuterGap);
}

int queryGlobalOuterGapRight()
{
    return queryIntSetting(QStringLiteral("outerGapRight"), Defaults::OuterGap);
}

int queryGlobalOverlayDisplayMode()
{
    return queryIntSetting(QStringLiteral("overlayDisplayMode"), 0);
}

} // namespace SettingsDbusQueries
} // namespace PlasmaZones
