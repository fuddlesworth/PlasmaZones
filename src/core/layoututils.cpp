// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoututils.h"
#include "constants.h"
#include "layout.h"
#include "zone.h"

#include <QColor>
#include <QJsonArray>

namespace PlasmaZones {

namespace LayoutUtils {

// ═══════════════════════════════════════════════════════════════════════════
// Zone conversion
// ═══════════════════════════════════════════════════════════════════════════

namespace {
QVariantMap zoneToVariantMap(Zone* zone, ZoneFields fields, const QRectF& referenceGeometry = QRectF())
{
    using namespace JsonKeys;
    QVariantMap map;

    if (!zone) {
        return map;
    }

    // Always include core fields
    map[Id] = zone->id().toString();
    map[ZoneNumber] = zone->zoneNumber();

    // Relative geometry (0.0-1.0) for resolution-independent rendering
    // Use normalizedGeometry() to compute correct 0-1 coords for any geometry mode
    QRectF relGeo = zone->normalizedGeometry(referenceGeometry);
    QVariantMap relGeoMap;
    relGeoMap[X] = relGeo.x();
    relGeoMap[Y] = relGeo.y();
    relGeoMap[Width] = relGeo.width();
    relGeoMap[Height] = relGeo.height();
    map[RelativeGeometry] = relGeoMap;

    // Per-zone geometry mode
    map[GeometryMode] = zone->geometryModeInt();
    if (zone->isFixedGeometry()) {
        QRectF fixedGeo = zone->fixedGeometry();
        QVariantMap fixedGeoMap;
        fixedGeoMap[X] = fixedGeo.x();
        fixedGeoMap[Y] = fixedGeo.y();
        fixedGeoMap[Width] = fixedGeo.width();
        fixedGeoMap[Height] = fixedGeo.height();
        map[FixedGeometry] = fixedGeoMap;
    }

    // Optional: Name
    if (fields.testFlag(ZoneField::Name)) {
        map[Name] = zone->name();
    }

    // Optional: Appearance properties (colors, opacities, border)
    if (fields.testFlag(ZoneField::Appearance)) {
        map[UseCustomColors] = zone->useCustomColors();

        // Colors as hex strings (ARGB format) for QML
        map[HighlightColor] = zone->highlightColor().name(QColor::HexArgb);
        map[InactiveColor] = zone->inactiveColor().name(QColor::HexArgb);
        map[BorderColor] = zone->borderColor().name(QColor::HexArgb);

        // Opacity and border properties
        map[ActiveOpacity] = zone->activeOpacity();
        map[InactiveOpacity] = zone->inactiveOpacity();
        map[BorderWidth] = zone->borderWidth();
        map[BorderRadius] = zone->borderRadius();
    }

    return map;
}
} // namespace

QVariantList zonesToVariantList(Layout* layout, ZoneFields fields, const QRectF& referenceGeometry)
{
    QVariantList list;

    if (!layout) {
        return list;
    }

    // Use layout's cached screen geometry to normalize fixed-mode zones to 0-1 coords.
    // The daemon recalculates ALL layouts on startup and screen changes, so this is always valid.
    const QRectF& refGeo = (referenceGeometry.width() > 0 && referenceGeometry.height() > 0)
        ? referenceGeometry
        : layout->lastRecalcGeometry();

    const auto zones = layout->zones();
    for (Zone* zone : zones) {
        if (!zone) {
            continue;
        }
        list.append(zoneToVariantMap(zone, fields, refGeo));
    }

    return list;
}

// ═══════════════════════════════════════════════════════════════════════════
// Layout conversion
// ═══════════════════════════════════════════════════════════════════════════

QVariantMap layoutToVariantMap(Layout* layout, ZoneFields zoneFields)
{
    using namespace JsonKeys;
    QVariantMap map;

    if (!layout) {
        return map;
    }

    map[Id] = layout->id().toString();
    map[Name] = layout->name();
    map[Description] = layout->description();
    map[ZoneCount] = layout->zoneCount();
    map[Zones] = zonesToVariantList(layout, zoneFields);
    map[Category] = static_cast<int>(LayoutCategory::Manual);
    map[AutoAssign] = layout->autoAssign();

    // Include reference aspect ratio for fixed-geometry layouts so previews
    // render at the correct proportions even when aspectRatioClass is "any"
    if (layout->hasFixedGeometryZones()) {
        QRectF refGeo = layout->lastRecalcGeometry();
        if (refGeo.height() > 0) {
            map[QLatin1String("referenceAspectRatio")] = refGeo.width() / refGeo.height();
        }
    }

    return map;
}

// ═══════════════════════════════════════════════════════════════════════════
// Allow-list serialization
// ═══════════════════════════════════════════════════════════════════════════

void serializeAllowLists(QJsonObject& json, const QStringList& screens, const QList<int>& desktops,
                         const QStringList& activities)
{
    using namespace JsonKeys;

    if (!screens.isEmpty()) {
        json[AllowedScreens] = QJsonArray::fromStringList(screens);
    }
    if (!desktops.isEmpty()) {
        QJsonArray arr;
        for (int d : desktops) {
            arr.append(d);
        }
        json[AllowedDesktops] = arr;
    }
    if (!activities.isEmpty()) {
        json[AllowedActivities] = QJsonArray::fromStringList(activities);
    }
}

void deserializeAllowLists(const QJsonObject& json, QStringList& screens, QList<int>& desktops, QStringList& activities)
{
    using namespace JsonKeys;

    screens.clear();
    desktops.clear();
    activities.clear();

    if (json.contains(AllowedScreens)) {
        for (const auto& v : json[AllowedScreens].toArray()) {
            screens.append(v.toString());
        }
    }
    if (json.contains(AllowedDesktops)) {
        for (const auto& v : json[AllowedDesktops].toArray()) {
            desktops.append(v.toInt());
        }
    }
    if (json.contains(AllowedActivities)) {
        for (const auto& v : json[AllowedActivities].toArray()) {
            activities.append(v.toString());
        }
    }
}

} // namespace LayoutUtils

} // namespace PlasmaZones
