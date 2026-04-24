// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Window lifecycle, layout change handling, state management, and private helpers.
// Part of WindowTrackingService — split from windowtrackingservice.cpp for SRP.

#include "../windowtrackingservice.h"
#include "../constants.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include "../../snap/SnapState.h"
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorScreens/Manager.h>
#include "../virtualdesktopmanager.h"
#include "../utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include "../logging.h"
#include <QScreen>
#include <QUuid>
#include <climits>

namespace PlasmaZones {

namespace {

static QRect clampToRect(const QRect& geometry, const QRect& bounds)
{
    QRect adjusted = geometry;
    if (adjusted.right() > bounds.right()) {
        adjusted.moveRight(bounds.right());
    }
    if (adjusted.left() < bounds.left()) {
        adjusted.moveLeft(bounds.left());
    }
    if (adjusted.bottom() > bounds.bottom()) {
        adjusted.moveBottom(bounds.bottom());
    }
    if (adjusted.top() < bounds.top()) {
        adjusted.moveTop(bounds.top());
    }
    return adjusted;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
// PhosphorZones::Zone–PhosphorZones::Layout Validation Helpers
// ═══════════════════════════════════════════════════════════════════════════════

/// Returns true if ANY of the given zone IDs exists in the layout.
static bool anyZoneExistsInLayout(const QStringList& zoneIds, PhosphorZones::Layout* layout)
{
    if (!layout)
        return false;
    for (const QString& zid : zoneIds) {
        auto uuid = Utils::parseUuid(zid);
        if (uuid && layout->zoneById(*uuid))
            return true;
    }
    return false;
}

/// Returns true if ALL of the given zone IDs exist in the layout.
static bool allZonesExistInLayout(const QStringList& zoneIds, PhosphorZones::Layout* layout)
{
    if (!layout)
        return false;
    for (const QString& zid : zoneIds) {
        auto uuid = Utils::parseUuid(zid);
        if (!uuid || !layout->zoneById(*uuid))
            return false;
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual Screen Migration
// ═══════════════════════════════════════════════════════════════════════════════

QString WindowTrackingService::findNearestVirtualScreen(const QStringList& vsIds, int oldIndex)
{
    if (vsIds.isEmpty()) {
        return {};
    }
    int bestIdx = 0;
    int bestDist = INT_MAX;
    for (int i = 0; i < vsIds.size(); ++i) {
        const int idx = PhosphorIdentity::VirtualScreenId::extractIndex(vsIds[i]);
        const int dist = qAbs(idx - oldIndex);
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
        }
    }
    return vsIds[bestIdx];
}

// Takes an explicit Phosphor::Screens::ScreenManager* parameter rather than using m_screenManager
// because this method is called during daemon startup (Daemon::start) before the
// singleton instance may be fully initialized, and the caller already holds a valid
// Phosphor::Screens::ScreenManager pointer from its own member (m_screenManager.get()).
void WindowTrackingService::migrateScreenAssignmentsToVirtual(const QString& physicalScreenId,
                                                              const QStringList& virtualScreenIds,
                                                              Phosphor::Screens::ScreenManager* mgr)
{
    if (virtualScreenIds.isEmpty() || !mgr) {
        return;
    }

    // Only clear resnap entries for this physical screen — preserve entries for
    // other screens so concurrent layout + VS config changes don't lose resnap data.
    m_resnapBuffer.erase(std::remove_if(m_resnapBuffer.begin(), m_resnapBuffer.end(),
                                        [&](const ResnapEntry& e) {
                                            return e.screenId == physicalScreenId
                                                || e.screenId.startsWith(
                                                    physicalScreenId + PhosphorIdentity::VirtualScreenId::Separator);
                                        }),
                         m_resnapBuffer.end());

    // Helper: determine which virtual screen a zone center falls within.
    // Returns the first virtual screen as default if the zone can't be resolved.
    // @param oldScreenId  The window's stored screen ID (physical or old virtual).
    //                     Used to select index-based fallback when re-migrating
    //                     between virtual screen configurations.
    auto resolveVirtualScreen = [&](const QStringList& zoneIds, const QString& oldScreenId) -> QString {
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
            PhosphorZones::Layout* vsLayout = m_layoutManager->resolveLayoutForScreen(vsId);
            if (!vsLayout) {
                continue;
            }
            PhosphorZones::Zone* zone = vsLayout->zoneById(*uuidOpt);
            if (zone) {
                candidates.append(vsId);
            }
        }
        if (candidates.size() == 1) {
            return candidates.first();
        }
        // Multiple matches (shared layout) or no matches — fall through to center-point

        // When re-migrating from an old virtual screen ID, zone relative coordinates
        // were defined relative to the old VS bounds, not the full physical screen.
        // Projecting them against the physical screen geometry gives wrong results
        // (e.g. a zone at (0,0,1,1) on the right-half VS maps to the physical center
        // instead of the right-half center). Use index-based proximity instead:
        // find the new VS with the nearest index to the old one.
        if (PhosphorIdentity::VirtualScreenId::isVirtual(oldScreenId)) {
            return findNearestVirtualScreen(virtualScreenIds,
                                            PhosphorIdentity::VirtualScreenId::extractIndex(oldScreenId));
        }

        // Fall back to the physical screen's layout and center-point mapping.
        // This path is only used when migrating from a physical screen ID
        // (first-time VS setup), where zone coords are relative to the physical
        // screen and center-point resolution is correct.
        PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(physicalScreenId);
        if (!layout) {
            return virtualScreenIds.first();
        }

        PhosphorZones::Zone* zone = layout->zoneById(*uuidOpt);
        if (!zone) {
            return virtualScreenIds.first();
        }

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
    const QString prefix = physicalScreenId + PhosphorIdentity::VirtualScreenId::Separator;

    // Take a mutable copy of the screen assignments from SnapState for migration.
    // After applying changes, push the whole map back.
    QHash<QString, QString> screenAssigns = m_snapState->screenAssignments();
    const QHash<QString, QStringList>& zoneAssigns = m_snapState->zoneAssignments();

    for (auto it = screenAssigns.begin(); it != screenAssigns.end(); ++it) {
        if (it.value() != physicalScreenId && !it.value().startsWith(prefix)) {
            continue;
        }

        // If the window already has a valid virtual screen ID that matches the
        // current config, skip migration — the saved assignment is correct.
        if (PhosphorIdentity::VirtualScreenId::isVirtual(it.value()) && virtualScreenIds.contains(it.value())) {
            continue;
        }

        QStringList zoneIds = zoneAssigns.value(it.key());
        QString targetVs = resolveVirtualScreen(zoneIds, it.value());
        it.value() = targetVs;
        migrated++;
    }

    if (migrated > 0) {
        m_snapState->setScreenAssignments(screenAssigns);
    }

    // Also migrate pre-float screen assignments (owned by SnapState).
    {
        QHash<QString, QString> preFloatScreens = m_snapState->preFloatScreenAssignments();
        const QHash<QString, QStringList>& preFloatZones = m_snapState->preFloatZoneAssignments();
        bool preFloatMigrated = false;
        for (auto it = preFloatScreens.begin(); it != preFloatScreens.end(); ++it) {
            if (it.value() != physicalScreenId && !it.value().startsWith(prefix)) {
                continue;
            }

            // If the stored screen is already a valid virtual screen ID in the current config, skip it.
            // Re-migrating would recompute via resolveVirtualScreen with stale zone coords.
            if (PhosphorIdentity::VirtualScreenId::isVirtual(it.value()) && virtualScreenIds.contains(it.value())) {
                continue;
            }

            // Pre-float entries may have zone info too; try to resolve
            QStringList pfZoneIds = preFloatZones.value(it.key());
            it.value() = resolveVirtualScreen(pfZoneIds, it.value());
            preFloatMigrated = true;
        }
        if (preFloatMigrated) {
            m_snapState->setPreFloatScreenAssignments(preFloatScreens);
            anyStateMigrated = true;
        }
    }

    // Also migrate pending restore queues — these have screenId per entry.
    // Same guard as active assignments: skip entries that already have a valid
    // virtual screen ID matching the current config. Re-migrating would run
    // resolveVirtualScreen with zone coords relative to the virtual screen
    // (not the physical screen), which can produce wrong results.
    for (auto queueIt = m_pendingRestoreQueues.begin(); queueIt != m_pendingRestoreQueues.end(); ++queueIt) {
        for (PendingRestore& entry : queueIt.value()) {
            if (entry.screenId != physicalScreenId && !entry.screenId.startsWith(prefix)) {
                continue;
            }
            if (PhosphorIdentity::VirtualScreenId::isVirtual(entry.screenId)
                && virtualScreenIds.contains(entry.screenId)) {
                continue;
            }
            entry.screenId = resolveVirtualScreen(entry.zoneIds, entry.screenId);
            anyStateMigrated = true;
        }
    }

    // Migrate lastUsedScreenId: if it matches the physical screen or an old virtual
    // screen on it, determine which VS the last-used zone belongs to.
    QString lastScreenId = m_snapState->lastUsedScreenId();
    if ((lastScreenId == physicalScreenId || lastScreenId.startsWith(prefix)) && !virtualScreenIds.isEmpty()) {
        // Determine which VS the last-used zone falls in
        QString targetVs = virtualScreenIds.first(); // default
        QString lastZoneId = m_snapState->lastUsedZoneId();
        if (!lastZoneId.isEmpty() && m_layoutManager) {
            for (const QString& vsId : virtualScreenIds) {
                PhosphorZones::Layout* vsLayout = m_layoutManager->resolveLayoutForScreen(vsId);
                if (vsLayout) {
                    auto uuidOpt = Utils::parseUuid(lastZoneId);
                    if (uuidOpt && vsLayout->zoneById(*uuidOpt)) {
                        targetVs = vsId;
                        break;
                    }
                }
            }
        }
        m_snapState->restoreLastUsedZone(m_snapState->lastUsedZoneId(), targetVs, m_snapState->lastUsedZoneClass(),
                                         m_snapState->lastUsedDesktop());
        anyStateMigrated = true;
        validateLastUsedZone(targetVs);
    }

    // Migrate pre-tile geometry screenId fields from physical (or old virtual) to new virtual.
    // PlacementEngineBase is the single store — mutate via engine pointer.
    if (m_snapEngine) {
        auto geos = m_snapEngine->unmanagedGeometries(); // take a copy
        bool geosChanged = false;
        for (auto it = geos.begin(); it != geos.end(); ++it) {
            if (it->screenId == physicalScreenId || it->screenId.startsWith(prefix)) {
                QStringList ptZoneIds = zoneAssigns.value(it.key());
                if (ptZoneIds.isEmpty()) {
                    continue; // No zone info — keep physical ID, don't guess VS
                }
                it->screenId = resolveVirtualScreen(ptZoneIds, it->screenId);
                geosChanged = true;
            }
        }
        if (geosChanged) {
            m_snapEngine->setUnmanagedGeometries(geos);
            anyStateMigrated = true;
        }
    }

    if (migrated > 0 || anyStateMigrated) {
        qCInfo(lcCore) << "Migrated" << migrated << "window screen assignments"
                       << "(plus auxiliary state)" << "from" << physicalScreenId << "to virtual screens";
        scheduleSaveState();
    }
}

void WindowTrackingService::migrateScreenAssignmentsFromVirtual(const QString& physicalScreenId)
{
    // Only clear resnap entries for this physical screen — preserve entries for
    // other screens so concurrent layout + VS config changes don't lose resnap data.
    const QString prefix = physicalScreenId + PhosphorIdentity::VirtualScreenId::Separator;
    m_resnapBuffer.erase(std::remove_if(m_resnapBuffer.begin(), m_resnapBuffer.end(),
                                        [&](const ResnapEntry& e) {
                                            return e.screenId == physicalScreenId || e.screenId.startsWith(prefix);
                                        }),
                         m_resnapBuffer.end());

    int migrated = 0;
    bool anyStateMigrated = false;

    // Take a mutable copy of the screen assignments for migration.
    QHash<QString, QString> screenAssigns = m_snapState->screenAssignments();
    for (auto it = screenAssigns.begin(); it != screenAssigns.end(); ++it) {
        if (it.value().startsWith(prefix)) {
            it.value() = physicalScreenId;
            migrated++;
        }
    }
    if (migrated > 0) {
        m_snapState->setScreenAssignments(screenAssigns);
    }

    // Also migrate pre-float screen assignments (owned by SnapState).
    {
        QHash<QString, QString> preFloatScreens = m_snapState->preFloatScreenAssignments();
        bool preFloatMigrated = false;
        for (auto it = preFloatScreens.begin(); it != preFloatScreens.end(); ++it) {
            if (it.value().startsWith(prefix)) {
                it.value() = physicalScreenId;
                preFloatMigrated = true;
            }
        }
        if (preFloatMigrated) {
            m_snapState->setPreFloatScreenAssignments(preFloatScreens);
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

    // B2: Migrate lastUsedScreenId if it references a virtual screen on this physical screen
    {
        QString lastScreenId = m_snapState->lastUsedScreenId();
        if (PhosphorIdentity::VirtualScreenId::isVirtual(lastScreenId)
            && PhosphorIdentity::VirtualScreenId::extractPhysicalId(lastScreenId) == physicalScreenId) {
            m_snapState->restoreLastUsedZone(m_snapState->lastUsedZoneId(), physicalScreenId,
                                             m_snapState->lastUsedZoneClass(), m_snapState->lastUsedDesktop());
            validateLastUsedZone(physicalScreenId);
        }
    }

    // B3: Migrate pre-tile geometry screenId fields via engine
    if (m_snapEngine) {
        auto geos = m_snapEngine->unmanagedGeometries();
        bool geosChanged = false;
        for (auto it = geos.begin(); it != geos.end(); ++it) {
            if (PhosphorIdentity::VirtualScreenId::isVirtual(it->screenId)
                && PhosphorIdentity::VirtualScreenId::extractPhysicalId(it->screenId) == physicalScreenId) {
                it->screenId = physicalScreenId;
                geosChanged = true;
            }
        }
        if (geosChanged) {
            m_snapEngine->setUnmanagedGeometries(geos);
        }
    }

    // Validate zone assignments against the physical screen's layout.
    // Virtual screen layouts may have zones that don't exist in the physical
    // screen's layout, so remove any assignments referencing invalid zone IDs.
    // Floating windows are preserved — their float state should survive VS removal
    // even if their zone assignments are invalid in the physical layout.
    PhosphorZones::Layout* physLayout =
        m_layoutManager ? m_layoutManager->resolveLayoutForScreen(physicalScreenId) : nullptr;
    if (physLayout) {
        QStringList windowsToRemove;
        const QHash<QString, QStringList>& zoneAssigns2 = m_snapState->zoneAssignments();
        const QHash<QString, QString>& screenAssigns2 = m_snapState->screenAssignments();
        for (auto it = zoneAssigns2.constBegin(); it != zoneAssigns2.constEnd(); ++it) {
            if (screenAssigns2.value(it.key()) != physicalScreenId) {
                continue;
            }
            // Preserve floating windows — clearing float state here would make
            // previously floating windows eligible for auto-snap again, which is
            // a user-visible behavior change.
            if (isWindowFloating(it.key())) {
                continue;
            }
            if (!allZonesExistInLayout(it.value(), physLayout)) {
                windowsToRemove.append(it.key());
            }
        }
        bool lastUsedCleared = false;
        for (const QString& wId : windowsToRemove) {
            auto unResult = m_snapState->unassignWindow(wId);
            lastUsedCleared |= unResult.lastUsedZoneCleared;
            if (m_snapEngine) {
                m_snapEngine->forgetWindow(wId);
            }
            m_snapState->clearPreFloatZone(wId);
            m_windowStickyStates.remove(wId);
            anyStateMigrated = true;
        }
        if (lastUsedCleared) {
            markDirty(DirtyLastUsedZone);
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
    // Query the registry-aware helper so a window that renamed mid-session
    // (Electron/CEF) lands in the restore queue under its CURRENT class,
    // not the first-seen one.
    QString appId = currentAppIdFor(windowId);

    // Persist the zone assignment to pending BEFORE removing from active tracking.
    // This ensures the window can be restored to its zone when reopened.
    // BUT: Don't persist if the window is floating - floating windows should stay floating
    // and not be auto-snapped when reopened.
    QStringList zoneIds = m_snapState->zonesForWindow(windowId);
    QString zoneId = zoneIds.isEmpty() ? QString() : zoneIds.first();
    // Check floating with full windowId first, fallback to appId
    bool isFloating = isWindowFloating(windowId);
    if (!zoneId.isEmpty() && !zoneId.startsWith(ZoneSelectorIdPrefix) && !isFloating) {
        if (!appId.isEmpty()) {
            PendingRestore entry;
            entry.zoneIds = zoneIds;

            QString screenId = m_snapState->screenForWindow(windowId);
            entry.screenId = screenId;

            // Use 0 (all desktops) as the fallback when the actual desktop is unknown.
            // 0 is the conservative default — it avoids restoring to the wrong desktop.
            int desktop = m_snapState->desktopForWindow(windowId);
            entry.virtualDesktop = desktop;

            // Save the layout ID to ensure we only restore if the same layout is active
            // This prevents restoring windows to wrong zones when layouts have been changed
            // Use resolveLayoutForScreen() for proper multi-screen support
            PhosphorZones::Layout* contextLayout =
                m_layoutManager ? m_layoutManager->resolveLayoutForScreen(screenId) : nullptr;
            if (contextLayout) {
                entry.layoutId = contextLayout->id().toString();
            }

            // Save zone numbers for fallback when zone UUIDs get regenerated on layout edit
            QList<int> zoneNumbers;
            for (const QString& zId : zoneIds) {
                PhosphorZones::Zone* z = findZoneById(zId);
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

    // Order matters: zoneIds/screenId/desktop are read above BEFORE this call
    // clears them in SnapState. Do not move reads below this line.
    m_snapState->windowClosed(windowId);

    // Convert pre-tile geometry from full windowId to appId for persistence
    // so that when the window reopens (with a new internal ID), the geometry
    // can still be found via appId fallback. The engine's storeUnmanagedGeometry
    // writes both keys, so we just clean up the stale full-windowId entry.
    if (m_snapEngine && m_snapEngine->hasUnmanagedGeometry(windowId) && appId != windowId) {
        m_snapEngine->storeUnmanagedGeometry(appId, m_snapEngine->unmanagedGeometry(windowId),
                                             m_snapEngine->unmanagedScreen(windowId), true);
        m_snapEngine->forgetWindow(windowId);
    }
    // Clear floating state on close — floating is a runtime-only state that
    // should not carry over when the window is reopened. Without this, closing
    // a floated window and reopening it would inherit the float state (via appId
    // fallback), causing a spurious "floated" OSD and preventing auto-snap.
    m_floatingWindows.remove(windowId);
    if (appId != windowId) {
        m_floatingWindows.remove(appId);
    }
    // Also clear pre-float zone/screen assignments since float state is gone.
    // SnapState is the authoritative store; clear both windowId and appId keys.
    m_snapState->clearPreFloatZone(windowId);
    if (appId != windowId) {
        m_snapState->clearPreFloatZone(appId);
    }
    // Remove autotile-floated tracking outright — do NOT migrate to appId.
    m_windowStickyStates.remove(windowId);

    scheduleSaveState();
}

void WindowTrackingService::onLayoutChanged()
{
    // Validate zone assignments against new layout.
    // NOTE: Do NOT early-return when the global activeLayout() is null. With virtual
    // screens, individual screens may have per-screen layouts via resolveLayoutForScreen().
    // The per-window loop below (~line 718) already resolves layouts per-screen, so a
    // null global layout does not mean "no layouts anywhere".
    PhosphorZones::Layout* newLayout = m_layoutManager->activeLayout();

    // Before removing stale assignments, capture (window, zonePosition) for resnap-to-new-layout.
    // When user presses the shortcut, we map zone N -> zone N (with cycling when layout has fewer zones).
    // Include BOTH m_windowZoneAssignments (tracked) AND m_pendingRestoreQueues (session-restored
    // windows that KWin placed in zones before we got windowSnapped - e.g. after login).
    //
    // PhosphorZones::LayoutRegistry ensures prevLayout is never null (captures current as previous on first set).
    // When prevLayout != newLayout: capture assignments to OLD layout (real switch).
    // When prevLayout == newLayout: capture assignments to CURRENT layout (startup re-apply).
    //
    // Only replace m_resnapBuffer when we capture at least one window. If user does A->B->C (snapped
    // on A, B has no windows), prev=B yields nothing - we keep the buffer from A->B so resnap on C works.
    PhosphorZones::Layout* prevLayout = m_layoutManager->previousLayout();
    if (!prevLayout) {
        qCInfo(lcCore) << "onLayoutChanged: no previous layout (first launch), skipping resnap buffer";
        return;
    }
    const bool layoutSwitched = (prevLayout != newLayout);
    qCDebug(lcCore) << "onLayoutChanged: newLayout=" << (newLayout ? newLayout->name() : QStringLiteral("null"))
                    << "prevLayout=" << prevLayout->name() << "switched=" << layoutSwitched
                    << "windowAssignments=" << m_snapState->zoneAssignments().size();
    {
        QVector<ResnapEntry> newBuffer;

        QHash<QString, int> globalZoneIdToPosition = PhosphorZones::LayoutUtils::buildZonePositionMap(prevLayout);
        int globalPrevZoneCount = prevLayout->zones().size();

        // Cache per-screen position maps for screens with per-screen layouts
        // Key: layout pointer (avoids rebuilding for screens sharing the same layout)
        QHash<PhosphorZones::Layout*, QHash<QString, int>> perLayoutPositionMaps;

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
                PhosphorZones::Layout* screenLayout = m_layoutManager->resolveLayoutForScreen(screenId);
                if (screenLayout && screenLayout != prevLayout) {
                    auto cacheIt = perLayoutPositionMaps.constFind(screenLayout);
                    if (cacheIt == perLayoutPositionMaps.constEnd()) {
                        cacheIt = perLayoutPositionMaps.insert(
                            screenLayout, PhosphorZones::LayoutUtils::buildZonePositionMap(screenLayout));
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
            // Also track appId so pending entries don't duplicate live ones.
            // Registry-aware so a renamed window dedups against its CURRENT class.
            QString appId = currentAppIdFor(windowIdOrStableId);
            if (!appId.isEmpty() && appId != windowIdOrStableId) {
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

        // Resolve the effective new layout for a given screen. Per-screen
        // assignments take precedence; windows with no screen (or screens
        // without an explicit assignment) fall back to the active layout.
        auto resolveNewLayoutForScreen = [&](const QString& screenId) -> PhosphorZones::Layout* {
            if (!screenId.isEmpty()) {
                PhosphorZones::Layout* perScreen = m_layoutManager->resolveLayoutForScreen(screenId);
                if (perScreen)
                    return perScreen;
            }
            return newLayout;
        };

        const QHash<QString, QStringList>& snapZones = m_snapState->zoneAssignments();
        const QHash<QString, QString>& snapScreens = m_snapState->screenAssignments();
        const QHash<QString, int>& snapDesktops = m_snapState->desktopAssignments();

        if (layoutSwitched) {
            // User switched layouts: capture assignments to zones from the OLD layout (not in new)
            // 1. Live assignments (windows we've tracked via windowSnapped)
            for (auto it = snapZones.constBegin(); it != snapZones.constEnd(); ++it) {
                QString windowScreen = snapScreens.value(it.key());
                PhosphorZones::Layout* effectiveLayout = resolveNewLayoutForScreen(windowScreen);
                if (anyZoneExistsInLayout(it.value(), effectiveLayout)) {
                    continue;
                }
                addToBuffer(it.key(), it.value(), windowScreen, snapDesktops.value(it.key(), 0));
            }

            // 2. Pending assignments (session-restored windows)
            for (auto it = m_pendingRestoreQueues.constBegin(); it != m_pendingRestoreQueues.constEnd(); ++it) {
                for (const PendingRestore& entry : it.value()) {
                    PhosphorZones::Layout* effectiveLayout = resolveNewLayoutForScreen(entry.screenId);
                    if (anyZoneExistsInLayout(entry.zoneIds, effectiveLayout)) {
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
            // Same layout (startup): capture assignments that belong to their screen's
            // effective layout. This lets resnap re-apply zone geometries for both
            // global and per-screen layout windows.
            // 1. Live assignments — check each window against its own screen's layout
            for (auto it = snapZones.constBegin(); it != snapZones.constEnd(); ++it) {
                const QString windowScreen = snapScreens.value(it.key());
                PhosphorZones::Layout* effectiveLayout = m_layoutManager->resolveLayoutForScreen(windowScreen);
                if (!anyZoneExistsInLayout(it.value(), effectiveLayout)) {
                    continue;
                }
                addToBuffer(it.key(), it.value(), windowScreen, snapDesktops.value(it.key(), 0));
            }

            // 2. Pending assignments — check against each entry's screen's layout
            for (auto it = m_pendingRestoreQueues.constBegin(); it != m_pendingRestoreQueues.constEnd(); ++it) {
                for (const PendingRestore& entry : it.value()) {
                    PhosphorZones::Layout* effectiveLayout = m_layoutManager->resolveLayoutForScreen(entry.screenId);
                    if (!anyZoneExistsInLayout(entry.zoneIds, effectiveLayout)) {
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

    const QHash<QString, QStringList>& lcZones = m_snapState->zoneAssignments();
    const QHash<QString, QString>& lcScreens = m_snapState->screenAssignments();
    const QHash<QString, int>& lcDesktops = m_snapState->desktopAssignments();

    QStringList toRemove;
    for (auto it = lcZones.constBegin(); it != lcZones.constEnd(); ++it) {
        const QStringList& zoneIdList = it.value();
        if (zoneIdList.isEmpty()) {
            toRemove.append(it.key());
            continue;
        }

        // Preserve zone assignments for windows on other desktops. Desktop 0
        // means "all desktops" (pinned window) — always process those.
        const int windowDesktop = lcDesktops.value(it.key(), 0);
        if (windowDesktop != 0 && windowDesktop != currentDesktop) {
            continue;
        }

        QString windowScreen = lcScreens.value(it.key());

        // If this screen's assignment is autotile, preserve zone assignments for resnap
        auto cached = screenIsAutotile.constFind(windowScreen);
        if (cached == screenIsAutotile.constEnd()) {
            QString assignmentId =
                m_layoutManager->assignmentIdForScreen(windowScreen, currentDesktop, currentActivity);
            cached = screenIsAutotile.insert(windowScreen, PhosphorLayout::LayoutId::isAutotile(assignmentId));
        }
        if (*cached) {
            continue;
        }

        PhosphorZones::Layout* effectiveLayout = m_layoutManager->resolveLayoutForScreen(windowScreen);
        if (!effectiveLayout) {
            toRemove.append(it.key());
            continue;
        }
        // Multi-zone windows are kept as long as at least one zone is valid —
        // matches calculateResnapFromCurrentAssignments which handles multi-zone
        // via multiZoneGeometry.
        if (!anyZoneExistsInLayout(zoneIdList, effectiveLayout)) {
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

void WindowTrackingService::scheduleSaveState(DirtyMask fields)
{
    // Mark the supplied fields dirty and wake the adaptor's save timer.
    // Default DirtyAll preserves pre-Phase-3 semantics for call sites that
    // haven't been updated to declare which fields they mutate.
    markDirty(fields);
}

void WindowTrackingService::markDirty(DirtyMask fields)
{
    // OR into the persistent mask so the next saveState() knows which
    // JSON maps it needs to rewrite. Always emit stateChanged so the
    // adaptor's debounced save timer is kicked — even if the caller
    // passed DirtyNone (e.g. ephemeral state that wants to wake the timer
    // for indirect reasons), the adaptor does the right thing when the
    // eventual saveState() sees an empty mask.
    m_dirtyMask |= fields;
    Q_EMIT stateChanged();
}

WindowTrackingService::DirtyMask WindowTrackingService::takeDirty()
{
    const DirtyMask current = m_dirtyMask;
    m_dirtyMask = DirtyNone;
    return current;
}

void WindowTrackingService::clearDirty()
{
    m_dirtyMask = DirtyNone;
}

void WindowTrackingService::setLastUsedZone(const QString& zoneId, const QString& screenId, const QString& zoneClass,
                                            int desktop)
{
    if (m_snapState) {
        m_snapState->restoreLastUsedZone(zoneId, screenId, zoneClass, desktop);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private Helpers
// ═══════════════════════════════════════════════════════════════════════════════

bool WindowTrackingService::isGeometryOnScreen(const QRect& geometry) const
{
    // Check virtual screens first (covers both virtual and non-subdivided physical screens).
    // Use area-overlap semantics (not center-point containment) so windows on virtual
    // screen boundaries are handled consistently with the physical-screen fallback path.
    auto* mgr = m_screenManager;
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

    // Fallback: physical screens only (no Phosphor::Screens::ScreenManager available)
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
    // Try virtual/effective screens first via Phosphor::Screens::ScreenManager
    auto* mgr = m_screenManager;
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
            return clampToRect(geometry, nearestGeo);
        }
    }

    // Fallback: physical screens only
    QScreen* nearest = Utils::findNearestScreen(geometry.center());
    if (!nearest) {
        return geometry;
    }

    return clampToRect(geometry, nearest->geometry());
}

void WindowTrackingService::validateLastUsedZone(const QString& targetScreen)
{
    QString lastZoneId = m_snapState->lastUsedZoneId();
    if (lastZoneId.isEmpty() || !m_layoutManager) {
        return;
    }
    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(targetScreen);
    if (layout) {
        auto uuidOpt = Utils::parseUuid(lastZoneId);
        if (uuidOpt && layout->zoneById(*uuidOpt)) {
            return;
        }
    }
    m_snapState->restoreLastUsedZone({}, {}, {}, 0);
}

QString WindowTrackingService::resolveEffectiveScreenId(const QString& screenId) const
{
    if (!PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        return screenId;
    }

    auto* smgr = m_screenManager;
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
    QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
    const QStringList vsIds = smgr->virtualScreenIdsFor(physId);
    if (!vsIds.isEmpty()) {
        // Find the virtual screen with the nearest index to the old one,
        // so windows migrate to the geometrically closest region rather
        // than always landing on the first virtual screen.
        QString nearest = findNearestVirtualScreen(vsIds, PhosphorIdentity::VirtualScreenId::extractIndex(screenId));
        qCInfo(lcCore) << "Virtual screen" << screenId << "no longer exists, falling back to" << nearest
                       << "on same physical monitor" << physId;
        return nearest;
    }

    qCWarning(lcCore) << "Virtual screen" << screenId << "no longer exists, falling back to physical screen" << physId;
    return physId;
}

PhosphorZones::Zone* WindowTrackingService::findZoneById(const QString& zoneId) const
{
    auto uuidOpt = Utils::parseUuid(zoneId);
    if (!uuidOpt) {
        return nullptr;
    }

    return findZoneInAllLayouts(*uuidOpt).zone;
}

WindowTrackingService::ZoneLookupResult WindowTrackingService::findZoneInAllLayouts(const QUuid& zoneUuid) const
{
    // Search all layouts, not just the active one, to support per-screen layouts
    for (PhosphorZones::Layout* layout : m_layoutManager->layouts()) {
        PhosphorZones::Zone* zone = layout->zoneById(zoneUuid);
        if (zone) {
            return {zone, layout};
        }
    }
    return {};
}

} // namespace PlasmaZones
