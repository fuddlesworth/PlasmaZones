// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoututils.h"
#include "constants.h"
#include "interfaces.h"
#include "layout.h"
#include "zone.h"
#include "../autotile/AlgorithmRegistry.h"
#include "../autotile/TilingAlgorithm.h"

#include <QColor>
#include <QJsonArray>
#include <QUuid>

namespace PlasmaZones {

QString UnifiedLayoutEntry::algorithmId() const
{
    if (!isAutotile) {
        return QString();
    }
    return LayoutId::extractAlgorithmId(id);
}

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

    return map;
}

// ═══════════════════════════════════════════════════════════════════════════
// Unified layout list building
// ═══════════════════════════════════════════════════════════════════════════

QVector<UnifiedLayoutEntry> buildUnifiedLayoutList(ILayoutManager* layoutManager, bool includeAutotile)
{
    QVector<UnifiedLayoutEntry> list;

    // Add manual layouts first
    if (layoutManager) {
        const auto layouts = layoutManager->layouts();
        for (Layout* layout : layouts) {
            if (!layout) {
                continue;
            }

            UnifiedLayoutEntry entry;
            entry.id = layout->id().toString();
            entry.name = layout->name();
            entry.description = layout->description();
            entry.isAutotile = false;
            entry.zoneCount = layout->zoneCount();

            // Use shared zone conversion with minimal fields (for preview thumbnails)
            entry.zones = zonesToVariantList(layout, ZoneField::Minimal);

            list.append(entry);
        }
    }

    // Add autotile algorithms (only if enabled)
    if (includeAutotile) {
        auto* registry = AlgorithmRegistry::instance();
        if (registry) {
            const QStringList algorithmIds = registry->availableAlgorithms();
            for (const QString& algorithmId : algorithmIds) {
                TilingAlgorithm* algo = registry->algorithm(algorithmId);
                if (!algo) {
                    continue;
                }

                UnifiedLayoutEntry entry;
                entry.id = LayoutId::makeAutotileId(algorithmId);
                entry.name = algo->name();
                entry.description = algo->description();
                entry.isAutotile = true;
                entry.zoneCount = 0; // Dynamic

                // Use AlgorithmRegistry's shared preview generation
                entry.zones = AlgorithmRegistry::generatePreviewZones(algo);

                list.append(entry);
            }
        }
    }

    return list;
}

QVariantMap toVariantMap(const UnifiedLayoutEntry& entry)
{
    using namespace JsonKeys;
    QVariantMap map;

    map[Id] = entry.id;
    map[Name] = entry.name;
    map[Description] = entry.description;
    map[Type] = entry.isAutotile ? -1 : 0; // -1 for autotile, 0 for custom
    map[ZoneCount] = entry.zoneCount;
    map[Zones] = entry.zones;
    map[Category] = static_cast<int>(entry.isAutotile ? LayoutCategory::Autotile : LayoutCategory::Manual);

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
    json[IsSystem] = entry.isAutotile; // Autotile algorithms are "system" layouts
    json[Type] = entry.isAutotile ? -1 : 0;
    json[Category] = static_cast<int>(entry.isAutotile ? LayoutCategory::Autotile : LayoutCategory::Manual);

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
