// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Window lifecycle, layout change handling, state management, and private helpers.
// Part of WindowTrackingService — split from windowtrackingservice.cpp for SRP.

#include "../windowtrackingservice.h"
#include "../constants.h"
#include "../layout.h"
#include "../zone.h"
#include "../layoutmanager.h"
#include "../screenmanager.h"
#include "../virtualdesktopmanager.h"
#include "../utils.h"
#include "../virtualscreen.h"
#include "../logging.h"
#include <QScreen>
#include <QUuid>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual Screen Migration
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::migrateScreenAssignmentsToVirtual(const QString& physicalScreenId,
                                                              const QStringList& virtualScreenIds, ScreenManager* mgr)
{
    if (virtualScreenIds.isEmpty() || !mgr) {
        return;
    }

    // Clear resnap buffer — stale physical screen IDs would cause mismatches
    m_resnapBuffer.clear();

    // Helper: determine which virtual screen a zone center falls within.
    // Returns the first virtual screen as default if the zone can't be resolved.
    auto resolveVirtualScreen = [&](const QStringList& zoneIds) -> QString {
        if (zoneIds.isEmpty() || !m_layoutManager) {
            return virtualScreenIds.first();
        }

        const QString& primaryZoneId = zoneIds.first();
        auto uuidOpt = Utils::parseUuid(primaryZoneId);
        if (!uuidOpt) {
            return virtualScreenIds.first();
        }

        // Try per-virtual-screen layouts first: if a VS has its own layout
        // assignment, the zone may belong to that layout rather than the
        // physical screen's layout.  Collect ALL matches — when multiple
        // virtual screens share the same layout the zone appears in all of
        // them, so we must fall through to center-point resolution.
        QStringList candidates;
        for (const QString& vsId : virtualScreenIds) {
            Layout* vsLayout = m_layoutManager->resolveLayoutForScreen(vsId);
            if (!vsLayout) {
                continue;
            }
            Zone* zone = vsLayout->zoneById(*uuidOpt);
            if (zone) {
                candidates.append(vsId);
            }
        }
        if (candidates.size() == 1) {
            return candidates.first();
        }
        // Multiple matches (shared layout) or no matches — fall through to center-point

        // Fall back to the physical screen's layout and center-point mapping
        Layout* layout = m_layoutManager->resolveLayoutForScreen(physicalScreenId);
        if (!layout) {
            return virtualScreenIds.first();
        }

        Zone* zone = layout->zoneById(*uuidOpt);
        if (!zone) {
            return virtualScreenIds.first();
        }

        // Get physical screen geometry to compute absolute zone position
        QScreen* physScreen = mgr->physicalQScreenFor(physicalScreenId);
        if (!physScreen) {
            return virtualScreenIds.first();
        }

        QRectF absGeo = zone->calculateAbsoluteGeometry(QRectF(physScreen->geometry()));
        QPoint center = absGeo.center().toPoint();
        QString vsId = mgr->virtualScreenAt(center, physicalScreenId);
        if (!vsId.isEmpty()) {
            return vsId;
        }

        return virtualScreenIds.first();
    };

    int migrated = 0;
    bool anyStateMigrated = false;
    // Match both the physical screen ID and any existing virtual screen IDs on it,
    // so re-configuration (VS config changed) re-migrates windows from old virtual IDs to new ones.
    const QString prefix = physicalScreenId + VirtualScreenId::separator();
    for (auto it = m_windowScreenAssignments.begin(); it != m_windowScreenAssignments.end(); ++it) {
        if (it.value() != physicalScreenId && !it.value().startsWith(prefix)) {
            continue;
        }

        QStringList zoneIds = m_windowZoneAssignments.value(it.key());
        QString targetVs = resolveVirtualScreen(zoneIds);
        it.value() = targetVs;
        migrated++;
    }

    // Also migrate pre-float screen assignments
    for (auto it = m_preFloatScreenAssignments.begin(); it != m_preFloatScreenAssignments.end(); ++it) {
        if (it.value() != physicalScreenId && !it.value().startsWith(prefix)) {
            continue;
        }

        // Pre-float entries may have zone info too; try to resolve
        QStringList zoneIds = m_preFloatZoneAssignments.value(it.key());
        it.value() = resolveVirtualScreen(zoneIds);
        anyStateMigrated = true;
    }

    // Also migrate pending restore queues — these have screenId per entry
    for (auto queueIt = m_pendingRestoreQueues.begin(); queueIt != m_pendingRestoreQueues.end(); ++queueIt) {
        for (PendingRestore& entry : queueIt.value()) {
            if (entry.screenId != physicalScreenId && !entry.screenId.startsWith(prefix)) {
                continue;
            }
            entry.screenId = resolveVirtualScreen(entry.zoneIds);
            anyStateMigrated = true;
        }
    }

    // Migrate m_lastUsedScreenId: if it matches the physical screen or an old virtual
    // screen on it, determine which VS the last-used zone belongs to.
    if ((m_lastUsedScreenId == physicalScreenId || m_lastUsedScreenId.startsWith(prefix))
        && !virtualScreenIds.isEmpty()) {
        // Determine which VS the last-used zone falls in
        QString targetVs = virtualScreenIds.first(); // default
        if (!m_lastUsedZoneId.isEmpty() && m_layoutManager) {
            for (const QString& vsId : virtualScreenIds) {
                Layout* vsLayout = m_layoutManager->resolveLayoutForScreen(vsId);
                if (vsLayout) {
                    auto uuidOpt = Utils::parseUuid(m_lastUsedZoneId);
                    if (uuidOpt && vsLayout->zoneById(*uuidOpt)) {
                        targetVs = vsId;
                        break;
                    }
                }
            }
        }
        m_lastUsedScreenId = targetVs;
        // Only clear the zone ID if it doesn't exist in the target virtual screen's layout.
        // If the zone is still valid in the new layout, preserve it for last-zone-snap.
        bool zoneStillValid = false;
        if (!m_lastUsedZoneId.isEmpty() && m_layoutManager) {
            Layout* targetLayout = m_layoutManager->resolveLayoutForScreen(targetVs);
            if (targetLayout) {
                auto uuidOpt = Utils::parseUuid(m_lastUsedZoneId);
                if (uuidOpt && targetLayout->zoneById(*uuidOpt)) {
                    zoneStillValid = true;
                }
            }
        }
        if (!zoneStillValid) {
            m_lastUsedZoneId.clear();
        }
    }

    // Migrate pre-tile geometry connectorName fields from physical (or old virtual) to new virtual
    for (auto it = m_preTileGeometries.begin(); it != m_preTileGeometries.end(); ++it) {
        if (it->connectorName == physicalScreenId || it->connectorName.startsWith(prefix)) {
            // Use resolveVirtualScreen if the window has zone info, otherwise default to first
            QStringList zoneIds = m_windowZoneAssignments.value(it.key());
            it->connectorName = resolveVirtualScreen(zoneIds);
        }
    }

    if (migrated > 0 || anyStateMigrated) {
        qCInfo(lcCore) << "Migrated" << migrated << "window screen assignments from" << physicalScreenId
                       << "to virtual screens";
        scheduleSaveState();
    }
}

void WindowTrackingService::migrateScreenAssignmentsFromVirtual(const QString& physicalScreenId)
{
    // Clear resnap buffer — stale virtual screen IDs would cause mismatches
    m_resnapBuffer.clear();

    const QString prefix = physicalScreenId + VirtualScreenId::separator();
    int migrated = 0;
    bool anyStateMigrated = false;

    for (auto it = m_windowScreenAssignments.begin(); it != m_windowScreenAssignments.end(); ++it) {
        if (it.value().startsWith(prefix)) {
            it.value() = physicalScreenId;
            migrated++;
        }
    }

    // Also migrate pre-float screen assignments
    for (auto it = m_preFloatScreenAssignments.begin(); it != m_preFloatScreenAssignments.end(); ++it) {
        if (it.value().startsWith(prefix)) {
            it.value() = physicalScreenId;
            anyStateMigrated = true;
        }
    }

    // Also migrate pending restore queues
    for (auto queueIt = m_pendingRestoreQueues.begin(); queueIt != m_pendingRestoreQueues.end(); ++queueIt) {
        for (PendingRestore& entry : queueIt.value()) {
            if (entry.screenId.startsWith(prefix)) {
                entry.screenId = physicalScreenId;
                anyStateMigrated = true;
            }
        }
    }

    // B2: Migrate m_lastUsedScreenId if it references a virtual screen on this physical screen
    if (VirtualScreenId::isVirtual(m_lastUsedScreenId)
        && VirtualScreenId::extractPhysicalId(m_lastUsedScreenId) == physicalScreenId) {
        m_lastUsedScreenId = physicalScreenId;
        // Clear stale zone ID — virtual screen layout zones don't exist in physical screen layout
        m_lastUsedZoneId.clear();
    }

    // B3: Migrate pre-tile geometry connectorName fields
    for (auto it = m_preTileGeometries.begin(); it != m_preTileGeometries.end(); ++it) {
        if (VirtualScreenId::isVirtual(it->connectorName)
            && VirtualScreenId::extractPhysicalId(it->connectorName) == physicalScreenId) {
            it->connectorName = physicalScreenId;
        }
    }

    // Validate zone assignments against the physical screen's layout.
    // Virtual screen layouts may have zones that don't exist in the physical
    // screen's layout, so remove any assignments referencing invalid zone IDs.
    Layout* physLayout = m_layoutManager ? m_layoutManager->resolveLayoutForScreen(physicalScreenId) : nullptr;
    if (physLayout) {
        QStringList windowsToRemove;
        for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
            if (m_windowScreenAssignments.value(it.key()) != physicalScreenId) {
                continue;
            }
            bool allValid = true;
            for (const QString& zoneId : it.value()) {
                auto uuid = Utils::parseUuid(zoneId);
                if (!uuid || !physLayout->zoneById(*uuid)) {
                    allValid = false;
                    break;
                }
            }
            if (!allValid) {
                windowsToRemove.append(it.key());
            }
        }
        for (const QString& wId : windowsToRemove) {
            m_windowZoneAssignments.remove(wId);
        }
    }

    if (migrated > 0 || anyStateMigrated) {
        qCInfo(lcCore) << "Migrated" << migrated << "window screen assignments from virtual screens back to"
                       << physicalScreenId;
        scheduleSaveState();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::windowClosed(const QString& windowId)
{
    QString appId = Utils::extractAppId(windowId);

    // Persist the zone assignment to pending BEFORE removing from active tracking.
    // This ensures the window can be restored to its zone when reopened.
    // BUT: Don't persist if the window is floating - floating windows should stay floating
    // and not be auto-snapped when reopened.
    QStringList zoneIds = m_windowZoneAssignments.value(windowId);
    QString zoneId = zoneIds.isEmpty() ? QString() : zoneIds.first();
    // Check floating with full windowId first, fallback to appId
    bool isFloating = isWindowFloating(windowId);
    if (!zoneId.isEmpty() && !zoneId.startsWith(ZoneSelectorIdPrefix) && !isFloating) {
        if (!appId.isEmpty()) {
            PendingRestore entry;
            entry.zoneIds = zoneIds;

            QString screenId = m_windowScreenAssignments.value(windowId);
            entry.screenId = screenId;

            int desktop = m_windowDesktopAssignments.value(windowId, 0);
            if (desktop <= 0 && m_virtualDesktopManager) {
                desktop = m_virtualDesktopManager->currentDesktop();
            }
            entry.virtualDesktop = desktop;

            // Save the layout ID to ensure we only restore if the same layout is active
            // This prevents restoring windows to wrong zones when layouts have been changed
            // Use resolveLayoutForScreen() for proper multi-screen support
            Layout* contextLayout = m_layoutManager ? m_layoutManager->resolveLayoutForScreen(screenId) : nullptr;
            if (contextLayout) {
                entry.layoutId = contextLayout->id().toString();
            }

            // Save zone numbers for fallback when zone UUIDs get regenerated on layout edit
            QList<int> zoneNumbers;
            for (const QString& zId : zoneIds) {
                Zone* z = findZoneById(zId);
                if (z)
                    zoneNumbers.append(z->zoneNumber());
            }
            entry.zoneNumbers = zoneNumbers;

            m_pendingRestoreQueues[appId].append(entry);

            qCInfo(lcCore) << "Persisted zone" << zoneId << "for closed window" << appId << "screen:" << screenId
                           << "desktop:" << desktop
                           << "layout:" << (contextLayout ? contextLayout->id().toString() : QStringLiteral("none"))
                           << "zoneNumbers:" << zoneNumbers;
        }
    }

    // Now clean up active tracking state (but NOT pre-snap geometry -
    // that persists across close/reopen for proper session restore behavior)
    m_windowZoneAssignments.remove(windowId);
    m_windowScreenAssignments.remove(windowId);
    m_windowDesktopAssignments.remove(windowId);

    // Convert pre-tile geometry from full windowId to appId for persistence
    // so that when the window reopens (with a new internal ID), the geometry
    // can still be found via appId fallback. storePreTileGeometry writes both
    // keys, so we just clean up the stale full-windowId entry.
    if (m_preTileGeometries.contains(windowId) && appId != windowId) {
        m_preTileGeometries.remove(windowId);
    }
    // Clear floating state on close — floating is a runtime-only state that
    // should not carry over when the window is reopened. Without this, closing
    // a floated window and reopening it would inherit the float state (via appId
    // fallback), causing a spurious "floated" OSD and preventing auto-snap.
    m_floatingWindows.remove(windowId);
    if (appId != windowId) {
        m_floatingWindows.remove(appId);
    }
    // Also clear pre-float zone/screen assignments since float state is gone
    m_preFloatZoneAssignments.remove(windowId);
    m_preFloatScreenAssignments.remove(windowId);
    if (appId != windowId) {
        m_preFloatZoneAssignments.remove(appId);
        m_preFloatScreenAssignments.remove(appId);
    }
    // Remove autotile-floated tracking outright — do NOT migrate to appId.
    // This set is ephemeral (not persisted); migrating to appId would create
    // a shared key that matches ALL instances of the same app, causing
    // cross-contamination when isAutotileFloated() is called for other instances.
    m_autotileFloatedWindows.remove(windowId);
    m_savedSnapFloatingWindows.remove(windowId);
    m_windowStickyStates.remove(windowId);
    m_autoSnappedWindows.remove(windowId);
    m_effectReportedWindows.remove(windowId);

    scheduleSaveState();
}

void WindowTrackingService::onLayoutChanged()
{
    // Validate zone assignments against new layout
    Layout* newLayout = m_layoutManager->activeLayout();
    if (!newLayout) {
        qCInfo(lcCore) << "onLayoutChanged: no active layout, clearing buffer";
        m_resnapBuffer.clear();
        return;
    }

    // Collect valid zone IDs from new active layout (for quick checks)
    QSet<QString> activeLayoutZoneIds;
    for (Zone* zone : newLayout->zones()) {
        activeLayoutZoneIds.insert(zone->id().toString());
    }

    // Before removing stale assignments, capture (window, zonePosition) for resnap-to-new-layout.
    // When user presses the shortcut, we map zone N -> zone N (with cycling when layout has fewer zones).
    // Include BOTH m_windowZoneAssignments (tracked) AND m_pendingRestoreQueues (session-restored
    // windows that KWin placed in zones before we got windowSnapped - e.g. after login).
    //
    // LayoutManager ensures prevLayout is never null (captures current as previous on first set).
    // When prevLayout != newLayout: capture assignments to OLD layout (real switch).
    // When prevLayout == newLayout: capture assignments to CURRENT layout (startup re-apply).
    //
    // Only replace m_resnapBuffer when we capture at least one window. If user does A->B->C (snapped
    // on A, B has no windows), prev=B yields nothing - we keep the buffer from A->B so resnap on C works.
    Layout* prevLayout = m_layoutManager->previousLayout();
    if (!prevLayout) {
        qCInfo(lcCore) << "onLayoutChanged: no previous layout (first launch), skipping resnap buffer";
        return;
    }
    const bool layoutSwitched = (prevLayout != newLayout);
    qCDebug(lcCore) << "onLayoutChanged: newLayout=" << newLayout->name() << "prevLayout=" << prevLayout->name()
                    << "switched=" << layoutSwitched << "windowAssignments=" << m_windowZoneAssignments.size();
    {
        QVector<ResnapEntry> newBuffer;

        QHash<QString, int> globalZoneIdToPosition = buildZonePositionMap(prevLayout);
        int globalPrevZoneCount = prevLayout ? prevLayout->zones().size() : 0;

        // Cache per-screen position maps for screens with per-screen layouts
        // Key: layout pointer (avoids rebuilding for screens sharing the same layout)
        QHash<Layout*, QHash<QString, int>> perLayoutPositionMaps;

        // Dedup: full windowId for live assignments (supports multi-instance apps),
        // appId for pending entries (avoids double-counting live + pending for same window)
        QSet<QString> addedIds;

        auto addToBuffer = [&](const QString& windowIdOrStableId, const QStringList& zoneIdList,
                               const QString& screenId, int vd) {
            // Skip ALL floating windows. Floating persists across mode toggles —
            // floating windows should stay at their current position, not be resnapped.
            if (windowIdOrStableId.isEmpty() || isWindowFloating(windowIdOrStableId)) {
                return;
            }
            if (addedIds.contains(windowIdOrStableId)) {
                return;
            }

            // Resolve the position map for this window's screen.
            // If the screen has a per-screen layout that differs from the global
            // previous layout, use that layout's zone positions instead.
            const QHash<QString, int>* posMap = &globalZoneIdToPosition;
            int prevZoneCount = globalPrevZoneCount;
            if (!screenId.isEmpty() && m_layoutManager) {
                Layout* screenLayout = m_layoutManager->resolveLayoutForScreen(screenId);
                if (screenLayout && screenLayout != prevLayout) {
                    auto cacheIt = perLayoutPositionMaps.constFind(screenLayout);
                    if (cacheIt == perLayoutPositionMaps.constEnd()) {
                        cacheIt = perLayoutPositionMaps.insert(screenLayout, buildZonePositionMap(screenLayout));
                    }
                    posMap = &cacheIt.value();
                    prevZoneCount = screenLayout->zones().size();
                }
            }

            // Use primary zone for position mapping
            QString zoneId = zoneIdList.isEmpty() ? QString() : zoneIdList.first();
            int pos = posMap->value(zoneId, 0);
            if (pos <= 0) {
                // Handle zoneselector synthetic IDs: "zoneselector-{layoutId}-{index}"
                if (zoneId.startsWith(ZoneSelectorIdPrefix)) {
                    int lastDash = zoneId.lastIndexOf(QLatin1Char('-'));
                    if (lastDash > 0) {
                        bool ok = false;
                        int idx = zoneId.mid(lastDash + 1).toInt(&ok);
                        if (ok && idx >= 0 && idx < prevZoneCount) {
                            pos = idx + 1; // 1-based position
                        }
                    }
                }
            }
            if (pos <= 0) {
                return;
            }
            // Track by exact key (full windowId for live, appId for pending)
            addedIds.insert(windowIdOrStableId);
            // Also track appId so pending entries don't duplicate live ones
            QString appId = Utils::extractAppId(windowIdOrStableId);
            if (appId != windowIdOrStableId) {
                addedIds.insert(appId);
            }
            // Collect all zone positions for multi-zone resnap
            QList<int> allPositions;
            for (const QString& zid : zoneIdList) {
                int p = posMap->value(zid, 0);
                if (p > 0)
                    allPositions.append(p);
            }

            ResnapEntry entry;
            entry.windowId = windowIdOrStableId;
            entry.zonePosition = pos;
            entry.allZonePositions = allPositions;
            entry.screenId = screenId;
            entry.virtualDesktop = vd;
            newBuffer.append(entry);
        };

        const QUuid prevLayoutId = prevLayout->id();

        // Helper to check if ANY of a window's zones exist in the active layout.
        // Multi-zone windows are kept as long as at least one zone is valid.
        auto anyZoneInActiveLayout = [&](const QStringList& zoneIdList) {
            for (const QString& zid : zoneIdList) {
                if (activeLayoutZoneIds.contains(zid))
                    return true;
            }
            return false;
        };

        // Helper: is a window on a screen that uses the global active layout?
        // Windows on screens with per-screen assignments that differ from the
        // new active layout are unaffected by this layout change.
        auto isAffectedByGlobalChange = [&](const QString& windowScreen) -> bool {
            if (windowScreen.isEmpty())
                return true;
            Layout* effectiveLayout = m_layoutManager->resolveLayoutForScreen(windowScreen);
            return !effectiveLayout || effectiveLayout == newLayout;
        };

        if (layoutSwitched) {
            // User switched layouts: capture assignments to zones from the OLD layout (not in new)
            // 1. Live assignments (windows we've tracked via windowSnapped)
            for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
                // Skip windows on screens with per-screen layouts unaffected by this change
                QString windowScreen = m_windowScreenAssignments.value(it.key());
                if (!isAffectedByGlobalChange(windowScreen)) {
                    continue;
                }
                if (anyZoneInActiveLayout(it.value())) {
                    continue;
                }
                addToBuffer(it.key(), it.value(), windowScreen, m_windowDesktopAssignments.value(it.key(), 0));
            }

            // 2. Pending assignments (session-restored windows)
            for (auto it = m_pendingRestoreQueues.constBegin(); it != m_pendingRestoreQueues.constEnd(); ++it) {
                for (const PendingRestore& entry : it.value()) {
                    if (!isAffectedByGlobalChange(entry.screenId)) {
                        continue;
                    }
                    if (anyZoneInActiveLayout(entry.zoneIds)) {
                        continue;
                    }
                    if (!entry.layoutId.isEmpty()) {
                        auto savedUuid = Utils::parseUuid(entry.layoutId);
                        if (!savedUuid || *savedUuid != prevLayoutId) {
                            continue; // pending is for a different layout
                        }
                    }
                    addToBuffer(it.key(), entry.zoneIds, entry.screenId, entry.virtualDesktop);
                }
            }
        } else {
            // Same layout (startup): capture assignments that belong to the current layout.
            // This lets resnap re-apply zone geometries for restored/pending windows.
            // 1. Live assignments in current layout
            for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
                if (!anyZoneInActiveLayout(it.value())) {
                    continue;
                }
                addToBuffer(it.key(), it.value(), m_windowScreenAssignments.value(it.key()),
                            m_windowDesktopAssignments.value(it.key(), 0));
            }

            // 2. Pending assignments for current layout
            for (auto it = m_pendingRestoreQueues.constBegin(); it != m_pendingRestoreQueues.constEnd(); ++it) {
                for (const PendingRestore& entry : it.value()) {
                    if (!anyZoneInActiveLayout(entry.zoneIds)) {
                        continue;
                    }
                    if (!entry.layoutId.isEmpty()) {
                        auto savedUuid = Utils::parseUuid(entry.layoutId);
                        if (!savedUuid || *savedUuid != prevLayoutId) {
                            continue;
                        }
                    }
                    addToBuffer(it.key(), entry.zoneIds, entry.screenId, entry.virtualDesktop);
                }
            }
        }

        if (!newBuffer.isEmpty()) {
            m_resnapBuffer = std::move(newBuffer);
            qCInfo(lcCore) << "Resnap buffer:" << m_resnapBuffer.size() << "windows (zone position -> window)";
            for (const ResnapEntry& e : m_resnapBuffer) {
                qCDebug(lcCore) << "Zone" << e.zonePosition << "<-" << e.windowId;
            }
        }
    }

    // Remove stale assignments: check each window against its screen's effective layout
    // (not just the global active), so per-screen assignments aren't incorrectly purged.
    // Skip windows on autotile screens — their zone assignments must survive the
    // autotile period so resnapCurrentAssignments() can restore them when tiling is toggled off.
    // Skip windows on OTHER virtual desktops — their zone assignments belong to that
    // desktop's layout and must not be purged when the current desktop's layout changes.
    const int currentDesktop = m_layoutManager->currentVirtualDesktop();
    const QString currentActivity = m_layoutManager->currentActivity();

    // Cache autotile status per screen to avoid redundant lookups (O(screens) instead of O(windows))
    QHash<QString, bool> screenIsAutotile;

    QStringList toRemove;
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        const QStringList& zoneIdList = it.value();
        if (zoneIdList.isEmpty()) {
            toRemove.append(it.key());
            continue;
        }

        // Preserve zone assignments for windows on other desktops. Desktop 0
        // means "all desktops" (pinned window) — always process those.
        const int windowDesktop = m_windowDesktopAssignments.value(it.key(), 0);
        if (windowDesktop != 0 && windowDesktop != currentDesktop) {
            continue;
        }

        QString windowScreen = m_windowScreenAssignments.value(it.key());

        // If this screen's assignment is autotile, preserve zone assignments for resnap
        auto cached = screenIsAutotile.constFind(windowScreen);
        if (cached == screenIsAutotile.constEnd()) {
            QString assignmentId =
                m_layoutManager->assignmentIdForScreen(windowScreen, currentDesktop, currentActivity);
            cached = screenIsAutotile.insert(windowScreen, LayoutId::isAutotile(assignmentId));
        }
        if (*cached) {
            continue;
        }

        Layout* effectiveLayout = m_layoutManager->resolveLayoutForScreen(windowScreen);
        if (!effectiveLayout) {
            toRemove.append(it.key());
            continue;
        }
        // Check if ANY assigned zone exists in the effective layout (not just
        // the primary). Multi-zone windows are kept as long as at least one
        // zone is valid — matches calculateResnapFromCurrentAssignments which
        // handles multi-zone via multiZoneGeometry.
        bool zoneFound = false;
        for (Zone* z : effectiveLayout->zones()) {
            const QString zid = z->id().toString();
            for (const QString& assignedZone : zoneIdList) {
                if (zid == assignedZone) {
                    zoneFound = true;
                    break;
                }
            }
            if (zoneFound)
                break;
        }
        if (!zoneFound) {
            toRemove.append(it.key());
        }
    }

    for (const QString& windowId : toRemove) {
        unassignWindow(windowId);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// State Management (persistence handled by adaptor via KConfig)
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::scheduleSaveState()
{
    // Signal to adaptor that state changed and needs saving
    // Adaptor handles actual KConfig persistence
    Q_EMIT stateChanged();
}

void WindowTrackingService::setLastUsedZone(const QString& zoneId, const QString& screenId, const QString& zoneClass,
                                            int desktop)
{
    m_lastUsedZoneId = zoneId;
    m_lastUsedScreenId = screenId;
    m_lastUsedZoneClass = zoneClass;
    m_lastUsedDesktop = desktop;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private Helpers
// ═══════════════════════════════════════════════════════════════════════════════

bool WindowTrackingService::isGeometryOnScreen(const QRect& geometry) const
{
    // Check virtual screens first (covers both virtual and non-subdivided physical screens).
    // Use area-overlap semantics (not center-point containment) so windows on virtual
    // screen boundaries are handled consistently with the physical-screen fallback path.
    auto* mgr = ScreenManager::instance();
    if (mgr) {
        const QStringList ids = mgr->effectiveScreenIds();
        for (const QString& id : ids) {
            QRect screenGeo = mgr->screenGeometry(id);
            if (!screenGeo.isValid()) {
                continue;
            }
            const QRect intersection = geometry.intersected(screenGeo);
            if (intersection.width() >= MinVisibleWidth && intersection.height() >= MinVisibleHeight) {
                return true;
            }
        }
        return false;
    }

    // Fallback: physical screens only (no ScreenManager available)
    for (QScreen* screen : Utils::allScreens()) {
        QRect intersection = geometry.intersected(screen->geometry());
        if (intersection.width() >= MinVisibleWidth && intersection.height() >= MinVisibleHeight) {
            return true;
        }
    }
    return false;
}

QRect WindowTrackingService::adjustGeometryToScreen(const QRect& geometry) const
{
    // Try virtual/effective screens first via ScreenManager
    auto* mgr = ScreenManager::instance();
    if (mgr) {
        const QStringList ids = mgr->effectiveScreenIds();
        const QPoint center = geometry.center();
        QRect nearestGeo;
        int minDist = INT_MAX;

        for (const QString& id : ids) {
            QRect screenGeo = mgr->screenGeometry(id);
            if (!screenGeo.isValid()) {
                continue;
            }
            // Manhattan distance from center to screen center
            QPoint diff = center - screenGeo.center();
            int dist = qAbs(diff.x()) + qAbs(diff.y());
            if (dist < minDist) {
                minDist = dist;
                nearestGeo = screenGeo;
            }
        }

        if (nearestGeo.isValid()) {
            QRect adjusted = geometry;
            if (adjusted.right() > nearestGeo.right()) {
                adjusted.moveRight(nearestGeo.right());
            }
            if (adjusted.left() < nearestGeo.left()) {
                adjusted.moveLeft(nearestGeo.left());
            }
            if (adjusted.bottom() > nearestGeo.bottom()) {
                adjusted.moveBottom(nearestGeo.bottom());
            }
            if (adjusted.top() < nearestGeo.top()) {
                adjusted.moveTop(nearestGeo.top());
            }
            return adjusted;
        }
    }

    // Fallback: physical screens only
    QScreen* nearest = Utils::findNearestScreen(geometry.center());
    if (!nearest) {
        return geometry;
    }

    QRect screenGeo = nearest->geometry();
    QRect adjusted = geometry;

    // Clamp to screen bounds while preserving size where possible
    if (adjusted.right() > screenGeo.right()) {
        adjusted.moveRight(screenGeo.right());
    }
    if (adjusted.left() < screenGeo.left()) {
        adjusted.moveLeft(screenGeo.left());
    }
    if (adjusted.bottom() > screenGeo.bottom()) {
        adjusted.moveBottom(screenGeo.bottom());
    }
    if (adjusted.top() < screenGeo.top()) {
        adjusted.moveTop(screenGeo.top());
    }

    return adjusted;
}

QString WindowTrackingService::resolveEffectiveScreenId(const QString& screenId) const
{
    if (!VirtualScreenId::isVirtual(screenId)) {
        return screenId;
    }

    auto* smgr = ScreenManager::instance();
    if (!smgr) {
        return screenId;
    }

    const QStringList effectiveIds = smgr->effectiveScreenIds();
    if (effectiveIds.contains(screenId)) {
        return screenId;
    }

    // The stored virtual screen no longer exists. Try to find another virtual screen
    // on the same physical monitor, so the window stays in the virtual-screen domain
    // (screensMatch() returns false for physical-vs-virtual comparisons).
    QString physId = VirtualScreenId::extractPhysicalId(screenId);
    const QStringList vsIds = smgr->virtualScreenIdsFor(physId);
    if (!vsIds.isEmpty()) {
        qCInfo(lcCore) << "Virtual screen" << screenId << "no longer exists, falling back to" << vsIds.first()
                       << "on same physical monitor" << physId;
        return vsIds.first();
    }

    qCWarning(lcCore) << "Virtual screen" << screenId << "no longer exists, falling back to physical screen" << physId;
    return physId;
}

Zone* WindowTrackingService::findZoneById(const QString& zoneId) const
{
    auto uuidOpt = Utils::parseUuid(zoneId);
    if (!uuidOpt) {
        return nullptr;
    }

    // Search all layouts, not just the active one, to support per-screen layouts
    for (Layout* layout : m_layoutManager->layouts()) {
        Zone* zone = layout->zoneById(*uuidOpt);
        if (zone) {
            return zone;
        }
    }
    return nullptr;
}

} // namespace PlasmaZones
