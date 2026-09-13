// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Per-desktop membership for snapping: which of a screen's desktops a window
// may hold its own zone assignment on.
//
// The third arm of the same change the scroll and tile engines carry, and the
// one that differs most. Those engines PLACE a window, so adopting one into a
// desktop means inserting it into that desktop's layout. Snapping places
// nothing by itself — the user drops a window into a zone — so adoption here
// only grants the window a store of its own on the desktop in view. What it
// buys is that the next snap writes THERE instead of overwriting the zone the
// window occupies on the desktop it came from, which is what made a sticky
// window share one zone across every desktop.
//
// An adopted membership holds NO data until the user snaps or floats the
// window there. On that desktop the window therefore reads as neither snapped
// nor floating for this engine; float is per store in snapping, the same way
// it is per engine across modes, so a window floated on desktop 1 is not
// floating on desktop 2 until the user floats it there.
//
// The eviction in stateForWindowOnScreen is the other half: it spares stores
// the window is a member of, so the two assignments coexist instead of the
// newer one wiping the older.

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>

#include "snapenginelogging.h"

#include <algorithm>
#include <optional>

namespace PhosphorSnapEngine {

using PhosphorEngine::DesktopSpan;
using PhosphorEngine::MembershipReconcileResult;
using PhosphorEngine::PlacementStateKey;

void SnapEngine::installContextResolver()
{
    // Without this the container's primary resolution falls back to a window's
    // FIRST membership, so a window snapped on desktop 1 and then on desktop 2
    // resolves to desktop 1's store both times — and the second snap overwrites
    // the first rather than living beside it. The global-scalar holder sits
    // under the empty key and is never a membership, so it is unaffected.
    m_states.setContextKeyResolver([this](const QString& screenId) {
        return currentKeyForScreen(screenId);
    });
}

void SnapEngine::seedPersistedDesktopZones(const QString& windowId, const PhosphorEngine::EngineSlot& slot,
                                           const QString& screenId, int restoreDesktop, const QString& activity)
{
    // A restored record carries the zone the window occupied on EVERY desktop
    // it was present on. The open path applies only the one for the desktop
    // being restored onto; the rest have to be put back into their own stores
    // here, or switching to those desktops after a restart would find nothing
    // and leave the window wherever it happens to be.
    //
    // A desktop the record names but the session no longer has (the count
    // shrank while the daemon was down) gets a store like any other; the
    // first membership pass on the screen finds the window's span does not
    // cover it and releases it, forgetting the persisted entry with it.
    if (slot.zonesByDesktop.isEmpty() || screenId.isEmpty()) {
        return;
    }
    // Memberships are keyed by the canonical id everywhere else in this
    // engine (stateForWindowOnScreen canonicalizes before it adds), so the
    // same form here, or the restore desktop's membership and the seeded ones
    // would sit under two different keys and never count as one window.
    const QString canonical = canonicalWindowId(windowId);
    // The desktops the window is on NOW, when the registry knows: a record
    // written while the window was on {1,2} and restored onto a window the
    // compositor put on {2} alone must not seed desktop 1, where the window
    // is not; the membership pass would only release it again, and until it
    // ran the zone counted a phantom occupant. A known, narrower span drops
    // the persisted entry with the seed, so a later capture does not revive
    // it. Sticky, or unknown, seeds everything the record names.
    std::optional<QSet<int>> presentOn;
    if (m_windowRegistry) {
        if (const auto ctx = m_windowRegistry->desktopContext(windowId);
            ctx && !ctx->sticky.value_or(false) && !ctx->virtualDesktops.isEmpty()) {
            presentOn = QSet<int>(ctx->virtualDesktops.cbegin(), ctx->virtualDesktops.cend());
        }
    }
    for (auto it = slot.zonesByDesktop.constBegin(); it != slot.zonesByDesktop.constEnd(); ++it) {
        const int desktop = it.key();
        if (desktop < 1 || desktop == restoreDesktop || it.value().isEmpty()) {
            continue; // the restore desktop is applied by the caller
        }
        if (presentOn && !presentOn->contains(desktop)) {
            if (m_windowTracker) {
                m_windowTracker->forgetDesktopZones(windowId, engineId(), desktop);
            }
            continue;
        }
        const PlacementStateKey key{screenId, desktop, activity};
        SnapState* state = ensureStateForKey(key);
        if (!state) {
            continue;
        }
        state->assignWindowToZones(windowId, it.value(), screenId, desktop);
        m_states.addMembership(canonical, key);
        qCInfo(lcSnapEngine) << "seedPersistedDesktopZones: restored" << windowId << "to" << it.value().size()
                             << "zone(s) on desktop" << desktop << "of" << screenId;
    }
}

struct SnapEngine::PendingMembership
{
    QString windowId;
    QList<PlacementStateKey> stale;
    bool adopt = false;
};

void SnapEngine::collectMembershipWork(const QString& windowId, const QString& screenId,
                                       const PlacementStateKey& currentKey, const DesktopSpan& span,
                                       QList<PendingMembership>& pending) const
{
    // An UNKNOWN span (the registry has not stamped a desktop for the window
    // yet) adopts nothing and releases nothing: reading it as "every desktop"
    // put windows into every desktop the user visited.
    if (!span.known) {
        return;
    }
    const QList<PlacementStateKey> held = m_states.membershipsForWindow(windowId);
    PendingMembership entry;
    entry.windowId = windowId;
    for (const PlacementStateKey& key : held) {
        if (key.screenId == screenId && !span.coversKey(key)) {
            entry.stale.append(key);
        }
    }
    entry.adopt = span.coversKey(currentKey) && !m_states.hasMembership(windowId, currentKey);
    if (entry.adopt || !entry.stale.isEmpty()) {
        pending.append(entry);
    }
}

MembershipReconcileResult SnapEngine::applyMembershipWork(const QString& screenId, const PlacementStateKey& currentKey,
                                                          const QList<PendingMembership>& pending)
{
    MembershipReconcileResult result;
    for (const PendingMembership& entry : pending) {
        for (const PlacementStateKey& stale : entry.stale) {
            if (SnapState* state = m_states.stateForKey(stale)) {
                // The zone assignment on a desktop the window has left is not
                // a float-back: it is still snapped on the desktops its span
                // does cover, so there is nothing to restore here.
                state->removeWindowData(entry.windowId);
            }
            m_states.removeMembership(entry.windowId, stale);
            // Forgetting a desktop has to be explicit: the store MERGES
            // zonesByDesktop, so a capture that simply stops naming this
            // desktop leaves the old zone on disk forever and a later restart
            // would resurrect the window onto a desktop it no longer occupies.
            // Through the tracker's wrapper, not the store: only the wrapper
            // marks the placements dirty, and a forget that stays in memory
            // never reaches disk — which is where this one has to land.
            if (m_windowTracker) {
                m_windowTracker->forgetDesktopZones(entry.windowId, engineId(), stale.desktop);
            }
            result.released.append({entry.windowId, stale});
            qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: released" << entry.windowId << "from desktop"
                                 << stale.desktop << "of" << stale.screenId << "— its span no longer covers it";
        }
        if (entry.adopt) {
            // Membership only. No zone is chosen for the window here: an
            // unsnapped window on this desktop stays unsnapped, and the store
            // exists so that snapping it later lands in ITS OWN assignment.
            ensureStateForKey(currentKey);
            m_states.addMembership(entry.windowId, currentKey);
            result.adopted.append({entry.windowId, currentKey});
            qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: adopted" << entry.windowId << "into desktop"
                                 << currentKey.desktop << "of" << currentKey.screenId;
        }
    }

    // A window that holds a zone in the context just entered AND a place on
    // another desktop is sitting at the other desktop's geometry right now (it
    // is one window), so its own assignment here has to be re-applied. Only
    // those: the single-desktop windows on the screen did not move, and
    // re-committing every one of them on every switch was churn the effect had
    // to absorb for nothing. Computed AFTER the arms above, not before: a
    // window adopted into this desktop by this very pass has to count. On a
    // restart that is the entire population — the membership map is rebuilt
    // from scratch, so every multi-desktop window is adopted here rather than
    // arriving already a member.
    // Only on a screen this engine is active on: a screen a tiling engine
    // owns keeps its snap memberships as frozen memory for a return to
    // snapping, and re-committing them here would fight the tiling engine's
    // own placement on every desktop switch (seen live: the window bounced
    // between its tile and its old zone).
    QSet<QString> reapply;
    if (const SnapState* currentState = isActiveOnScreen(screenId) ? m_states.stateForKey(currentKey) : nullptr) {
        for (const QString& windowId : m_states.trackedWindowIds()) {
            if (m_states.hasMembership(windowId, currentKey) && m_states.membershipsForWindow(windowId).size() > 1
                && !currentState->zonesForWindow(windowId).isEmpty()) {
                reapply.insert(windowId);
            }
        }
    }
    if (!reapply.isEmpty()) {
        // Scoped to this screen: the other outputs' assignments did not move,
        // and a whole-session resnap would fight whatever they are doing.
        qCInfo(lcSnapEngine) << "reconcileDesktopMemberships: re-applying" << reapply.size() << "window(s) on"
                             << screenId << "for desktop" << currentKey.desktop
                             << "— multi-desktop windows are snapped here";
        resnapCurrentAssignments(screenId, reapply);
    }
    return result;
}

MembershipReconcileResult SnapEngine::reconcileDesktopMemberships(const QString& screenId,
                                                                  const PhosphorEngine::DesktopSpanQuery& spanOf)
{
    if (!spanOf || screenId.isEmpty()) {
        return {};
    }
    // A screen a tiling engine owns has no snap placements to grant: the
    // zones belong to its layout, and a membership minted here would be a
    // store nothing snaps into. The release arm still runs, so a window that
    // left a desktop of a screen that has since switched mode stops being an
    // occupant of its old zone there.
    const bool snapping = isActiveOnScreen(screenId);
    const PlacementStateKey currentKey = currentKeyForScreen(screenId);

    // Snapshot: the arms mutate the membership map and create stores.
    QList<PendingMembership> pending;
    for (const QString& windowId : m_states.trackedWindowIds()) {
        const QList<PlacementStateKey> held = m_states.membershipsForWindow(windowId);
        const bool onThisScreen = std::any_of(held.cbegin(), held.cend(), [&screenId](const auto& key) {
            return key.screenId == screenId;
        });
        if (!onThisScreen) {
            continue;
        }
        collectMembershipWork(windowId, screenId, currentKey, spanOf(windowId), pending);
        if (!snapping && !pending.isEmpty()) {
            pending.last().adopt = false;
            if (pending.last().stale.isEmpty()) {
                pending.removeLast();
            }
        }
    }
    return applyMembershipWork(screenId, currentKey, pending);
}

MembershipReconcileResult SnapEngine::reconcileWindowMemberships(const QString& windowId,
                                                                 const PhosphorEngine::DesktopSpanQuery& spanOf)
{
    if (!spanOf || windowId.isEmpty()) {
        return {};
    }
    const QString canonical = canonicalWindowId(windowId);
    // A window is on exactly one screen: every membership shares it, and the
    // first one names it. An untracked window has no context to reconcile;
    // it gains its first membership when it is snapped or floated, and the
    // screen-wide pass on the next switch does the rest.
    QString screenId;
    for (const PlacementStateKey& key : m_states.membershipsForWindow(canonical)) {
        if (!key.screenId.isEmpty()) {
            screenId = key.screenId;
            break;
        }
    }
    if (screenId.isEmpty()) {
        return {};
    }
    const PlacementStateKey currentKey = currentKeyForScreen(screenId);
    QList<PendingMembership> pending;
    collectMembershipWork(canonical, screenId, currentKey, spanOf(canonical), pending);
    if (!isActiveOnScreen(screenId) && !pending.isEmpty()) {
        pending.last().adopt = false;
        if (pending.last().stale.isEmpty()) {
            pending.removeLast();
        }
    }
    return applyMembershipWork(screenId, currentKey, pending);
}

} // namespace PhosphorSnapEngine
