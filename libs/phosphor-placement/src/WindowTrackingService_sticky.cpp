// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Per-desktop placement bookkeeping: the compositor's report of a window's
// sticky (on-all-desktops) state, the dirty-marking wrappers the engines'
// per-desktop membership pass calls to keep the persisted per-desktop zones
// honest, and the membership-aware walk over every snap store's zone
// assignments that the occupancy and resnap consumers read.
//
// Its own translation unit because WindowTrackingService.cpp is over the
// file-size ceiling, and these are one concern: what the membership pass
// reads, what it writes back, and how a window present on several desktops
// is reported to the consumers that ask about every store at once. The
// float path's unsnap lives here too: it is the one writer that forgets a
// desktop's persisted zone outside the pass.

#include <PhosphorPlacement/WindowTrackingService.h>

#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include "placementlogging.h"

namespace PhosphorPlacement {

void WindowTrackingService::unsnapForFloat(const QString& windowId)
{
    PhosphorSnapEngine::SnapState* snapState = snapForWindow(windowId);
    if (!snapState || !snapState->isWindowSnapped(windowId)) {
        return;
    }

    // Read zone/screen for logging BEFORE unsnapForFloat clears them.
    QStringList zoneIds = snapState->zonesForWindow(windowId);
    QString screenId = snapState->screenForWindow(windowId);
    // The desktop this store's assignment belongs to, for the persisted map:
    // a window present on several desktops keeps its pre-float zones in
    // slot.zoneIds (the float-back), but its per-desktop entry has to go, or
    // a restart onto another desktop seeds the zone back as a live snap on
    // the desktop the user had floated it on.
    const int floatedDesktop = snapState->desktopForWindow(windowId);

    // SnapState::unsnapForFloat saves pre-float state (windowId-keyed) and unassigns.
    auto unassignResult = snapState->unsnapForFloat(windowId);
    if (floatedDesktop >= 1) {
        forgetDesktopZones(windowId, PhosphorEngine::WindowPlacement::snapEngineId(), floatedDesktop);
    }

    // Also write an appId-keyed entry into the SAME store for session-restore
    // fallback. SnapState::unsnapForFloat only writes the windowId key; the appId
    // alias lets preFloatZone()/preFloatScreen() find the entry after a window
    // close+reopen cycle where the windowId changes but the appId persists. It
    // shares the window's owning store so the per-window preFloat lookup finds both.
    QString appId = currentAppIdFor(windowId);
    if (appId != windowId && !appId.isEmpty()) {
        snapState->addPreFloatZone(appId, zoneIds);
        if (!screenId.isEmpty()) {
            snapState->addPreFloatScreen(appId, screenId);
        }
    }
    qCInfo(lcPlacement) << "Saved pre-float zones for" << windowId << "->" << zoneIds << "screen:" << screenId;

    // Last-used-zone coupling: unsnapForFloat already cleared this store's own
    // per-key last-used if it named the floated zone. The global holder still carries
    // the representative restored from disk, so clear it too if it named the zone.
    bool lastUsedCleared = unassignResult.lastUsedZoneCleared;
    lastUsedCleared |= clearGlobalLastUsedIfRemoved(zoneIds, snapState);

    Q_EMIT windowZoneChanged(windowId, QString());
    // One mark for every store this path touched: markDirty also kicks the
    // adaptor's debounced save through stateChanged, so splitting it just
    // restarts the same timer twice.
    markDirty(DirtyPreFloatZones | DirtyPreFloatScreens | DirtyZoneAssignments
              | (lastUsedCleared ? DirtyLastUsedZone : DirtyNone));

    consumePendingAssignment(windowId);
}

void WindowTrackingService::setWindowSticky(const QString& rawWindowId, bool sticky)
{
    // Canonicalize so sticky state survives the effect-restart-after-class-mutation
    // re-identification skew (issue #628). The daemon seeds the canonical mapping
    // in WindowTrackingAdaptor::setWindowMetadata, so canonicalizeForLookup
    // resolves to the first-seen composite without seeding here.
    //
    // Storage only. The per-desktop membership pass is driven by the daemon
    // from the registry's metadata change, which the effect pushes for the
    // same transition and which also carries the window's new desktop set;
    // a signal from here would fire BEFORE that push arrives (the effect
    // reports stickiness first) and reconcile against the old desktops.
    m_windowStickyStates[canonicalizeForLookup(rawWindowId)] = sticky;
}

bool WindowTrackingService::isWindowSticky(const QString& rawWindowId) const
{
    return m_windowStickyStates.value(canonicalizeForLookup(rawWindowId), false);
}

void WindowTrackingService::forgetDesktopZones(const QString& windowId, const QString& engineId, int desktop)
{
    if (windowId.isEmpty() || engineId.isEmpty() || desktop < 1) {
        return;
    }
    // A wrapper, not a direct store call from the engines, for the reason
    // releaseEngineSlot gives: the store has no dirty concept. The first cut
    // reached into the store directly and the forget never reached disk: the
    // in-memory record dropped the desktop, nothing marked the placements
    // dirty, and the next restart resurrected the window onto a desktop it
    // had left.
    if (m_placementStore.forgetDesktopZones(windowId, engineId, desktop)) {
        markDirty(DirtyWindowPlacements);
    }
}

void WindowTrackingService::renumberDesktopZones(int removedDesktop)
{
    if (removedDesktop < 1) {
        return;
    }
    if (m_placementStore.renumberDesktopZones(removedDesktop) > 0) {
        markDirty(DirtyWindowPlacements);
    }
    // Every structure keyed by a bare desktop number moves with the store
    // (IPlacementEngine::renumberDesktopsAfterRemoval's contract): a pending
    // restore or a buffered resnap queued across a mid-list removal would
    // otherwise land one desktop off.
    const auto shift = [removedDesktop](int& desktop) {
        if (desktop == removedDesktop) {
            desktop = 0;
        } else if (desktop > removedDesktop) {
            --desktop;
        }
    };
    bool pendingTouched = false;
    for (auto& queue : m_pendingRestoreQueues) {
        for (PhosphorEngine::PendingRestore& entry : queue) {
            const int before = entry.virtualDesktop;
            shift(entry.virtualDesktop);
            pendingTouched |= (entry.virtualDesktop != before);
        }
    }
    if (pendingTouched) {
        markDirty(DirtyPendingRestores);
    }
    for (PhosphorEngine::ResnapEntry& entry : m_resnapBuffer) {
        shift(entry.virtualDesktop);
    }
}

QStringList WindowTrackingService::recordedSnapZones(const QString& windowId) const
{
    // Prefer the live, runtime assignment — it reflects this session's snaps.
    // Go through the canonicalizing point accessor (not the raw whole-map getter)
    // so a class-mutated window still resolves its live zones (issue #628).
    if (const PhosphorSnapEngine::SnapState* snapState = snapForWindow(windowId)) {
        // The store for the desktop the window's screen SHOWS, when it holds
        // the window: a multi-desktop window's primary falls back to another
        // desktop's store while the viewed desktop holds no membership, and
        // that store's zone is the wrong answer here (a return to snapping
        // would put the window in the other desktop's zone).
        const PhosphorSnapEngine::SnapState* inView = snapForScreen(snapState->screenForWindow(windowId));
        if (inView && inView != snapState && snapHoldsWindow(windowId, inView)) {
            snapState = inView;
        }
        int held = 0;
        for (const PhosphorSnapEngine::SnapState* state : snapAllStates()) {
            held += snapHoldsWindow(windowId, state) ? 1 : 0;
        }
        const QStringList live = snapState->zonesForWindow(windowId);
        if (!live.isEmpty() && (snapState == inView || held <= 1)) {
            return live;
        }
    }
    // Cold cache (post-restart, or after handoffRelease cleared the live map):
    // fall back to the DURABLE snap slot in the placement record. windowId is the
    // exact `appId|uuid`; KWin uuids are stable across a daemon restart, so peek's
    // exact-id branch resolves the right window. The appId fallback is DELIBERATE
    // relogin support (pinned by testRecordedSnapZones_appIdFallbackAfterRelogin):
    // a new-uuid window resolves its app's durable zone for the resnap /
    // never-snapped consumers. Accepted tradeoff: a record-less LIVE window with
    // no live zones reads the same app-level answer — indistinguishable from the
    // relogin case at this layer, and a live-ASSIGNED sibling always resolves via
    // its own live store first (see the sameAppInstancesEachKeepOwnZone test).
    // The window's OWN record stays authoritative: an exact-instance record
    // whose snap slot is NOT snapped answers "no zones" outright — falling
    // through to the app-level fallback there would hand a window that
    // explicitly floats a sibling's zone list.
    if (const auto own = m_placementStore.peekExact(windowId)) {
        const PhosphorEngine::EngineSlot ownSlot = own->slotFor(PhosphorEngine::WindowPlacement::snapEngineId());
        return ownSlot.state == PhosphorEngine::WindowPlacement::stateSnapped()
            ? snapZonesOnDesktopInView(ownSlot, own->screenId)
            : QStringList{};
    }
    // Record-less window: the appId fallback, with an accept selecting
    // genuinely SNAPPED records. peek's appId branch returns the newest
    // record by sequence, and close captures restamp a pure-float sibling to
    // the highest sequence — without the accept, that float record shadowed
    // an older sibling's real snapped slot and a durably-snapped window read
    // as "never snapped" (mis-seeding it into autotile and losing its resnap
    // target). validatedUnmanagedGeometry documents the same shadowing trap
    // for the geometry axis.
    const auto rec =
        m_placementStore.peek(QString(), currentAppIdFor(windowId), [](const PhosphorEngine::WindowPlacement& p) {
            return p.slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).state
                == PhosphorEngine::WindowPlacement::stateSnapped();
        });
    if (rec) {
        const PhosphorEngine::EngineSlot snapSlot = rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId());
        if (snapSlot.state == PhosphorEngine::WindowPlacement::stateSnapped()) {
            return snapZonesOnDesktopInView(snapSlot, rec->screenId);
        }
    }
    return {};
}

QStringList WindowTrackingService::snapZonesOnDesktopInView(const PhosphorEngine::EngineSlot& slot,
                                                            const QString& screenId) const
{
    // A multi-desktop record names a zone per desktop; its flat zoneIds are
    // whichever desktop was in view at the last capture. The consumers that
    // put a window back on a zone (the buffer resnap, the order resnap on a
    // return to snapping) act on the desktop the screen shows, so that is
    // the desktop whose zone answers; a desktop the map does not name is one
    // the window was unsnapped on.
    if (slot.zonesByDesktop.isEmpty()) {
        return slot.zoneIds;
    }
    const int desktop =
        m_virtualDesktopManager && !screenId.isEmpty() ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : 0;
    return desktop > 0 ? slot.zonesByDesktop.value(desktop) : slot.zoneIds;
}

void WindowTrackingService::forEachZoneAssignedWindow(
    const std::function<void(const QString&, const QStringList&, const QString&, int)>& fn) const
{
    Q_ASSERT(hasSnapState());
    if (!hasSnapState()) {
        return;
    }
    for (const PhosphorSnapEngine::SnapState* state : snapAllStates()) {
        const QHash<QString, QStringList>& zones = state->zoneAssignments();
        const QHash<QString, QString>& screens = state->screenAssignments();
        const QHash<QString, int>& desktops = state->desktopAssignments();
        for (auto it = zones.constBegin(); it != zones.constEnd(); ++it) {
            // Membership guard: a window's data is authoritative only in the
            // stores it is a MEMBER of. Re-keying a window to a new
            // per-(screen,desktop,activity) store only moves the membership —
            // it does NOT evict the window's zone/desktop data from the old
            // store (see PerScreenStates::setKeyForWindow / removeWindow, which
            // "do not touch state objects"). Iterating the raw stores therefore
            // sees a re-keyed window twice, once per store, with each store's
            // own (possibly stale) desktop value — the cross-desktop resnap
            // leak (a VD1 window read as VD2 from a leftover store, then
            // resnapped off its real desktop). A window present on several
            // desktops, on the other hand, is a member of one store per
            // desktop and each of those holds real data the user chose, so it
            // is reported from every member store with that store's desktop.
            // Kept in lockstep with SnapEngine::stateForWindowOnScreen's
            // eviction, which spares member stores for the same reason.
            // An untracked window resolves to the global holder, so only its
            // entries in that holder are reported (that store is its own
            // owner); a leftover it has in a per-screen store is a phantom and
            // is skipped like any other non-member store.
            const PhosphorSnapEngine::SnapState* owner = snapForWindow(it.key());
            if (owner && owner != state && !snapHoldsWindow(it.key(), state)) {
                continue;
            }
            fn(it.key(), it.value(), screens.value(it.key()), desktops.value(it.key(), 0));
        }
    }
}

} // namespace PhosphorPlacement
