// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// The resnap buffer, the autotile seed order, the work-area re-apply and the instant-restore cache.
// Part of WindowTrackingService, split from WindowTrackingService.cpp by concern.

#include <PhosphorPlacement/WindowTrackingService.h>
#include "placementutils.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "placementlogging.h"
#include <QSet>
#include <algorithm>
#include <utility>

namespace PhosphorPlacement {

void WindowTrackingService::populateResnapBufferForAllScreens(const QSet<QString>& excludeScreens,
                                                              const QSet<QString>& includeScreens, int desktopFilter)
{
    if (!hasSnapState() || !m_layoutManager)
        return;

    QVector<ResnapEntry> newBuffer;
    QSet<QString> addedIds;

    // One zoneId → position map over every loaded layout. Every switch caller
    // (the quick-layout keys and picker, the KCM apply, a return to snapping)
    // has assigned the NEW layout by now while the windows' zone ids still name
    // the OLD one, so each window's position is read from whichever layout holds
    // its zone; see buildGlobalZonePositionMap for why the merge is unambiguous
    // (zone UUIDs are unique across layouts: the registry re-mints a copied
    // one at load and import, F552).
    const QHash<QString, int> globalZoneIdToPosition =
        PhosphorZones::LayoutUtils::buildGlobalZonePositionMap(m_layoutManager->layouts());

    // Per-screen current-desktop memo for the filter below: one VDM lookup per
    // screen instead of one per candidate window (live pass + durable pass both
    // funnel through addCandidate).
    QHash<QString, int> screenDesktopMemo;

    // Per-window candidate processing, shared by the live and durable passes below.
    const auto addCandidate = [&](const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                  int virtualDesktop) {
        // SNAP's own float bit, not the mode-routed read. This candidate set is
        // snap-owned by construction (live snap-store assignments plus durable
        // records whose snap slot is stateSnapped), and the routed read
        // dispatches on the window's screen's CURRENT mode — which on a screen
        // mid-flip is the DESTINATION engine. Float is per engine, so a
        // destination-engine float bit must not decide whether a snap-owned
        // candidate participates. Same fix as resnap_calc.cpp's.
        const PhosphorSnapEngine::SnapState* snap = snapForWindow(windowId);
        if (zoneIds.isEmpty() || (snap && snap->isFloating(windowId)))
            return;
        if (screenId.isEmpty())
            return;
        // Skip windows on excluded screens (the screens a tiling engine runs)
        if (excludeScreens.contains(screenId))
            return;
        // When include-filter is set, only process windows on the specified screens
        if (!includeScreens.isEmpty() && !includeScreens.contains(screenId))
            return;
        // Desktop filter: a per-desktop layout change resnaps only the windows on
        // that desktop. virtualDesktop==0 means sticky / unknown (visible on every
        // desktop), so those pass whatever the filter. Under per-output virtual
        // desktops (#648) each window is compared against ITS screen's current
        // desktop, not the single value the caller passed. The VDM always answers
        // one (a screen it has no entry for reads the global desktop), so the
        // caller's value stands in only with no VDM wired, or a VDM double that
        // answers nothing.
        if (desktopFilter > 0 && virtualDesktop != 0) {
            int screenDesktop = screenDesktopMemo.value(screenId, 0);
            if (screenDesktop <= 0) {
                const int vdmDesktop =
                    m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : 0;
                screenDesktop = vdmDesktop > 0 ? vdmDesktop : desktopFilter;
                screenDesktopMemo.insert(screenId, screenDesktop);
            }
            if (virtualDesktop != screenDesktop)
                return;
        }

        // Dedup on the CANONICAL key. The live pass feeds this canonical store
        // keys while the durable pass below feeds each record's own composite,
        // and for a class-mutating window (its store key is the first-seen
        // canonical, its record carries the current composite) those two
        // spellings differ — so a raw compare missed the duplicate and the
        // window contributed two resnap rows.
        const QString dedupKey = canonicalizeForLookup(windowId);
        if (addedIds.contains(dedupKey))
            return;
        addedIds.insert(dedupKey);

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
        entry.zoneIds = zoneIds;
        newBuffer.append(entry);
    };

    // 1. Live snap assignments: this session's snaps, kept while the window's screen
    // tiles, so a return to snapping in the same session finds them here.
    // Per-state visitation: each window's screen/desktop come from the store that
    // owns it, never from a cross-store flat-map join.
    // Another activity's store is not in view: its zone ids belong to the
    // layout that activity runs (F126, F127).
    const QString currentActivity = m_layoutManager->currentActivity();
    forEachZoneAssignedWindow([&](const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                  int desktop, const QString& activity, PhosphorSnapEngine::SnapState*) {
        if (activityInView(activity, currentActivity)) {
            addCandidate(windowId, zoneIds, screenId, desktop);
        }
    });

    // 2. Restart-robustness: a window snapped in a PRIOR session and then tiled
    // has its snap zones only in the durable WindowPlacement record — the live
    // snap store map above is cold after a daemon restart. Without this pass a
    // layout switch, a KCM apply or a scrolling→snapping return right after a
    // restart resnaps nothing (empty buffer, no applyGeometriesBatch, so the effect
    // never marks the windows snapped and the snap border / title-bar appearance is
    // never applied). The per-screen autotile→snapping toggle does not come here;
    // it resnaps through calculateResnapFromAutotileOrder over recordedSnapZones.
    for (const PhosphorEngine::WindowPlacement& rec : m_placementStore.records()) {
        // Canonical, matching what addCandidate inserts — see its own note.
        if (addedIds.contains(canonicalizeForLookup(rec.windowId)))
            continue;
        // Liveness gate: the store persists records of long-closed windows
        // (session history), and a dead-id entry in the resnap batch is NOT
        // inert — the compositor's appId fallback resolves it onto the one
        // live unclaimed window of the same app and teleports it to the dead
        // record's zone. The restart case this pass exists for survives the
        // gate: a daemon restart keeps the compositor-issued window ids, so
        // the re-announced live windows match their records exactly. No
        // registry wired (test envs) keeps the historical permissive path.
        // One spelling of liveness for the whole file: the store's probe is
        // the registry lookup plus the bare-id refusal, and a bare id would
        // otherwise read the whole string as an instance.
        if (m_windowRegistry && !m_placementStore.isLiveInstance(rec.windowId))
            continue;
        const PhosphorEngine::EngineSlot snapSlot = rec.slotFor(PhosphorEngine::WindowPlacement::snapEngineId());
        if (snapSlot.state != PhosphorEngine::WindowPlacement::stateSnapped())
            continue;
        // A multi-desktop record answers for the desktop its screen shows,
        // stamped with that desktop so the filter above keeps it. `shown` is 0
        // only with no VDM wired, where the record's own desktop stands.
        int desktop = rec.virtualDesktop;
        const int shown = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(rec.screenId) : 0;
        if (!snapSlot.zonesByDesktop.isEmpty() && shown > 0)
            desktop = shown;
        // Where the window IS answers over where the record says it was: a
        // window moved to another desktop or activity since the record was
        // written (a move into a tiling desktop leaves the snap slot behind)
        // is not on the screen's shown context, and admitting it would snap
        // a window that is not there (F516). A window on several activities is
        // in view on any of them, not only its first (F426, F1034).
        if (const auto context = m_windowRegistry ? m_windowRegistry->desktopContext(rec.windowId) : std::nullopt) {
            if (!context->activity.isEmpty()
                && (context->activities.size() > 1 ? !context->activities.contains(currentActivity)
                                                   : context->activity != currentActivity))
                continue;
            if (const auto desktops = context->desktopSet(); desktops && shown > 0) {
                if (!desktops->contains(shown))
                    continue;
                desktop = shown;
            }
        }
        addCandidate(rec.windowId, snapZonesOnDesktopInView(snapSlot, rec.screenId), rec.screenId, desktop);
    }

    // 3. A window minimized while its monitor switches layouts is not moved,
    // and its unminimize returns it to its pre-float zones, which name the
    // layout it left. They take the same-numbered zone of the layout its
    // store's context runs now (F963(b), l18_minimized_layout_switch), the
    // re-slot the desktop carry gives a minimized window (F953). A layout
    // short of that zone leaves them alone, and the unfloat's layout guard
    // keeps the window at its float spot (F963(a)).
    reslotSuspendedPreFloats(globalZoneIdToPosition);

    // Replaced even when empty: a row kept from an earlier populate belongs
    // to a switch that already ran, and replaying it would move that window
    // again (F34).
    m_resnapBuffer = std::move(newBuffer);
    qCDebug(lcPlacement) << "Resnap buffer (all screens):" << m_resnapBuffer.size() << "windows";
}

void WindowTrackingService::reslotSuspendedPreFloats(const QHash<QString, int>& positions)
{
    int reslotted = 0;
    for (PhosphorSnapEngine::SnapState* store : snapAllStates()) {
        const QHash<QString, QStringList> preFloats = store->preFloatZoneAssignments();
        const std::optional<PhosphorEngine::PlacementStateKey> key =
            m_snapResolver.keyFor ? m_snapResolver.keyFor(store) : std::nullopt;
        for (auto it = preFloats.constBegin(); it != preFloats.constEnd(); ++it) {
            const QString& windowId = it.key();
            if (!store->isFloating(windowId) || !isSuspensionFloat(windowId)) {
                continue;
            }
            // A member store only, as forEachZoneAssignedWindow walks them.
            const PhosphorSnapEngine::SnapState* owner = snapForWindow(windowId);
            if (owner && owner != store && !snapHoldsWindow(windowId, store)) {
                continue;
            }
            QString screenId = key ? key->screenId : store->preFloatScreen(windowId);
            if (screenId.isEmpty()) {
                screenId = store->screenForWindow(windowId);
            }
            if (screenId.isEmpty()) {
                continue;
            }
            const QStringList slot = PhosphorZones::LayoutUtils::sameNumberedZones(
                it.value(), lastUsedLayoutFor(store, screenId), positions);
            if (!slot.isEmpty() && slot != it.value()) {
                store->addPreFloatZone(windowId, slot);
                ++reslotted;
            }
        }
    }
    if (reslotted > 0) {
        qCDebug(lcPlacement) << "Re-slotted the pre-float zones of" << reslotted << "minimized window(s)";
        markDirty(DirtyWindowPlacements);
    }
}

void WindowTrackingService::bufferWindowsOfRemovedLayout(PhosphorZones::Layout* layout)
{
    QVector<ResnapEntry> buffer;
    if (layout && hasSnapState() && m_layoutManager) {
        const QHash<QString, int> positions = PhosphorZones::LayoutUtils::buildZonePositionMap(layout);
        const QString currentActivity = m_layoutManager->currentActivity();
        QSet<QString> added;
        // In view only: a hidden desktop's or activity's window keeps nothing
        // of a deleted layout and is unsnapped by the prune that follows
        // (F403, F429). A window floating in its store is not snapped there.
        forEachZoneAssignedWindow([&](const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                      int desktop, const QString& activity, PhosphorSnapEngine::SnapState* store) {
            const int position = zoneIds.isEmpty() ? 0 : positions.value(zoneIds.first(), 0);
            if (position <= 0 || screenId.isEmpty() || store->isFloating(windowId)) {
                return;
            }
            // A screen with no desktop known (shown <= 0) holds every desktop
            // in view, the answer updatedWindowGeometries gives: with nothing to
            // say a window is hidden, it keeps its slot rather than being
            // unsnapped by the prune.
            const int shown = m_layoutManager->currentVirtualDesktopForScreen(screenId);
            const bool inView =
                (desktop == 0 || shown <= 0 || desktop == shown) && activityInView(activity, currentActivity);
            if (!inView || (m_snapEngine && !m_snapEngine->isActiveOnScreen(screenId))) {
                return;
            }
            const QString key = canonicalizeForLookup(windowId);
            if (added.contains(key)) {
                return;
            }
            added.insert(key);
            ResnapEntry entry;
            entry.windowId = windowId;
            entry.zonePosition = position;
            entry.screenId = screenId;
            entry.virtualDesktop = desktop;
            // A context the delete leaves on no layout sends it to its
            // float-back, unlike a switch to none (cr1_f1093).
            entry.fromRemovedLayout = true;
            buffer.append(entry);
        });
        // A minimized window's pre-float zones re-slot as on a layout switch
        // (F963(b)), but the layout each context runs next is picked after this
        // signal, so the positions wait for the prune that follows (F1035).
        m_removedLayoutPositions.insert(positions);
    }
    m_resnapBuffer = std::move(buffer);
    qCDebug(lcPlacement) << "Resnap buffer (removed layout):" << m_resnapBuffer.size() << "windows";
}

QStringList WindowTrackingService::buildZoneOrderedWindowList(const QString& screenId) const
{
    if (!m_layoutManager) {
        return {};
    }

    // Each window's zone number in the layout that holds the zone. Asking the
    // screen's layout fails after the screen has already switched to tiling,
    // which resolves the registry default, not the layout the windows were
    // snapped in (F183, the updateEngineScreens seed).
    const QHash<QString, int> zoneNumberMap =
        PhosphorZones::LayoutUtils::buildGlobalZonePositionMap(m_layoutManager->layouts());

    // Screen assignments may store connector names or EDID-based screen IDs
    // depending on the code path. Use screensMatch() for format-agnostic comparison.

    // This list SEEDS the autotile state for (screenId, CURRENT virtual desktop,
    // current activity). The screen also holds the snaps of its other desktops
    // and activities (e.g. screen S snaps on VD1 and autotiles on VD2 via
    // per-desktop rules), and pulling those into this context's autotile state
    // tiles a window that lives elsewhere, overwriting its snap geometry there.
    // Scope to the current desktop and activity (F139); desktop==0 (sticky /
    // unknown) stays desktop-agnostic and is kept. Mirrors the filters in
    // populateResnapBufferForAllScreens (addCandidate).
    const int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : 0;

    QVector<std::pair<int, QString>> windowsByZone; // (zone position, window id)
    // Per-window dedup. The single-owning-store invariant makes duplicates
    // impossible today, but a duplicate here would double-seed the autotile
    // order (double-tile), so the guard is kept.
    //
    // Not identical to populateResnapBufferForAllScreens' addedIds, which
    // keys on canonicalizeForLookup: this pass has a single source, so the
    // raw id cannot spell the same window two ways.
    QSet<QString> seenWindowIds;
    const QString currentActivity = m_layoutManager->currentActivity();
    forEachZoneAssignedWindow([&](const QString& windowId, const QStringList& zoneIds, const QString& windowScreen,
                                  int windowDesktop, const QString& activity, PhosphorSnapEngine::SnapState*) {
        if (!PhosphorScreens::ScreenIdentity::screensMatch(windowScreen, screenId)) {
            return;
        }
        if (!activityInView(activity, currentActivity)) {
            return;
        }
        if (!desktopMatchesFilter(windowDesktop, currentDesktop)) {
            return;
        }
        if (seenWindowIds.contains(windowId)) {
            return;
        }
        seenWindowIds.insert(windowId);
        // Skip floating windows — the user's manual-mode float choice is
        // preserved across the transition.
        //
        // SNAP's own bit, not the mode-routed read. This list is the
        // snapping→tiling SEED source, and by the time it is built the
        // screen's mode has already flipped to the DESTINATION engine, so
        // the routed read would let the destination engine's own float bit
        // decide what enters its own seed order — a transition-path read
        // must be a SOURCE-mode read. The seed filter downstream applies
        // the destination engine's per-engine rule separately.
        const PhosphorSnapEngine::SnapState* snap = snapForWindow(windowId);
        if (snap && snap->isFloating(windowId)) {
            return;
        }
        if (zoneIds.isEmpty()) {
            return;
        }

        // Use primary zone's zone number
        auto numIt = zoneNumberMap.constFind(zoneIds.first());
        if (numIt != zoneNumberMap.constEnd()) {
            windowsByZone.append({numIt.value(), windowId});
        } else {
            qCWarning(lcPlacement) << "buildZoneOrderedWindowList: zone UUID" << zoneIds.first() << "for window"
                                   << windowId << "is in no registered layout - skipping";
        }
    });

    // Zone number first. Windows stacked in one zone have no order of their
    // own here (the walk is over hash maps, whose order changes from run to
    // run), so the window id breaks the tie and the seed is the same every
    // run (F476).
    std::sort(windowsByZone.begin(), windowsByZone.end());

    QStringList result;
    result.reserve(windowsByZone.size());
    for (const auto& entry : windowsByZone) {
        result.append(entry.second);
    }

    qCDebug(lcPlacement) << "buildZoneOrderedWindowList for" << screenId << ":" << result;
    return result;
}

// calculateSnapAllWindows moved to SnapEngine (libs/phosphor-snap-engine/src/navigation.cpp).

// ═══════════════════════════════════════════════════════════════════════════════
// Resolution Change Handling
// ═══════════════════════════════════════════════════════════════════════════════

QHash<QString, QRect> WindowTrackingService::updatedWindowGeometries() const
{
    QHash<QString, QRect> result;

    if (!m_config.keepWindowsInZonesOnResolutionChange) {
        return result;
    }

    // One answer per window: the membership of the context in view, else the
    // primary's, so a window held only on hidden desktops still gets one. The
    // walk visits every member store, and the last one visited used to win,
    // which could be another desktop's or activity's zone (F128, F544).
    struct Pick
    {
        QStringList zones;
        QString screenId;
        bool inView = false;
    };
    QHash<QString, Pick> picks;
    const QString currentActivity = m_layoutManager ? m_layoutManager->currentActivity() : QString();
    forEachZoneAssignedWindow([&](const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                  int desktop, const QString& activity, PhosphorSnapEngine::SnapState* store) {
        if (zoneIds.isEmpty()) {
            return;
        }
        const int shown = m_layoutManager ? m_layoutManager->currentVirtualDesktopForScreen(screenId) : 0;
        const bool inView =
            (desktop == 0 || shown <= 0 || desktop == shown) && activityInView(activity, currentActivity);
        Pick& pick = picks[windowId];
        if (pick.inView) {
            return;
        }
        if (inView || pick.zones.isEmpty() || store == snapForWindow(windowId)) {
            pick = {zoneIds, screenId, inView};
        }
    });
    for (auto it = picks.constBegin(); it != picks.constEnd(); ++it) {
        const QRect geo = resolveZoneGeometry(it->zones, it->screenId);
        if (geo.isValid()) {
            result.insert(it.key(), geo);
        }
    }
    return result;
}

QHash<QString, QList<WindowTrackingService::PendingRestoreTarget>>
WindowTrackingService::pendingRestoreGeometries() const
{
    using PhosphorEngine::WindowPlacement;
    using PhosphorIdentity::VirtualScreenId::extractPhysicalId;
    QHash<QString, QList<PendingRestoreTarget>> result;

    // Source the effect's instant-restore cache from the unified placement
    // store: every snapped record of a closed window that the async resolve
    // would restore where the window opens now, resolved to its zone geometry
    // and grouped by appId, newest record first. The effect applies the newest
    // entry saved on the opener's own output before the resolve runs, and the
    // resolve claims the newest record on that output, so the two agree only
    // while they read the same record in the same context:
    //  - the context is the one the window opens into (the screen's shown
    //    desktop, the activity in view), as the resolver's is;
    //  - on each output the list stops at the first newer record the claim
    //    would take instead (a floated sibling, or a snapped one refused here).
    // The resolve does not move a teleported window back when it restores no
    // zone. The effect fetches this again when the daemon emits
    // WindowTracking.pendingRestoresChanged (a layout cycle, a desktop or
    // activity switch, a context disabled, a monitor rearranged: F625,
    // l18_restore_cache_refetch), so an entry is stale only until then.
    // A record the effect can see is still open is dropped there by window id.
    const QString activity = m_layoutManager ? m_layoutManager->currentActivity() : QString();
    QHash<QString, QList<quint64>> sequences;
    // Per app and physical output, the newest record the open claim could take
    // that this cache does not serve. The empty output key is every output: an
    // unscreened snapped record is claimable on any of them.
    QHash<QString, QHash<QString, quint64>> newestUnserved;
    const auto noteUnserved = [&](const WindowPlacement& p, const QString& output) {
        quint64& seq = newestUnserved[p.appId][output];
        seq = std::max(seq, p.sequence);
    };
    for (const WindowPlacement& p : m_placementStore.records()) {
        // A record whose window is still open is that window's, not a pending
        // restore: served through the appId-keyed cache it teleported the
        // next same-app window into the open sibling's zone. Answers false
        // without a registry (daemon-only restart, before the effect has
        // re-announced), which is why the target also carries the window id
        // for the effect to check against what it can see. The claim skips
        // it too, so it cuts nothing.
        if (p.appId.isEmpty() || m_placementStore.isLiveInstance(p.windowId)) {
            continue;
        }
        const PhosphorEngine::EngineSlot snapSlot = p.slotFor(WindowPlacement::snapEngineId());
        if (snapSlot.state != WindowPlacement::stateSnapped()) {
            // A floated record with a float spot on the opener's screen is one
            // the open claim takes for an opener there (claimPlacementForOpen).
            // An unscreened one answers on every output it holds a spot on.
            if (snapSlot.state == WindowPlacement::stateFloating()) {
                if (!p.screenId.isEmpty()) {
                    if (p.freeGeometryFor(p.screenId).isValid()) {
                        noteUnserved(p, extractPhysicalId(p.screenId));
                    }
                } else {
                    for (auto it = p.freeGeometryByScreen.constBegin(); it != p.freeGeometryByScreen.constEnd(); ++it) {
                        if (it.value().isValid() && !it.key().isEmpty()) {
                            noteUnserved(p, extractPhysicalId(it.key()));
                        }
                    }
                }
            }
            continue;
        }

        const QString screenId = resolveEffectiveScreenId(p.screenId);
        // Per-output virtual desktops (#648): the desktop ITS screen shows.
        const int shown = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : 0;
        // Skip screens a tiling engine runs: it places there and would fight a
        // snap teleport. The context in view answers by its live mode, so a
        // configured tiling mode whose engine is off runs snapping here as
        // everywhere else (F112). A record captured on another desktop also
        // needs that desktop to snap, as the resolve's accept gate asks. The
        // claim refuses a record either gate refuses, so neither cuts.
        if (m_snapEngine ? !m_snapEngine->isActiveOnScreen(screenId)
                         : (m_layoutManager
                            && m_layoutManager->modeForScreen(screenId, shown, activity)
                                != PhosphorZones::AssignmentEntry::Mode::Snapping)) {
            continue;
        }
        if (p.virtualDesktop >= 1 && p.virtualDesktop != shown && m_layoutManager
            && m_layoutManager->modeForScreen(screenId, p.virtualDesktop, activity)
                != PhosphorZones::AssignmentEntry::Mode::Snapping) {
            continue;
        }
        // A single-desktop record from another desktop is left to the resolve;
        // a multi-desktop record answers with the zone of the desktop shown
        // (F130).
        QStringList zoneIds;
        if (snapSlot.zonesByDesktop.isEmpty()) {
            if (desktopMatchesFilter(p.virtualDesktop, shown)) {
                zoneIds = snapSlot.zoneIds;
            }
        } else {
            zoneIds = shown > 0 ? snapSlot.zonesByDesktop.value(shown) : snapSlot.zoneIds;
        }
        // The zone must belong to the layout of the context the window opens
        // into, in the activity in view, the gate the resolver applies (#1104,
        // F205): zone geometry finds a zone in ANY loaded layout.
        const QRect geo = (!zoneIds.isEmpty()
                           && PhosphorZones::LayoutUtils::contextLayoutHoldsZones(m_layoutManager, screenId, shown,
                                                                                  activity, zoneIds))
            ? resolveZoneGeometry(zoneIds, screenId)
            : QRect();
        if (!geo.isValid()) {
            noteUnserved(p, extractPhysicalId(p.screenId));
            continue;
        }
        // Insert in descending sequence order (newest first).
        QList<PendingRestoreTarget>& targets = result[p.appId];
        QList<quint64>& order = sequences[p.appId];
        int at = 0;
        while (at < order.size() && order.at(at) > p.sequence) {
            ++at;
        }
        order.insert(at, p.sequence);
        targets.insert(at, PendingRestoreTarget{geo, screenId, p.windowId});
    }

    // On each output, cut an app's list at the first newer record the claim
    // would take instead (F285). Over-counting a blocker only costs a
    // teleport, never a wrong one: a floated record on one virtual screen also
    // cuts an opener on its sibling.
    for (auto app = result.begin(); app != result.end();) {
        const auto blockers = newestUnserved.constFind(app.key());
        if (blockers != newestUnserved.constEnd()) {
            const QList<quint64>& order = sequences[app.key()];
            QList<PendingRestoreTarget> kept;
            for (int i = 0; i < app->size(); ++i) {
                const quint64 cut =
                    std::max(blockers->value(QString(), 0), blockers->value(extractPhysicalId(app->at(i).screenId), 0));
                if (order.at(i) > cut) {
                    kept.append(app->at(i));
                }
            }
            *app = kept;
        }
        if (app->isEmpty()) {
            app = result.erase(app);
        } else {
            ++app;
        }
    }
    return result;
}

} // namespace PhosphorPlacement
