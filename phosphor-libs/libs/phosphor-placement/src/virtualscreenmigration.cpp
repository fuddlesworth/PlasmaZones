// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Virtual-screen migration cluster for WindowTrackingService — the to/from
// virtual-screen assignment rewrites and their pruning/scan helpers. Split
// from lifecycle.cpp for SRP / file-size. Shared Zone/Layout validation
// helpers live in placementvalidation_p.h.

#include <PhosphorPlacement/WindowTrackingService.h>
#include "placementutils.h"
#include "placementvalidation_p.h"

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
#include <algorithm>
#include <climits>

namespace PhosphorPlacement {

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
    // virtual sub-screen its zone falls in, and the snap engine then re-files the
    // window under the store for its new screen (F105, rekeyToRecordedScreens
    // below): left on the physical key, the next commit there read as a move off
    // the monitor and released the window's other desktops. Pushing the
    // aggregated union onto the global holder instead would strand duplicate
    // stale entries in every other store. The zone lookup reads the SAME store's
    // zone map — a window's zones live alongside its screen entry, so no
    // cross-store union is needed.
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
    // union-then-redistribute round-trip re-homed entries with no live window
    // onto the global holder as a side effect; the per-state rewrite
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

    // Migrate lastUsedScreenId per store: last-used is per-key, so rewrite the
    // stored screen on each store that points at the physical screen (or an old
    // virtual sub-screen on it) to the virtual screen its last-used zone falls in.
    // (No empty-list guard needed: the function early-returned on empty
    // virtualScreenIds above.)
    for (PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        const QString lastScreenId = state->lastUsedScreenId();
        if (lastScreenId != physicalScreenId && !lastScreenId.startsWith(prefix)) {
            continue;
        }
        // Already a valid VS in the CURRENT config — leave the SCREEN alone,
        // like the two sibling loops above. Re-resolving would let a shared
        // layout (zone present on every VS) silently rewrite a correct vs:1
        // last-used to the first candidate, and force a save for a no-op.
        //
        // The ZONE still gets validated: what is settled here is only which
        // screen the last-used belongs to, not that its zone survived this
        // reconfiguration. Skipping straight past the validateLastUsedZone
        // below would leave a store pointing at a zone that no longer exists in
        // its own VS layout — the exact staleness that call exists to clear,
        // and which the unconditional re-resolve this branch replaced used to
        // catch for free.
        if (PhosphorIdentity::VirtualScreenId::isVirtual(lastScreenId) && virtualScreenIds.contains(lastScreenId)) {
            validateLastUsedZone(lastScreenId);
            continue;
        }
        const QString lastZoneId = state->lastUsedZoneId();
        QString targetVs = virtualScreenIds.first(); // default
        if (!lastZoneId.isEmpty() && m_layoutManager) {
            for (const QString& vsId : virtualScreenIds) {
                // The store's own context's layout on that VS, not the
                // desktop in view (F162).
                PhosphorZones::Layout* vsLayout = lastUsedLayoutFor(state, vsId);
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

    // Migrate the shared free-geometry screenId KEYS from physical (or old virtual)
    // to the new virtual screen. The unified WindowPlacementStore is the single
    // float-back store; rewrite its per-screen keys in place.
    {
        const int changed = m_placementStore.transform([&](PhosphorEngine::WindowPlacement& p) {
            // Point lookup against the window's OWNING store (canonicalizing),
            // replacing the former flat-union copy. A window with no live
            // zones (closed, or not yet re-announced after a daemon start) is
            // placed by its record's snapped slot, else every closed record
            // kept the whole monitor's id and reopened stretched across the
            // split (F381). The slot's flat zones are the desktop in view at
            // the last capture; a per-desktop slot without them answers its
            // record's own desktop. A floating slot's pre-float zones say
            // nothing of where the window floats, so it keeps the monitor's id.
            QStringList ptZoneIds = zonesForWindow(p.windowId);
            if (ptZoneIds.isEmpty()) {
                const PhosphorEngine::EngineSlot slot = p.slotFor(PhosphorEngine::WindowPlacement::snapEngineId());
                if (slot.state == PhosphorEngine::WindowPlacement::stateSnapped()) {
                    ptZoneIds = slot.zoneIds.isEmpty() ? slot.zonesByDesktop.value(p.virtualDesktop) : slot.zoneIds;
                }
            }
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
                // lastUsed loops do. Re-resolving on a VS
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
            // The MANAGED screen migrates by the same rule as the geometry
            // keys. Left as the dead physical id, the record became
            // permanently unrestorable: the reopen accept compares
            // p.screenId verbatim on both its arms, the stale-state scan
            // below never reported it (it checks only the geometry keys),
            // and once the record carries the asking engine's slot the
            // exact-final gate turned that mismatch into a standing veto.
            if (!ptZoneIds.isEmpty() && !p.screenId.isEmpty()
                && !(PhosphorIdentity::VirtualScreenId::isVirtual(p.screenId) && virtualScreenIds.contains(p.screenId))
                && (p.screenId == physicalScreenId || p.screenId.startsWith(prefix))) {
                const QString resolved = resolveVirtualScreen(ptZoneIds, p.screenId);
                if (resolved != p.screenId) {
                    p.screenId = resolved;
                    any = true;
                }
            }
            return any;
        });
        if (changed > 0) {
            anyStateMigrated = true;
        }
    }

    // Each store's key follows the screen values rewritten above before the
    // prune judges the zones in it, so the prune and every later commit see
    // the window filed under its virtual screen (F105).
    if (m_snapResolver.rekeyToRecordedScreens) {
        m_snapResolver.rekeyToRecordedScreens(physicalScreenId);
    }

    // Validate each migrated window's zone assignment against its new
    // virtual-screen layout. Migration above moved the window's *screen*
    // onto a VS id, but its *zone* ids still reference the pre-subdivision
    // physical-screen layout — and the VS has its own layout with different
    // zone ids. Without this prune, resnapForVirtualScreenReconfigure resnaps
    // the window to the physical layout's zone geometry (full-monitor width)
    // instead of the VS layout, so windows resize to the wrong target.
    // Symmetric with the physical-layout validation in
    // migrateScreenAssignmentsFromVirtual. Each membership is judged in its
    // own store against the layout of that store's desktop and activity, not
    // the context in view (F533), and a store holding the window floating
    // keeps it: its float state must survive a VS reconfigure (F566, the
    // store's own bit, not the routed primary's). A tiling or None context
    // answers no layout and keeps its zones (F264).
    if (m_layoutManager) {
        QList<QPair<QString, PhosphorSnapEngine::SnapState*>> toRemove;
        forEachZoneAssignedWindow([&](const QString& windowId, const QStringList& zoneIds, const QString& winScreen,
                                      int /*desktop*/, const QString& /*activity*/,
                                      PhosphorSnapEngine::SnapState* store) {
            if (!virtualScreenIds.contains(winScreen) || store->isFloating(windowId)) {
                return;
            }
            PhosphorZones::Layout* layout = lastUsedLayoutFor(store, winScreen);
            if (layout && !allZonesExistInLayout(zoneIds, layout)) {
                toRemove.append({windowId, store});
            }
        });
        anyStateMigrated |= pruneMigratedWindows(toRemove);
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
        // The MANAGED screen folds back to the physical id by the same rule
        // — the ToVirtual sibling documents why leaving it stale makes the
        // record permanently unrestorable through the reopen accept.
        if (PhosphorIdentity::VirtualScreenId::isVirtual(p.screenId)
            && PhosphorIdentity::VirtualScreenId::extractPhysicalId(p.screenId) == physicalScreenId) {
            p.screenId = physicalScreenId;
            any = true;
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

    // The stores filed under a virtual id move back under the physical one,
    // like the ToVirtual sibling's re-key (F105).
    if (m_snapResolver.rekeyToRecordedScreens) {
        m_snapResolver.rekeyToRecordedScreens(physicalScreenId);
    }

    // Validate zone assignments against the physical screen's layout.
    // Virtual screen layouts may have zones that don't exist in the physical
    // screen's layout, so remove any assignments referencing invalid zone IDs,
    // each judged in its own store and context like the ToVirtual sibling.
    // A store holding the window floating keeps it: clearing float state here
    // would make a floating window eligible for auto-snap again.
    if (m_layoutManager) {
        QList<QPair<QString, PhosphorSnapEngine::SnapState*>> toRemove;
        forEachZoneAssignedWindow([&](const QString& windowId, const QStringList& zoneIds, const QString& winScreen,
                                      int /*desktop*/, const QString& /*activity*/,
                                      PhosphorSnapEngine::SnapState* store) {
            if (winScreen != physicalScreenId || store->isFloating(windowId)) {
                return;
            }
            PhosphorZones::Layout* layout = lastUsedLayoutFor(store, physicalScreenId);
            if (layout && !allZonesExistInLayout(zoneIds, layout)) {
                toRemove.append({windowId, store});
            }
        });
        anyStateMigrated |= pruneMigratedWindows(toRemove);
    }

    if (migrated > 0 || anyStateMigrated) {
        qCInfo(lcPlacement) << "Migrated" << migrated << "window screen assignments from virtual screens back to"
                            << physicalScreenId;
        scheduleSaveState();
    }
}

bool WindowTrackingService::pruneMigratedWindows(const QList<QPair<QString, PhosphorSnapEngine::SnapState*>>& visits)
{
    for (const auto& visit : visits) {
        const QString& wId = visit.first;
        PhosphorSnapEngine::SnapState* store = visit.second;
        // In the visited store, not through the primary (F533).
        // unassignFromStore forgets that desktop's persisted zone, scrubs the
        // global holder's disk-restored last-used and emits windowZoneChanged
        // only when the primary lost its zone.
        unassignFromStore(wId, store);
        // The window is LIVE: only its zones did not survive the virtual
        // screen change. Its free geometry (the float-back on every monitor)
        // and its sticky flag stay, so it can float back and the sticky
        // exclusion still holds (F441). A window floating in another store
        // keeps the pre-float home that float returns to; otherwise the
        // pre-float zone named a zone that is gone, and a window left with no
        // zone anywhere gives up its record's snap slot too, or the next
        // populate re-admits it into a zone the screen no longer runs.
        const QList<PhosphorSnapEngine::SnapState*> states = snapAllStates();
        const bool floatingElsewhere = std::any_of(states.cbegin(), states.cend(), [&](const auto* state) {
            return state->isFloating(wId);
        });
        if (floatingElsewhere) {
            continue;
        }
        clearPreFloatZone(wId);
        if (!holdsAnyZone(wId)) {
            releaseEngineSlot(wId, PhosphorEngine::WindowPlacement::snapEngineId());
        }
    }
    return !visits.isEmpty();
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
    for (const PhosphorEngine::WindowPlacement& p : m_placementStore.records()) {
        for (auto it = p.freeGeometryByScreen.constBegin(); it != p.freeGeometryByScreen.constEnd(); ++it) {
            check(it.key());
        }
        // The MANAGED screen carries a screen id too — a record whose
        // p.screenId is a stale virtual id must trigger the migration the
        // same way a stale geometry key does, or the migration that would
        // repair it is never invoked.
        check(p.screenId);
    }
    return result;
}

} // namespace PhosphorPlacement
