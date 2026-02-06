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
        zoneObj[QLatin1String("name")] = zone[JsonKeys::Name].toString();
        zoneObj[QLatin1String("zoneNumber")] = zone[JsonKeys::ZoneNumber].toInt();
        zoneObj[QLatin1String("x")] = zone[JsonKeys::X].toDouble();
        zoneObj[QLatin1String("y")] = zone[JsonKeys::Y].toDouble();
        zoneObj[QLatin1String("width")] = zone[JsonKeys::Width].toDouble();
        zoneObj[QLatin1String("height")] = zone[JsonKeys::Height].toDouble();

        // Appearance properties
        zoneObj[QLatin1String("highlightColor")] = zone[JsonKeys::HighlightColor].toString();
        zoneObj[QLatin1String("inactiveColor")] = zone[JsonKeys::InactiveColor].toString();
        zoneObj[QLatin1String("borderColor")] = zone[JsonKeys::BorderColor].toString();
        zoneObj[QLatin1String("activeOpacity")] =
            zone.contains(JsonKeys::ActiveOpacity) ? zone[JsonKeys::ActiveOpacity].toDouble() : Defaults::Opacity;
        zoneObj[QLatin1String("inactiveOpacity")] = zone.contains(JsonKeys::InactiveOpacity)
            ? zone[JsonKeys::InactiveOpacity].toDouble()
            : Defaults::InactiveOpacity;
        zoneObj[QLatin1String("borderWidth")] =
            zone.contains(JsonKeys::BorderWidth) ? zone[JsonKeys::BorderWidth].toInt() : Defaults::BorderWidth;
        zoneObj[QLatin1String("borderRadius")] =
            zone.contains(JsonKeys::BorderRadius) ? zone[JsonKeys::BorderRadius].toInt() : Defaults::BorderRadius;

        QString useCustomColorsKey = QString::fromLatin1(JsonKeys::UseCustomColors);
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
        zone[JsonKeys::Id] = zoneObj[QLatin1String("id")].toString();
        zone[JsonKeys::Name] = zoneObj[QLatin1String("name")].toString();
        zone[JsonKeys::ZoneNumber] = zoneObj[QLatin1String("zoneNumber")].toInt();
        zone[JsonKeys::X] = zoneObj[QLatin1String("x")].toDouble();
        zone[JsonKeys::Y] = zoneObj[QLatin1String("y")].toDouble();
        zone[JsonKeys::Width] = zoneObj[QLatin1String("width")].toDouble();
        zone[JsonKeys::Height] = zoneObj[QLatin1String("height")].toDouble();

        // Appearance properties
        zone[JsonKeys::HighlightColor] = zoneObj[QLatin1String("highlightColor")].toString();
        zone[JsonKeys::InactiveColor] = zoneObj[QLatin1String("inactiveColor")].toString();
        zone[JsonKeys::BorderColor] = zoneObj[QLatin1String("borderColor")].toString();
        zone[JsonKeys::ActiveOpacity] = zoneObj[QLatin1String("activeOpacity")].toDouble(Defaults::Opacity);
        zone[JsonKeys::InactiveOpacity] = zoneObj[QLatin1String("inactiveOpacity")].toDouble(Defaults::InactiveOpacity);
        zone[JsonKeys::BorderWidth] = zoneObj[QLatin1String("borderWidth")].toInt(Defaults::BorderWidth);
        zone[JsonKeys::BorderRadius] = zoneObj[QLatin1String("borderRadius")].toInt(Defaults::BorderRadius);

        QString useCustomColorsKey = QString::fromLatin1(JsonKeys::UseCustomColors);
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
        zone[JsonKeys::Id] = QUuid::createUuid().toString();

        // Adjust position with offset
        qreal x = zone[JsonKeys::X].toDouble() + offsetX;
        qreal y = zone[JsonKeys::Y].toDouble() + offsetY;
        qreal width = zone[JsonKeys::Width].toDouble();
        qreal height = zone[JsonKeys::Height].toDouble();

        // Clamp to bounds
        x = qBound(0.0, x, 1.0 - width);
        y = qBound(0.0, y, 1.0 - height);

        zone[JsonKeys::X] = x;
        zone[JsonKeys::Y] = y;
        zone[JsonKeys::ZoneNumber] = zoneNumber++;

        preparedZones.append(zone);
    }

    return preparedZones;
}

} // namespace ZoneSerialization
} // namespace PlasmaZones
