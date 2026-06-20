// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Resnap, snap-all, and resolution change geometry calculations.
// Part of WindowTrackingService — split from windowtrackingservice.cpp for SRP.

#include <PhosphorPlacement/WindowTrackingService.h>
#include "placementutils.h"
#include <PhosphorZones/GeometryUtils.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorIdentity/WindowId.h>
#include "placementlogging.h"
#include <QScreen>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <tuple>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PhosphorPlacement {

void WindowTrackingService::populateResnapBufferForAllScreens(const QSet<QString>& excludeScreens,
                                                              const QSet<QString>& includeScreens, int desktopFilter)
{
    if (!m_snapState || !m_layoutManager)
        return;

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

    // Build a global zoneId → position map merged across all layouts.
    // Shared with `WindowTrackingService::onLayoutChanged` (lifecycle.cpp);
    // see `PhosphorZones::LayoutUtils::buildGlobalZonePositionMap` for
    // why the merge is unambiguous (zone UUIDs are unique across layouts).
    const QHash<QString, int> globalZoneIdToPosition =
        PhosphorZones::LayoutUtils::buildGlobalZonePositionMap(m_layoutManager->layouts());

    const QHash<QString, QStringList>& snapZones = m_snapState->zoneAssignments();
    const QHash<QString, QString>& snapScreens = m_snapState->screenAssignments();
    const QHash<QString, int>& snapDesktops = m_snapState->desktopAssignments();

    // Per-window candidate processing, shared by the live and durable passes below.
    const auto addCandidate = [&](const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                  int virtualDesktop) {
        if (zoneIds.isEmpty() || isWindowFloating(windowId))
            return;
        if (screenId.isEmpty())
            return;
        // Skip windows on excluded screens (e.g. autotile screens)
        if (excludeScreens.contains(screenId))
            return;
        // When include-filter is set, only process windows on the specified screens
        if (!includeScreens.isEmpty() && !includeScreens.contains(screenId))
            return;
        // Desktop filter: a per-desktop layout change should resnap only the
        // windows on that desktop. virtualDesktop==0 means sticky / unknown
        // (visible on every desktop) so include those regardless of the filter.
        // Under Plasma 6.7 per-output virtual desktops (#648) the "current desktop"
        // is per-screen, so when filtering (desktopFilter > 0) compare each window
        // against ITS screen's current desktop rather than the single global value
        // the caller passed (falling back to that value when no VDM is wired).
        const int screenDesktop =
            m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : desktopFilter;
        if (desktopFilter > 0 && virtualDesktop != 0 && virtualDesktop != screenDesktop)
            return;

        if (addedIds.contains(windowId))
            return;
        addedIds.insert(windowId);

        // Look up the zone position from the global map
        const QString& primaryZoneId = zoneIds.first();
        int position = globalZoneIdToPosition.value(primaryZoneId, 0);
        if (position <= 0)
            return;

        ResnapEntry entry;
        entry.windowId = windowId;
        entry.zonePosition = position;
        entry.screenId = screenId;
        entry.virtualDesktop = virtualDesktop;
        newBuffer.append(entry);
    };

    // 1. Live snap assignments — this session's snaps (retained while a window is
    // autotiled, which is why the non-restart autotile→snap swap finds them here).
    for (auto it = snapZones.constBegin(); it != snapZones.constEnd(); ++it) {
        addCandidate(it.key(), it.value(), snapScreens.value(it.key()), snapDesktops.value(it.key(), 0));
    }

    // 2. Restart-robustness: a window snapped in a PRIOR session and then autotiled
    // has its snap zones only in the durable WindowPlacement record — the live
    // m_snapState map above is cold after a daemon restart. Without this pass an
    // autotile→snapping swap right after a restart resnaps nothing (empty buffer →
    // no applyGeometriesBatch → the effect never marks the windows snapped, so the
    // per-mode snap border / title-bar appearance is never applied). Mirrors the
    // live-or-durable fallback in recordedSnapZones().
    for (const PhosphorEngine::WindowPlacement& rec : m_placementStore.records()) {
        if (addedIds.contains(rec.windowId))
            continue;
        const PhosphorEngine::EngineSlot snapSlot = rec.slotFor(PhosphorEngine::WindowPlacement::snapEngineId());
        if (snapSlot.state != PhosphorEngine::WindowPlacement::stateSnapped())
            continue;
        addCandidate(rec.windowId, snapSlot.zoneIds, rec.screenId, rec.virtualDesktop);
    }

    if (!newBuffer.isEmpty()) {
        m_resnapBuffer = std::move(newBuffer);
        qCInfo(lcPlacement) << "Resnap buffer (all screens):" << m_resnapBuffer.size() << "windows";
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
    const QHash<QString, int>& snapDesktops = m_snapState->desktopAssignments();

    // This list SEEDS the autotile state for (screenId, CURRENT virtual desktop).
    // Snap assignments are screen-keyed but desktop-agnostic, so the same screen
    // can hold windows snapped on a DIFFERENT desktop (e.g. screen S snaps on VD1
    // and autotiles on VD2 via per-desktop rules). Those off-desktop windows must
    // NOT be pulled into this desktop's autotile state — doing so eagerly inserts
    // and tiles a window that lives on another desktop, overwriting its snap
    // geometry there (switching to the autotile desktop would corrupt the snap
    // desktop's window positions). Scope to the current desktop; desktop==0
    // (sticky / unknown) stays desktop-agnostic and is kept. Mirrors the
    // desktopFilter guard in populateResnapBufferForAllScreens (addCandidate).
    const int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : 0;

    int insertionIdx = 0;
    QVector<std::tuple<int, int, QString>> windowsByZone; // (zoneNum, insertionIdx, windowId)
    for (auto it = snapScreens.constBegin(); it != snapScreens.constEnd(); ++it) {
        if (!PhosphorScreens::ScreenIdentity::screensMatch(it.value(), screenId)) {
            continue;
        }
        const QString& windowId = it.key();
        const int windowDesktop = snapDesktops.value(windowId, 0);
        if (currentDesktop > 0 && windowDesktop != 0 && windowDesktop != currentDesktop) {
            continue;
        }
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
            qCWarning(lcPlacement) << "buildZoneOrderedWindowList: zone UUID" << zoneIds.first() << "for window"
                                   << windowId << "not found in layout - skipping";
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

    qCDebug(lcPlacement) << "buildZoneOrderedWindowList for" << screenId << ":" << result;
    return result;
}

// calculateSnapAllWindows moved to SnapEngine (src/snap/snapengine/resnap_calc.cpp).

// ═══════════════════════════════════════════════════════════════════════════════
// Resolution Change Handling
// ═══════════════════════════════════════════════════════════════════════════════

QHash<QString, QRect> WindowTrackingService::updatedWindowGeometries() const
{
    QHash<QString, QRect> result;

    if (!m_config.keepWindowsInZonesOnResolutionChange) {
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

    // Source the effect's instant-restore cache from the unified placement store:
    // one snapped WindowPlacement per appId-keyed window, resolved to its zone
    // geometry. The async resolveWindowRestore re-validates and corrects, so this
    // is a best-effort anti-flash fast path (an invalid/stale zone resolves to an
    // empty rect and is skipped). When an appId has several snapped records (multi
    // instance), pick the lowest-sequence record (least-recently-recorded; sequence
    // is re-stamped on every record()) deterministically so a repeated lookup is
    // stable and matches the FIFO consumption order, rather than depending on
    // unordered hash iteration.
    QHash<QString, quint64> chosenSequence;
    for (const PhosphorEngine::WindowPlacement& p : m_placementStore.records()) {
        const PhosphorEngine::EngineSlot snapSlot = p.slotFor(PhosphorEngine::WindowPlacement::snapEngineId());
        if (snapSlot.state != PhosphorEngine::WindowPlacement::stateSnapped()) {
            continue;
        }
        const QStringList zoneIds = snapSlot.zoneIds;
        if (zoneIds.isEmpty() || p.appId.isEmpty()) {
            continue;
        }

        const QString screenId = resolveEffectiveScreenId(p.screenId);
        // Per-output virtual desktops (#648): validate the record against ITS
        // screen's current desktop, not the global current.
        const int currentDesktop =
            m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : 0;

        // Skip screens currently in autotile mode — autotile owns placement there
        // and would otherwise fight a stale snap teleport. Both context
        // dimensions come from the RECORD (mirrors the cross-engine claim
        // gates, which key desktop AND activity off the record so the
        // engines reach identical verdicts).
        if (m_layoutManager
            && m_layoutManager->modeForScreen(screenId, p.virtualDesktop, p.activity)
                != PhosphorZones::AssignmentEntry::Mode::Snapping) {
            continue;
        }

        // Validate desktop context.
        if (p.virtualDesktop > 0 && currentDesktop > 0 && p.virtualDesktop != currentDesktop) {
            continue;
        }

        // Keep only the lowest-sequence (least-recently-recorded) record per appId.
        const auto seqIt = chosenSequence.constFind(p.appId);
        if (seqIt != chosenSequence.constEnd() && seqIt.value() <= p.sequence) {
            continue;
        }

        const QRect geo = resolveZoneGeometry(zoneIds, screenId);
        if (geo.isValid()) {
            result.insert(p.appId, PendingRestoreTarget{geo, screenId});
            chosenSequence.insert(p.appId, p.sequence);
        }
    }

    return result;
}

} // namespace PhosphorPlacement
