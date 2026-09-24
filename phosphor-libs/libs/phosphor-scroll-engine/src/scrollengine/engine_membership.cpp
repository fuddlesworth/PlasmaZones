// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Per-desktop membership: which of a screen's desktops each window holds a
// place in, and the adopt / release pair that maintains it.
//
// A window present on several desktops at once — sticky (all of them) or a
// span such as {1,2} — used to live in exactly one, whichever context was
// current when it opened. On every other desktop it was visible but absent
// from the strip, floating over the columns rather than being one. Here it
// gets a real column in each desktop it spans, so it can be placed and sized
// independently per desktop.
//
// Its own translation unit because engine_context.cpp was at the file-size
// ceiling and this is a separate concern from context lifetime and reaping:
// it has its own entry points, its own daemon triggers (a desktop switch AND
// a change to the window's desktop set), and it ADDS memberships where every
// path in that file moves or drops them.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorIdentity/VirtualScreenId.h>

#include "scrollenginelogging.h"

#include <algorithm>

namespace PhosphorScrollEngine {

using PhosphorEngine::DesktopSpan;
using PhosphorEngine::MembershipReconcileResult;
using PhosphorEngine::PlacementStateKey;

void ScrollEngine::installContextResolver()
{
    // Teach the state container which key each screen is showing, so a window
    // holding a membership on several desktops resolves to the one in view
    // rather than to whichever it was adopted into first. Capturing `this` is
    // safe: the container is a member and cannot outlive the engine.
    m_states.setContextKeyResolver([this](const QString& screenId) {
        return currentKeyForScreen(screenId);
    });
}

struct ScrollEngine::PendingMembership
{
    QString windowId;
    QList<PlacementStateKey> stale;
    bool adopt = false;
};

void ScrollEngine::collectMembershipWork(const QString& windowId, const QString& screenId,
                                         const PlacementStateKey& currentKey, const DesktopSpan& span,
                                         bool adoptAllowed, QList<PendingMembership>& pending) const
{
    // An UNKNOWN span (the registry has not stamped a desktop for the window
    // yet) adopts nothing and releases nothing: reading it as "every desktop"
    // put windows into every desktop the user visited.
    if (!span.known) {
        return;
    }
    // A window mid-drag is DETACHED from every strip while it stays tracked
    // (DETACH-ONCE); adopting it into the strip under the preview would have
    // the commit's insert refused and the window degraded to floating while
    // still tiled here. The drop or cancel re-homes it, and the next pass
    // sees it settled.
    if (m_dragInsertPreview && m_dragInsertPreview->windowId == windowId) {
        return;
    }
    // A held (own-fullscreen) window is NOT exempt: see releaseMembership.
    const QList<PlacementStateKey> held = m_states.membershipsForWindow(windowId);
    PendingMembership entry;
    entry.windowId = windowId;
    const int pinned = m_context.stickyPinnedDesktop(screenId);
    for (const PlacementStateKey& key : held) {
        // A membership under the screen's sticky pin is not evidence the
        // window left anything: the pin keys every desktop's state by the
        // pinned desktop, and the engine's own unpin migration moves it.
        if (key.screenId == screenId && key.desktop != pinned && !span.coversKey(key)) {
            entry.stale.append(key);
        }
    }
    entry.adopt = adoptAllowed && span.coversKey(currentKey) && !m_states.hasMembership(windowId, currentKey);
    if (entry.adopt || !entry.stale.isEmpty()) {
        pending.append(entry);
    }
}

MembershipReconcileResult ScrollEngine::applyMembershipWork(const QString& screenId,
                                                            const PlacementStateKey& currentKey,
                                                            const QList<PendingMembership>& pending)
{
    MembershipReconcileResult result;
    if (pending.isEmpty()) {
        return result;
    }
    bool touchedCurrent = false;
    for (const PendingMembership& entry : pending) {
        for (const PlacementStateKey& stale : entry.stale) {
            releaseMembership(entry.windowId, stale);
            result.released.append({entry.windowId, stale});
            touchedCurrent |= (stale == currentKey);
        }
        if (entry.adopt && adoptIntoContext(entry.windowId, currentKey)) {
            result.adopted.append({entry.windowId, currentKey});
            touchedCurrent = true;
        }
    }
    if (touchedCurrent) {
        // The strip the user is looking at changed shape. Force the batch:
        // the adopted window's rect memory belongs to a DIFFERENT desktop's
        // strip, so the emit-on-change gate cannot be trusted to notice —
        // the same reason the unpin migration arms m_forceEmitScreens.
        m_forceEmitScreens.insert(screenId);
        scheduleRetileForScreen(screenId);
    }
    return result;
}

MembershipReconcileResult ScrollEngine::reconcileDesktopMemberships(const QString& screenId,
                                                                    const PhosphorEngine::DesktopSpanQuery& spanOf)
{
    if (!spanOf || screenId.isEmpty()) {
        return {};
    }
    // Only a screen this engine scrolls adopts (a column granted on a screen
    // in another mode is one no strip ever shows); the RELEASE arm runs
    // regardless, because a window that moved off a desktop of a screen that
    // has since left the scrolling set still has to give that column up.
    // Under RestoreOnly / IgnoreAll a sticky window is floated out of the
    // strip at insertion (see insertOpenedWindow's stickyExcluded arm), so
    // there is no column for it to hold on any desktop and granting one would
    // contradict the setting. A span window ({1,2} shrinking to {1}) is
    // tiled normally under every setting.
    const bool adoptAllowed = m_scrollingScreens.contains(screenId)
        && effectiveStickyWindowHandling(screenId) == PhosphorEngine::StickyWindowHandling::TreatAsNormal;
    const PlacementStateKey currentKey = currentKeyForScreen(screenId);

    // Snapshot first: both arms mutate the membership map, and the adopt arm
    // additionally creates states, so neither may run inside its iteration.
    QList<PendingMembership> pending;
    for (const QString& windowId : m_states.trackedWindowIds()) {
        const QList<PlacementStateKey> held = m_states.membershipsForWindow(windowId);
        const bool onThisScreen = std::any_of(held.cbegin(), held.cend(), [&screenId](const auto& key) {
            return key.screenId == screenId;
        });
        if (!onThisScreen) {
            continue;
        }
        const DesktopSpan span = spanOf(windowId);
        // A sticky window's adopt is what the setting gates; a span window is
        // tiled under every setting on a scrolling screen.
        collectMembershipWork(windowId, screenId, currentKey, span,
                              adoptAllowed || (!span.sticky && m_scrollingScreens.contains(screenId)), pending);
    }
    return applyMembershipWork(screenId, currentKey, pending);
}

MembershipReconcileResult ScrollEngine::reconcileWindowMemberships(const QString& rawWindowId,
                                                                   const PhosphorEngine::DesktopSpanQuery& spanOf)
{
    if (!spanOf || rawWindowId.isEmpty()) {
        return {};
    }
    const QString windowId = canonicalizeForLookup(rawWindowId);
    // A window is on exactly one screen: every membership shares it, and the
    // first one names it. An untracked window has no context to reconcile.
    QString screenId;
    for (const PlacementStateKey& key : m_states.membershipsForWindow(windowId)) {
        if (!key.screenId.isEmpty()) {
            screenId = key.screenId;
            break;
        }
    }
    if (screenId.isEmpty()) {
        return {};
    }
    const DesktopSpan span = spanOf(windowId);
    const bool adoptAllowed = m_scrollingScreens.contains(screenId)
        && (!span.sticky
            || effectiveStickyWindowHandling(screenId) == PhosphorEngine::StickyWindowHandling::TreatAsNormal);
    const PlacementStateKey currentKey = currentKeyForScreen(screenId);
    QList<PendingMembership> pending;
    collectMembershipWork(windowId, screenId, currentKey, span, adoptAllowed, pending);
    return applyMembershipWork(screenId, currentKey, pending);
}

bool ScrollEngine::adoptIntoContext(const QString& windowId, const PlacementStateKey& key)
{
    ScrollState* state = stateForKey(key, true);
    if (!state) {
        return false;
    }
    m_closedFullscreenHolds.remove(windowId); // any re-entry ends the closed-hold answer
    if (state->strip().containsWindow(windowId) || state->isFloating(windowId)) {
        // The state already holds the window without a membership naming it
        // (a leftover of an earlier teardown that dropped the membership and
        // not the tile). The membership is the only thing missing; grant it
        // rather than refusing forever.
        m_states.addMembership(windowId, key);
        qCWarning(lcScrollEngine) << "reconcileDesktopMemberships:" << windowId << "was held on desktop" << key.desktop
                                  << "of" << key.screenId << "without a membership — restoring the membership only";
        return true;
    }
    // Carry the window's state across from a strip that already holds it.
    // The compositor reports the min size once at open, and this window is
    // not opening; and a window the user FLOATED (or minimized, which the
    // daemon models as a float) on the desktop it came from is floating
    // here too: tiling it on the new desktop would give a hidden or
    // deliberately floated window a live column, with the daemon's per-window
    // float mirror still saying "floating".
    int minWidth = 0;
    int minHeight = 0;
    bool sourceFloating = false;
    for (const PlacementStateKey& other : m_states.membershipsForWindow(windowId)) {
        const ScrollState* source = m_states.stateForKey(other);
        if (!source) {
            continue;
        }
        if (source->isFloating(windowId)) {
            sourceFloating = true;
            if (const auto restore = m_floatRestore.constFind(windowId); restore != m_floatRestore.constEnd()) {
                minWidth = restore->minWidth;
                minHeight = restore->minHeight;
            }
            break;
        }
        bool found = false;
        for (const Column& column : source->strip().columns()) {
            for (const Tile& tile : column.tiles) {
                if (tile.windowId == windowId) {
                    minWidth = tile.minWidth;
                    minHeight = tile.minHeight;
                    found = true;
                    break;
                }
            }
            if (found) {
                break;
            }
        }
        if (found) {
            break;
        }
    }
    // Membership BEFORE the insert, matching windowOpened: the insert can
    // emit windowFloatingStateSynced, and a subscriber querying back
    // synchronously must already see the window as this engine's.
    m_states.addMembership(windowId, key);
    if (sourceFloating) {
        // Floating on the desktop it came from, so floating here. The
        // FloatRestore entry and the mode-float marker are per window and
        // already describe it; nothing announces, because the daemon's
        // mirror already says floating.
        state->addFloating(windowId);
        qCInfo(lcScrollEngine) << "reconcileDesktopMemberships: adopted" << windowId << "into desktop" << key.desktop
                               << "of" << key.screenId << "as floating";
        Q_EMIT placementChanged(key.screenId);
        return true;
    }
    // The applied-geometry memos describe the OTHER desktop's strip and are
    // keyed by window alone, so they would gate this context's first batch
    // against a rect that was never on this strip. A window arriving from
    // exactly one other context has never had its memo parked (the switch
    // swap skips single-membership windows), so it is parked under that
    // context now rather than dropped, and the switch back restores it. The
    // retile the caller schedules re-derives this context's own.
    const QList<PlacementStateKey> priorHeld = m_states.membershipsForWindow(windowId);
    if (priorHeld.size() == 2) { // this key was just added; the other is the source
        const PlacementStateKey source = priorHeld.first() == key ? priorHeld.last() : priorHeld.first();
        if (const auto rect = m_lastAppliedRect.constFind(windowId); rect != m_lastAppliedRect.constEnd()) {
            m_contextRectMemory[source].insert(windowId, *rect);
        }
        if (const auto edge = m_parkedScrollEdge.constFind(windowId); edge != m_parkedScrollEdge.constEnd()) {
            m_contextParkedEdge[source].insert(windowId, *edge);
        }
    }
    m_lastAppliedRect.remove(windowId);
    m_parkedScrollEdge.remove(windowId);
    m_lastAppliedWindowedFs.remove(windowId);
    m_lastAppliedMaximizedToEdges.remove(windowId);
    ScrollOpenParams openParams;
    QString displacedTab;
    // An ADOPTION, not an open: the float arms (oversized, rule, sticky
    // handling) already ran for this window on the desktop it came from and
    // answered "tile", and its placement record is cross-session memory that
    // a live window must not consume (the same-app FIFO could hand it a dead
    // sibling's float-back and teleport it).
    if (!insertOpenedWindow(state, windowId, key.screenId, minWidth, minHeight, &openParams, /*migration=*/true,
                            &displacedTab, /*adoption=*/true)) {
        m_states.removeMembership(windowId, key);
        return false;
    }
    qCInfo(lcScrollEngine) << "reconcileDesktopMemberships: adopted" << windowId << "into desktop" << key.desktop
                           << "of" << key.screenId;
    Q_EMIT placementChanged(key.screenId);
    return true;
}

void ScrollEngine::releaseMembership(const QString& windowId, const PlacementStateKey& key)
{
    bool wasFloatingHere = false;
    ScrollState* state = m_states.stateForKey(key);
    if (state) {
        state->strip().removeWindow(windowId, layoutParamsForKey(key));
        wasFloatingHere = state->isFloating(windowId);
        state->removeFloating(windowId);
    }
    m_states.removeMembership(windowId, key);
    forgetContextMemo(key, windowId);
    if (!state) {
        // A membership whose state is gone (torn down earlier in the same
        // sweep) owes no relayout and no dirty mark: there is no strip on
        // that context, and a retile scheduled for a screen that is leaving
        // the set would re-announce the strip it just released.
        return;
    }
    const bool releasedInView = (key == currentKeyForScreen(key.screenId));
    if (releasedInView) {
        // The window-level memos describe the strip the window just left.
        // All four, the set the header keeps together. A BACKGROUND release
        // leaves them alone: they are the window's actual frame and the
        // float-back poison guard reads them (lastManagedRect), and the next
        // entry into a held context replaces them from its parked copy.
        m_lastAppliedRect.remove(windowId);
        m_parkedScrollEdge.remove(windowId);
        m_lastAppliedWindowedFs.remove(windowId);
        m_lastAppliedMaximizedToEdges.remove(windowId);
    }
    // The window's float state is per store; the daemon's mirror is per
    // window. A float that lived only on the released desktop has to be
    // withdrawn from the mirror, or the effect keeps float chrome on a window
    // that is a tile everywhere it still lives. Answered from the membership
    // in view: if the window still floats there, nothing changed.
    bool stillFloating = false;
    for (const PlacementStateKey& other : m_states.membershipsForWindow(windowId)) {
        if (const ScrollState* s = m_states.stateForKey(other); s && s->isFloating(windowId)) {
            stillFloating = true;
            break;
        }
    }
    if (wasFloatingHere && !stillFloating) {
        // A fullscreen hold goes with the float here: the ordinary desktop
        // move takes the effect's own arm (releaseWindowTracking drops its
        // record), so nothing is stranded; a reconcile-only release (an
        // activity move) orphans ANY tracked window, which predates the hold.
        m_floatRestore.remove(windowId);
        m_scrollFloatedWindows.remove(windowId);
        Q_EMIT windowFloatingStateSynced(windowId, false, key.screenId);
    }
    if (releasedInView) {
        m_forceEmitScreens.insert(key.screenId);
        scheduleRetileForScreen(key.screenId);
    }
    // placementChanged is the sole producer of the strip's dirty mark. A
    // release on a background desktop mutates persisted structure just as a
    // close does, and a save landing before that desktop's next batch would
    // otherwise persist the column the window just left.
    Q_EMIT placementChanged(key.screenId);
    qCInfo(lcScrollEngine) << "reconcileDesktopMemberships: released" << windowId << "from desktop" << key.desktop
                           << "of" << key.screenId << "— its desktop span no longer covers it";
}

void ScrollEngine::forgetContextMemo(const PlacementStateKey& key, const QString& windowId)
{
    // find() rather than operator[]: a release must not mint an empty inner
    // map for every context it ever touched.
    if (auto it = m_contextRectMemory.find(key); it != m_contextRectMemory.end()) {
        it->remove(windowId);
        if (it->isEmpty()) {
            m_contextRectMemory.erase(it);
        }
    }
    if (auto it = m_contextParkedEdge.find(key); it != m_contextParkedEdge.end()) {
        it->remove(windowId);
        if (it->isEmpty()) {
            m_contextParkedEdge.erase(it);
        }
    }
}

void ScrollEngine::dropFromOtherContexts(const QString& windowId, const PlacementStateKey& keepKey)
{
    // A window leaving the engine (close, handoff, prune, mode reassignment)
    // or leaving its screen (a cross-output move) leaves EVERY context it
    // held, not the one the caller happened to resolve. The callers all
    // clean the primary state themselves and then drop the memberships in
    // one go; without this the other desktops' strips kept a tile nothing
    // ever reaped — pruneStaleWindows walks tracked ids, which no longer
    // named the window, and adoptIntoContext refused to re-adopt a window
    // its strip already held.
    // The kept key's parked memo goes too. Every caller either takes the
    // window out of keepKey right after (a close, a handoff, a replace onto
    // the destination) or has just added it there from another output, where
    // no parked entry can exist; a parked entry left under it would be
    // restored onto a later re-adoption there as "on screen at the old rect".
    forgetContextMemo(keepKey, windowId);
    for (const PlacementStateKey& key : m_states.membershipsForWindow(windowId)) {
        if (key == keepKey) {
            continue;
        }
        ScrollState* state = m_states.stateForKey(key);
        if (state) {
            state->strip().removeWindow(windowId, layoutParamsForKey(key));
            state->removeFloating(windowId);
        }
        m_states.removeMembership(windowId, key);
        forgetContextMemo(key, windowId);
        if (!state) {
            continue; // torn down already: nothing to reflow or mark
        }
        if (key == currentKeyForScreen(key.screenId)) {
            m_forceEmitScreens.insert(key.screenId);
            scheduleRetileForScreen(key.screenId);
        }
        Q_EMIT placementChanged(key.screenId);
    }
}

void ScrollEngine::swapContextRectMemory(const QString& screenId, const PlacementStateKey& oldKey,
                                         const PlacementStateKey& newKey)
{
    if (oldKey == newKey) {
        return;
    }
    // m_lastAppliedRect and m_parkedScrollEdge are keyed by window alone,
    // and applyLayout derives the park / arrive discriminator from them. A
    // window with a column in several of this screen's strips has a
    // different last-applied rect in each; comparing one desktop's relayout
    // against the rect the other desktop applied read a window parked here
    // but on screen there as ARRIVING and gave it a spurious edge slide. So
    // each context keeps its own copy: the leaving context's memo is parked
    // under its key, and the entering context's is put back (or, with none,
    // the window reads as arriving, which on its first batch there it is).
    //
    // The two arms are independent: a switch that passes THROUGH a desktop
    // the window is not on (1 → 2 → 3 for a window on {1,3}) parks on the way
    // out of 1 and restores on the way into 3, with nothing to do at 2.
    //
    // Parking COPIES; it never clears the window-level entry. That entry is
    // also the float-back poison guard: windowClosed and handoffRelease keep
    // it on purpose so the adaptor can tell a close frame still on the tile
    // rect from a genuine free position (lastManagedRect), and a window on
    // {1,3} closed from desktop 2 has no other copy in reach. The entering
    // arm replaces it when it has something to put back, so a window on
    // both keys still reads its own context's rect.
    // The push names the PHYSICAL output; a subdivided output's strips are
    // keyed by its children, so the filter is samePhysical (plain equality
    // for an unsubdivided one).
    for (const QString& windowId : m_states.trackedWindowIds()) {
        const QList<PlacementStateKey> held = m_states.membershipsForWindow(windowId);
        if (held.isEmpty() || !PhosphorIdentity::VirtualScreenId::samePhysical(held.first().screenId, screenId)) {
            continue;
        }
        if (held.size() >= 2 && held.contains(oldKey)) {
            if (const auto rect = m_lastAppliedRect.constFind(windowId); rect != m_lastAppliedRect.constEnd()) {
                m_contextRectMemory[oldKey].insert(windowId, *rect);
            }
            if (const auto edge = m_parkedScrollEdge.constFind(windowId); edge != m_parkedScrollEdge.constEnd()) {
                m_contextParkedEdge[oldKey].insert(windowId, *edge);
            }
        }
        if (!held.contains(newKey)) {
            continue;
        }
        // Restore the entering context's memo when one is parked. Gated on
        // the membership, not the count: a window that shrank to one
        // membership after its memo was parked still owns that memo, and
        // leaving it parked would restore a stale rect onto whatever later
        // re-adoption made the window multi-membership again.
        if (auto entering = m_contextRectMemory.find(newKey); entering != m_contextRectMemory.end()) {
            if (const auto rect = entering->constFind(windowId); rect != entering->constEnd()) {
                m_lastAppliedRect.insert(windowId, *rect);
                entering->erase(rect);
            } else if (held.size() >= 2) {
                m_lastAppliedRect.remove(windowId); // the other context's rect: arriving here
            }
            if (entering->isEmpty()) {
                m_contextRectMemory.erase(entering);
            }
        } else if (held.size() >= 2) {
            m_lastAppliedRect.remove(windowId);
        }
        if (auto entering = m_contextParkedEdge.find(newKey); entering != m_contextParkedEdge.end()) {
            if (const auto edge = entering->constFind(windowId); edge != entering->constEnd()) {
                m_parkedScrollEdge.insert(windowId, *edge);
                entering->erase(edge);
            } else if (held.size() >= 2) {
                m_parkedScrollEdge.remove(windowId);
            }
            if (entering->isEmpty()) {
                m_contextParkedEdge.erase(entering);
            }
        } else if (held.size() >= 2) {
            m_parkedScrollEdge.remove(windowId);
        }
    }
}

} // namespace PhosphorScrollEngine
