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

int queryIntSetting(const QString& settingKey, int defaultValue)
{
    QDBusInterface settingsIface(
        QString::fromLatin1(DBus::ServiceName),
        QString::fromLatin1(DBus::ObjectPath),
        QString::fromLatin1(DBus::Interface::Settings),
        QDBusConnection::sessionBus());

    if (!settingsIface.isValid()) {
        return defaultValue;
    }

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
    QDBusInterface settingsIface(
        QString::fromLatin1(DBus::ServiceName),
        QString::fromLatin1(DBus::ObjectPath),
        QString::fromLatin1(DBus::Interface::Settings),
        QDBusConnection::sessionBus());

    if (!settingsIface.isValid()) {
        return defaultValue;
    }

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

} // namespace SettingsDbusQueries
} // namespace PlasmaZones
