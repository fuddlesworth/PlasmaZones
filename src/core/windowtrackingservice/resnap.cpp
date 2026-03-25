// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Resnap, snap-all, and resolution change geometry calculations.
// Part of WindowTrackingService — split from windowtrackingservice.cpp for SRP.

#include "../windowtrackingservice.h"
#include "../geometryutils.h"
#include "../layout.h"
#include "../screenmanager.h"
#include "../zone.h"
#include "../layoutmanager.h"
#include "../utils.h"
#include "../logging.h"
#include <QScreen>
#include <QUuid>
#include <algorithm>
#include <tuple>

namespace PlasmaZones {

namespace {
/// Sort zones by zone number ascending, with UUID tie-breaker for determinism
/// when multiple zones share the same number. Used by resnap, snap-all, and
/// autotile-order-to-zone-assignment logic.
void sortZonesByNumber(QVector<Zone*>& zones)
{
    std::stable_sort(zones.begin(), zones.end(), [](Zone* a, Zone* b) {
        if (a->zoneNumber() != b->zoneNumber())
            return a->zoneNumber() < b->zoneNumber();
        return a->id() < b->id();
    });
}
} // anonymous namespace

QVector<ZoneAssignmentEntry> WindowTrackingService::calculateResnapFromPreviousLayout()
{
    QVector<ZoneAssignmentEntry> result;
    if (m_resnapBuffer.isEmpty()) {
        qCDebug(lcCore) << "calculateResnapFromPreviousLayout: buffer is empty";
        return result;
    }
    qCDebug(lcCore) << "calculateResnapFromPreviousLayout: buffer has" << m_resnapBuffer.size() << "entries";

    // Helper: create a "__restore__" entry that tells the KWin effect to move
    // the window back to its pre-tile geometry instead of snapping it to a zone.
    auto tryAppendRestore = [this, &result](const ResnapEntry* entry) {
        auto preTile = preTileGeometry(entry->windowId);
        if (!preTile) {
            preTile = preTileGeometry(Utils::extractAppId(entry->windowId));
        }
        if (preTile && preTile->isValid()) {
            ZoneAssignmentEntry rotEntry;
            rotEntry.windowId = entry->windowId;
            rotEntry.sourceZoneId = QString();
            rotEntry.targetZoneId = QStringLiteral("__restore__");
            rotEntry.targetGeometry = *preTile;
            result.append(rotEntry);
        } else {
            qCDebug(lcCore) << "No pre-tile geometry for excess window" << entry->windowId
                            << "- window will remain at stale position";
        }
    };

    // Group resnap entries by screen so each screen uses its own layout
    QHash<QString, QVector<const ResnapEntry*>> entriesByScreen;
    for (const ResnapEntry& entry : m_resnapBuffer) {
        entriesByScreen[entry.screenId].append(&entry);
    }

    for (auto screenIt = entriesByScreen.constBegin(); screenIt != entriesByScreen.constEnd(); ++screenIt) {
        const QString& screenId = screenIt.key();

        // Get the layout assigned to this screen (not the global active layout)
        Layout* newLayout = m_layoutManager->resolveLayoutForScreen(screenId);
        if (!newLayout || newLayout->zoneCount() == 0) {
            continue;
        }

        QVector<Zone*> newZones = newLayout->zones();
        sortZonesByNumber(newZones);
        const int newZoneCount = newZones.size();

        for (const ResnapEntry* entry : screenIt.value()) {
            // Multi-zone windows: map zone positions to the new layout.
            // Only include positions that exist in the new layout — don't cycle
            // excess positions (e.g. zone 3 shouldn't map to zone 1 when going
            // from a 3-zone to a 2-zone layout).
            if (entry->allZonePositions.size() > 1) {
                QStringList targetZoneIds;
                for (int pos : entry->allZonePositions) {
                    if (pos > newZoneCount)
                        continue; // skip positions beyond new layout
                    Zone* z = newZones.value(pos - 1, nullptr);
                    if (z)
                        targetZoneIds.append(z->id().toString());
                }
                if (!targetZoneIds.isEmpty()) {
                    QRect geo = (targetZoneIds.size() > 1) ? multiZoneGeometry(targetZoneIds, screenId)
                                                           : zoneGeometry(targetZoneIds.first(), screenId);
                    if (geo.isValid()) {
                        ZoneAssignmentEntry rotEntry;
                        rotEntry.windowId = entry->windowId;
                        rotEntry.sourceZoneId = QString();
                        rotEntry.targetZoneId = targetZoneIds.first();
                        if (targetZoneIds.size() > 1)
                            rotEntry.targetZoneIds = targetZoneIds;
                        rotEntry.targetGeometry = geo;
                        result.append(rotEntry);
                    }
                } else {
                    // ALL zone positions exceed the new layout — restore to pre-tile geometry.
                    tryAppendRestore(entry);
                }
            } else {
                // Single-zone: map 1:1 by position (1->1, 2->2).
                // Windows whose position exceeds the new zone count are restored to
                // their pre-tile geometry — a window snapped to zone 3 should go back
                // to its original size when switching to a 2-zone layout.
                if (entry->zonePosition > newZoneCount) {
                    tryAppendRestore(entry);
                    continue;
                }
                Zone* targetZone = newZones.value(entry->zonePosition - 1, nullptr);
                if (!targetZone) {
                    continue;
                }

                QRect geo = zoneGeometry(targetZone->id().toString(), entry->screenId);
                if (!geo.isValid()) {
                    continue;
                }

                ZoneAssignmentEntry rotEntry;
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

void WindowTrackingService::populateResnapBufferForAllScreens(const QSet<QString>& excludeScreens)
{
    QVector<ResnapEntry> newBuffer;
    QSet<QString> addedIds;

    // Build per-screen zone position maps: for each screen, resolve the CURRENT
    // layout and build zoneId → position mapping. This captures the OLD state
    // before the KCM's new assignments take effect on the daemon.
    // (At this point, LayoutManager already has the new assignments from the KCM's
    // D-Bus calls, so resolveLayoutForScreen returns the NEW layout. But the
    // window zone assignments in WTS still reference zone IDs from the OLD layout.)
    //
    // Since zone IDs from the old layout may not exist in the new layout,
    // we need to find each window's POSITION in the old layout. The old layout
    // is the one whose zone IDs match the window's zone assignments.
    // We look up each zone ID in ALL loaded layouts to find the position.

    // Build a global zoneId → (layoutId, position) map from all layouts
    QHash<QString, int> globalZoneIdToPosition;
    for (Layout* layout : m_layoutManager->layouts()) {
        QVector<Zone*> zones = layout->zones();
        std::sort(zones.begin(), zones.end(), [](Zone* a, Zone* b) {
            return a->zoneNumber() < b->zoneNumber();
        });
        for (int i = 0; i < zones.size(); ++i) {
            globalZoneIdToPosition[zones[i]->id().toString()] = i + 1; // 1-based
        }
    }

    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        const QString& windowId = it.key();
        const QStringList& zoneIds = it.value();
        if (zoneIds.isEmpty() || isWindowFloating(windowId))
            continue;

        const QString screenId = m_windowScreenAssignments.value(windowId);
        if (screenId.isEmpty())
            continue;

        // Skip windows on excluded screens (e.g. autotile screens)
        if (excludeScreens.contains(screenId))
            continue;

        if (addedIds.contains(windowId))
            continue;
        addedIds.insert(windowId);

        // Look up the zone position from the global map
        const QString& primaryZoneId = zoneIds.first();
        int position = globalZoneIdToPosition.value(primaryZoneId, 0);
        if (position <= 0)
            continue;

        ResnapEntry entry;
        entry.windowId = windowId;
        entry.zonePosition = position;
        entry.screenId = screenId;

        // Multi-zone: collect all positions
        if (zoneIds.size() > 1) {
            for (const QString& zid : zoneIds) {
                int p = globalZoneIdToPosition.value(zid, 0);
                if (p > 0)
                    entry.allZonePositions.append(p);
            }
        }

        newBuffer.append(entry);
    }

    if (!newBuffer.isEmpty()) {
        m_resnapBuffer = std::move(newBuffer);
        qCInfo(lcCore) << "Resnap buffer (all screens):" << m_resnapBuffer.size() << "windows";
    }
}

QVector<ZoneAssignmentEntry>
WindowTrackingService::calculateResnapFromCurrentAssignments(const QString& screenFilter) const
{
    QVector<ZoneAssignmentEntry> result;

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

        QString screenId = m_windowScreenAssignments.value(windowId);
        if (!screenFilter.isEmpty() && !Utils::screensMatch(screenId, screenFilter)) {
            continue;
        }

        QRect geo =
            (zoneIds.size() > 1) ? multiZoneGeometry(zoneIds, screenId) : zoneGeometry(zoneIds.first(), screenId);
        if (!geo.isValid()) {
            continue;
        }

        ZoneAssignmentEntry entry;
        entry.windowId = windowId;
        entry.sourceZoneId = QString();
        entry.targetZoneId = zoneIds.first();
        entry.targetGeometry = geo;
        result.append(entry);
    }

    qCInfo(lcCore) << "Resnap from current assignments:" << result.size() << "windows"
                   << "(total zone assignments:" << m_windowZoneAssignments.size() << ")"
                   << (screenFilter.isEmpty() ? QStringLiteral("(all screens)")
                                              : QStringLiteral("(screen: %1)").arg(screenFilter));
    if (result.isEmpty() && !m_windowZoneAssignments.isEmpty() && lcCore().isDebugEnabled()) {
        for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
            QString screen = m_windowScreenAssignments.value(it.key());
            bool floating = isWindowFloating(it.key());
            QRect geo = it.value().isEmpty() ? QRect()
                                             : (it.value().size() > 1 ? multiZoneGeometry(it.value(), screen)
                                                                      : zoneGeometry(it.value().first(), screen));
            qCDebug(lcCore) << "  skipped:" << it.key() << "zones=" << it.value() << "screen=" << screen
                            << "floating=" << floating << "geoValid=" << geo.isValid()
                            << "screenMatch=" << (screenFilter.isEmpty() || screen == screenFilter);
        }
    }
    return result;
}

QVector<ZoneAssignmentEntry>
WindowTrackingService::calculateResnapFromAutotileOrder(const QStringList& autotileWindowOrder,
                                                        const QString& screenId) const
{
    QVector<ZoneAssignmentEntry> result;

    if (autotileWindowOrder.isEmpty()) {
        return result;
    }

    Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    if (!layout || layout->zoneCount() == 0) {
        qCWarning(lcCore) << "calculateResnapFromAutotileOrder: no layout for screen" << screenId;
        return result;
    }

    QVector<Zone*> zones = layout->zones();
    sortZonesByNumber(zones);

    // Get screen and gap settings for geometry calculation
    QScreen* screen = ScreenManager::resolvePhysicalScreen(screenId);
    if (!screen) {
        return result;
    }
    auto* smgr = ScreenManager::instance();
    QRect vsGeom = smgr ? smgr->screenGeometry(screenId) : QRect();
    QRect vsAvailGeom = smgr ? smgr->screenAvailableGeometry(screenId) : QRect();

    int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings, screenId);
    EdgeGaps outerGaps = GeometryUtils::getEffectiveOuterGaps(layout, m_settings, screenId);

    const int zoneCount = zones.size();
    const int windowCount = autotileWindowOrder.size();

    // Cap at zoneCount: excess windows beyond available zones would stack on top of each
    // other with cycling. Instead, leave them unassigned (they stay where they are).
    if (windowCount > zoneCount) {
        qCWarning(lcCore) << "calculateResnapFromAutotileOrder:" << windowCount << "windows but only" << zoneCount
                          << "zones on screen" << screenId << "- excess" << (windowCount - zoneCount)
                          << "windows will not be resnapped";
    }

    // Build a lookup from zone ID → zone pointer for original-zone restoration
    QHash<QString, Zone*> zoneById;
    for (Zone* z : zones) {
        zoneById[z->id().toString()] = z;
    }

    // Track which zones have been claimed by original-assignment restoration
    // so positional fallback doesn't double-assign.
    QSet<int> claimedZoneIndices;

    // First pass: restore windows to their ORIGINAL zone assignment (pre-autotile).
    // m_windowZoneAssignments preserves zone IDs from before autotile was activated.
    // This ensures a window in zone 3 returns to zone 3, not whatever autotile
    // position it ended up in.
    for (int i = 0; i < std::min(windowCount, zoneCount); ++i) {
        const QString& windowId = autotileWindowOrder.at(i);
        const QStringList& savedZones = m_windowZoneAssignments.value(windowId);

        if (savedZones.isEmpty())
            continue;

        // Use the first saved zone assignment (primary zone for multi-zone snaps)
        const QString& savedZoneId = savedZones.first();
        Zone* targetZone = zoneById.value(savedZoneId);
        if (!targetZone)
            continue; // Zone no longer exists in the restored layout

        int zoneIdx = zones.indexOf(targetZone);
        if (zoneIdx < 0 || claimedZoneIndices.contains(zoneIdx))
            continue; // Already claimed by another window

        bool useAvail = !layout->useFullScreenGeometry();
        QRectF geoF;
        if (vsGeom.isValid()) {
            QRect availGeom = vsAvailGeom.isValid() ? vsAvailGeom : vsGeom;
            geoF = GeometryUtils::getZoneGeometryWithGaps(targetZone, vsGeom, availGeom, zonePadding, outerGaps,
                                                          useAvail, screenId);
        } else {
            geoF = GeometryUtils::getZoneGeometryWithGaps(targetZone, screen, zonePadding, outerGaps, useAvail);
        }
        QRect geo = GeometryUtils::snapToRect(geoF);

        if (geo.isValid()) {
            ZoneAssignmentEntry entry;
            entry.windowId = windowId;
            entry.sourceZoneId = QString();
            entry.targetZoneId = targetZone->id().toString();
            entry.targetGeometry = geo;
            result.append(entry);
            claimedZoneIndices.insert(zoneIdx);
        }
    }

    // Second pass: windows without a valid original zone get positional fallback
    // (autotile order position → first unclaimed zone).
    for (int i = 0; i < std::min(windowCount, zoneCount); ++i) {
        const QString& windowId = autotileWindowOrder.at(i);

        // Skip if already placed by original-zone restoration
        bool alreadyPlaced = false;
        for (const auto& entry : result) {
            if (entry.windowId == windowId) {
                alreadyPlaced = true;
                break;
            }
        }
        if (alreadyPlaced)
            continue;

        // Find next unclaimed zone
        int zoneIdx = -1;
        for (int z = 0; z < zoneCount; ++z) {
            if (!claimedZoneIndices.contains(z)) {
                zoneIdx = z;
                break;
            }
        }
        if (zoneIdx < 0)
            break; // No more zones available

        Zone* targetZone = zones.at(zoneIdx);
        bool useAvail = !layout->useFullScreenGeometry();
        QRectF geoF;
        if (vsGeom.isValid()) {
            QRect availGeom = vsAvailGeom.isValid() ? vsAvailGeom : vsGeom;
            geoF = GeometryUtils::getZoneGeometryWithGaps(targetZone, vsGeom, availGeom, zonePadding, outerGaps,
                                                          useAvail, screenId);
        } else {
            geoF = GeometryUtils::getZoneGeometryWithGaps(targetZone, screen, zonePadding, outerGaps, useAvail);
        }
        QRect geo = GeometryUtils::snapToRect(geoF);

        if (geo.isValid()) {
            ZoneAssignmentEntry entry;
            entry.windowId = windowId;
            entry.sourceZoneId = QString();
            entry.targetZoneId = targetZone->id().toString();
            entry.targetGeometry = geo;
            result.append(entry);
            claimedZoneIndices.insert(zoneIdx);
        }
    }

    qCInfo(lcCore) << "Resnap from autotile order:" << result.size() << "windows for screen" << screenId;
    return result;
}

QStringList WindowTrackingService::buildZoneOrderedWindowList(const QString& screenId) const
{
    if (!m_layoutManager) {
        return {};
    }

    // Get the current layout to resolve zone numbers
    Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
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
    // m_windowScreenAssignments may store connector names or EDID-based screen IDs
    // depending on the code path. Use screensMatch() for format-agnostic comparison.
    int insertionIdx = 0;
    QVector<std::tuple<int, int, QString>> windowsByZone; // (zoneNum, insertionIdx, windowId)
    for (auto it = m_windowScreenAssignments.constBegin(); it != m_windowScreenAssignments.constEnd(); ++it) {
        if (!Utils::screensMatch(it.value(), screenId)) {
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
                              << "not found in layout - skipping";
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

    qCDebug(lcCore) << "buildZoneOrderedWindowList for" << screenId << ":" << result;
    return result;
}

QVector<ZoneAssignmentEntry> WindowTrackingService::calculateSnapAllWindows(const QStringList& windowIds,
                                                                            const QString& screenId) const
{
    QVector<ZoneAssignmentEntry> result;

    Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    if (!layout || layout->zoneCount() == 0) {
        return result;
    }

    QVector<Zone*> zones = layout->zones();
    sortZonesByNumber(zones);

    QSet<QUuid> occupiedZoneIds = buildOccupiedZoneSet(screenId);

    // Get screen and gap settings for geometry calculation
    QScreen* screen = ScreenManager::resolvePhysicalScreen(screenId);
    if (!screen) {
        return result;
    }
    auto* smgr = ScreenManager::instance();
    QRect vsGeom = smgr ? smgr->screenGeometry(screenId) : QRect();
    QRect vsAvailGeom = smgr ? smgr->screenAvailableGeometry(screenId) : QRect();

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
        QRectF geoF;
        if (vsGeom.isValid()) {
            QRect availGeom = vsAvailGeom.isValid() ? vsAvailGeom : vsGeom;
            geoF = GeometryUtils::getZoneGeometryWithGaps(targetZone, vsGeom, availGeom, zonePadding, outerGaps,
                                                          useAvail, screenId);
        } else {
            geoF = GeometryUtils::getZoneGeometryWithGaps(targetZone, screen, zonePadding, outerGaps, useAvail);
        }
        QRect geo = GeometryUtils::snapToRect(geoF);

        if (geo.isValid()) {
            ZoneAssignmentEntry entry;
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
        QString screenId = m_windowScreenAssignments.value(windowId);

        QRect geo =
            (zoneIds.size() > 1) ? multiZoneGeometry(zoneIds, screenId) : zoneGeometry(zoneIds.first(), screenId);
        if (geo.isValid()) {
            result[windowId] = geo;
        }
    }

    return result;
}

} // namespace PlasmaZones
