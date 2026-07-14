// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Window lifecycle, layout change handling, state management, and private helpers.
// Part of WindowTrackingService — split from windowtrackingservice.cpp for SRP.

#include <PhosphorPlacement/WindowTrackingService.h>
#include "placementutils.h"

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/VirtualScreen.h>
#include "placementlogging.h"
#include <QScreen>
#include <QUuid>
#include <climits>

namespace PhosphorPlacement {

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
        auto uuid = parseUuid(zid);
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
        auto uuid = parseUuid(zid);
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

// Takes an explicit PhosphorScreens::ScreenManager* parameter rather than using m_screenManager
// because this method is called during daemon startup (Daemon::start) before the
// singleton instance may be fully initialized, and the caller already holds a valid
// PhosphorScreens::ScreenManager pointer from its own member (m_screenManager.get()).
void WindowTrackingService::migrateScreenAssignmentsToVirtual(const QString& physicalScreenId,
                                                              const QStringList& virtualScreenIds,
                                                              PhosphorScreens::ScreenManager* mgr)
{
    if (virtualScreenIds.isEmpty() || !mgr) {
        return;
    }

    // Only clear resnap entries for this physical screen — preserve entries for
    // other screens so concurrent layout + VS config changes don't lose resnap
    // data. The virtual-sub-screen prefix is hoisted out of the lambda,
    // matching the FromVirtual sibling, and reused for the store rewrites
    // below (it matches old virtual IDs so a VS re-configuration re-migrates).
    const QString prefix = physicalScreenId + PhosphorIdentity::VirtualScreenId::Separator;
    m_resnapBuffer.erase(std::remove_if(m_resnapBuffer.begin(), m_resnapBuffer.end(),
                                        [&](const ResnapEntry& e) {
                                            return e.screenId == physicalScreenId || e.screenId.startsWith(prefix);
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
        auto uuidOpt = parseUuid(primaryZoneId);
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

        const PhosphorScreens::PhysicalScreen physScreen = mgr->physicalScreenFor(physicalScreenId);
        if (!physScreen.isValid()) {
            return virtualScreenIds.first();
        }

        QRectF absGeo = zone->calculateAbsoluteGeometry(QRectF(physScreen.geometry));
        QPoint center = absGeo.center().toPoint();
        QString vsId = mgr->virtualScreenAt(center, physicalScreenId);
        if (!vsId.isEmpty()) {
            return vsId;
        }

        return virtualScreenIds.first();
    };

    int migrated = 0;
    bool anyStateMigrated = false;
    // Rewrite each per-screen store's OWN live-screen map in place. A window's
    // screen VALUE moves from the physical id (or an old virtual id) to the new
    // virtual sub-screen its zone falls in; the store the window lives in is
    // unchanged (per-window lookups key off the screen value + the reverse map, so
    // a now-stale store key is benign and self-heals on the next genuine
    // cross-monitor migration). Pushing the aggregated union onto the global holder
    // instead would strand duplicate stale entries in every other store. The zone
    // lookup reads the SAME store's zone map — a window's zones live alongside its
    // screen entry, so no cross-store union is needed.
    for (PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        QHash<QString, QString> assigns = state->screenAssignments();
        const QHash<QString, QStringList>& stateZones = state->zoneAssignments();
        bool storeChanged = false;
        for (auto it = assigns.begin(); it != assigns.end(); ++it) {
            if (it.value() != physicalScreenId && !it.value().startsWith(prefix)) {
                continue;
            }
            // If the window already has a valid virtual screen ID that matches the
            // current config, skip migration — the saved assignment is correct.
            if (PhosphorIdentity::VirtualScreenId::isVirtual(it.value()) && virtualScreenIds.contains(it.value())) {
                continue;
            }
            it.value() = resolveVirtualScreen(stateZones.value(it.key()), it.value());
            storeChanged = true;
            migrated++;
        }
        if (storeChanged) {
            state->setScreenAssignments(assigns);
        }
    }

    // Also migrate pre-float screen assignments (owned by SnapState). Rewritten
    // per store, in place, like the live-screen map above — the former
    // union-then-redistribute round-trip re-homed appId-alias entries with no
    // live window onto the global holder as a side effect; the per-state rewrite
    // leaves every entry in the store it lives in. The pre-float zone lookup
    // reads the same store: the zone and screen halves of a pre-float entry are
    // written together into the window's owning store.
    for (PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        QHash<QString, QString> preFloatScreens = state->preFloatScreenAssignments();
        const QHash<QString, QStringList>& preFloatZones = state->preFloatZoneAssignments();
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
            state->setPreFloatScreenAssignments(preFloatScreens);
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

    // Migrate lastUsedScreenId per store: last-used is per-key, so rewrite the
    // stored screen on each store that points at the physical screen (or an old
    // virtual sub-screen on it) to the virtual screen its last-used zone falls in.
    // (No empty-list guard needed: the function early-returned on empty
    // virtualScreenIds above.)
    {
        for (PhosphorSnapEngine::SnapState* state : snapAllStates()) {
            const QString lastScreenId = state->lastUsedScreenId();
            if (lastScreenId != physicalScreenId && !lastScreenId.startsWith(prefix)) {
                continue;
            }
            // Already a valid VS in the CURRENT config — leave it alone, like
            // the three sibling loops above. Re-resolving would let a shared
            // layout (zone present on every VS) silently rewrite a correct
            // vs:1 last-used to the first candidate, and force a save for a
            // no-op.
            if (PhosphorIdentity::VirtualScreenId::isVirtual(lastScreenId) && virtualScreenIds.contains(lastScreenId)) {
                continue;
            }
            const QString lastZoneId = state->lastUsedZoneId();
            QString targetVs = virtualScreenIds.first(); // default
            if (!lastZoneId.isEmpty() && m_layoutManager) {
                for (const QString& vsId : virtualScreenIds) {
                    PhosphorZones::Layout* vsLayout = m_layoutManager->resolveLayoutForScreen(vsId);
                    if (vsLayout) {
                        auto uuidOpt = parseUuid(lastZoneId);
                        if (uuidOpt && vsLayout->zoneById(*uuidOpt)) {
                            targetVs = vsId;
                            break;
                        }
                    }
                }
            }
            state->restoreLastUsedZone(lastZoneId, targetVs, state->lastUsedZoneClass(), state->lastUsedDesktop());
            anyStateMigrated = true;
            validateLastUsedZone(targetVs);
        }
    }

    // Migrate the shared free-geometry screenId KEYS from physical (or old virtual)
    // to the new virtual screen. The unified WindowPlacementStore is the single
    // float-back store; rewrite its per-screen keys in place.
    {
        const int changed = m_placementStore.transform([&](PhosphorEngine::WindowPlacement& p) {
            // Point lookup against the window's OWNING store (canonicalizing),
            // replacing the former flat-union copy.
            const QStringList ptZoneIds = zonesForWindow(p.windowId);
            if (ptZoneIds.isEmpty()) {
                return false; // No zone info — keep physical ID, don't guess VS
            }
            QHash<QString, QRect> remapped;
            bool any = false;
            // Deterministic collision rule for keys folding into one target
            // (QHash iteration order is unspecified, so last-visit-wins would
            // make the surviving float-back rect nondeterministic run to run):
            // an untouched identity key beats any remapped one, and among
            // remapped sources the lexicographically smallest source key wins.
            QHash<QString, QString> chosenSource;
            for (auto it = p.freeGeometryByScreen.constBegin(); it != p.freeGeometryByScreen.constEnd(); ++it) {
                QString screen = it.key();
                // Already a valid VS in the CURRENT config — leave it as its own
                // identity source, exactly as the live-screen / pre-float /
                // pending-restore / lastUsed loops do. Re-resolving on a VS
                // RECONFIGURATION would push a still-valid vs:1 key through
                // resolveVirtualScreen, whose per-VS-layout candidate branch can
                // return vs:0 for a zone that resolves uniquely there, silently
                // relocating (and possibly collision-dropping) that float-back
                // rect — and would force a save for a no-op re-resolution.
                const bool alreadyValidVs =
                    PhosphorIdentity::VirtualScreenId::isVirtual(screen) && virtualScreenIds.contains(screen);
                if (!alreadyValidVs && (screen == physicalScreenId || screen.startsWith(prefix))) {
                    screen = resolveVirtualScreen(ptZoneIds, screen);
                    any = true;
                }
                const auto prev = chosenSource.constFind(screen);
                if (prev != chosenSource.constEnd()) {
                    const bool prevIsIdentity = (prev.value() == screen);
                    const bool thisIsIdentity = (it.key() == screen);
                    if (prevIsIdentity || (!thisIsIdentity && prev.value() <= it.key())) {
                        continue; // keep the previously chosen entry
                    }
                }
                chosenSource.insert(screen, it.key());
                remapped.insert(screen, it.value());
            }
            if (any) {
                p.freeGeometryByScreen = remapped;
            }
            return any;
        });
        if (changed > 0) {
            anyStateMigrated = true;
        }
    }

    // Validate each migrated window's zone assignment against its new
    // virtual-screen layout. Migration above moved the window's *screen*
    // onto a VS id, but its *zone* ids still reference the pre-subdivision
    // physical-screen layout — and the VS has its own layout with different
    // zone ids. Without this prune, resnapForVirtualScreenReconfigure resnaps
    // the window to the physical layout's zone geometry (full-monitor width)
    // instead of the VS layout, so windows resize to the wrong target.
    // Symmetric with the physical-layout validation in
    // migrateScreenAssignmentsFromVirtual. Floating windows are skipped —
    // their float state must survive a VS reconfigure.
    if (m_layoutManager) {
        QStringList windowsToRemove;
        forEachZoneAssignedWindow(
            [&](const QString& windowId, const QStringList& zoneIds, const QString& winScreen, int /*desktop*/) {
                if (!virtualScreenIds.contains(winScreen)) {
                    return;
                }
                if (isWindowFloating(windowId)) {
                    return;
                }
                PhosphorZones::Layout* vsLayout = m_layoutManager->resolveLayoutForScreen(winScreen);
                if (vsLayout && !allZonesExistInLayout(zoneIds, vsLayout)) {
                    windowsToRemove.append(windowId);
                }
            });
        anyStateMigrated |= pruneMigratedWindows(windowsToRemove);
    }

    if (migrated > 0 || anyStateMigrated) {
        qCInfo(lcPlacement) << "Migrated" << migrated << "window screen assignments"
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

    // Rewrite each per-screen store's OWN live-screen map in place, folding virtual
    // sub-screen ids on this monitor back to the physical id (see the sibling
    // migrateScreenAssignmentsToVirtual for why this is per-store, not a union push
    // onto the global holder).
    for (PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        QHash<QString, QString> assigns = state->screenAssignments();
        bool storeChanged = false;
        for (auto it = assigns.begin(); it != assigns.end(); ++it) {
            if (it.value().startsWith(prefix)) {
                it.value() = physicalScreenId;
                storeChanged = true;
                migrated++;
            }
        }
        if (storeChanged) {
            state->setScreenAssignments(assigns);
        }
    }

    // Also migrate pre-float screen assignments (owned by SnapState). Per-state
    // in-place rewrite, mirroring the live-screen fold above — see the sibling
    // migrateScreenAssignmentsToVirtual for why this must not round-trip through
    // a union-and-redistribute.
    for (PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        QHash<QString, QString> preFloatScreens = state->preFloatScreenAssignments();
        bool preFloatMigrated = false;
        for (auto it = preFloatScreens.begin(); it != preFloatScreens.end(); ++it) {
            if (it.value().startsWith(prefix)) {
                it.value() = physicalScreenId;
                preFloatMigrated = true;
            }
        }
        if (preFloatMigrated) {
            state->setPreFloatScreenAssignments(preFloatScreens);
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

    // B2: Migrate lastUsedScreenId per store — fold a virtual sub-screen on this
    // physical monitor back to the physical id. Last-used is per-key, so sweep every
    // store rather than a single global scalar.
    for (PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        const QString lastScreenId = state->lastUsedScreenId();
        if (PhosphorIdentity::VirtualScreenId::isVirtual(lastScreenId)
            && PhosphorIdentity::VirtualScreenId::extractPhysicalId(lastScreenId) == physicalScreenId) {
            state->restoreLastUsedZone(state->lastUsedZoneId(), physicalScreenId, state->lastUsedZoneClass(),
                                       state->lastUsedDesktop());
            anyStateMigrated = true;
            validateLastUsedZone(physicalScreenId);
        }
    }

    // B3: Migrate the shared free-geometry screenId KEYS from virtual back to
    // physical (the unified record is the single float-back store).
    const int freeGeoRemapped = m_placementStore.transform([&](PhosphorEngine::WindowPlacement& p) {
        QHash<QString, QRect> remapped;
        bool any = false;
        // Same deterministic collision rule as the ToVirtual sibling: identity
        // beats remapped, then smallest source key — every VS sub-key here
        // folds into the ONE physical key, so without it the surviving rect
        // would follow QHash iteration order.
        QHash<QString, QString> chosenSource;
        for (auto it = p.freeGeometryByScreen.constBegin(); it != p.freeGeometryByScreen.constEnd(); ++it) {
            QString screen = it.key();
            if (PhosphorIdentity::VirtualScreenId::isVirtual(screen)
                && PhosphorIdentity::VirtualScreenId::extractPhysicalId(screen) == physicalScreenId) {
                screen = physicalScreenId;
                any = true;
            }
            const auto prev = chosenSource.constFind(screen);
            if (prev != chosenSource.constEnd()) {
                const bool prevIsIdentity = (prev.value() == screen);
                const bool thisIsIdentity = (it.key() == screen);
                if (prevIsIdentity || (!thisIsIdentity && prev.value() <= it.key())) {
                    continue; // keep the previously chosen entry
                }
            }
            chosenSource.insert(screen, it.key());
            remapped.insert(screen, it.value());
        }
        if (any) {
            p.freeGeometryByScreen = remapped;
        }
        return any;
    });
    // Mirror the migrateScreenAssignmentsToVirtual sibling: a remapped free-geometry
    // key must trigger a save even when no zone assignment migrated (e.g. a purely
    // floating window whose only state is a float-back rect), else the rewritten
    // key is never persisted and the stale virtual key reloads on restart.
    if (freeGeoRemapped > 0) {
        anyStateMigrated = true;
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
        forEachZoneAssignedWindow(
            [&](const QString& windowId, const QStringList& zoneIds, const QString& winScreen, int /*desktop*/) {
                if (winScreen != physicalScreenId) {
                    return;
                }
                // Preserve floating windows — clearing float state here would make
                // previously floating windows eligible for auto-snap again, which is
                // a user-visible behavior change.
                if (isWindowFloating(windowId)) {
                    return;
                }
                if (!allZonesExistInLayout(zoneIds, physLayout)) {
                    windowsToRemove.append(windowId);
                }
            });
        anyStateMigrated |= pruneMigratedWindows(windowsToRemove);
    }

    if (migrated > 0 || anyStateMigrated) {
        qCInfo(lcPlacement) << "Migrated" << migrated << "window screen assignments from virtual screens back to"
                            << physicalScreenId;
        scheduleSaveState();
    }
}

bool WindowTrackingService::pruneMigratedWindows(const QStringList& windowsToRemove)
{
    bool lastUsedCleared = false;
    for (const QString& wId : windowsToRemove) {
        if (PhosphorSnapEngine::SnapState* store = snapForWindow(wId)) {
            const QStringList removedZones = store->zonesForWindow(wId);
            auto unResult = store->unassignWindow(wId);
            lastUsedCleared |= unResult.lastUsedZoneCleared;
            // Scrub the disk-restored representative on the global holder too, as
            // unassignWindow / unsnapForFloat do — a migrated-away zone must not
            // linger as the global last-used.
            lastUsedCleared |= clearGlobalLastUsedIfRemoved(removedZones, store);
            // Notify zone-state consumers exactly as the interactive unassign
            // path does (WindowTrackingService::unassignWindow, which gates on
            // wasAssigned — the store lookup is this loop's equivalent) — a
            // prune that lands in storage but never reaches listeners leaves
            // them tracking a window this service no longer considers snapped.
            Q_EMIT windowZoneChanged(wId, QString());
        }
        clearFreeGeometry(wId); // drop the record's shared free geometry
        clearPreFloatZoneForWindow(wId);
        m_windowStickyStates.remove(wId);
    }
    if (lastUsedCleared) {
        markDirty(DirtyLastUsedZone);
    }
    return !windowsToRemove.isEmpty();
}

QSet<QString>
WindowTrackingService::physicalScreensWithStaleVirtualAssignments(const QSet<QString>& subdividedPhysicalIds) const
{
    // Mirrors the state stores swept by migrateScreenAssignmentsFromVirtual —
    // missing one here would leave a window stranded on a stale VS id even
    // after migration runs. Co-locating the scan with the migrator keeps that
    // pairing maintainable: any future store added to the migrator must also
    // be added here, both edits land in the same file under the same review.
    QSet<QString> result;
    auto check = [&](const QString& screenId) {
        if (!PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
            return;
        }
        const QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId);
        if (!subdividedPhysicalIds.contains(physId)) {
            result.insert(physId);
        }
    };

    if (hasSnapState()) {
        for (const PhosphorSnapEngine::SnapState* state : snapAllStates()) {
            const QHash<QString, QString>& assigns = state->screenAssignments();
            for (auto it = assigns.constBegin(); it != assigns.constEnd(); ++it) {
                check(it.value());
            }
            const QHash<QString, QString>& preFloat = state->preFloatScreenAssignments();
            for (auto it = preFloat.constBegin(); it != preFloat.constEnd(); ++it) {
                check(it.value());
            }
        }
    }
    for (auto qit = m_pendingRestoreQueues.constBegin(); qit != m_pendingRestoreQueues.constEnd(); ++qit) {
        for (const auto& entry : qit.value()) {
            check(entry.screenId);
        }
    }
    for (const PhosphorEngine::WindowPlacement& p : m_placementStore.records()) {
        for (auto it = p.freeGeometryByScreen.constBegin(); it != p.freeGeometryByScreen.constEnd(); ++it) {
            check(it.key());
        }
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::windowClosed(const QString& windowId, PhosphorEngine::WindowKind kind)
{
    if (!hasSnapState())
        return;
    PhosphorSnapEngine::SnapState* snapState = snapForWindow(windowId);

    // Query the registry-aware helper so a window that renamed mid-session
    // (Electron/CEF) lands in the restore queue under its CURRENT class,
    // not the first-seen one.
    QString appId = currentAppIdFor(windowId);

    // Persist the zone assignment to pending BEFORE removing from active tracking.
    // This ensures the window can be restored to its zone when reopened.
    // BUT: Don't persist if the window is floating - floating windows should stay floating
    // and not be auto-snapped when reopened.
    QStringList zoneIds = snapState ? snapState->zonesForWindow(windowId) : QStringList{};
    QString zoneId = zoneIds.isEmpty() ? QString() : zoneIds.first();
    // Check floating with full windowId first, fallback to appId
    bool isFloating = isWindowFloating(windowId);
    if (!zoneId.isEmpty() && !zoneId.startsWith(kZoneSelectorIdPrefix)
        && !isFloating
        // A whitespace-only / whitespace-bearing appId is a corrupt window
        // identity (KWin reported a blank class). Persisting a PendingRestore
        // under it pollutes the restore queue with a key no real window
        // matches cleanly — and a blank " " key is then consumed
        // indiscriminately by every later blank-class window. Drop it.
        && PhosphorIdentity::WindowId::isValidAppId(appId)) {
        const QString screenId = snapState ? snapState->screenForWindow(windowId) : QString();
        // SnapState::desktopForWindow returns 0 (all-desktops / unknown
        // sentinel) for untracked windows. The disabled-context predicate
        // short-circuits its desktop-disabled check on desktop <= 0, so
        // sticky and untracked windows fall through the monitor-disabled
        // gate only — intentional: a sticky window isn't "on" any one
        // desktop. The restore path treats 0 as "don't constrain on
        // desktop" too, so a value of 0 carried into the PendingRestore
        // entry won't migrate the window to a wrong desktop on reopen.
        const int desktop = snapState ? snapState->desktopForWindow(windowId) : 0;

        // Disabled-context gate (discussion #461 item 2). When the closing
        // window lives on a monitor/desktop the user disabled snap for,
        // recording a PendingRestore turns into a delayed footgun: either
        // the same app reopens on the disabled screen and the snap restore
        // path (gated on destination only) drags it to the saved zone, or
        // KWin rehomes the window onto a surviving monitor after the
        // disabled screen sleeps and the same restore fires there. The fix
        // is to not record the entry at all — the snap-restore path
        // already returns noSnap when the queue is empty.
        //
        // The predicate only runs when we have a screenId to evaluate. If
        // SnapState pruned the window's screen before windowClosed ran
        // (screen disconnect race) the gate cannot apply, so we persist
        // unconditionally — pre-3.0 behaviour for untracked windows.
        const bool gateApplies = !screenId.isEmpty() && m_shouldTrackPredicate;
        if (gateApplies && !m_shouldTrackPredicate(screenId, desktop)) {
            qCDebug(lcPlacement) << "Skipped PendingRestore for closed window" << appId
                                 << "-- disabled context, screen:" << screenId << "desktop:" << desktop;
        } else {
            if (screenId.isEmpty()) {
                qCDebug(lcPlacement) << "PendingRestore gate: empty screenId for closed window" << appId
                                     << "-- persisting unconditionally";
            }
            PendingRestore entry;
            entry.zoneIds = zoneIds;
            entry.screenId = screenId;
            entry.virtualDesktop = desktop;
            entry.windowKind = kind;

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

            qCInfo(lcPlacement) << "Persisted zone" << zoneId << "for closed window" << appId << "screen:" << screenId
                                << "desktop:" << desktop << "layout:"
                                << (contextLayout ? contextLayout->id().toString() : QStringLiteral("none"))
                                << "zoneNumbers:" << zoneNumbers;
        }
    }

    // Order matters: zoneIds/screenId/desktop are read above BEFORE this call
    // clears them in SnapState. Do not move reads below this line.
    if (snapState) {
        snapState->windowClosed(windowId);
    }
    if (m_snapResolver.forgetWindow) {
        m_snapResolver.forgetWindow(windowId);
    }

    // No manual full-windowId→appId float-back copy is needed: the unified record's
    // freeGeometry rides on the record itself, which is stored in its appId bucket,
    // so the appId fallback (peek/take) finds it on reopen automatically.
    // The authoritative engine float bit was already cleared by
    // snapState->windowClosed above (and AutotileEngine::windowClosed clears
    // its own); here we only clear the LEGACY fallback float set + its appId
    // aliases — floating is a runtime-only state that must not carry over when
    // the window is reopened. Without this, closing a floated window and
    // reopening it would inherit the float state (via appId fallback), causing
    // a spurious "floated" OSD and preventing auto-snap.
    m_floatingWindows.remove(windowId);
    if (appId != windowId) {
        m_floatingWindows.remove(appId);
    }
    // Also clear pre-float zone/screen assignments since float state is gone.
    // SnapState is the authoritative store; clear both windowId and appId keys.
    clearPreFloatZone(windowId);
    // Remove sticky-window tracking outright — do NOT migrate to appId. The
    // sticky map is keyed on the canonical (first-seen) composite, so remove
    // under it (issue #628).
    m_windowStickyStates.remove(canonicalizeForLookup(windowId));

    scheduleSaveState();
}

void WindowTrackingService::onLayoutChanged()
{
    if (!hasSnapState() || !m_layoutManager)
        return;

    // Validate zone assignments against new layout.
    // NOTE: Do NOT early-return when the global activeLayout() is null. With virtual
    // screens, individual screens may have per-screen layouts via resolveLayoutForScreen().
    // The per-window stale-assignment loop further down resolves layouts
    // per-screen via resolveLayoutForScreen(), so a null global layout does not
    // mean "no layouts anywhere".
    PhosphorZones::Layout* newLayout = m_layoutManager->activeLayout();

    // Before removing stale assignments, capture (window, zonePosition) for resnap-to-new-layout.
    // The resnap maps a window's primary zone N -> zone N in the new layout; a window whose
    // position exceeds the new layout's zone count is restored to its pre-tile geometry (see
    // SnapEngine::calculateResnapFromPreviousLayout).
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
    // Resnap-buffer construction requires a previous layout to map zone
    // positions from. On first launch (no prevLayout) skip just the buffer
    // build — but DO NOT return: the stale-assignment cleanup further down
    // still has to run, otherwise session-restored windows whose zoneIds
    // reference a since-deleted layout would stay in the snap store forever
    // (the only other purge path is windowClosed, which doesn't fire for
    // assignments restored at startup).
    const bool layoutSwitched = prevLayout && (prevLayout != newLayout);
    if (prevLayout) {
        qCDebug(lcPlacement) << "onLayoutChanged: newLayout="
                             << (newLayout ? newLayout->name() : QStringLiteral("null"))
                             << "prevLayout=" << prevLayout->name() << "switched=" << layoutSwitched
                             << "snappedWindows=" << snappedWindows().size();
    } else {
        qCDebug(lcPlacement) << "onLayoutChanged: no previous layout (first launch); skipping resnap-buffer "
                                "build, running stale-assignment cleanup only";
    }
    if (prevLayout) {
        QVector<ResnapEntry> newBuffer;

        // Build the position map from EVERY loaded layout, not just
        // prevLayout. The window's stored zoneIds came from whatever
        // layout was the cascade winner at the time it snapped — which
        // can differ from prevLayout when the user has per-screen /
        // per-context entries. Restricting to prevLayout's zones broke
        // the resnap path on promote edits: clearing shadows shifts the
        // cascade winner from a per-context entry (whose layout differs
        // from the global active) to the screen-level slot, but the
        // window is still in the old per-context layout's zones. With
        // only prevLayout in the map, the lookup misses, addToBuffer
        // returns early, and the resnap buffer ends up empty.
        // Zone UUIDs are unique across all layouts, so this is safe.
        const QHash<QString, int> globalZoneIdToPosition =
            PhosphorZones::LayoutUtils::buildGlobalZonePositionMap(m_layoutManager->layouts());
        const int globalPrevZoneCount = prevLayout->zones().size();

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

            // Use the all-layouts position map built above. The previous
            // per-screen-layout branch resolved the screen's layout AFTER
            // the change had landed, which yielded the new layout and made
            // the lookup miss for OLD zoneIds. The all-layouts map handles
            // every layout the window could have snapped to. The @p screenId
            // parameter is still consumed below when constructing the
            // ResnapEntry.

            // Use primary zone for position mapping
            QString zoneId = zoneIdList.isEmpty() ? QString() : zoneIdList.first();
            int pos = globalZoneIdToPosition.value(zoneId, 0);
            if (pos <= 0) {
                // Handle zoneselector synthetic IDs: "zoneselector-{layoutId}-{index}"
                if (zoneId.startsWith(kZoneSelectorIdPrefix)) {
                    int lastDash = zoneId.lastIndexOf(QLatin1Char('-'));
                    if (lastDash > 0) {
                        bool ok = false;
                        int idx = zoneId.mid(lastDash + 1).toInt(&ok);
                        if (ok && idx >= 0 && idx < globalPrevZoneCount) {
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
            ResnapEntry entry;
            entry.windowId = windowIdOrStableId;
            entry.zonePosition = pos;
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

        if (layoutSwitched) {
            // User switched layouts: capture assignments to zones from the OLD layout (not in new)
            // 1. Live assignments (windows we've tracked via windowSnapped)
            forEachZoneAssignedWindow(
                [&](const QString& windowId, const QStringList& zoneIds, const QString& windowScreen, int desktop) {
                    PhosphorZones::Layout* effectiveLayout = resolveNewLayoutForScreen(windowScreen);
                    if (anyZoneExistsInLayout(zoneIds, effectiveLayout)) {
                        return;
                    }
                    addToBuffer(windowId, zoneIds, windowScreen, desktop);
                });

            // 2. Pending assignments (session-restored windows)
            for (auto it = m_pendingRestoreQueues.constBegin(); it != m_pendingRestoreQueues.constEnd(); ++it) {
                for (const PendingRestore& entry : it.value()) {
                    PhosphorZones::Layout* effectiveLayout = resolveNewLayoutForScreen(entry.screenId);
                    if (anyZoneExistsInLayout(entry.zoneIds, effectiveLayout)) {
                        continue;
                    }
                    if (!entry.layoutId.isEmpty()) {
                        auto savedUuid = parseUuid(entry.layoutId);
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
            forEachZoneAssignedWindow(
                [&](const QString& windowId, const QStringList& zoneIds, const QString& windowScreen, int desktop) {
                    PhosphorZones::Layout* effectiveLayout = m_layoutManager->resolveLayoutForScreen(windowScreen);
                    if (!anyZoneExistsInLayout(zoneIds, effectiveLayout)) {
                        return;
                    }
                    addToBuffer(windowId, zoneIds, windowScreen, desktop);
                });

            // 2. Pending assignments — check against each entry's screen's layout
            for (auto it = m_pendingRestoreQueues.constBegin(); it != m_pendingRestoreQueues.constEnd(); ++it) {
                for (const PendingRestore& entry : it.value()) {
                    PhosphorZones::Layout* effectiveLayout = m_layoutManager->resolveLayoutForScreen(entry.screenId);
                    if (!anyZoneExistsInLayout(entry.zoneIds, effectiveLayout)) {
                        continue;
                    }
                    if (!entry.layoutId.isEmpty()) {
                        auto savedUuid = parseUuid(entry.layoutId);
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
            qCInfo(lcPlacement) << "Resnap buffer:" << m_resnapBuffer.size() << "windows (zone position -> window)";
            for (const ResnapEntry& e : m_resnapBuffer) {
                qCDebug(lcPlacement) << "Zone" << e.zonePosition << "<-" << e.windowId;
            }
        }
    }

    // Remove stale assignments: check each window against its screen's effective layout
    // (not just the global active), so per-screen assignments aren't incorrectly purged.
    // Skip windows on autotile screens — their zone assignments must survive the
    // autotile period so resnapCurrentAssignments() can restore them when tiling is toggled off.
    // Skip windows on OTHER virtual desktops — their zone assignments belong to that
    // desktop's layout and must not be purged when the current desktop's layout changes.
    const QString currentActivity = m_layoutManager->currentActivity();

    // Cache autotile status per screen to avoid redundant lookups (O(screens) instead of O(windows))
    QHash<QString, bool> screenIsAutotile;

    QStringList toRemove;
    // Multi-zone windows where SOME zones survived the layout change: we
    // keep the window, but rewrite the assignment to drop the dangling zone
    // ids. Without this, multiZoneGeometry / zonesForWindow downstream would
    // keep seeing invalid uuids and either return zero rects for them or
    // (worse) fold them into geometry queries that silently no-op.
    struct RewriteTarget
    {
        QStringList zones;
        QString screenId;
        int desktop = 0;
    };
    QHash<QString, RewriteTarget> toRewrite;
    // Collect-then-mutate: forEachZoneAssignedWindow iterates the live stores, so
    // the unassign / re-assign below runs after the visitation completes. The
    // desktop is captured per window here so the rewrite pass needs no second
    // lookup against state that the removal pass may have already changed.
    forEachZoneAssignedWindow(
        [&](const QString& windowId, const QStringList& zoneIdList, const QString& windowScreen, int windowDesktop) {
            if (zoneIdList.isEmpty()) {
                toRemove.append(windowId);
                return;
            }

            // Preserve zone assignments for windows on other desktops. Desktop 0
            // means "all desktops" (pinned window) — always process those.
            //
            // Ordering note: this desktop gate is BEFORE the autotile-screen gate
            // below. Both gates ultimately preserve the assignment (by returning),
            // so order is observationally irrelevant — but the intent is "windows
            // on other desktops are preserved categorically, autotile preservation
            // is a separate axis that only matters for windows whose desktop is
            // current." Don't reorder these without checking that the new ordering
            // still preserves the union {other-desktop OR autotile-screen}.
            //
            // Per-output virtual desktops (#648): "other desktop" is relative to the
            // window's OWN screen's current desktop, not the global current. NOT the
            // shared desktopMatchesFilter helper: this gate must also preserve when
            // the screen's current desktop is unknown (0), where the helper's
            // filter-disabled semantics would process the window instead.
            const int currentDesktop = m_layoutManager->currentVirtualDesktopForScreen(windowScreen);
            if (windowDesktop != 0 && windowDesktop != currentDesktop) {
                return;
            }

            // If this screen's assignment is autotile, preserve zone assignments for resnap
            auto cached = screenIsAutotile.constFind(windowScreen);
            if (cached == screenIsAutotile.constEnd()) {
                QString assignmentId =
                    m_layoutManager->assignmentIdForScreen(windowScreen, currentDesktop, currentActivity);
                cached = screenIsAutotile.insert(windowScreen, PhosphorLayout::LayoutId::isAutotile(assignmentId));
            }
            if (*cached) {
                return;
            }

            PhosphorZones::Layout* effectiveLayout = m_layoutManager->resolveLayoutForScreen(windowScreen);
            if (!effectiveLayout) {
                toRemove.append(windowId);
                return;
            }
            if (allZonesExistInLayout(zoneIdList, effectiveLayout)) {
                return; // fully valid, nothing to do
            }
            // Partial or full invalidity: rebuild the surviving subset. Empty
            // result means the whole window lost its zones → mark for unassign.
            QStringList survivingZones;
            survivingZones.reserve(zoneIdList.size());
            for (const QString& zid : zoneIdList) {
                const auto uuid = parseUuid(zid);
                if (uuid && effectiveLayout->zoneById(*uuid)) {
                    survivingZones.append(zid);
                }
            }
            if (survivingZones.isEmpty()) {
                toRemove.append(windowId);
            } else {
                toRewrite.insert(windowId, {survivingZones, windowScreen, windowDesktop});
            }
        });

    for (const QString& windowId : toRemove) {
        unassignWindow(windowId);
    }
    for (auto it = toRewrite.constBegin(); it != toRewrite.constEnd(); ++it) {
        const RewriteTarget& target = it.value();
        assignWindowToZones(it.key(), target.zones, target.screenId, target.desktop);
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
    // Restore onto the store that owns @p screenId's context; an empty screenId
    // (the disk restore, which persists only the zone id) lands on the global
    // holder as the single representative until the first live snap repopulates a
    // per-screen store.
    PhosphorSnapEngine::SnapState* store = snapForScreen(screenId);
    if (!store) {
        store = snapGlobals();
    }
    if (store) {
        store->restoreLastUsedZone(zoneId, screenId, zoneClass, desktop);
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

    // Fallback: physical screens only (no PhosphorScreens::ScreenManager available)
    for (QScreen* screen : QGuiApplication::screens()) {
        QRect intersection = geometry.intersected(screen->geometry());
        if (intersection.width() >= MinVisibleWidth && intersection.height() >= MinVisibleHeight) {
            return true;
        }
    }
    return false;
}

QRect WindowTrackingService::adjustGeometryToScreen(const QRect& geometry) const
{
    // Try virtual/effective screens first via PhosphorScreens::ScreenManager
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
    QScreen* nearest = findNearestScreen(geometry.center());
    if (!nearest) {
        return geometry;
    }

    return clampToRect(geometry, nearest->geometry());
}

void WindowTrackingService::validateLastUsedZone(const QString& targetScreen)
{
    if (!m_layoutManager) {
        return;
    }
    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(targetScreen);
    // Last-used is per-key: clear the last-used on any store that points at
    // @p targetScreen but whose zone no longer exists in that screen's layout.
    for (PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        const QString lastZoneId = state->lastUsedZoneId();
        if (lastZoneId.isEmpty() || state->lastUsedScreenId() != targetScreen) {
            continue;
        }
        if (layout) {
            auto uuidOpt = parseUuid(lastZoneId);
            if (uuidOpt && layout->zoneById(*uuidOpt)) {
                continue;
            }
        }
        state->restoreLastUsedZone({}, {}, {}, 0);
    }
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
        qCInfo(lcPlacement) << "Virtual screen" << screenId << "no longer exists, falling back to" << nearest
                            << "on same physical monitor" << physId;
        return nearest;
    }

    qCWarning(lcPlacement) << "Virtual screen" << screenId << "no longer exists, falling back to physical screen"
                           << physId;
    return physId;
}

PhosphorZones::Zone* WindowTrackingService::findZoneById(const QString& zoneId) const
{
    auto uuidOpt = parseUuid(zoneId);
    if (!uuidOpt) {
        return nullptr;
    }

    return findZoneInAllLayouts(*uuidOpt).zone;
}

WindowTrackingService::ZoneLookupResult WindowTrackingService::findZoneInAllLayouts(const QUuid& zoneUuid) const
{
    // Guard locally like every other m_layoutManager consumer in this file
    // (onLayoutChanged, validateLastUsedZone) — the API does not promise a
    // non-null manager.
    if (!m_layoutManager) {
        return {};
    }
    // Search all layouts, not just the active one, to support per-screen layouts
    for (PhosphorZones::Layout* layout : m_layoutManager->layouts()) {
        PhosphorZones::Zone* zone = layout->zoneById(zoneUuid);
        if (zone) {
            return {zone, layout};
        }
    }
    return {};
}

} // namespace PhosphorPlacement
