// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Resnap, snap-all, and resolution change geometry calculations.
// Part of WindowTrackingService — split from windowtrackingservice.cpp for SRP.

#include "../windowtrackingservice.h"
#include "../geometryutils.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "../utils.h"
#include "../logging.h"
#include <QScreen>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <tuple>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

void WindowTrackingService::populateResnapBufferForAllScreens(const QSet<QString>& excludeScreens,
                                                              const QSet<QString>& includeScreens)
{
    QVector<ResnapEntry> newBuffer;
    QSet<QString> addedIds;

    // Build per-screen zone position maps: for each screen, resolve the CURRENT
    // layout and build zoneId → position mapping. This captures the OLD state
    // before the KCM's new assignments take effect on the daemon.
    // (At this point, PhosphorZones::LayoutRegistry already has the new assignments from the KCM's
    // D-Bus calls, so resolveLayoutForScreen returns the NEW layout. But the
    // window zone assignments in WTS still reference zone IDs from the OLD layout.)
    //
    // Since zone IDs from the old layout may not exist in the new layout,
    // we need to find each window's POSITION in the old layout. The old layout
    // is the one whose zone IDs match the window's zone assignments.
    // We look up each zone ID in ALL loaded layouts to find the position.

    // Build a global zoneId → (layoutId, position) map from all layouts
    QHash<QString, int> globalZoneIdToPosition;
    for (PhosphorZones::Layout* layout : m_layoutManager->layouts()) {
        QHash<QString, int> layoutMap = PhosphorZones::LayoutUtils::buildZonePositionMap(layout);
        for (auto it = layoutMap.constBegin(); it != layoutMap.constEnd(); ++it) {
            globalZoneIdToPosition[it.key()] = it.value();
        }
    }

    const QHash<QString, QStringList>& snapZones = m_snapState->zoneAssignments();
    const QHash<QString, QString>& snapScreens = m_snapState->screenAssignments();
    const QHash<QString, int>& snapDesktops = m_snapState->desktopAssignments();

    for (auto it = snapZones.constBegin(); it != snapZones.constEnd(); ++it) {
        const QString& windowId = it.key();
        const QStringList& zoneIds = it.value();
        if (zoneIds.isEmpty() || isWindowFloating(windowId))
            continue;

        const QString screenId = snapScreens.value(windowId);
        if (screenId.isEmpty())
            continue;

        // Skip windows on excluded screens (e.g. autotile screens)
        if (excludeScreens.contains(screenId))
            continue;

        // When include-filter is set, only process windows on the specified screens
        if (!includeScreens.isEmpty() && !includeScreens.contains(screenId))
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
        entry.virtualDesktop = snapDesktops.value(windowId, 0);

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

QStringList WindowTrackingService::buildZoneOrderedWindowList(const QString& screenId) const
{
    if (!m_layoutManager) {
        return {};
    }

    // Get the current layout to resolve zone numbers
    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    if (!layout || layout->zoneCount() == 0) {
        return {};
    }

    // Build zone UUID → zone number lookup (both formats for robustness)
    const QVector<PhosphorZones::Zone*> zones = layout->zones();
    QHash<QString, int> zoneNumberMap;
    for (PhosphorZones::Zone* zone : zones) {
        zoneNumberMap[zone->id().toString()] = zone->zoneNumber();
    }

    // Collect (zoneNumber, insertionIndex, windowId) for windows on this screen.
    // Screen assignments may store connector names or EDID-based screen IDs
    // depending on the code path. Use screensMatch() for format-agnostic comparison.
    const QHash<QString, QString>& snapScreens = m_snapState->screenAssignments();
    const QHash<QString, QStringList>& snapZones = m_snapState->zoneAssignments();

    int insertionIdx = 0;
    QVector<std::tuple<int, int, QString>> windowsByZone; // (zoneNum, insertionIdx, windowId)
    for (auto it = snapScreens.constBegin(); it != snapScreens.constEnd(); ++it) {
        if (!Phosphor::Screens::ScreenIdentity::screensMatch(it.value(), screenId)) {
            continue;
        }
        const QString& windowId = it.key();
        // Skip floating windows — they should not participate in zone-ordered
        // transitions (the user's manual-mode float choice should be preserved).
        if (isWindowFloating(windowId)) {
            continue;
        }
        const QStringList& zoneIds = snapZones.value(windowId);
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

// calculateSnapAllWindows moved to SnapEngine (src/snap/snapengine/resnap_calc.cpp).

// ═══════════════════════════════════════════════════════════════════════════════
// Resolution Change Handling
// ═══════════════════════════════════════════════════════════════════════════════

QHash<QString, QRect> WindowTrackingService::updatedWindowGeometries() const
{
    QHash<QString, QRect> result;

    if (!m_settings || !m_settings->keepWindowsInZonesOnResolutionChange()) {
        return result;
    }

    const QHash<QString, QStringList>& uwgZones = m_snapState->zoneAssignments();
    const QHash<QString, QString>& uwgScreens = m_snapState->screenAssignments();

    for (auto it = uwgZones.constBegin(); it != uwgZones.constEnd(); ++it) {
        QString windowId = it.key();
        const QStringList& zoneIds = it.value();
        if (zoneIds.isEmpty()) {
            continue;
        }
        QString screenId = uwgScreens.value(windowId);

        QRect geo = resolveZoneGeometry(zoneIds, screenId);
        if (geo.isValid()) {
            result[windowId] = geo;
        }
    }

    return result;
}

QHash<QString, WindowTrackingService::PendingRestoreTarget> WindowTrackingService::pendingRestoreGeometries() const
{
    QHash<QString, PendingRestoreTarget> result;
    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    QString currentActivity = m_layoutManager ? m_layoutManager->currentActivity() : QString();

    for (auto it = m_pendingRestoreQueues.constBegin(); it != m_pendingRestoreQueues.constEnd(); ++it) {
        if (it->isEmpty()) {
            continue;
        }

        // Only the first entry per appId (FIFO consumption order)
        const PendingRestore& entry = it->first();
        if (entry.zoneIds.isEmpty()) {
            continue;
        }

        QString screenId = resolveEffectiveScreenId(entry.screenId);

        // Skip entries whose saved screen is currently in autotile mode. The
        // effect cache is a snap-mode fast path — autotile owns its screens
        // and will place the window itself when it opens. Populating the
        // cache for autotile-owned screens would let the effect teleport the
        // window to a stale snap rect before autotile's tile request lands.
        if (m_layoutManager
            && m_layoutManager->modeForScreen(screenId, entry.virtualDesktop, currentActivity)
                != PhosphorZones::AssignmentEntry::Mode::Snapping) {
            continue;
        }

        // Validate layout context — same checks as calculateRestoreFromSession.
        // Without this, the cache could contain geometry for a zone that no longer
        // exists in the current layout, causing a wrong-position teleport that the
        // async resolveWindowRestore then rejects (orphaned window at zone position
        // with no zone assignment).
        if (!entry.layoutId.isEmpty() && m_layoutManager) {
            PhosphorZones::Layout* currentLayout =
                m_layoutManager->layoutForScreen(screenId, entry.virtualDesktop, currentActivity);
            if (!currentLayout) {
                currentLayout = m_layoutManager->activeLayout();
            }
            if (!currentLayout) {
                continue;
            }
            QUuid savedUuid = QUuid::fromString(entry.layoutId);
            if (!savedUuid.isNull() && currentLayout->id() != savedUuid) {
                continue;
            }
        }

        // Validate desktop context
        if (entry.virtualDesktop > 0 && currentDesktop > 0 && entry.virtualDesktop != currentDesktop) {
            continue;
        }

        QRect geo = resolveZoneGeometry(entry.zoneIds, screenId);
        if (geo.isValid()) {
            result.insert(it.key(), PendingRestoreTarget{geo, screenId});
        }
    }

    return result;
}

} // namespace PlasmaZones
