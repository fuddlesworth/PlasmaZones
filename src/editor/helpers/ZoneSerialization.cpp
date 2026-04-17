// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ZoneSerialization.h"
#include "../../core/constants.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QtMath>

namespace PlasmaZones {
namespace ZoneSerialization {

QString serializeZonesToClipboard(const QVariantList& zones)
{
    QJsonObject clipboardData;
    clipboardData[QLatin1String("version")] = QLatin1String("1.0");
    clipboardData[QLatin1String("application")] = QLatin1String("PlasmaZones");
    clipboardData[QLatin1String("dataType")] = QLatin1String("zones");

    QJsonArray zonesArray;
    for (const QVariant& zoneVar : zones) {
        QVariantMap zone = zoneVar.toMap();
        QJsonObject zoneObj;

        // Generate new UUID for paste (preserve original ID in metadata)
        zoneObj[QLatin1String("id")] = QUuid::createUuid().toString();
        zoneObj[QLatin1String("name")] = zone[::PhosphorZones::ZoneJsonKeys::Name].toString();
        zoneObj[QLatin1String("zoneNumber")] = zone[::PhosphorZones::ZoneJsonKeys::ZoneNumber].toInt();
        zoneObj[QLatin1String("x")] = zone[::PhosphorZones::ZoneJsonKeys::X].toDouble();
        zoneObj[QLatin1String("y")] = zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
        zoneObj[QLatin1String("width")] = zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
        zoneObj[QLatin1String("height")] = zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble();

        // Appearance properties
        zoneObj[QLatin1String("highlightColor")] = zone[::PhosphorZones::ZoneJsonKeys::HighlightColor].toString();
        zoneObj[QLatin1String("inactiveColor")] = zone[::PhosphorZones::ZoneJsonKeys::InactiveColor].toString();
        zoneObj[QLatin1String("borderColor")] = zone[::PhosphorZones::ZoneJsonKeys::BorderColor].toString();
        zoneObj[QLatin1String("activeOpacity")] = zone.contains(::PhosphorZones::ZoneJsonKeys::ActiveOpacity)
            ? zone[::PhosphorZones::ZoneJsonKeys::ActiveOpacity].toDouble()
            : ::PhosphorZones::ZoneDefaults::Opacity;
        zoneObj[QLatin1String("inactiveOpacity")] = zone.contains(::PhosphorZones::ZoneJsonKeys::InactiveOpacity)
            ? zone[::PhosphorZones::ZoneJsonKeys::InactiveOpacity].toDouble()
            : ::PhosphorZones::ZoneDefaults::InactiveOpacity;
        zoneObj[QLatin1String("borderWidth")] = zone.contains(::PhosphorZones::ZoneJsonKeys::BorderWidth)
            ? zone[::PhosphorZones::ZoneJsonKeys::BorderWidth].toInt()
            : ::PhosphorZones::ZoneDefaults::BorderWidth;
        zoneObj[QLatin1String("borderRadius")] = zone.contains(::PhosphorZones::ZoneJsonKeys::BorderRadius)
            ? zone[::PhosphorZones::ZoneJsonKeys::BorderRadius].toInt()
            : ::PhosphorZones::ZoneDefaults::BorderRadius;

        QString useCustomColorsKey = QString::fromLatin1(::PhosphorZones::ZoneJsonKeys::UseCustomColors);
        zoneObj[QLatin1String("useCustomColors")] =
            zone.contains(useCustomColorsKey) ? zone[useCustomColorsKey].toBool() : false;

        zonesArray.append(zoneObj);
    }
    clipboardData[QLatin1String("zones")] = zonesArray;

    QJsonDocument doc(clipboardData);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

QVariantList deserializeZonesFromClipboard(const QString& clipboardText)
{
    QJsonDocument doc = QJsonDocument::fromJson(clipboardText.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        return QVariantList();
    }

    QJsonObject clipboardData = doc.object();

    // Validate clipboard format
    if (clipboardData[QLatin1String("application")].toString() != QLatin1String("PlasmaZones")
        || clipboardData[QLatin1String("dataType")].toString() != QLatin1String("zones")) {
        return QVariantList();
    }

    QJsonArray zonesArray = clipboardData[QLatin1String("zones")].toArray();
    QVariantList zones;

    for (const QJsonValue& zoneVal : zonesArray) {
        QJsonObject zoneObj = zoneVal.toObject();
        QVariantMap zone;

        // Convert JSON to QVariantMap format used by ZoneManager
        zone[::PhosphorZones::ZoneJsonKeys::Id] = zoneObj[QLatin1String("id")].toString();
        zone[::PhosphorZones::ZoneJsonKeys::Name] = zoneObj[QLatin1String("name")].toString();
        zone[::PhosphorZones::ZoneJsonKeys::ZoneNumber] = zoneObj[QLatin1String("zoneNumber")].toInt();
        zone[::PhosphorZones::ZoneJsonKeys::X] = zoneObj[QLatin1String("x")].toDouble();
        zone[::PhosphorZones::ZoneJsonKeys::Y] = zoneObj[QLatin1String("y")].toDouble();
        zone[::PhosphorZones::ZoneJsonKeys::Width] = zoneObj[QLatin1String("width")].toDouble();
        zone[::PhosphorZones::ZoneJsonKeys::Height] = zoneObj[QLatin1String("height")].toDouble();

        // Appearance properties
        zone[::PhosphorZones::ZoneJsonKeys::HighlightColor] = zoneObj[QLatin1String("highlightColor")].toString();
        zone[::PhosphorZones::ZoneJsonKeys::InactiveColor] = zoneObj[QLatin1String("inactiveColor")].toString();
        zone[::PhosphorZones::ZoneJsonKeys::BorderColor] = zoneObj[QLatin1String("borderColor")].toString();
        zone[::PhosphorZones::ZoneJsonKeys::ActiveOpacity] =
            zoneObj[QLatin1String("activeOpacity")].toDouble(::PhosphorZones::ZoneDefaults::Opacity);
        zone[::PhosphorZones::ZoneJsonKeys::InactiveOpacity] =
            zoneObj[QLatin1String("inactiveOpacity")].toDouble(::PhosphorZones::ZoneDefaults::InactiveOpacity);
        zone[::PhosphorZones::ZoneJsonKeys::BorderWidth] =
            zoneObj[QLatin1String("borderWidth")].toInt(::PhosphorZones::ZoneDefaults::BorderWidth);
        zone[::PhosphorZones::ZoneJsonKeys::BorderRadius] =
            zoneObj[QLatin1String("borderRadius")].toInt(::PhosphorZones::ZoneDefaults::BorderRadius);

        QString useCustomColorsKey = QString::fromLatin1(::PhosphorZones::ZoneJsonKeys::UseCustomColors);
        zone[useCustomColorsKey] = zoneObj[QLatin1String("useCustomColors")].toBool(false);

        zones.append(zone);
    }

    return zones;
}

bool isValidClipboardFormat(const QString& clipboardText)
{
    if (clipboardText.isEmpty()) {
        return false;
    }

    // Quick validation - check if it's valid JSON with our format
    QJsonDocument doc = QJsonDocument::fromJson(clipboardText.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        return false;
    }

    QJsonObject clipboardData = doc.object();
    return clipboardData[QLatin1String("application")].toString() == QLatin1String("PlasmaZones")
        && clipboardData[QLatin1String("dataType")].toString() == QLatin1String("zones");
}

QVariantList prepareZonesForPaste(const QVariantList& zones, qreal offsetX, qreal offsetY, int startingZoneNumber)
{
    QVariantList preparedZones;
    int zoneNumber = startingZoneNumber;

    for (const QVariant& zoneVar : zones) {
        QVariantMap zone = zoneVar.toMap();

        // Generate new ID
        zone[::PhosphorZones::ZoneJsonKeys::Id] = QUuid::createUuid().toString();

        // Adjust position with offset
        qreal x = zone[::PhosphorZones::ZoneJsonKeys::X].toDouble() + offsetX;
        qreal y = zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble() + offsetY;
        qreal width = zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
        qreal height = zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble();

        // Clamp to bounds
        x = qBound(0.0, x, 1.0 - width);
        y = qBound(0.0, y, 1.0 - height);

        zone[::PhosphorZones::ZoneJsonKeys::X] = x;
        zone[::PhosphorZones::ZoneJsonKeys::Y] = y;
        zone[::PhosphorZones::ZoneJsonKeys::ZoneNumber] = zoneNumber++;

        preparedZones.append(zone);
    }

    return preparedZones;
}

} // namespace ZoneSerialization
} // namespace PlasmaZones
