// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Resnap, rotation, and snap-all calculation methods (moved from WindowTrackingService).
// Part of SnapEngine — split into its own translation unit for SRP.

#include "../SnapEngine.h"
#include <PhosphorZones/SnapState.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorZones/LayoutUtils.h>
#include "core/constants.h"
#include "core/geometryutils.h"
#include "core/isettings.h"
#include "core/logging.h"
#include <QGuiApplication>
#include <QScreen>
#include <QUuid>
#include <algorithm>
#include <tuple>

namespace PlasmaZones {

QVector<ZoneAssignmentEntry> SnapEngine::calculateResnapFromPreviousLayout()
{
    QVector<ZoneAssignmentEntry> result;
    const QVector<ResnapEntry> resnapBuffer = m_windowTracker->takeResnapBuffer();
    if (resnapBuffer.isEmpty()) {
        qCDebug(lcCore) << "calculateResnapFromPreviousLayout: buffer is empty";
        return result;
    }
    qCDebug(lcCore) << "calculateResnapFromPreviousLayout: buffer has" << resnapBuffer.size() << "entries";

    // Helper: create a "__restore__" entry that tells the KWin effect to move
    // the window back to its pre-tile geometry instead of snapping it to a zone.
    auto tryAppendRestore = [this, &result](const ResnapEntry* entry) {
        // No screen validation needed — resnap restores to the exact captured geometry.
        auto preTile = m_windowTracker->validatedUnmanagedGeometry(entry->windowId, QString());
        if (preTile && preTile->isValid()) {
            ZoneAssignmentEntry restoreEntry;
            restoreEntry.windowId = entry->windowId;
            restoreEntry.sourceZoneId = QString();
            restoreEntry.targetZoneId = QString(RestoreSentinel);
            restoreEntry.targetGeometry = *preTile;
            result.append(restoreEntry);
        } else {
            qCDebug(lcCore) << "No pre-tile geometry for excess window" << entry->windowId
                            << "- window will remain at stale position";
        }
    };

    // Group resnap entries by screen so each screen uses its own layout
    QHash<QString, QVector<const ResnapEntry*>> entriesByScreen;
    for (const ResnapEntry& entry : resnapBuffer) {
        entriesByScreen[entry.screenId].append(&entry);
    }

    for (auto screenIt = entriesByScreen.constBegin(); screenIt != entriesByScreen.constEnd(); ++screenIt) {
        const QString& screenId = screenIt.key();

        // Get the layout assigned to this screen (not the global active layout)
        PhosphorZones::Layout* newLayout = m_layoutManager->resolveLayoutForScreen(screenId);
        if (!newLayout || newLayout->zoneCount() == 0) {
            continue;
        }

        QVector<PhosphorZones::Zone*> newZones = newLayout->zones();
        PhosphorZones::LayoutUtils::sortZonesByNumber(newZones);
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
                    PhosphorZones::Zone* z = newZones.value(pos - 1, nullptr);
                    if (z)
                        targetZoneIds.append(z->id().toString());
                }
                if (!targetZoneIds.isEmpty()) {
                    QRect geo = m_windowTracker->resolveZoneGeometry(targetZoneIds, screenId);
                    if (geo.isValid()) {
                        ZoneAssignmentEntry assignEntry;
                        assignEntry.windowId = entry->windowId;
                        assignEntry.sourceZoneId = QString();
                        assignEntry.targetZoneId = targetZoneIds.first();
                        if (targetZoneIds.size() > 1)
                            assignEntry.targetZoneIds = targetZoneIds;
                        assignEntry.targetGeometry = geo;
                        result.append(assignEntry);
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
                PhosphorZones::Zone* targetZone = newZones.value(entry->zonePosition - 1, nullptr);
                if (!targetZone) {
                    continue;
                }

                QRect geo = m_windowTracker->zoneGeometry(targetZone->id().toString(), entry->screenId);
                if (!geo.isValid()) {
                    continue;
                }

                ZoneAssignmentEntry assignEntry;
                assignEntry.windowId = entry->windowId;
                assignEntry.sourceZoneId = QString();
                assignEntry.targetZoneId = targetZone->id().toString();
                assignEntry.targetGeometry = geo;
                result.append(assignEntry);
            }
        }
    }

    return result;
}

QVector<ZoneAssignmentEntry> SnapEngine::calculateResnapFromCurrentAssignments(const QString& screenFilter) const
{
    QVector<ZoneAssignmentEntry> result;

    const auto& zoneAssignments = m_windowTracker->zoneAssignments();
    const auto& screenAssignments = m_windowTracker->screenAssignments();

    for (auto it = zoneAssignments.constBegin(); it != zoneAssignments.constEnd(); ++it) {
        const QString& windowId = it.key();
        const QStringList& zoneIds = it.value();
        if (zoneIds.isEmpty()) {
            continue;
        }
        // Skip ALL floating windows. Floating state persists across mode
        // toggles — if the user floated a window (in either mode), it stays
        // floating and should not be resnapped to a zone.
        if (m_windowTracker->isWindowFloating(windowId)) {
            continue;
        }

        QString screenId = screenAssignments.value(windowId);
        if (!screenFilter.isEmpty()) {
            // If the filter is a virtual screen ID, require exact equality —
            // screensMatch already enforces that distinct VS IDs (and VS vs
            // physical) never match. If the filter is a physical screen ID,
            // match windows stored on that physical screen OR on any of its
            // virtual children — belongsToPhysicalScreen handles both cases.
            const bool match = PhosphorIdentity::VirtualScreenId::isVirtual(screenFilter)
                ? Phosphor::Screens::ScreenIdentity::screensMatch(screenId, screenFilter)
                : Phosphor::Screens::ScreenIdentity::belongsToPhysicalScreen(screenId, screenFilter);
            if (!match) {
                continue;
            }
        }

        QRect geo = m_windowTracker->resolveZoneGeometry(zoneIds, screenId);
        if (!geo.isValid()) {
            continue;
        }

        ZoneAssignmentEntry entry;
        entry.windowId = windowId;
        entry.sourceZoneId = QString();
        entry.targetZoneId = zoneIds.first();
        if (zoneIds.size() > 1)
            entry.targetZoneIds = zoneIds;
        entry.targetGeometry = geo;
        // Stamp the authoritative target screen so processBatchEntries skips
        // re-derivation from geometry.center(). If the layout's geometry ever
        // resolved to a stale fallback (e.g. a transient cache miss), the
        // re-derivation could land in a sibling VS and clobber the stored
        // assignment. Trust the source screen we already read above.
        entry.targetScreenId = screenId;
        result.append(entry);
    }

    qCInfo(lcCore) << "Resnap from current assignments:" << result.size() << "windows"
                   << "(total zone assignments:" << zoneAssignments.size() << ")"
                   << (screenFilter.isEmpty() ? QStringLiteral("(all screens)")
                                              : QStringLiteral("(screen: %1)").arg(screenFilter));
    if (result.isEmpty() && !zoneAssignments.isEmpty() && lcCore().isDebugEnabled()) {
        auto screenMatches = [&](const QString& screen) {
            if (screenFilter.isEmpty())
                return true;
            return PhosphorIdentity::VirtualScreenId::isVirtual(screenFilter)
                ? Phosphor::Screens::ScreenIdentity::screensMatch(screen, screenFilter)
                : Phosphor::Screens::ScreenIdentity::belongsToPhysicalScreen(screen, screenFilter);
        };
        for (auto it = zoneAssignments.constBegin(); it != zoneAssignments.constEnd(); ++it) {
            QString screen = screenAssignments.value(it.key());
            bool floating = m_windowTracker->isWindowFloating(it.key());
            QRect geo = it.value().isEmpty() ? QRect() : m_windowTracker->resolveZoneGeometry(it.value(), screen);
            qCDebug(lcCore) << "  skipped:" << it.key() << "zones=" << it.value() << "screen=" << screen
                            << "floating=" << floating << "geoValid=" << geo.isValid()
                            << "screenMatch=" << screenMatches(screen);
        }
    }
    return result;
}

QVector<ZoneAssignmentEntry> SnapEngine::calculateResnapFromAutotileOrder(const QStringList& autotileWindowOrder,
                                                                          const QString& screenId) const
{
    QVector<ZoneAssignmentEntry> result;

    if (autotileWindowOrder.isEmpty()) {
        return result;
    }

    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    if (!layout || layout->zoneCount() == 0) {
        qCWarning(lcCore) << "calculateResnapFromAutotileOrder: no layout for screen" << screenId;
        return result;
    }

    QVector<PhosphorZones::Zone*> zones = layout->zones();
    PhosphorZones::LayoutUtils::sortZonesByNumber(zones);

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
    QHash<QString, PhosphorZones::Zone*> zoneById;
    for (PhosphorZones::Zone* z : zones) {
        zoneById[z->id().toString()] = z;
    }

    // Track which zones have been claimed by original-assignment restoration
    // so positional fallback doesn't double-assign.
    QSet<int> claimedZoneIndices;
    // Track windows placed in the first pass for O(1) lookup in the second pass
    QSet<QString> placedWindowIds;

    const auto& zoneAssignments = m_windowTracker->zoneAssignments();

    // First pass: restore windows to their ORIGINAL zone assignment (pre-autotile).
    // m_windowZoneAssignments preserves zone IDs from before autotile was activated.
    // This ensures a window in zone 3 returns to zone 3, not whatever autotile
    // position it ended up in.
    for (int i = 0; i < std::min(windowCount, zoneCount); ++i) {
        const QString& windowId = autotileWindowOrder.at(i);
        const QStringList& savedZones = zoneAssignments.value(windowId);

        if (savedZones.isEmpty())
            continue;

        // Restore to ALL saved zone assignments (supports multi-zone spans).
        // Verify all zones still exist in the restored layout.
        QStringList validZoneIds;
        QList<int> validZoneIndices;
        for (const QString& zid : savedZones) {
            PhosphorZones::Zone* z = zoneById.value(zid);
            if (!z)
                continue;
            int idx = zones.indexOf(z);
            if (idx < 0 || claimedZoneIndices.contains(idx))
                continue;
            validZoneIds.append(zid);
            validZoneIndices.append(idx);
        }
        if (validZoneIds.isEmpty())
            continue;

        // Compute geometry: combined for multi-zone, single for normal
        QRect geo = m_windowTracker->resolveZoneGeometry(validZoneIds, screenId);

        if (geo.isValid()) {
            ZoneAssignmentEntry entry;
            entry.windowId = windowId;
            entry.sourceZoneId = QString();
            entry.targetZoneId = validZoneIds.first();
            if (validZoneIds.size() > 1)
                entry.targetZoneIds = validZoneIds;
            entry.targetGeometry = geo;
            result.append(entry);
            placedWindowIds.insert(windowId);
            for (int idx : validZoneIndices)
                claimedZoneIndices.insert(idx);
        }
    }

    // Second pass: windows without a valid original zone get positional fallback
    // (autotile order position → first unclaimed zone).
    for (int i = 0; i < std::min(windowCount, zoneCount); ++i) {
        const QString& windowId = autotileWindowOrder.at(i);

        // Skip if already placed by original-zone restoration
        if (placedWindowIds.contains(windowId))
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

        PhosphorZones::Zone* targetZone = zones.at(zoneIdx);
        QRect geo = m_windowTracker->zoneGeometry(targetZone->id().toString(), screenId);

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

QVector<ZoneAssignmentEntry> SnapEngine::calculateSnapAllWindowEntries(const QStringList& windowIds,
                                                                       const QString& screenId) const
{
    QVector<ZoneAssignmentEntry> result;

    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    if (!layout || layout->zoneCount() == 0) {
        return result;
    }

    QVector<PhosphorZones::Zone*> zones = layout->zones();
    PhosphorZones::LayoutUtils::sortZonesByNumber(zones);

    // Filter occupancy by the current virtual desktop so windows parked on other
    // desktops don't make zones appear occupied on the current-desktop batch snap.
    const int desktopFilter = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    QSet<QUuid> occupiedZoneIds = m_windowTracker->buildOccupiedZoneSet(screenId, desktopFilter);

    // Resolve physical screen for zone geometry calculation
    auto* screenManager = m_windowTracker->screenManager();
    QScreen* screen = (screenManager ? screenManager->physicalQScreenFor(screenId) : QGuiApplication::primaryScreen());
    if (!screen) {
        return result;
    }

    // Track zones we're assigning in this batch (to avoid double-assigning)
    QSet<QUuid> batchOccupied = occupiedZoneIds;

    for (const QString& windowId : windowIds) {
        // Find the first unoccupied zone
        PhosphorZones::Zone* targetZone = nullptr;
        for (PhosphorZones::Zone* zone : zones) {
            if (!batchOccupied.contains(zone->id())) {
                targetZone = zone;
                break;
            }
        }

        if (!targetZone) {
            // No more empty zones available
            break;
        }

        QRect geo =
            GeometryUtils::getZoneGeometryForScreen(screenManager, targetZone, screen, screenId, layout, m_settings);

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

QVector<ZoneAssignmentEntry> SnapEngine::calculateRotation(bool clockwise, const QString& screenFilter) const
{
    QVector<ZoneAssignmentEntry> result;

    const auto& zoneAssignments = m_windowTracker->zoneAssignments();
    const auto& screenAssignments = m_windowTracker->screenAssignments();

    // Group snapped windows by screen so each screen rotates independently
    // using its own per-screen layout (not the global active layout)
    QHash<QString, QVector<QPair<QString, QString>>> windowsByScreen; // screenId -> [(windowId, primaryZoneId)]
    for (auto it = zoneAssignments.constBegin(); it != zoneAssignments.constEnd(); ++it) {
        const QStringList& zoneIdList = it.value();
        if (zoneIdList.isEmpty()) {
            continue;
        }

        QString screenId = screenAssignments.value(it.key());

        // When a screen filter is set, only include windows on that screen
        if (!screenFilter.isEmpty() && !Phosphor::Screens::ScreenIdentity::screensMatch(screenId, screenFilter)) {
            continue;
        }

        windowsByScreen[screenId].append({it.key(), zoneIdList.first()});
    }

    // Process each screen independently
    for (auto screenIt = windowsByScreen.constBegin(); screenIt != windowsByScreen.constEnd(); ++screenIt) {
        const QString& screenId = screenIt.key();

        // Get the layout assigned to THIS screen (not the global active layout)
        PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
        if (!layout || layout->zoneCount() < 2) {
            continue;
        }

        // Get zones sorted by zone number
        QVector<PhosphorZones::Zone*> zones = layout->zones();
        PhosphorZones::LayoutUtils::sortZonesByNumber(zones);

        // Build zone ID -> index map (with and without braces for format-agnostic matching)
        QHash<QString, int> zoneIdToIndex;
        for (int i = 0; i < zones.size(); ++i) {
            zoneIdToIndex[zones[i]->id().toString()] = i;
        }

        // Find zone indices for windows on this screen
        QVector<QPair<QString, int>> windowZoneIndices;
        for (const auto& windowEntry : screenIt.value()) {
            const QString& windowId = windowEntry.first;
            const QString& storedZoneId = windowEntry.second;
            int zoneIndex = -1;

            // Try direct match first
            if (zoneIdToIndex.contains(storedZoneId)) {
                zoneIndex = zoneIdToIndex.value(storedZoneId);
            } else {
                // Try matching by parsing as UUID (handles format differences)
                QUuid storedUuid = QUuid::fromString(storedZoneId);
                if (!storedUuid.isNull()) {
                    for (int i = 0; i < zones.size(); ++i) {
                        if (zones[i]->id() == storedUuid) {
                            zoneIndex = i;
                            break;
                        }
                    }
                }
            }

            if (zoneIndex >= 0) {
                windowZoneIndices.append({windowId, zoneIndex});
            } else {
                qCDebug(lcCore) << "Window" << windowId << "has zone ID" << storedZoneId
                                << "not found in layout for screen" << screenId << "- skipping rotation";
            }
        }

        // Resolve physical screen for zone geometry calculation
        auto* screenManager = m_windowTracker->screenManager();
        QScreen* screen =
            (screenManager ? screenManager->physicalQScreenFor(screenId) : QGuiApplication::primaryScreen());
        if (!screen) {
            continue;
        }

        // Calculate rotated positions within this screen's zones
        for (const auto& pair : windowZoneIndices) {
            int currentIdx = pair.second;
            int targetIdx =
                clockwise ? (currentIdx + 1) % zones.size() : (currentIdx - 1 + zones.size()) % zones.size();

            PhosphorZones::Zone* sourceZone = zones[currentIdx];
            PhosphorZones::Zone* targetZone = zones[targetIdx];
            QRect geo = GeometryUtils::getZoneGeometryForScreen(screenManager, targetZone, screen, screenId, layout,
                                                                m_settings);

            if (geo.isValid()) {
                ZoneAssignmentEntry entry;
                entry.windowId = pair.first;
                entry.sourceZoneId = sourceZone->id().toString();
                entry.targetZoneId = targetZone->id().toString();
                entry.targetGeometry = geo;
                result.append(entry);
            }
        }
    }

    return result;
}

} // namespace PlasmaZones
