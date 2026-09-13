// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Per-desktop membership: which of a screen's desktops each window holds a
// place in, and the adopt / release pair that maintains it.
//
// The autotile twin of the scroll engine's engine_membership.cpp, and the two
// answer the same question in the same terms. A window present on several
// desktops at once — sticky (all of them) or a span such as {1,2} — used to
// live in exactly one, whichever context was current when it opened; on every
// other desktop it was visible but absent from the layout, overlapping the
// tiles rather than being one. Here it gets a real tile in each desktop it
// spans, so the algorithm places it independently per desktop.
//
// Its own translation unit for the reason the scroll twin gives: this ADDS
// memberships where every path in context.cpp moves or drops them.

#include <PhosphorTileEngine/AutotileEngine.h>

#include <PhosphorTiles/TilingState.h>
// releaseMembership calls through the algorithm's lifecycle hooks, so the
// definition is needed here. AutotileEngine.h only forward-declares it, and a
// unity build hid the omission by pulling it in from a sibling TU.
#include <PhosphorTiles/TilingAlgorithm.h>
#include "tileenginelogging.h"

#include <QScopeGuard>

#include <algorithm>

namespace PhosphorTileEngine {

// The key alias the sibling TUs reach through engine_internal.h.
using PhosphorEngine::DesktopSpan;
using PhosphorEngine::MembershipReconcileResult;
using PhosphorEngine::TilingStateKey;

void AutotileEngine::installContextResolver()
{
    // Teach the state container which key each screen is showing, so a window
    // holding a membership on several desktops resolves to the one in view
    // rather than to whichever it was adopted into first. Capturing `this` is
    // safe: the container is a member and cannot outlive the engine.
    m_states.setContextKeyResolver([this](const QString& screenId) {
        return currentKeyForScreen(screenId);
    });
}

struct AutotileEngine::PendingMembership
{
    QString windowId;
    QList<TilingStateKey> stale;
    bool adopt = false;
};

void AutotileEngine::collectMembershipWork(const QString& windowId, const QString& screenId,
                                           const TilingStateKey& currentKey, const DesktopSpan& span, bool adoptAllowed,
                                           QList<PendingMembership>& pending) const
{
    // An UNKNOWN span (the registry has not stamped a desktop for the window
    // yet) adopts nothing and releases nothing: reading it as "every desktop"
    // put windows into every desktop the user visited.
    if (!span.known) {
        return;
    }
    // A window mid-drag is out of every layout while it stays tracked;
    // adopting it would double it up at the drop. The drop or cancel re-homes
    // it, and the next pass sees it settled.
    if (m_dragInsertPreview && m_dragInsertPreview->windowId == windowId) {
        return;
    }
    const QList<TilingStateKey> held = m_states.membershipsForWindow(windowId);
    PendingMembership entry;
    entry.windowId = windowId;
    const int pinned = m_context.stickyPinnedDesktop(screenId);
    for (const TilingStateKey& key : held) {
        // A membership under the screen's sticky pin is not evidence the
        // window left anything: the pin keys every desktop's state by the
        // pinned desktop, and the engine's own unpin migration moves it
        // (the daemon's per-window reconcile made the same exemption).
        if (key.screenId == screenId && key.desktop != pinned && !span.coversKey(key)) {
            entry.stale.append(key);
        }
    }
    // shouldTileWindow reads the sticky-handling setting (RestoreOnly and
    // IgnoreAll refuse a sticky window outright) and the current-context
    // float, so those two modes grant no membership without a second check
    // that could drift from the first. A MINIMIZED window is refused too: the
    // strict seed and the effect's catch-scan both defer a hidden window, and
    // a tile granted here would hold a layout slot for something the user
    // cannot see. Its unminimize re-announces it, which adopts it then.
    const bool minimized = (m_windowRegistry && m_windowRegistry->minimizedState(windowId).value_or(false))
        || (m_windowTracker && m_windowTracker->isSuspensionFloat(windowId));
    entry.adopt = adoptAllowed && span.coversKey(currentKey) && !m_states.hasMembership(windowId, currentKey)
        && !minimized && shouldTileWindow(windowId);
    if (entry.adopt || !entry.stale.isEmpty()) {
        pending.append(entry);
    }
}

MembershipReconcileResult AutotileEngine::applyMembershipWork(const QString& screenId, const TilingStateKey& currentKey,
                                                              const QList<PendingMembership>& pending)
{
    MembershipReconcileResult result;
    bool touchedCurrent = false;
    for (const PendingMembership& entry : pending) {
        for (const TilingStateKey& stale : entry.stale) {
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
        scheduleRetileForScreen(screenId);
    }
    return result;
}

void AutotileEngine::syncFloatMirrorForContext(const QString& screenId, const TilingStateKey& currentKey)
{
    // Float is per context here and per window in the daemon's mirror and the
    // effect's cache, which latch whatever was last announced. A window
    // floated on one desktop and tiled on another therefore reads "floating"
    // everywhere until told otherwise. The context the screen just entered
    // is the one the mirror should describe, so re-announce it for every
    // window that holds a place on several desktops; single-desktop windows
    // never diverge and are left alone. Passive sync: no geometry moves.
    const PhosphorTiles::TilingState* state = m_states.stateForKey(currentKey);
    if (!state) {
        return;
    }
    for (const QString& windowId : m_states.trackedWindowIds()) {
        if (m_states.membershipsForWindow(windowId).size() < 2 || !m_states.hasMembership(windowId, currentKey)
            || !state->containsWindow(windowId)) {
            continue;
        }
        Q_EMIT windowFloatingStateSynced(windowId, state->isFloating(windowId), screenId);
    }
}

MembershipReconcileResult AutotileEngine::reconcileDesktopMemberships(const QString& screenId,
                                                                      const PhosphorEngine::DesktopSpanQuery& spanOf)
{
    if (!spanOf || screenId.isEmpty()) {
        return {};
    }
    // Only a screen this engine tiles adopts: a tile granted on a screen in
    // another mode would be a slot no layout ever fills. The RELEASE arm runs
    // regardless, because a window that moved off a desktop of a screen that
    // has since left the tiling set still has to give that desktop's tile up.
    const bool adoptAllowed = isAutotileScreen(screenId);
    const TilingStateKey currentKey = currentKeyForScreen(screenId);

    // Snapshot first: both arms mutate the membership map and the adopt arm
    // creates states, so neither may run inside its iteration.
    QList<PendingMembership> pending;
    for (const QString& windowId : m_states.trackedWindowIds()) {
        const QList<TilingStateKey> held = m_states.membershipsForWindow(windowId);
        const bool onThisScreen = std::any_of(held.cbegin(), held.cend(), [&screenId](const auto& key) {
            return key.screenId == screenId;
        });
        if (!onThisScreen) {
            continue;
        }
        collectMembershipWork(windowId, screenId, currentKey, spanOf(windowId), adoptAllowed, pending);
    }
    MembershipReconcileResult result = applyMembershipWork(screenId, currentKey, pending);
    syncFloatMirrorForContext(screenId, currentKey);
    return result;
}

MembershipReconcileResult AutotileEngine::reconcileWindowMemberships(const QString& rawWindowId,
                                                                     const PhosphorEngine::DesktopSpanQuery& spanOf)
{
    if (!spanOf || rawWindowId.isEmpty()) {
        return {};
    }
    const QString windowId = canonicalizeForLookup(rawWindowId);
    // A window is on exactly one screen: every membership shares it, and the
    // first one names it. An untracked window has no context to reconcile.
    QString screenId;
    for (const TilingStateKey& key : m_states.membershipsForWindow(windowId)) {
        if (!key.screenId.isEmpty()) {
            screenId = key.screenId;
            break;
        }
    }
    if (screenId.isEmpty()) {
        return {};
    }
    const TilingStateKey currentKey = currentKeyForScreen(screenId);
    QList<PendingMembership> pending;
    collectMembershipWork(windowId, screenId, currentKey, spanOf(windowId), isAutotileScreen(screenId), pending);
    return applyMembershipWork(screenId, currentKey, pending);
}

bool AutotileEngine::adoptIntoContext(const QString& windowId, const TilingStateKey& key)
{
    if (PhosphorTiles::TilingState* state = m_states.stateForKey(key); state && state->containsWindow(windowId)) {
        // The state already holds the window without a membership naming it
        // (a leftover of an earlier teardown that dropped the membership and
        // not the tile). The membership is the only thing missing; grant it
        // rather than having insertWindow refuse the duplicate forever.
        m_states.addMembership(windowId, key);
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "reconcileDesktopMemberships:" << windowId << "was held on desktop" << key.desktop << "of"
            << key.screenId << "without a membership — restoring the membership only";
        return true;
    }
    // A migration ARRIVAL, not an open. insertWindow's tiers were written for
    // a first open: tier 2 consumes the window's placement record (cross-
    // session memory a live window must not spend, and one whose free
    // geometry would teleport it), and tier 3 re-derives the float verdict
    // from the open-time rule. The arrival marker makes both read the LIVE
    // state instead — the same scoping migrateWindowBetweenKeys uses — and
    // carries the window's float state on the desktop it came from across,
    // so a window the user floated there is floated here rather than given a
    // tile while the daemon's per-window mirror still says "floating".
    const PhosphorTiles::TilingState* primary = m_states.forWindow(windowId);
    const bool wasFloating = primary && primary->isFloating(windowId);
    QScopeGuard clearArrival([this] {
        m_migrationArrival.reset();
    });
    m_migrationArrival = MigrationArrival{windowId, wasFloating};
    // insertWindow ADDS the current key (its tail is an addMembership), so
    // the window's other contexts survive; on a refusal it drops only that
    // key and sweeps the per-window caches only when no context is left.
    if (!insertWindow(windowId, key.screenId)) {
        return false;
    }
    PhosphorTiles::TilingState* state = m_states.stateForKey(key);
    if (state && !state->isFloating(windowId)) {
        // The algorithm's add hook, so a memory algorithm (the dwindle split
        // tree) learns of the arrival the way every other insert site tells
        // it; the remove hook in releaseMembership is its pair.
        notifyAlgorithmWindowAdded(state, key.screenId, windowId);
    }
    // Same passive float-state sync the other insert callers perform, so a
    // window this insert floated does not desync from the daemon until its
    // next add.
    emitInsertFloatStateSync(windowId, key.screenId);
    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "reconcileDesktopMemberships: adopted" << windowId << "into desktop" << key.desktop << "of" << key.screenId
        << (wasFloating ? "as floating" : "as a tile");
    return true;
}

void AutotileEngine::releaseMembership(const QString& windowId, const TilingStateKey& key)
{
    // Per-context teardown, not removeTrackedWindowNoRetile: that drops the
    // window's tracking outright, which would take the desktops its span
    // still covers with it.
    bool mutated = false;
    if (PhosphorTiles::TilingState* state = m_states.stateForKey(key)) {
        if (state->containsWindow(windowId)) {
            // The algorithm's remove hook, so a lifecycle-aware layout keeps
            // its own bookkeeping straight — the same pairing every other
            // removal path makes.
            PhosphorTiles::TilingAlgorithm* algo = effectiveAlgorithm(key.screenId);
            const int index = state->tiledWindows().indexOf(windowId);
            if (algo && algo->supportsLifecycleHooks() && index >= 0) {
                algo->onWindowRemoved(state, index);
            }
            state->removeWindow(windowId);
            mutated = true;
        }
    }
    m_states.removeMembership(windowId, key);
    if (!mutated) {
        // A membership whose state is gone (torn down earlier in the same
        // sweep) or never held the window owes no relayout: there is nothing
        // on that context to reflow, and a retile scheduled for a screen that
        // is leaving the set would re-announce the strip it just released.
        return;
    }
    if (key == currentKeyForScreen(key.screenId)) {
        // A pending post-retile focus naming a window that just left this
        // desktop must not survive to activate it from another desktop.
        purgePendingFocusForWindow(windowId);
    } else {
        // The desktop the window left is not on screen, so nothing retiles
        // it now; the identical-set desktop switch deliberately retiles
        // nothing either, so the hole would stay until the next insert or
        // close there. Remember the context, and retile it when it comes
        // back into view (see m_dirtyBackgroundContexts).
        m_dirtyBackgroundContexts.insert(key);
    }
    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "reconcileDesktopMemberships: released" << windowId << "from desktop" << key.desktop << "of" << key.screenId
        << "— its desktop span no longer covers it";
}

void AutotileEngine::dropFromOtherContexts(const QString& windowId, const TilingStateKey& keepKey)
{
    // A window leaving the engine (close, handoff, prune, mode reassignment)
    // or leaving its screen (a cross-output move) leaves EVERY context it
    // held, not the one the caller happened to resolve. The callers all
    // clean the state they resolved themselves; this takes the window out of
    // the rest, with the same hook pairing, or those states keep a tile
    // nothing reaps — pruneStaleWindows walks tracked ids, which no longer
    // name the window, and the layout goes on reserving a slot for it.
    for (const TilingStateKey& key : m_states.membershipsForWindow(windowId)) {
        if (key == keepKey) {
            continue;
        }
        releaseMembership(windowId, key);
    }
}

void AutotileEngine::retileIfDirtyBackground(const QString& screenId)
{
    // Consumed once per return: the entry is re-armed only by another
    // release on that context.
    if (m_dirtyBackgroundContexts.remove(currentKeyForScreen(screenId))) {
        scheduleRetileForScreen(screenId);
    }
}

} // namespace PhosphorTileEngine
