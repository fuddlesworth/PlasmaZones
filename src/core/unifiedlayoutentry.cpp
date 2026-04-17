// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "unifiedlayoutentry.h"

#include "constants.h"
#include "interfaces.h"
#include "layout.h"
#include "layoututils.h"
#include "pz_i18n.h"
#include "utils.h"
#include "../autotile/AlgorithmRegistry.h"
#include "../autotile/TilingAlgorithm.h"

#include <algorithm>

#include <QHash>
#include <QJsonArray>
#include <QUuid>

namespace PlasmaZones {

QString UnifiedLayoutEntry::algorithmId() const
{
    if (!isAutotile)
        return QString();
    return LayoutId::extractAlgorithmId(id);
}

namespace LayoutUtils {

// ═══════════════════════════════════════════════════════════════════════════
// Unified layout list building
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Map aspect ratio class to section label and order for manual layouts
 */
static void setAspectRatioSection(UnifiedLayoutEntry& entry)
{
    const auto cls = static_cast<AspectRatioClass>(entry.aspectRatioClass);
    entry.sectionKey = ScreenClassification::toString(cls);
    switch (cls) {
    case AspectRatioClass::Any:
        entry.sectionLabel = PzI18n::tr("All Monitors");
        entry.sectionOrder = 0;
        break;
    case AspectRatioClass::Standard:
        entry.sectionLabel = PzI18n::tr("Standard (16:9)");
        entry.sectionOrder = 1;
        break;
    case AspectRatioClass::Ultrawide:
        entry.sectionLabel = PzI18n::tr("Ultrawide (21:9)");
        entry.sectionOrder = 2;
        break;
    case AspectRatioClass::SuperUltrawide:
        entry.sectionLabel = PzI18n::tr("Super-Ultrawide (32:9)");
        entry.sectionOrder = 3;
        break;
    case AspectRatioClass::Portrait:
        entry.sectionLabel = PzI18n::tr("Portrait (9:16)");
        entry.sectionOrder = 4;
        break;
    }
}

static UnifiedLayoutEntry entryFromLayout(Layout* layout)
{
    UnifiedLayoutEntry entry;
    entry.id = layout->id().toString();
    entry.name = layout->name();
    entry.description = layout->description();
    entry.zoneCount = layout->zoneCount();
    entry.zones = zonesToVariantList(layout, ZoneField::Minimal);
    entry.autoAssign = layout->autoAssign();
    entry.aspectRatioClass = static_cast<int>(layout->aspectRatioClass());
    if (layout->hasFixedGeometryZones()) {
        QRectF refGeo = layout->lastRecalcGeometry();
        if (refGeo.height() > 0)
            entry.referenceAspectRatio = refGeo.width() / refGeo.height();
    }
    setAspectRatioSection(entry);
    return entry;
}

static void appendAutotileEntries(QVector<UnifiedLayoutEntry>& list)
{
    auto* registry = AlgorithmRegistry::instance();
    if (!registry) {
        return;
    }
    const QStringList algoIds = registry->availableAlgorithms();
    for (const QString& algoId : algoIds) {
        TilingAlgorithm* algo = registry->algorithm(algoId);
        if (!algo) {
            continue;
        }
        UnifiedLayoutEntry entry;
        entry.id = LayoutId::makeAutotileId(algoId);
        entry.name = algo->name();
        entry.description = algo->description();
        entry.isAutotile = true;
        entry.previewZones = AlgorithmRegistry::generatePreviewZones(algo);
        entry.zones = entry.previewZones;
        entry.zoneCount = AlgorithmRegistry::effectiveMaxWindows(algo);
        entry.zoneNumberDisplay = algo->zoneNumberDisplay();
        entry.memory = algo->supportsMemory();
        entry.supportsMasterCount = algo->supportsMasterCount();
        entry.supportsSplitRatio = algo->supportsSplitRatio();
        entry.producesOverlappingZones = algo->producesOverlappingZones();
        entry.supportsCustomParams = algo->supportsCustomParams();
        entry.isScripted = algo->isScripted();
        entry.isUserScript = algo->isUserScript();

        list.append(entry);
    }
}

static bool defaultEntryLessThan(const UnifiedLayoutEntry& a, const UnifiedLayoutEntry& b)
{
    if (a.recommended != b.recommended) {
        return a.recommended;
    }
    if (a.isAutotile != b.isAutotile) {
        return !a.isAutotile;
    }
    return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
}

static void sortUnifiedEntries(QVector<UnifiedLayoutEntry>& list, const QStringList& customOrder = {})
{
    if (!customOrder.isEmpty()) {
        // Build index map for O(1) lookup
        QHash<QString, int> orderMap;
        for (int i = 0; i < customOrder.size(); ++i) {
            orderMap.insert(customOrder[i], i);
        }

        // Entries in customOrder come first (in that order), then the rest by default sort
        std::stable_sort(list.begin(), list.end(),
                         [&orderMap](const UnifiedLayoutEntry& a, const UnifiedLayoutEntry& b) {
                             const int aIdx = orderMap.value(a.id, INT_MAX);
                             const int bIdx = orderMap.value(b.id, INT_MAX);
                             if (aIdx != bIdx) {
                                 return aIdx < bIdx;
                             }
                             return defaultEntryLessThan(a, b);
                         });
    } else {
        std::sort(list.begin(), list.end(), defaultEntryLessThan);
    }
}

QStringList buildCustomOrder(const IOrderingSettings* settings, bool includeManual, bool includeAutotile)
{
    QStringList order;
    if (!settings) {
        return order;
    }
    if (includeManual) {
        order.append(settings->snappingLayoutOrder());
    }
    if (includeAutotile) {
        order.append(settings->tilingAlgorithmOrder());
    }
    return order;
}

QVector<UnifiedLayoutEntry> buildUnifiedLayoutList(ILayoutManager* layoutManager, bool includeAutotile,
                                                   const QStringList& customOrder)
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

    if (includeAutotile) {
        appendAutotileEntries(list);
    }

    sortUnifiedEntries(list, customOrder);

    return list;
}

QVector<UnifiedLayoutEntry> buildUnifiedLayoutList(ILayoutManager* layoutManager, const QString& screenId,
                                                   int virtualDesktop, const QString& activity, bool includeManual,
                                                   bool includeAutotile, qreal screenAspectRatio,
                                                   bool filterByAspectRatio, const QStringList& customOrder)
{
    QVector<UnifiedLayoutEntry> list;

    if (!layoutManager) {
        return list;
    }

    // Translate connector name to screen ID for allowedScreens matching
    QString resolvedScreenId;
    if (!screenId.isEmpty()) {
        resolvedScreenId = Utils::isConnectorName(screenId) ? Utils::screenIdForName(screenId) : screenId;
    }

    // Track the active layout so we can guarantee it appears in the list
    // (prevents empty selector / broken cycling when active layout is hidden)
    Layout* activeLayout = layoutManager->activeLayout();

    if (includeManual) {
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
            if (!isActive && !resolvedScreenId.isEmpty() && !layout->allowedScreens().isEmpty()) {
                if (!layout->allowedScreens().contains(resolvedScreenId)) {
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

            auto entry = entryFromLayout(layout);

            // Tag whether layout is recommended for this screen's aspect ratio
            if (screenAspectRatio > 0.0) {
                entry.recommended = layout->matchesAspectRatio(screenAspectRatio);
            }

            // When filterByAspectRatio is on, skip non-recommended layouts
            // (unless this is the active layout, which always passes)
            if (filterByAspectRatio && screenAspectRatio > 0.0 && !entry.recommended && !isActive) {
                continue;
            }

            list.append(entry);
        }
    }

    if (includeAutotile) {
        appendAutotileEntries(list);
    }

    sortUnifiedEntries(list, customOrder);

    return list;
}

QVariantMap toVariantMap(const UnifiedLayoutEntry& entry)
{
    using namespace JsonKeys;
    QVariantMap map;

    map[Id] = entry.id;
    map[Name] = entry.name;
    map[Description] = entry.description;
    map[ZoneCount] = entry.zoneCount;
    map[Zones] = entry.zones;
    map[Category] = static_cast<int>(entry.isAutotile ? LayoutCategory::Autotile : LayoutCategory::Manual);
    map[AutoAssign] = entry.autoAssign;
    map[QLatin1String("isAutotile")] = entry.isAutotile;
    map[IsSystem] = entry.isSystemEntry();
    map[AspectRatioClassKey] = ScreenClassification::toString(static_cast<AspectRatioClass>(entry.aspectRatioClass));
    map[QLatin1String("recommended")] = entry.recommended;
    if (!entry.zoneNumberDisplay.isEmpty()) {
        map[QLatin1String("zoneNumberDisplay")] = entry.zoneNumberDisplay;
    }
    if (entry.memory) {
        map[QLatin1String("memory")] = true;
    }
    if (entry.isAutotile) {
        map[QLatin1String("supportsMasterCount")] = entry.supportsMasterCount;
        map[QLatin1String("supportsSplitRatio")] = entry.supportsSplitRatio;
        map[QLatin1String("producesOverlappingZones")] = entry.producesOverlappingZones;
        map[QLatin1String("supportsCustomParams")] = entry.supportsCustomParams;
    }
    if (entry.referenceAspectRatio > 0.0) {
        map[QLatin1String("referenceAspectRatio")] = entry.referenceAspectRatio;
    }

    // Generic section grouping
    if (!entry.sectionKey.isEmpty()) {
        map[QLatin1String("sectionKey")] = entry.sectionKey;
        map[QLatin1String("sectionLabel")] = entry.sectionLabel;
        map[QLatin1String("sectionOrder")] = entry.sectionOrder;
    }

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
    json[IsSystem] = entry.isSystemEntry();
    json[Category] = static_cast<int>(entry.isAutotile ? LayoutCategory::Autotile : LayoutCategory::Manual);
    json[QLatin1String("isAutotile")] = entry.isAutotile;
    if (entry.aspectRatioClass != 0) {
        json[AspectRatioClassKey] =
            ScreenClassification::toString(static_cast<AspectRatioClass>(entry.aspectRatioClass));
    }
    if (entry.autoAssign) {
        json[AutoAssign] = true;
    }
    // Generic section grouping
    if (!entry.sectionKey.isEmpty()) {
        json[QLatin1String("sectionKey")] = entry.sectionKey;
        json[QLatin1String("sectionLabel")] = entry.sectionLabel;
        json[QLatin1String("sectionOrder")] = entry.sectionOrder;
    }

    if (!entry.zoneNumberDisplay.isEmpty()) {
        json[QLatin1String("zoneNumberDisplay")] = entry.zoneNumberDisplay;
    }
    if (entry.memory) {
        json[QLatin1String("memory")] = true;
    }
    if (entry.isAutotile) {
        json[QLatin1String("supportsMasterCount")] = entry.supportsMasterCount;
        json[QLatin1String("supportsSplitRatio")] = entry.supportsSplitRatio;
        json[QLatin1String("producesOverlappingZones")] = entry.producesOverlappingZones;
        json[QLatin1String("supportsCustomParams")] = entry.supportsCustomParams;
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
