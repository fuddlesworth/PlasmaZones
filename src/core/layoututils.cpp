// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoututils.h"
#include "constants.h"
#include "interfaces.h"
#include "layout.h"
#include "zone.h"
#include "../autotile/AlgorithmRegistry.h"
#include "../autotile/TilingAlgorithm.h"

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

QVector<UnifiedLayoutEntry> buildUnifiedLayoutList(ILayoutManager* layoutManager)
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
            entry.id = layout->id().toString(QUuid::WithoutBraces);
            entry.name = layout->name();
            entry.description = layout->description();
            entry.isAutotile = false;
            entry.zoneCount = layout->zoneCount();

            // Build zone list for preview
            QVariantList zonesList;
            const auto zones = layout->zones();
            for (Zone* zone : zones) {
                if (!zone) {
                    continue;
                }
                QVariantMap zoneMap;
                zoneMap[JsonKeys::Id] = zone->id().toString();
                zoneMap[JsonKeys::ZoneNumber] = zone->zoneNumber();

                // Relative geometry (0.0-1.0) for resolution-independent preview
                QRectF relGeo = zone->relativeGeometry();
                QVariantMap relGeoMap;
                relGeoMap[JsonKeys::X] = relGeo.x();
                relGeoMap[JsonKeys::Y] = relGeo.y();
                relGeoMap[JsonKeys::Width] = relGeo.width();
                relGeoMap[JsonKeys::Height] = relGeo.height();
                zoneMap[JsonKeys::RelativeGeometry] = relGeoMap;

                zonesList.append(zoneMap);
            }
            entry.zones = zonesList;

            list.append(entry);
        }
    }

    // Add autotile algorithms
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
