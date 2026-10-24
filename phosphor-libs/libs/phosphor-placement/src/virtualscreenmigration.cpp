// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Virtual-screen migration cluster for WindowTrackingService — the to/from
// virtual-screen assignment rewrites and their pruning/scan helpers, and the
// rewrites an output move or identifier flip needs. Split from lifecycle.cpp
// for SRP / file-size. Shared Zone/Layout validation helpers live in
// placementvalidation_p.h.

#include <PhosphorPlacement/WindowTrackingService.h>
#include "placementutils.h"
#include "placementvalidation_p.h"

#include <PhosphorZones/GeometryUtils.h>
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
#include <optional>

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

namespace {
/// Whether @p candidate should replace @p chosen as the source of the rect a
/// virtual-screen fold files under @p target. Several keys can fold into one
/// target and QHash iteration order is unspecified, so the rule is a total
/// order, else the surviving float-back rect would differ run to run. The
/// record's own screen @p ownKey (read before the fold) wins, because it is
/// the screen the window was last managed on, then a key that already was
/// @p target, then the lexicographically smallest source key.
bool replacesFoldSource(const QString& chosen, const QString& candidate, const QString& target, const QString& ownKey)
{
    const bool chosenOwn = !ownKey.isEmpty() && chosen == ownKey;
    const bool candidateOwn = !ownKey.isEmpty() && candidate == ownKey;
    if (chosenOwn != candidateOwn) {
        return candidateOwn;
    }
    const bool chosenIdentity = chosen == target;
    const bool candidateIdentity = candidate == target;
    if (chosenIdentity != candidateIdentity) {
        return candidateIdentity;
    }
    return candidate < chosen;
}
} // namespace

// The ScreenManager comes in as a parameter, and the geometry reads below use
// it rather than m_screenManager: the migration is a request about the
// manager the CALLER holds (the daemon passes its own at startup and on every
// virtual-screen reconfigure, and the tests stage a headless one), and a
// service constructed without a manager can still be migrated by one.
void WindowTrackingService::migrateScreenAssignmentsToVirtual(const QString& physicalScreenId,
                                                              const QStringList& virtualScreenIds,
                                                              PhosphorScreens::ScreenManager* mgr)
{
    if (virtualScreenIds.isEmpty() || !mgr) {
        return;
    }
    const PhosphorScreens::PhysicalScreen physScreen = mgr->physicalScreenFor(physicalScreenId);

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

    // A closed record's own (desktop, activity), for a record with no live
    // store to read its context from. Zero and empty are the record's
    // "unknown" sentinels (WindowPlacement::virtualDesktop).
    using RecordContext = std::optional<std::pair<int, QString>>;

    // The layout a store's memory runs on @p screenId: the store's OWN desktop
    // and activity, the context the prunes below judge it in, so a window
    // remembered on a desktop not in view is not resolved against the layout
    // the desktop in view runs. Without a store, the @p record context when
    // one is given, with an unknown desktop or activity read as the one in
    // view, and otherwise the context in view.
    auto layoutOn = [&](const PhosphorSnapEngine::SnapState* store, const QString& screenId,
                        const RecordContext& record = std::nullopt) -> PhosphorZones::Layout* {
        if (!m_layoutManager) {
            return nullptr;
        }
        if (!store && record) {
            const int desktop =
                record->first >= 1 ? record->first : m_layoutManager->currentVirtualDesktopForScreen(screenId);
            const QString activity = record->second.isEmpty() ? m_layoutManager->currentActivity() : record->second;
            return m_layoutManager->layoutForScreen(screenId, desktop, activity);
        }
        if (!store) {
            return m_layoutManager->resolveLayoutForScreen(screenId);
        }
        const auto [desktop, activity] = storeContextFor(store, screenId);
        return m_layoutManager->layoutForScreen(screenId, desktop, activity);
    };

    // Helper: determine which virtual screen a zone center falls within.
    // Returns the first virtual screen as default if the zone can't be resolved.
    // @param oldScreenId  The window's stored screen ID (physical or old virtual).
    //                     Used to select index-based fallback when re-migrating
    //                     between virtual screen configurations.
    // @param store        The store the zones belong to, whose context names
    //                     the layouts they are looked up in (null: @p record,
    //                     else in view).
    // @param record       A closed record's own context (see layoutOn).
    auto resolveVirtualScreen = [&](const QStringList& zoneIds, const QString& oldScreenId,
                                    const PhosphorSnapEngine::SnapState* store,
                                    const RecordContext& record = std::nullopt) -> QString {
        const std::optional<QUuid> uuidOpt = zoneIds.isEmpty() ? std::nullopt : parseUuid(zoneIds.first());

        // Try per-virtual-screen layouts first: if a VS has its own layout
        // assignment, the zone may belong to that layout rather than the
        // physical screen's layout.  Collect ALL matches — when multiple
        // virtual screens share the same layout the zone appears in all of
        // them, so we must fall through to center-point resolution.
        if (uuidOpt && m_layoutManager) {
            QStringList candidates;
            for (const QString& vsId : virtualScreenIds) {
                PhosphorZones::Layout* vsLayout = layoutOn(store, vsId, record);
                if (vsLayout && vsLayout->zoneById(*uuidOpt)) {
                    candidates.append(vsId);
                }
            }
            if (candidates.size() == 1) {
                return candidates.first();
            }
        }
        // Multiple matches (shared layout), no matches, or no zone at all.

        // When re-migrating from an old virtual screen ID, zone relative coordinates
        // were defined relative to the old VS bounds, not the full physical screen.
        // Projecting them against the physical screen geometry gives wrong results
        // (e.g. a zone at (0,0,1,1) on the right-half VS maps to the physical center
        // instead of the right-half center). Use index-based proximity instead:
        // find the new VS with the nearest index to the old one. It answers a
        // window with no zone too (a snap-floated one with no pre-float home),
        // which would otherwise land on the first virtual screen whatever
        // half it lived on.
        if (PhosphorIdentity::VirtualScreenId::isVirtual(oldScreenId)) {
            return findNearestVirtualScreen(virtualScreenIds,
                                            PhosphorIdentity::VirtualScreenId::extractIndex(oldScreenId));
        }

        if (!uuidOpt || !m_layoutManager) {
            return virtualScreenIds.first();
        }

        // Fall back to the physical screen's layout and center-point mapping.
        // This path is only used when migrating from a physical screen ID
        // (first-time VS setup), where zone coords are relative to the physical
        // screen and center-point resolution is correct.
        PhosphorZones::Layout* layout = layoutOn(store, physicalScreenId, record);
        if (!layout) {
            return virtualScreenIds.first();
        }

        PhosphorZones::Zone* zone = layout->zoneById(*uuidOpt);
        if (!zone) {
            return virtualScreenIds.first();
        }

        if (!physScreen.isValid()) {
            return virtualScreenIds.first();
        }

        // Projected onto the rect the layout is actually laid out against (the
        // available area unless the layout uses the full screen), so a panel's
        // thickness does not shift the centre into a neighbouring virtual
        // screen near a split.
        const QRectF frame = PhosphorZones::GeometryUtils::effectiveScreenGeometry(mgr, layout, physicalScreenId);
        const QRectF absGeo =
            zone->calculateAbsoluteGeometry(frame.isValid() ? frame : QRectF(physScreen.geometry),
                                            PhosphorZones::GeometryUtils::screenScale(mgr, physicalScreenId));
        QPoint center = absGeo.center().toPoint();
        QString vsId = mgr->virtualScreenAt(center, physicalScreenId);
        if (!vsId.isEmpty()) {
            return vsId;
        }

        return virtualScreenIds.first();
    };

    // The virtual screen a float-back rect on this monitor sits in, by its
    // centre. A centre off every virtual screen (a rect dragged partly off
    // the output) answers the nearest one, since ScreenManager::virtualScreenAt
    // falls back to the closest edge. Empty only for an invalid rect or an
    // output the manager cannot place a split on. A rect is kept in the frame of the
    // output's last known geometry (its origin, or (0,0) for an output this
    // session has not seen), so it is re-anchored on the live origin before
    // the lookup.
    auto virtualScreenOfRect = [&](const QRect& rect) -> QString {
        if (!rect.isValid() || !physScreen.isValid()) {
            return {};
        }
        const QPoint frameOrigin = outputOrigin(physicalScreenId).value_or(QPoint());
        return mgr->virtualScreenAt(rect.center() - frameOrigin + physScreen.geometry.topLeft(), physicalScreenId);
    };

    // Where a screen id of a record with no zone to go by moves: the virtual
    // screen its float-back rect sits in, else the nearest index for an old
    // virtual id. A bare physical id with no rect under it stays, since
    // nothing says which half it belongs to.
    auto resolveZoneless = [&](const QString& oldScreenId, const QRect& rect) -> QString {
        if (const QString byRect = virtualScreenOfRect(rect); !byRect.isEmpty()) {
            return byRect;
        }
        if (PhosphorIdentity::VirtualScreenId::isVirtual(oldScreenId)) {
            return findNearestVirtualScreen(virtualScreenIds,
                                            PhosphorIdentity::VirtualScreenId::extractIndex(oldScreenId));
        }
        return oldScreenId;
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
        const QHash<QString, QStringList>& statePreFloatZones = state->preFloatZoneAssignments();
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
            // A window floated from a zone keeps its screen entry but has no
            // live zone: its pre-float home, held in the same store, is where
            // it belongs. Resolved from nothing it was filed on the first
            // virtual screen, and the next activation on its real one read as
            // a move and dropped that home. The pre-float loop below
            // resolves the home's own screen from the same zones, so the two
            // agree.
            QStringList zones = stateZones.value(it.key());
            if (zones.isEmpty()) {
                zones = statePreFloatZones.value(it.key());
            }
            it.value() = resolveVirtualScreen(zones, it.value(), state);
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
            it.value() = resolveVirtualScreen(pfZoneIds, it.value(), state);
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
            // nothing of where the window floats, so a record with no zone to
            // go by is placed by its float-back rects instead
            // (resolveZoneless): left on the monitor's bare id, the reopen accept, which
            // compares p.screenId verbatim, never restored it on a split
            // monitor.
            QStringList ptZoneIds = zonesForWindow(p.windowId);
            const bool live = !ptZoneIds.isEmpty();
            if (!live) {
                const PhosphorEngine::EngineSlot slot = p.slotFor(PhosphorEngine::WindowPlacement::snapEngineId());
                if (slot.state == PhosphorEngine::WindowPlacement::stateSnapped()) {
                    ptZoneIds = slot.zoneIds.isEmpty() ? slot.zonesByDesktop.value(p.virtualDesktop) : slot.zoneIds;
                }
            }
            const bool zoneless = ptZoneIds.isEmpty();
            // A live window's zones are looked up in its own store's context,
            // a record's slot zones in the record's own desktop and activity.
            // A window with no membership resolves to the global holder, which
            // has no key, so its context would be that holder's last-used
            // desktop, a desktop the record need not have been captured on.
            const PhosphorSnapEngine::SnapState* owner = live ? snapForWindow(p.windowId) : nullptr;
            const RecordContext record =
                live ? RecordContext() : RecordContext(std::make_pair(p.virtualDesktop, p.activity));
            const QString ownKey = p.screenId;
            // Read before the keys are rewritten below.
            const QRect ownRect = p.freeGeometryFor(ownKey);
            QHash<QString, QRect> remapped;
            bool any = false;
            // Deterministic collision rule for keys folding into one target
            // (replacesFoldSource): the record's own screen, then an untouched
            // identity key, then the smallest source key.
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
                    screen = zoneless ? resolveZoneless(screen, it.value())
                                      : resolveVirtualScreen(ptZoneIds, screen, owner, record);
                    any |= screen != it.key();
                }
                const auto prev = chosenSource.constFind(screen);
                if (prev != chosenSource.constEnd() && !replacesFoldSource(prev.value(), it.key(), screen, ownKey)) {
                    continue; // keep the previously chosen entry
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
            // A zoneless record goes by the rect it kept under that screen.
            if (!ownKey.isEmpty()
                && !(PhosphorIdentity::VirtualScreenId::isVirtual(ownKey) && virtualScreenIds.contains(ownKey))
                && (ownKey == physicalScreenId || ownKey.startsWith(prefix))) {
                const QString resolved = zoneless ? resolveZoneless(ownKey, ownRect)
                                                  : resolveVirtualScreen(ptZoneIds, ownKey, owner, record);
                if (resolved != ownKey) {
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
        // Same deterministic collision rule as the ToVirtual sibling
        // (replacesFoldSource), and it matters more here: every VS sub-key
        // folds into the ONE physical key. The record's own screen, read
        // before the fold, wins, so an unsplit keeps the spot on the virtual
        // screen the window was last managed on rather than an older one, say
        // a physical key left over from before the split.
        const QString ownKey = p.screenId;
        QHash<QString, QString> chosenSource;
        for (auto it = p.freeGeometryByScreen.constBegin(); it != p.freeGeometryByScreen.constEnd(); ++it) {
            QString screen = it.key();
            if (PhosphorIdentity::VirtualScreenId::isVirtual(screen)
                && PhosphorIdentity::VirtualScreenId::extractPhysicalId(screen) == physicalScreenId) {
                screen = physicalScreenId;
                any = true;
            }
            const auto prev = chosenSource.constFind(screen);
            if (prev != chosenSource.constEnd() && !replacesFoldSource(prev.value(), it.key(), screen, ownKey)) {
                continue; // keep the previously chosen entry
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

// ═══════════════════════════════════════════════════════════════════════════════
// Output moves and identifier flips
// ═══════════════════════════════════════════════════════════════════════════════

std::optional<QPoint> WindowTrackingService::outputOrigin(const QString& screenId) const
{
    const auto it = m_outputGeometry.constFind(PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId));
    if (it == m_outputGeometry.constEnd()) {
        return std::nullopt;
    }
    return it->topLeft();
}

void WindowTrackingService::followOutputGeometry(const PhosphorScreens::PhysicalScreen& screen)
{
    if (!screen.isValid() || !screen.geometry.isValid()) {
        return;
    }
    const QString outputId = screen.identifier.isEmpty() ? screen.name : screen.identifier;
    // The frame the output's rects are in: its last geometry, or (0,0) for one
    // this session has not seen, whose rects the load left relative to it.
    const QRect before = m_outputGeometry.value(outputId);
    m_outputGeometry.insert(outputId, screen.geometry);
    const QPoint delta = screen.geometry.topLeft() - before.topLeft();
    const bool resized = before.isValid() && before.size() != screen.geometry.size();
    if (delta.isNull() && !resized) {
        return;
    }
    // A remembered floating spot keeps its place ON ITS MONITOR (F814,
    // l18_floatback_relative): a move carries every key of the output along,
    // its virtual screens' included. A new size leaves alone a spot still on
    // the output, since a size change can be transient (a wake, a mode set),
    // and pulls one it left off back onto it, shrunk to fit.
    //
    // That pull is stored, not applied at read time, so it is one-way. A
    // transient shrink (a wake at a lower mode, a rotation) that leaves a spot
    // off the smaller output moves and shrinks it for good, and growing back
    // does not restore it, because nothing remembers the original. Only spots
    // the shrink pushed off the output are affected. One that stayed on it,
    // the common case, is untouched in both directions.
    const int moved = m_placementStore.transform([&](PhosphorEngine::WindowPlacement& p) {
        bool any = false;
        for (auto it = p.freeGeometryByScreen.begin(); it != p.freeGeometryByScreen.end(); ++it) {
            if (PhosphorIdentity::VirtualScreenId::extractPhysicalId(it.key()) != outputId) {
                continue;
            }
            QRect rect = it.value().translated(delta);
            if (resized && !geometryOverlapsScreen(rect, it.key())) {
                rect =
                    clampToRect(QRect(rect.topLeft(), rect.size().boundedTo(screen.geometry.size())), screen.geometry);
            }
            if (rect != it.value()) {
                it.value() = rect;
                any = true;
            }
        }
        return any;
    });
    if (moved > 0) {
        qCInfo(lcPlacement) << "Output" << outputId << "now at" << screen.geometry << "- moved the floating spots of"
                            << moved << "window(s) with it";
        markDirty(DirtyWindowPlacements);
    }
}

void WindowTrackingService::renameScreen(const QString& oldPhysicalId, const QString& newPhysicalId)
{
    if (oldPhysicalId.isEmpty() || newPhysicalId.isEmpty() || oldPhysicalId == newPhysicalId) {
        return;
    }
    // An EDID twin's identifier flips between the bare and the connector-
    // suffixed form when its twin connects or leaves (F189). The window never
    // moved, so every stored id of the monitor is rewritten in place; left
    // stale, a resnap's screen filters skipped the window and the snap engine
    // read its next commit as a move off the monitor.
    const QString oldPrefix = oldPhysicalId + PhosphorIdentity::VirtualScreenId::Separator;
    const auto renamed = [&](const QString& screenId) -> QString {
        if (screenId == oldPhysicalId) {
            return newPhysicalId;
        }
        return screenId.startsWith(oldPrefix) ? newPhysicalId + screenId.mid(oldPhysicalId.size()) : QString();
    };
    int rewritten = 0;
    if (const auto geometry = m_outputGeometry.take(oldPhysicalId); geometry.isValid()) {
        m_outputGeometry.insert(newPhysicalId, geometry);
    }
    for (ResnapEntry& entry : m_resnapBuffer) {
        if (const QString to = renamed(entry.screenId); !to.isEmpty()) {
            entry.screenId = to;
        }
    }
    if (m_pendingLastUsedZone) {
        if (const QString to = renamed(m_pendingLastUsedZone->screenId); !to.isEmpty()) {
            m_pendingLastUsedZone->screenId = to;
        }
    }
    const auto renameValues = [&](QHash<QString, QString> values) -> std::optional<QHash<QString, QString>> {
        bool any = false;
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (const QString to = renamed(it.value()); !to.isEmpty()) {
                it.value() = to;
                any = true;
                ++rewritten;
            }
        }
        if (!any) {
            return std::nullopt;
        }
        return values;
    };
    for (PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        if (const auto screens = renameValues(state->screenAssignments())) {
            state->setScreenAssignments(*screens);
        }
        if (const auto preFloat = renameValues(state->preFloatScreenAssignments())) {
            state->setPreFloatScreenAssignments(*preFloat);
        }
        if (const QString to = renamed(state->lastUsedScreenId()); !to.isEmpty()) {
            state->restoreLastUsedZone(state->lastUsedZoneId(), to, state->lastUsedZoneClass(),
                                       state->lastUsedDesktop());
            ++rewritten;
        }
    }
    rewritten += m_placementStore.transform([&](PhosphorEngine::WindowPlacement& p) {
        bool any = false;
        if (const QString to = renamed(p.screenId); !to.isEmpty()) {
            p.screenId = to;
            any = true;
        }
        QHash<QString, QRect> keyed;
        for (auto it = p.freeGeometryByScreen.constBegin(); it != p.freeGeometryByScreen.constEnd(); ++it) {
            const QString to = renamed(it.key());
            if (to.isEmpty()) {
                keyed.insert(it.key(), it.value());
            } else if (!p.freeGeometryByScreen.contains(to)) {
                // A key already under the new id wins, as in the virtual-screen folds.
                keyed.insert(to, it.value());
            }
            any |= !to.isEmpty();
        }
        p.freeGeometryByScreen = keyed;
        return any;
    });
    if (m_snapResolver.renameScreen) {
        m_snapResolver.renameScreen(oldPhysicalId, newPhysicalId);
    }
    if (rewritten > 0) {
        qCInfo(lcPlacement) << "Screen" << oldPhysicalId << "is now" << newPhysicalId << "- renamed" << rewritten
                            << "stored screen id(s)";
        scheduleSaveState();
    }
}

} // namespace PhosphorPlacement
