// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoututils.h"
#include "constants.h"
#include "interfaces.h"
#include "layout.h"
#include "utils.h"
#include "zone.h"

#include <algorithm>

#include <QColor>
#include <QJsonArray>
#include <QUuid>

namespace PlasmaZones {

namespace LayoutUtils {

// ═══════════════════════════════════════════════════════════════════════════
// Zone conversion
// ═══════════════════════════════════════════════════════════════════════════

QVariantMap zoneToVariantMap(Zone* zone, ZoneFields fields)
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
    QRectF relGeo = zone->relativeGeometry();
    QVariantMap relGeoMap;
    relGeoMap[X] = relGeo.x();
    relGeoMap[Y] = relGeo.y();
    relGeoMap[Width] = relGeo.width();
    relGeoMap[Height] = relGeo.height();
    map[RelativeGeometry] = relGeoMap;

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

QVariantList zonesToVariantList(Layout* layout, ZoneFields fields)
{
    QVariantList list;

    if (!layout) {
        return list;
    }

    const auto zones = layout->zones();
    for (Zone* zone : zones) {
        if (!zone) {
            continue;
        }
        list.append(zoneToVariantMap(zone, fields));
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
    map[Type] = static_cast<int>(layout->type());
    map[ZoneCount] = layout->zoneCount();
    map[Zones] = zonesToVariantList(layout, zoneFields);
    map[Category] = static_cast<int>(LayoutCategory::Manual);
    map[AutoAssign] = layout->autoAssign();

    return map;
}

// ═══════════════════════════════════════════════════════════════════════════
// Unified layout list building
// ═══════════════════════════════════════════════════════════════════════════

static UnifiedLayoutEntry entryFromLayout(Layout* layout)
{
    UnifiedLayoutEntry entry;
    entry.id = layout->id().toString();
    entry.name = layout->name();
    entry.description = layout->description();
    entry.zoneCount = layout->zoneCount();
    entry.zones = zonesToVariantList(layout, ZoneField::Minimal);
    entry.autoAssign = layout->autoAssign();
    return entry;
}

QVector<UnifiedLayoutEntry> buildUnifiedLayoutList(ILayoutManager* layoutManager)
{
    QVector<UnifiedLayoutEntry> list;

    if (layoutManager) {
        const auto layouts = layoutManager->layouts();
        for (Layout* layout : layouts) {
            if (!layout) {
                continue;
            }
            list.append(entryFromLayout(layout));
        }
    }

    std::sort(list.begin(), list.end(), [](const UnifiedLayoutEntry& a, const UnifiedLayoutEntry& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    return list;
}

QVector<UnifiedLayoutEntry> buildUnifiedLayoutList(
    ILayoutManager* layoutManager,
    const QString& screenName,
    int virtualDesktop,
    const QString& activity)
{
    QVector<UnifiedLayoutEntry> list;

    if (!layoutManager) {
        return list;
    }

    // Translate connector name to screen ID for allowedScreens matching
    QString screenId;
    if (!screenName.isEmpty()) {
        screenId = Utils::isConnectorName(screenName) ? Utils::screenIdForName(screenName) : screenName;
    }

    // Track the active layout so we can guarantee it appears in the list
    // (prevents empty selector / broken cycling when active layout is hidden)
    Layout* activeLayout = layoutManager->activeLayout();

    const auto layouts = layoutManager->layouts();
    for (Layout* layout : layouts) {
        if (!layout) {
            continue;
        }

        bool isActive = (layout == activeLayout);

        // Tier 1: skip globally hidden layouts (unless active)
        if (layout->hiddenFromSelector() && !isActive) {
            continue;
        }

        // Tier 2: screen filter (unless active)
        if (!isActive && !screenId.isEmpty() && !layout->allowedScreens().isEmpty()) {
            if (!layout->allowedScreens().contains(screenId)) {
                continue;
            }
        }

        // Tier 2: desktop filter (unless active)
        if (!isActive && virtualDesktop > 0 && !layout->allowedDesktops().isEmpty()) {
            if (!layout->allowedDesktops().contains(virtualDesktop)) {
                continue;
            }
        }

        // Tier 2: activity filter (unless active)
        if (!isActive && !activity.isEmpty() && !layout->allowedActivities().isEmpty()) {
            if (!layout->allowedActivities().contains(activity)) {
                continue;
            }
        }

        list.append(entryFromLayout(layout));
    }

    std::sort(list.begin(), list.end(), [](const UnifiedLayoutEntry& a, const UnifiedLayoutEntry& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    return list;
}

QVariantMap toVariantMap(const UnifiedLayoutEntry& entry)
{
    using namespace JsonKeys;
    QVariantMap map;

    map[Id] = entry.id;
    map[Name] = entry.name;
    map[Description] = entry.description;
    map[Type] = 0;
    map[ZoneCount] = entry.zoneCount;
    map[Zones] = entry.zones;
    map[Category] = static_cast<int>(LayoutCategory::Manual);
    map[AutoAssign] = entry.autoAssign;

    return map;
}

QVariantList toVariantList(const QVector<UnifiedLayoutEntry>& entries)
{
    QVariantList list;
    list.reserve(entries.size());

    for (const auto& entry : entries) {
        list.append(toVariantMap(entry));
    }

    return list;
}

QJsonObject toJson(const UnifiedLayoutEntry& entry)
{
    using namespace JsonKeys;
    QJsonObject json;

    json[Id] = entry.id;
    json[Name] = entry.name;
    json[Description] = entry.description;
    json[ZoneCount] = entry.zoneCount;
    json[IsSystem] = false;
    json[Type] = 0;
    json[Category] = static_cast<int>(LayoutCategory::Manual);
    if (entry.autoAssign) {
        json[AutoAssign] = true;
    }
    // hiddenFromSelector is added by callers that have access to the Layout*

    // Convert zones to JSON array
    QJsonArray zonesArray;
    for (const QVariant& zoneVar : entry.zones) {
        const QVariantMap zoneMap = zoneVar.toMap();
        QJsonObject zoneJson;

        zoneJson[ZoneNumber] = zoneMap[ZoneNumber].toInt();

        const QVariantMap relGeoMap = zoneMap[RelativeGeometry].toMap();
        QJsonObject relGeo;
        relGeo[X] = relGeoMap[X].toDouble();
        relGeo[Y] = relGeoMap[Y].toDouble();
        relGeo[Width] = relGeoMap[Width].toDouble();
        relGeo[Height] = relGeoMap[Height].toDouble();
        zoneJson[RelativeGeometry] = relGeo;

        zonesArray.append(zoneJson);
    }
    json[Zones] = zonesArray;

    return json;
}

// ═══════════════════════════════════════════════════════════════════════════
// Allow-list serialization
// ═══════════════════════════════════════════════════════════════════════════

void serializeAllowLists(QJsonObject& json, const QStringList& screens,
                          const QList<int>& desktops, const QStringList& activities)
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

void deserializeAllowLists(const QJsonObject& json, QStringList& screens,
                            QList<int>& desktops, QStringList& activities)
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

int findLayoutIndex(const QVector<UnifiedLayoutEntry>& entries, const QString& layoutId)
{
    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i].id == layoutId) {
            return i;
        }
    }
    return -1;
}

const UnifiedLayoutEntry* findLayout(const QVector<UnifiedLayoutEntry>& entries, const QString& layoutId)
{
    int index = findLayoutIndex(entries, layoutId);
    if (index >= 0) {
        return &entries[index];
    }
    return nullptr;
}

} // namespace LayoutUtils

} // namespace PlasmaZones
