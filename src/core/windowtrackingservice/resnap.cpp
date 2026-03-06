// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Resnap, snap-all, and resolution change geometry calculations.
// Part of WindowTrackingService — split from windowtrackingservice.cpp for SRP.

#include "../windowtrackingservice.h"
#include "../geometryutils.h"
#include "../layout.h"
#include "../zone.h"
#include "../layoutmanager.h"
#include "../utils.h"
#include "../logging.h"
#include <QScreen>
#include <QUuid>
#include <algorithm>
#include <tuple>

namespace PlasmaZones {

QVector<RotationEntry> WindowTrackingService::calculateResnapFromPreviousLayout()
{
    QVector<RotationEntry> result;
    if (m_resnapBuffer.isEmpty()) {
        return result;
    }

    // Group resnap entries by screen so each screen uses its own layout
    QHash<QString, QVector<const ResnapEntry*>> entriesByScreen;
    for (const ResnapEntry& entry : m_resnapBuffer) {
        entriesByScreen[entry.screenId].append(&entry);
    }

    for (auto screenIt = entriesByScreen.constBegin(); screenIt != entriesByScreen.constEnd(); ++screenIt) {
        const QString& screenName = screenIt.key();

        // Get the layout assigned to this screen (not the global active layout)
        Layout* newLayout = m_layoutManager->resolveLayoutForScreen(screenName);
        if (!newLayout || newLayout->zoneCount() == 0) {
            continue;
        }

        QVector<Zone*> newZones = newLayout->zones();
        std::sort(newZones.begin(), newZones.end(), [](Zone* a, Zone* b) {
            return a->zoneNumber() < b->zoneNumber();
        });
        const int newZoneCount = newZones.size();

        for (const ResnapEntry* entry : screenIt.value()) {
            // Multi-zone windows: map all zone positions to the new layout
            if (entry->allZonePositions.size() > 1) {
                QStringList targetZoneIds;
                for (int pos : entry->allZonePositions) {
                    int targetPos = ((pos - 1) % newZoneCount) + 1;
                    Zone* z = newZones.value(targetPos - 1, nullptr);
                    if (z)
                        targetZoneIds.append(z->id().toString());
                }
                if (!targetZoneIds.isEmpty()) {
                    QRect geo = (targetZoneIds.size() > 1) ? multiZoneGeometry(targetZoneIds, screenName)
                                                           : zoneGeometry(targetZoneIds.first(), screenName);
                    if (geo.isValid()) {
                        RotationEntry rotEntry;
                        rotEntry.windowId = entry->windowId;
                        rotEntry.sourceZoneId = QString();
                        rotEntry.targetZoneId = targetZoneIds.first();
                        rotEntry.targetGeometry = geo;
                        result.append(rotEntry);
                    }
                }
            } else {
                // Single-zone: map position with cycling (1->1, 2->2, 4->1 when 5->3 zones)
                int targetPos = ((entry->zonePosition - 1) % newZoneCount) + 1;
                Zone* targetZone = newZones.value(targetPos - 1, nullptr);
                if (!targetZone) {
                    continue;
                }

                QRect geo = zoneGeometry(targetZone->id().toString(), entry->screenId);
                if (!geo.isValid()) {
                    continue;
                }

                RotationEntry rotEntry;
                rotEntry.windowId = entry->windowId;
                rotEntry.sourceZoneId = QString();
                rotEntry.targetZoneId = targetZone->id().toString();
                rotEntry.targetGeometry = geo;
                result.append(rotEntry);
            }
        }
    }

    m_resnapBuffer.clear();
    return result;
}

QVector<RotationEntry> WindowTrackingService::calculateResnapFromCurrentAssignments(const QString& screenFilter) const
{
    QVector<RotationEntry> result;

    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        const QString& windowId = it.key();
        const QStringList& zoneIds = it.value();
        if (zoneIds.isEmpty()) {
            continue;
        }
        // Skip ALL floating windows. Floating state persists across mode
        // toggles — if the user floated a window (in either mode), it stays
        // floating and should not be resnapped to a zone.
        if (isWindowFloating(windowId)) {
            continue;
        }

        QString screenName = m_windowScreenAssignments.value(windowId);
        if (!screenFilter.isEmpty() && screenName != screenFilter) {
            continue;
        }

        QRect geo =
            (zoneIds.size() > 1) ? multiZoneGeometry(zoneIds, screenName) : zoneGeometry(zoneIds.first(), screenName);
        if (!geo.isValid()) {
            continue;
        }

        RotationEntry entry;
        entry.windowId = windowId;
        entry.sourceZoneId = QString();
        entry.targetZoneId = zoneIds.first();
        entry.targetGeometry = geo;
        result.append(entry);
    }

    qCInfo(lcCore) << "Resnap from current assignments:" << result.size() << "windows"
                   << (screenFilter.isEmpty() ? QStringLiteral("(all screens)")
                                              : QStringLiteral("(screen: %1)").arg(screenFilter));
    return result;
}

QVector<RotationEntry> WindowTrackingService::calculateResnapFromAutotileOrder(const QStringList& autotileWindowOrder,
                                                                               const QString& screenName) const
{
    QVector<RotationEntry> result;

    if (autotileWindowOrder.isEmpty()) {
        return result;
    }

    Layout* layout = m_layoutManager->resolveLayoutForScreen(screenName);
    if (!layout || layout->zoneCount() == 0) {
        qCWarning(lcCore) << "calculateResnapFromAutotileOrder: no layout for screen" << screenName;
        return result;
    }

    // Get zones sorted by zone number (stable sort + UUID tie-breaker for
    // deterministic ordering when multiple zones share the same number)
    QVector<Zone*> zones = layout->zones();
    std::stable_sort(zones.begin(), zones.end(), [](Zone* a, Zone* b) {
        if (a->zoneNumber() != b->zoneNumber())
            return a->zoneNumber() < b->zoneNumber();
        return a->id() < b->id();
    });

    // Get screen and gap settings for geometry calculation
    QScreen* screen = screenName.isEmpty() ? Utils::primaryScreen() : Utils::findScreenByIdOrName(screenName);
    if (!screen) {
        screen = Utils::primaryScreen();
    }
    if (!screen) {
        return result;
    }

    QString screenId = Utils::screenIdentifier(screen);
    int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings, screenId);
    EdgeGaps outerGaps = GeometryUtils::getEffectiveOuterGaps(layout, m_settings, screenId);

    const int zoneCount = zones.size();
    const int windowCount = autotileWindowOrder.size();

    // Cap at zoneCount: excess windows beyond available zones would stack on top of each
    // other with cycling. Instead, leave them unassigned (they stay where they are).
    if (windowCount > zoneCount) {
        qCWarning(lcCore) << "calculateResnapFromAutotileOrder:" << windowCount << "windows but only" << zoneCount
                          << "zones on screen" << screenName << "- excess" << (windowCount - zoneCount)
                          << "windows will not be resnapped";
    }

    for (int i = 0; i < std::min(windowCount, zoneCount); ++i) {
        const QString& windowId = autotileWindowOrder.at(i);

        // Map autotile position directly to zone (1:1, no cycling)
        int zoneIdx = i;
        Zone* targetZone = zones.at(zoneIdx);

        bool useAvail = !layout->useFullScreenGeometry();
        QRectF geoF = GeometryUtils::getZoneGeometryWithGaps(targetZone, screen, zonePadding, outerGaps, useAvail);
        QRect geo = GeometryUtils::snapToRect(geoF);

        if (geo.isValid()) {
            RotationEntry entry;
            entry.windowId = windowId;
            entry.sourceZoneId = QString();
            entry.targetZoneId = targetZone->id().toString();
            entry.targetGeometry = geo;
            result.append(entry);
        }
    }

    qCInfo(lcCore) << "Resnap from autotile order:" << result.size() << "windows for screen" << screenName;
    return result;
}

QStringList WindowTrackingService::buildZoneOrderedWindowList(const QString& screenName) const
{
    if (!m_layoutManager) {
        return {};
    }

    // Get the current layout to resolve zone numbers
    Layout* layout = m_layoutManager->resolveLayoutForScreen(screenName);
    if (!layout || layout->zoneCount() == 0) {
        return {};
    }

    // Build zone UUID → zone number lookup (both formats for robustness)
    const QVector<Zone*> zones = layout->zones();
    QHash<QString, int> zoneNumberMap;
    for (Zone* zone : zones) {
        zoneNumberMap[zone->id().toString()] = zone->zoneNumber();
    }

    // Collect (zoneNumber, insertionIndex, windowId) for windows on this screen.
    // m_windowScreenAssignments stores connector names (screen->name()).
    // Use insertion index as tie-breaker instead of alphabetical windowId
    // to preserve iteration order for same-app windows (avoids scrambling).
    int insertionIdx = 0;
    QVector<std::tuple<int, int, QString>> windowsByZone; // (zoneNum, insertionIdx, windowId)
    for (auto it = m_windowScreenAssignments.constBegin(); it != m_windowScreenAssignments.constEnd(); ++it) {
        if (it.value() != screenName) {
            continue;
        }
        const QString& windowId = it.key();
        // Skip floating windows — they should not participate in zone-ordered
        // transitions (the user's manual-mode float choice should be preserved).
        if (isWindowFloating(windowId)) {
            continue;
        }
        const QStringList& zoneIds = m_windowZoneAssignments.value(windowId);
        if (zoneIds.isEmpty()) {
            continue;
        }

        // Use primary zone's zone number
        auto numIt = zoneNumberMap.constFind(zoneIds.first());
        if (numIt != zoneNumberMap.constEnd()) {
            windowsByZone.append({numIt.value(), insertionIdx++, windowId});
        } else {
            qCWarning(lcCore) << "buildZoneOrderedWindowList: zone UUID" << zoneIds.first() << "for window" << windowId
                              << "not found in layout — skipping";
        }
    }

    // Sort by zone number ascending, preserving iteration order as tie-breaker
    std::stable_sort(windowsByZone.begin(), windowsByZone.end(), [](const auto& a, const auto& b) {
        if (std::get<0>(a) != std::get<0>(b))
            return std::get<0>(a) < std::get<0>(b);
        return std::get<1>(a) < std::get<1>(b); // preserve iteration order
    });

    QStringList result;
    result.reserve(windowsByZone.size());
    for (const auto& entry : windowsByZone) {
        result.append(std::get<2>(entry));
    }

    qCDebug(lcCore) << "buildZoneOrderedWindowList for" << screenName << ":" << result;
    return result;
}

QVector<RotationEntry> WindowTrackingService::calculateSnapAllWindows(const QStringList& windowIds,
                                                                      const QString& screenName) const
{
    QVector<RotationEntry> result;

    Layout* layout = m_layoutManager->resolveLayoutForScreen(screenName);
    if (!layout || layout->zoneCount() == 0) {
        return result;
    }

    // Get zones sorted by zone number (stable sort + UUID tie-breaker for
    // deterministic ordering when multiple zones share the same number)
    QVector<Zone*> zones = layout->zones();
    std::stable_sort(zones.begin(), zones.end(), [](Zone* a, Zone* b) {
        if (a->zoneNumber() != b->zoneNumber())
            return a->zoneNumber() < b->zoneNumber();
        return a->id() < b->id();
    });

    QSet<QUuid> occupiedZoneIds = buildOccupiedZoneSet(screenName);

    // Get screen and gap settings for geometry calculation
    QScreen* screen = screenName.isEmpty() ? Utils::primaryScreen() : Utils::findScreenByIdOrName(screenName);
    if (!screen) {
        screen = Utils::primaryScreen();
    }
    if (!screen) {
        return result;
    }

    QString screenId = Utils::screenIdentifier(screen);
    int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings, screenId);
    EdgeGaps outerGaps = GeometryUtils::getEffectiveOuterGaps(layout, m_settings, screenId);

    // Track zones we're assigning in this batch (to avoid double-assigning)
    QSet<QUuid> batchOccupied = occupiedZoneIds;

    for (const QString& windowId : windowIds) {
        // Find the first unoccupied zone
        Zone* targetZone = nullptr;
        for (Zone* zone : zones) {
            if (!batchOccupied.contains(zone->id())) {
                targetZone = zone;
                break;
            }
        }

        if (!targetZone) {
            // No more empty zones available
            break;
        }

        bool useAvail = !(layout && layout->useFullScreenGeometry());
        QRectF geoF = GeometryUtils::getZoneGeometryWithGaps(targetZone, screen, zonePadding, outerGaps, useAvail);
        QRect geo = GeometryUtils::snapToRect(geoF);

        if (geo.isValid()) {
            RotationEntry entry;
            entry.windowId = windowId;
            entry.sourceZoneId = QString(); // Not previously snapped
            entry.targetZoneId = targetZone->id().toString();
            entry.targetGeometry = geo;
            result.append(entry);

            // Mark zone as occupied for subsequent iterations
            batchOccupied.insert(targetZone->id());
        }
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Resolution Change Handling
// ═══════════════════════════════════════════════════════════════════════════════

QHash<QString, QRect> WindowTrackingService::updatedWindowGeometries() const
{
    QHash<QString, QRect> result;

    if (!m_settings || !m_settings->keepWindowsInZonesOnResolutionChange()) {
        return result;
    }

    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        QString windowId = it.key();
        const QStringList& zoneIds = it.value();
        if (zoneIds.isEmpty()) {
            continue;
        }
        QString screenName = m_windowScreenAssignments.value(windowId);

        QRect geo =
            (zoneIds.size() > 1) ? multiZoneGeometry(zoneIds, screenName) : zoneGeometry(zoneIds.first(), screenName);
        if (geo.isValid()) {
            result[windowId] = geo;
        }
    }

    return result;
}

} // namespace PlasmaZones
