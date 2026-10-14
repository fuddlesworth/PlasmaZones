// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Context lifetime and reaping: which (screen, desktop, activity) key the
// engine answers for right now, the release of a screen's state on a mode
// reassignment, and every prune that reaps state — by aliveness
// (pruneStaleWindows) or because a context or an output died. Split out of
// engine_core.cpp, which was over its size ceiling; the two halves share
// nothing but the members. The sticky-desktop pin pass lives in
// engine_stickypin.cpp, the desktop renumber in engine_desktopremap.cpp and
// the strip stash's aliveness sweep in engine_stashleave.cpp.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorIdentity/VirtualScreenId.h>

#include "evacueepark_p.h"
#include "scrollenginelogging.h"

namespace PhosphorScrollEngine {

int ScrollEngine::pruneStaleWindows(const QSet<QString>& aliveWindowIds)
{
    int pruned = PlacementEngineBase::pruneStaleWindows(aliveWindowIds);
    // m_lastAppliedRect entries are retained through windowClosed and
    // handoffRelease (poison-guard memory), so THIS sweep is their sole
    // reclaimer — keyed on aliveness, independent of tracking (a window
    // whose key was dropped long ago must still age out here).
    for (auto it = m_lastAppliedRect.begin(); it != m_lastAppliedRect.end();) {
        if (!aliveWindowIds.contains(it.key())) {
            it = m_lastAppliedRect.erase(it);
        } else {
            ++it;
        }
    }
    // The emit-gate memory for the windowed-fullscreen flag ages out the same
    // way; a stale true for a re-used id would only force one redundant emit,
    // but there is no reason to keep dead ids around.
    for (auto it = m_lastAppliedWindowedFs.begin(); it != m_lastAppliedWindowedFs.end();) {
        if (!aliveWindowIds.contains(*it)) {
            it = m_lastAppliedWindowedFs.erase(it);
        } else {
            ++it;
        }
    }
    // Same treatment for the column-maximize leg, which is maintained
    // alongside it everywhere.
    for (auto it = m_lastAppliedMaximizedToEdges.begin(); it != m_lastAppliedMaximizedToEdges.end();) {
        if (!aliveWindowIds.contains(*it)) {
            it = m_lastAppliedMaximizedToEdges.erase(it);
        } else {
            ++it;
        }
    }
    // The remembered park edge is written only while a window sits parked and
    // consumed when it scrolls back on screen. windowClosed drops the entry
    // (with the fs memory above) at close, and every path that takes a
    // still-alive window off the strip (float, handoff,
    // cross-screen move, drag commit/cancel, the context sweeps) drops the
    // edge beside it — so this sweep, which fires exactly ONCE per session
    // at bring-up, is a belt for ids that died while this engine was not
    // listening, not a recurring reclaimer.
    for (auto it = m_parkedScrollEdge.begin(); it != m_parkedScrollEdge.end();) {
        if (!aliveWindowIds.contains(it.key())) {
            it = m_parkedScrollEdge.erase(it);
        } else {
            ++it;
        }
    }
    // The closed-hold memory is a belt for a close the daemon never captured
    // (a lost windowClosed relay); aliveness is its only other reaper.
    m_closedFullscreenHolds.removeIf([&aliveWindowIds](const QString& id) {
        return !aliveWindowIds.contains(id);
    });
    QStringList dead;
    // One entry per window: the teardown below drops every membership, so a
    // multi-desktop window must not appear twice in the dead list.
    for (const QString& trackedId : m_states.trackedWindowIds()) {
        if (!aliveWindowIds.contains(trackedId)) {
            dead.append(trackedId);
        }
    }
    QSet<QString> affectedScreens;
    // Per-CONTEXT params cache: resolving them costs a ScreenManager query plus
    // a context-gap provider invocation, and a batch prune of N dead windows in
    // one context needs it once, not N times.
    //
    // Keyed by the whole key, not by the screen. The gap rules are per (screen,
    // desktop, activity), so a screen-keyed cache would resolve the first
    // context a batch happened to touch and then hand those gaps to windows on
    // that screen's OTHER desktops — and removeWindow re-derives the view
    // anchor from them, which is the stale-anchor error windowClosed's own
    // comment says can survive the desktop return.
    //
    // The params are content-dependent (the smart-gaps arm reads the live
    // column count), so a batch that empties a context down to one column
    // resolves its later removals against the pre-batch gap verdict; the
    // scheduled retile below re-resolves and re-anchors before anything paints,
    // which is the same borrow layoutParamsForKey already documents for a
    // background context.
    QHash<PhosphorEngine::PlacementStateKey, ScrollLayoutParams> paramsByContext;
    for (const QString& windowId : dead) {
        // Before any state mutation, mirroring windowClosed (and autotile's
        // prune): a preview naming a dead id must not survive to re-add or
        // float it at commit/cancel — the prune is the backstop for exactly
        // the windows that died WITHOUT a windowClosed signal.
        dropClosedWindowFromDragPreview(windowId);
        PhosphorEngine::PlacementStateKey key;
        ScrollState* state = stateForWindow(windowId, &key);
        if (state) {
            auto paramsIt = paramsByContext.find(key);
            if (paramsIt == paramsByContext.end()) {
                paramsIt = paramsByContext.insert(key, layoutParamsForKey(key));
            }
            state->strip().removeWindow(windowId, *paramsIt);
            state->removeFloating(windowId);
            affectedScreens.insert(key.screenId);
        }
        // The dead window's OTHER desktops' strips too, or a multi-desktop
        // window leaves a tile nothing reaps in each of them.
        for (const PhosphorEngine::PlacementStateKey& other : m_states.membershipsForWindow(windowId)) {
            if (other != key) {
                affectedScreens.insert(other.screenId);
            }
        }
        dropFromOtherContexts(windowId, key);
        m_states.removeWindow(windowId);
        m_lastAppliedRect.remove(windowId);
        m_parkedScrollEdge.remove(windowId);
        m_floatRestore.remove(windowId);
        m_scrollFloatedWindows.remove(windowId);
        // A dead window's queued self-activation echo can never be answered;
        // left behind it would eat the first genuine focus of a reused id
        // (windowClosed and releaseScreenState sweep for the same reason).
        m_pendingSelfActivations.removeAll(windowId);
        m_pendingSelfActivationQueuedAt.remove(windowId);
        // The declined-open marker with it. This sweep matters more for that
        // map than for the one above: the marker's other three sweeps all hang
        // off a windowClosed signal, and a window that dies WITHOUT one — the
        // case this whole function exists to catch — reaches none of them. It
        // also has no size cap of its own, so nothing else bounds its growth.
        m_declinedOpenFocus.remove(windowId);
        ++pruned;
    }
    // Seed lists are swept against the ids this batch just untracked, NOT
    // against aliveness. A seeded id the engine has never tracked is the
    // normal, load-bearing case: the captured order names windows that have
    // not opened yet, and each one is meant to sit out however many prunes
    // happen before it arrives and claims its column. Sweeping on aliveness
    // instead would reap exactly those pending entries, because a window that
    // has not mapped yet is legitimately absent from the caller's alive set —
    // see pruneStaleWindowsReclaimsRectsAndSeeds, which pins that {a,c} must
    // survive a prune naming only c. So the reaper here is a window that WAS
    // tracked and has now gone, which is what `dead` holds. One sweep for the
    // whole batch — per dead window it would re-walk every screen's list.
    if (!dead.isEmpty()) {
        const QSet<QString> deadSet(dead.cbegin(), dead.cend());
        for (auto seedIt = m_pendingInitialOrder.begin(); seedIt != m_pendingInitialOrder.end();) {
            seedIt->removeIf([&deadSet](const QString& seeded) {
                return deadSet.contains(seeded);
            });
            // Keep the consumed set a subset of the (now shorter) list so
            // the all-consumed drop condition stays exact.
            auto consumedIt = m_consumedInitialOrder.find(seedIt.key());
            if (consumedIt != m_consumedInitialOrder.end()) {
                consumedIt->subtract(deadSet);
            }
            if (seedIt->isEmpty()
                || (consumedIt != m_consumedInitialOrder.end() && consumedIt->size() >= seedIt->size())) {
                m_consumedInitialOrder.remove(seedIt.key());
                seedIt = m_pendingInitialOrder.erase(seedIt);
            } else {
                ++seedIt;
            }
        }
    }

    // The stash on aliveness, and the restart arm for a window moved while the
    // daemon was down: see sweepStripStashForAliveness (engine_stashleave.cpp).
    sweepStripStashForAliveness(aliveWindowIds);
    for (const QString& screenId : affectedScreens) {
        scheduleRetileForScreen(screenId);
        // The strip structure mutated durably (a column may have closed).
        // placementChanged is this engine's only way to mark DirtyScrollStrips;
        // the tracking service's scheduleSaveState (DirtyAll) also sets that
        // bit, but a prune is not a windowClosed whose DirtyAll covers it.
        Q_EMIT placementChanged(screenId);
    }
    return pruned;
}

// ── Desktop / activity context ──────────────────────────────────────────────

void ScrollEngine::setCurrentDesktop(int desktop)
{
    m_isDesktopContextSwitch |= m_context.setCurrentDesktop(desktop).armSwitch;
}

void ScrollEngine::setCurrentDesktopForScreen(const QString& screenId, int desktop)
{
    const PhosphorEngine::PlacementStateKey before = currentKeyForScreen(screenId);
    m_isDesktopContextSwitch |= m_context.setCurrentDesktopForScreen(screenId, desktop).armSwitch;
    swapContextRectMemory(screenId, before, currentKeyForScreen(screenId));
}

void ScrollEngine::clearCurrentDesktopForScreen(const QString& screenId)
{
    m_context.clearCurrentDesktopForScreen(screenId);
}

bool ScrollEngine::migrateStateKey(const PhosphorEngine::PlacementStateKey& oldKey,
                                   const PhosphorEngine::PlacementStateKey& newKey, QStringList& displacedWindows,
                                   QSet<QString>& displacedScreens)
{
    if (oldKey == newKey) {
        return false;
    }
    const QString screenId = oldKey.screenId;
    // A pending background-focus emit armed against the key we are moving off
    // can never match again in this era, and a desktop index KWin later reuses
    // would make it match the WRONG era, so drop it rather than leave it
    // standing. The centering it owed is not lost: every caller arms
    // m_forceEmitScreens for the screens whose announce fires, and a migration
    // that moved the key is exactly that case.
    m_pendingFocusEmitContexts.remove(oldKey);
    // A live preview's captured keys are plain copies that rekeyWindows cannot
    // rewrite — migrating under it would strand the preview on the dead key
    // and commit would then materialise a fresh empty state there. Every
    // sibling context-mutating path unwinds the preview the same way.
    if (m_dragInsertPreview
        && (m_dragInsertPreview->targetKey == oldKey
            || (m_dragInsertPreview->hadPriorState && m_dragInsertPreview->priorKey == oldKey))) {
        cancelDragInsertPreview();
    }
    ScrollState* migrated = m_states.stateForKey(oldKey);
    if (!migrated) {
        return false;
    }
    if (ScrollState* existing = m_states.stateForKey(newKey)) {
        // Normally a placeholder a transient lookup created, and empty. If it
        // is NOT, its windows are real and discarding the state silently would
        // leave them tracked at a key nothing holds, with no windowsReleased
        // for the daemon's restore consumers. Hand them back through the FULL
        // release, matching pruneStatesForRemovedScreen: the placement-record
        // snapshot and the unfloat-slot drop happen inside releaseScreenState,
        // the float markers and last-applied rects survive for the handler,
        // and the emit + final sweep run after the loop.
        if (!existing->managedWindows().isEmpty()) {
            qCWarning(lcScrollEngine) << "updateStickyScreenPins: releasing" << existing->managedWindows().size()
                                      << "window(s) held by the state the unpin migration displaced on" << screenId;
            displacedScreens.insert(screenId);
        }
        // RELEASE FIRST, then unhook. releaseScreenState's placement snapshot
        // goes through capturePlacement -> stateForWindow, which resolves the
        // window's reverse-map key and then looks THAT key up in the forward
        // map. Take the state out first and every one of those lookups misses,
        // so capturePlacement returns nullopt for every displaced window and
        // not one record is written — the exact loss the comment above says
        // this arm prevents. removeStatesIf documents the same ordering
        // ("invoke onRemove BEFORE dropping the entry") for its callers.
        releaseScreenState(existing, displacedWindows);
        m_states.takeState(newKey);
    }
    m_states.takeState(oldKey);
    m_states.insertState(newKey, migrated);
    m_states.rekeyWindows(oldKey, newKey);
    // The per-context rect memos follow the key the same way (move-only-if-
    // vacant, like the stash maps below).
    if (m_contextRectMemory.contains(oldKey) && !m_contextRectMemory.contains(newKey)) {
        m_contextRectMemory.insert(newKey, m_contextRectMemory.take(oldKey));
    }
    m_contextRectMemory.remove(oldKey);
    if (m_contextParkedEdge.contains(oldKey) && !m_contextParkedEdge.contains(newKey)) {
        m_contextParkedEdge.insert(newKey, m_contextParkedEdge.take(oldKey));
    }
    m_contextParkedEdge.remove(oldKey);
    // The stash maps are keyed by context too — left at the old key they
    // become unreachable (no live context resolves it) until a prune reaps
    // them, and a restore for the new key finds nothing. Move-only-if-vacant:
    // the new key can already hold a stash awaiting re-adoption, and
    // clobbering it would lose those pending restores — in that case the
    // old-key entry stays for the prunes, exactly the pre-fix behaviour.
    // "Vacant" means no STRUCTURE: a cursor-only carrier at the new key (an
    // all-floating exit's stash) holds no windows to lose, so a structural
    // stash moves over it and absorbs its cursor with qMax rather than
    // dropping it — and the blueprint IDENTITY that cursor was counting
    // against, when the moved entry hands none over of its own. The two are
    // only meaningful together (StashedStrip::blueprintIdentity): keeping the
    // displaced entry's cursor while dropping its identity leaves the
    // consumption site unable to tell a resumed template from a swapped one.
    if (m_stripStash.contains(oldKey) && m_stripStash.value(newKey).isEmpty()) {
        StashedStrip moved = m_stripStash.take(oldKey);
        const StashedStrip displaced = m_stripStash.value(newKey);
        // Absorb the displaced cursor ONLY when the two cursors are counting
        // against the same blueprint — either the identities match, or the
        // moved entry has none of its own and takes the displaced one below.
        // Two entries with DIFFERENT valid identities are counting against
        // different templates, and a qMax across them would pair this entry's
        // identity with the other's progress, the pairing the paragraph above
        // says must hold.
        const bool sameBlueprint = !moved.blueprintIdentity.isValid() || !displaced.blueprintIdentity.isValid()
            || moved.blueprintIdentity == displaced.blueprintIdentity;
        if (sameBlueprint) {
            moved.blueprintCursor = qMax(moved.blueprintCursor, displaced.blueprintCursor);
        }
        if (!moved.blueprintIdentity.isValid() && displaced.blueprintIdentity.isValid()) {
            moved.blueprintIdentity = displaced.blueprintIdentity;
        }
        // The consumed marker is merged the same way the cursor above was,
        // NOT moved over the top of the destination's. Both sets name ids
        // already restored out of the stash at their key, and the two stashes
        // just became one, so the union is what that one entry has had
        // restored. A plain overwrite dropped the displaced entry's half and a
        // plain move would have been unreachable for it.
        //
        // Intersected with the merged entry's own ids, because
        // restoreFromStripStash drops the pair once the consumed set reaches
        // tileCount(). An id from the displaced entry that names no tile here
        // would inflate the count against a smaller total and retire a stash
        // that still has tiles waiting to be claimed. Every writer of this set
        // keeps it a subset of its stash; the intersect is what preserves that.
        QSet<QString> mergedConsumed = m_stripStashConsumed.take(oldKey);
        mergedConsumed.unite(m_stripStashConsumed.take(newKey));
        if (!mergedConsumed.isEmpty()) {
            QSet<QString> stillStashed;
            for (const StashedColumn& column : std::as_const(moved.columns)) {
                for (const StashedTile& tile : column.tiles) {
                    stillStashed.insert(tile.windowId);
                }
            }
            mergedConsumed.intersect(stillStashed);
        }
        m_stripStash.insert(newKey, moved);
        if (!mergedConsumed.isEmpty()) {
            m_stripStashConsumed.insert(newKey, mergedConsumed);
        }
    }
    // The mid-burst deferred-apply marker is context-keyed too: left at the
    // old key it can never drain (endArrivalBurst resolves live keys), silently
    // dropping the deferred apply and its focusWindowAfter. Same
    // move-only-if-vacant rule as the stash.
    if (m_burstPendingApplies.contains(oldKey) && !m_burstPendingApplies.contains(newKey)) {
        m_burstPendingApplies.insert(newKey, m_burstPendingApplies.take(oldKey));
    }
    // The per-context rule/template overrides move with the state for the
    // same reason: left at the old key the migrated strip resolves no
    // template, no preset vocabulary and no axis override until the daemon's
    // next per-pass push re-seeds them, and its blueprint identity compare
    // meets an empty blueprint in the meantime. Same move-only-if-vacant rule
    // as the stash above — a map already sitting at the new key was resolved
    // FOR that context and outranks the one being migrated into it. An EMPTY
    // map counts as vacant: the daemon pushes {} for every scrolling context
    // that resolves nothing, so presence alone no longer says the context was
    // resolved to anything worth outranking the migrated map.
    if (m_perScreenOverrides.contains(oldKey) && m_perScreenOverrides.value(newKey).isEmpty()) {
        m_perScreenOverrides.insert(newKey, m_perScreenOverrides.take(oldKey));
    }
    return true;
}

void ScrollEngine::finishDisplacedRelease(QStringList& displacedWindows, const QSet<QString>& displacedScreens)
{
    if (!displacedWindows.isEmpty()) {
        // A re-entrant setActiveScreens (see the snapshot note above) may
        // already have released some collected windows through its own
        // teardown; re-announcing those would double-release. Keep only the
        // ids the engine still tracks.
        displacedWindows.removeIf([this](const QString& wid) {
            return !m_states.hasWindow(wid);
        });
    }
    if (!displacedWindows.isEmpty()) {
        // A displaced window leaves the engine, so every context it held on other
        // desktops goes too (removeWindowsIf drops only the memberships).
        for (const QString& wid : std::as_const(displacedWindows)) {
            dropFromOtherContexts(wid, PhosphorEngine::PlacementStateKey{});
        }
        const QSet<QString> displacedSet(displacedWindows.cbegin(), displacedWindows.cend());
        m_states.removeWindowsIf([&displacedSet](const QString& wid, const PhosphorEngine::PlacementStateKey&) {
            return displacedSet.contains(wid);
        });
        announceReleasedFullscreenHolds();
        Q_EMIT windowsReleased(displacedWindows, displacedScreens);
        // Only NOW may the per-window side maps go — the handler above has
        // consumed the float markers and the last-applied rects (same
        // ordering contract as pruneStatesForRemovedScreen).
        for (const QString& windowId : std::as_const(displacedWindows)) {
            m_lastAppliedRect.remove(windowId);
            m_lastAppliedWindowedFs.remove(windowId);
            m_lastAppliedMaximizedToEdges.remove(windowId);
            m_parkedScrollEdge.remove(windowId);
            m_scrollFloatedWindows.remove(windowId);
        }
    } else {
        // Drain even when the re-entrant filter emptied the set: an id this
        // release collected must not carry to an unrelated later one, where
        // the daemon would clear a re-held window's marker.
        announceReleasedFullscreenHolds();
    }
}

void ScrollEngine::setCurrentActivity(const QString& activity)
{
    // Activity is a global axis of every screen's key, so every scrolling
    // screen's context moves at once.
    QHash<QString, PhosphorEngine::PlacementStateKey> before;
    for (const QString& screenId : std::as_const(m_scrollingScreens)) {
        before.insert(screenId, currentKeyForScreen(screenId));
    }
    m_isDesktopContextSwitch |= m_context.setCurrentActivity(activity).armSwitch;
    for (auto it = before.cbegin(); it != before.cend(); ++it) {
        swapContextRectMemory(it.key(), it.value(), currentKeyForScreen(it.key()));
    }
}

QSet<int> ScrollEngine::desktopsWithActiveState() const
{
    QSet<int> desktops;
    const auto& states = m_states.states();
    for (auto it = states.cbegin(); it != states.cend(); ++it) {
        desktops.insert(it.key().desktop);
    }
    return desktops;
}

void ScrollEngine::consumePendingInitialOrder(const QString& screenId, const QString& windowId)
{
    const auto it = m_pendingInitialOrder.find(screenId);
    if (it == m_pendingInitialOrder.end() || !it->contains(windowId)) {
        return;
    }
    // Mark, don't remove: the list's positions are what later arrivals use
    // to count their earlier-arrived neighbours, so shrinking it on each
    // consume would collapse every later insert to column 0 (order
    // reversal). Both entries drop once the whole seed is consumed.
    QSet<QString>& consumed = m_consumedInitialOrder[screenId];
    consumed.insert(windowId);
    if (consumed.size() >= it->size()) {
        m_pendingInitialOrder.erase(it);
        m_consumedInitialOrder.remove(screenId);
    }
}

void ScrollEngine::dropWindowBookkeeping(const PhosphorEngine::PlacementStateKey& key, const ScrollState* state)
{
    // Shared sweep for every state-destruction path: the per-window side
    // maps must die with the state or they grow unbounded and
    // lastManagedRect keeps answering for windows whose context is gone —
    // the float-back poison-guard input (mirrors the autotile prunes'
    // in-callback drops).
    //
    // Per membership: a window that still holds a column on another of
    // this screen's desktops keeps its per-window memos (its unfloat slot,
    // its mode-float marker), because those describe a window that is
    // still live here, not the context that is dying.
    m_contextRectMemory.remove(key);
    m_contextParkedEdge.remove(key);
    const QStringList windows = state->managedWindows();
    for (const QString& windowId : windows) {
        // A membership survives only if its state is still live. In a sweep
        // that reaps several of a window's contexts at once (an activity
        // going away takes every desktop of it), the earlier ones are already
        // out of the forward map by the time the later ones are visited, so
        // a raw membership count would keep the memos of a window with no
        // context left.
        bool survives = false;
        for (const PhosphorEngine::PlacementStateKey& other : m_states.membershipsForWindow(windowId)) {
            if (other != key && m_states.stateForKey(other)) {
                survives = true;
                break;
            }
        }
        if (survives) {
            continue;
        }
        m_lastAppliedRect.remove(windowId);
        m_lastAppliedWindowedFs.remove(windowId);
        m_lastAppliedMaximizedToEdges.remove(windowId);
        m_parkedScrollEdge.remove(windowId);
        rememberReleasedFullscreenHold(windowId); // announced at the end of the silent prune
        m_floatRestore.remove(windowId);
        m_scrollFloatedWindows.remove(windowId);
        // The dying context's windows can never answer their queued echoes;
        // a stale entry would swallow the first genuine focus of a reused
        // id (windowClosed and releaseScreenState sweep the same way).
        m_pendingSelfActivations.removeAll(windowId);
        m_pendingSelfActivationQueuedAt.remove(windowId);
        // Identical reasoning, and the same reused-id hazard: a declined-open
        // marker left behind swallows the first genuine focus the next window
        // to take this id receives. Unlike its sibling above it carries no
        // size cap, so this sweep is the only thing bounding it here.
        m_declinedOpenFocus.remove(windowId);
    }
}

void ScrollEngine::sweepStatelessScreenBookkeeping(const QSet<QString>& screenIds)
{
    // Per-screen (not per-state) bookkeeping outlives an individual context
    // prune only while ANOTHER context still exists for the screen. Once
    // the last state is gone, a stale seed or tab-strip latch would replay
    // against whatever state is built there next. m_perScreenOverrides
    // deliberately survives (the rule/template config re-applies on re-entry,
    // and it is keyed per CONTEXT rather than per screen, so it outlives any
    // one context's state by design). Its purgers are clearPerScreenConfig
    // for a screen leaving scrolling, the context prunes for a destroyed
    // desktop or activity, and pruneStatesForRemovedScreen for a dead output.
    // This reference is held across the synchronous clearTabStripsForScreen
    // emit below, which is safe only because no tabStripsChanged consumer
    // re-enters the engine: the sole production slot is TilingAdaptor's relay,
    // which touches its own cache and re-emits onto D-Bus (the effect reads it
    // out of process). A future IN-PROCESS consumer that inserted or removed a
    // state would change what the remaining passes see, so re-hoist this read
    // inside the loop if one is ever added.
    const auto& states = m_states.states();
    for (const QString& screenId : screenIds) {
        bool hasState = false;
        for (auto it = states.cbegin(); it != states.cend(); ++it) {
            if (it.key().screenId == screenId) {
                hasState = true;
                break;
            }
        }
        if (!hasState) {
            m_pendingInitialOrder.remove(screenId);
            m_consumedInitialOrder.remove(screenId);
            // The focus seed is per-screen bookkeeping of exactly this kind:
            // with the screen's last state gone there is no strip left for it
            // to name, and its only consumer runs per arrival burst, which a
            // stateless screen never reaches.
            m_pendingInitialFocus.remove(screenId);
            clearTabStripsForScreen(screenId);
        }
    }
}

void ScrollEngine::pruneContextKeyedScreenArms(
    const std::function<bool(const PhosphorEngine::PlacementStateKey&)>& contextDied)
{
    // Value-keyed, not key-keyed: the screen is still live and still scrolling,
    // it is the CONTEXT the entry names that has gone.
    for (auto it = m_pendingFocusEmitContexts.begin(); it != m_pendingFocusEmitContexts.end();) {
        it = contextDied(*it) ? m_pendingFocusEmitContexts.erase(it) : std::next(it);
    }
    // This one is context-keyed outright.
    for (auto it = m_burstPendingApplies.begin(); it != m_burstPendingApplies.end();) {
        it = contextDied(it.key()) ? m_burstPendingApplies.erase(it) : std::next(it);
    }
}

void ScrollEngine::pruneStatesForDesktop(int removedDesktop)
{
    // Unwind a preview stranded by the dying context while both its states
    // still exist; cancel's own guards degrade gracefully if the prior
    // context is the one being pruned.
    if (m_dragInsertPreview
        && (m_dragInsertPreview->targetKey.desktop == removedDesktop
            || (m_dragInsertPreview->hadPriorState && m_dragInsertPreview->priorKey.desktop == removedDesktop))) {
        cancelDragInsertPreview();
    }
    QSet<QString> touchedScreens;
    m_states.removeStatesIf(
        [removedDesktop](const PhosphorEngine::PlacementStateKey& key, ScrollState*) {
            return key.desktop == removedDesktop;
        },
        [this, &touchedScreens](const PhosphorEngine::PlacementStateKey& key, ScrollState* state) {
            touchedScreens.insert(key.screenId);
            dropWindowBookkeeping(key, state);
            state->deleteLater();
        });
    m_states.removeWindowsIf([removedDesktop](const QString&, const PhosphorEngine::PlacementStateKey& key) {
        return key.desktop == removedDesktop;
    });
    announceReleasedFullscreenHolds(); // a silently pruned hold still clears the daemon's float bit
    m_context.pruneDesktop(removedDesktop);
    sweepStatelessScreenBookkeeping(touchedScreens);
    sweepStripStash([removedDesktop](const PhosphorEngine::PlacementStateKey& key) {
        return key.desktop == removedDesktop;
    });
    // A strip parked for an output's return on this desktop goes with it: the
    // sweep just took its stash, and its context never comes into view again.
    if (m_evacueePark) {
        m_evacueePark->dropContextsWhere([removedDesktop](const PhosphorEngine::PlacementStateKey& key) {
            return key.desktop == removedDesktop;
        });
    }
    // The override map is keyed per CONTEXT, so a destroyed desktop leaves
    // entries no live context can ever resolve. They are not merely dead
    // weight: KWin renumbers desktops on removal, so the index is handed out
    // again, and the next desktop to take it would resolve the template of the
    // one the user deleted. Same predicate as the state prune above.
    for (auto it = m_perScreenOverrides.begin(); it != m_perScreenOverrides.end();) {
        it = it.key().desktop == removedDesktop ? m_perScreenOverrides.erase(it) : std::next(it);
    }
    // Same renumbering hazard, one level indirect: these two are keyed by
    // SCREEN and carry the context as their value, so a screen that stays
    // scrolling keeps its entry when its desktop dies. The promotion in
    // applyLayout and the burst drain in endArrivalBurst both gate on the
    // stored key equalling the current one, which is exactly the compare a
    // reused desktop index passes — the entry would then force a batch, or
    // apply an arrival focus, for a context from before the deletion.
    pruneContextKeyedScreenArms([removedDesktop](const PhosphorEngine::PlacementStateKey& key) {
        return key.desktop == removedDesktop;
    });
}

void ScrollEngine::pruneStatesForActivities(const QStringList& validActivities)
{
    // Empty is the activity service going away, not every activity removed (F423).
    if (validActivities.isEmpty()) {
        return;
    }
    const auto stale = [&validActivities](const QString& activity) {
        return !activity.isEmpty() && !validActivities.contains(activity);
    };
    // Same preview unwind as pruneStatesForDesktop, on the activity axis.
    if (m_dragInsertPreview
        && (stale(m_dragInsertPreview->targetKey.activity)
            || (m_dragInsertPreview->hadPriorState && stale(m_dragInsertPreview->priorKey.activity)))) {
        cancelDragInsertPreview();
    }
    QSet<QString> touchedScreens;
    m_states.removeStatesIf(
        [&stale](const PhosphorEngine::PlacementStateKey& key, ScrollState*) {
            return stale(key.activity);
        },
        [this, &touchedScreens](const PhosphorEngine::PlacementStateKey& key, ScrollState* state) {
            touchedScreens.insert(key.screenId);
            dropWindowBookkeeping(key, state);
            state->deleteLater();
        });
    m_states.removeWindowsIf([&stale](const QString&, const PhosphorEngine::PlacementStateKey& key) {
        return stale(key.activity);
    });
    announceReleasedFullscreenHolds(); // a silently pruned hold still clears the daemon's float bit
    sweepStatelessScreenBookkeeping(touchedScreens);
    sweepStripStash([&stale](const PhosphorEngine::PlacementStateKey& key) {
        return stale(key.activity);
    });
    // And the parked strips of a removed activity, on the desktop prune's
    // terms: left standing, hasParked keeps answering true for their windows
    // and readoptParked grants a context that never comes into view.
    if (m_evacueePark) {
        m_evacueePark->dropContextsWhere([&stale](const PhosphorEngine::PlacementStateKey& key) {
            return stale(key.activity);
        });
    }
    // Per-context override entries for a deleted activity, on the same terms
    // as the desktop prune's sweep: no live context resolves them again, and
    // an activity id can be reused by a restore from backup.
    for (auto it = m_perScreenOverrides.begin(); it != m_perScreenOverrides.end();) {
        it = stale(it.key().activity) ? m_perScreenOverrides.erase(it) : std::next(it);
    }
    // No m_context call here, unlike the desktop prune's pruneDesktop, and the
    // tracker has no activity analog on purpose: desktops are a PER-OUTPUT
    // dimension it keeps a map of, so a removed one leaves per-screen entries
    // to reap, while the activity dimension is a single current value
    // (ScreenContextTracker's "the activity dimension is always the current
    // activity"). setCurrentActivity replaces it wholesale, so there is no
    // per-screen activity state for a prune to unwind.
    //
    // Context-valued arms, on the same terms as the desktop prune's sweep — see
    // the rationale there.
    pruneContextKeyedScreenArms([&stale](const PhosphorEngine::PlacementStateKey& key) {
        return stale(key.activity);
    });
}

void ScrollEngine::pruneStatesForRemovedScreen(const QString& physicalScreenId)
{
    if (physicalScreenId.isEmpty()) {
        return;
    }
    // Match the physical id and every virtual sub-screen of it. samePhysical
    // strips the "/vs:N" suffix, so "DP-1/vs:0" matches a removed "DP-1"
    // while a prefix-sharing sibling ("DP-10") does not.
    const auto matches = [&physicalScreenId](const QString& screenId) {
        return !screenId.isEmpty() && PhosphorIdentity::VirtualScreenId::samePhysical(screenId, physicalScreenId);
    };
    // Same preview unwind as pruneStatesForDesktop, for a dying output (the
    // virtual sub-screen match included).
    if (m_dragInsertPreview
        && (matches(m_dragInsertPreview->targetScreenId)
            || (m_dragInsertPreview->hadPriorState && matches(m_dragInsertPreview->priorKey.screenId)))) {
        cancelDragInsertPreview();
    }
    QStringList releasedWindows;
    QSet<QString> releasedScreens;
    m_states.removeStatesIf(
        [&matches](const PhosphorEngine::PlacementStateKey& key, ScrollState*) {
            return matches(key.screenId);
        },
        [this, &releasedWindows, &releasedScreens](const PhosphorEngine::PlacementStateKey&, ScrollState* state) {
            releasedScreens.insert(state->screenId());
            // Removed screen: the per-context rule overrides go with the state
            // too — this prune is their documented purger, and releaseScreenState
            // does not touch them because a mode exit must keep them. The
            // removal itself is the standalone sweep further down, which walks
            // every key on the dying output rather than only the contexts that
            // happened to have built a state.
            // Through the FULL release, not a bare bookkeeping drop. The windows
            // are alive (only their output is gone), so the daemon's
            // windowsReleased handler below still reads each one's float marker
            // and last-applied rect; dropping those first left every
            // scroll-floated window without its snap-float clear and slot
            // restore on an unplug, and skipped the placement-record snapshot
            // that carries the scrolling slot across the release.
            // AutotileEngine's twin routes this path through its full teardown
            // for exactly these reasons.
            releaseScreenState(state, releasedWindows);
        });
    m_states.removeWindowsIf([&matches](const QString&, const PhosphorEngine::PlacementStateKey& key) {
        return matches(key.screenId);
    });
    // A readoptParked grant on the dying output goes first: its window is
    // tracked nowhere here, so the park taken before this prune never saw it,
    // and nothing could claim it back. The stash it kept parked goes with it.
    if (m_evacueePark) {
        m_evacueePark->dropGrantsWhere(QString(), [&matches](const PhosphorEngine::PlacementStateKey& key) {
            return matches(key.screenId);
        });
    }
    sweepStripStash([this, &matches](const PhosphorEngine::PlacementStateKey& key) {
        return matches(key.screenId) && !isEvacueeParkedKey(key); // a parked strip outlives the output
    });
    // Standalone sweep for STATELESS sub-screens too: a virtual sub-screen
    // of the removed monitor can carry a seed or a rule override without
    // ever having built a state (the daemon applies overrides before
    // setActiveScreens), and the per-state callback above never sees it.
    // This prune is the documented purger of overrides, so the gap would
    // defeat its own contract.
    for (auto it = m_pendingInitialOrder.begin(); it != m_pendingInitialOrder.end();) {
        if (matches(it.key())) {
            m_consumedInitialOrder.remove(it.key());
            it = m_pendingInitialOrder.erase(it);
        } else {
            ++it;
        }
    }
    // The focus seed rides the same stateless sweep, and for the same reason:
    // the daemon can seed a screen that never built a state, and with the
    // output gone the seed can only ever name a window on a monitor that is
    // no longer there.
    for (auto it = m_pendingInitialFocus.begin(); it != m_pendingInitialFocus.end();) {
        if (matches(it.key())) {
            it = m_pendingInitialFocus.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_perScreenOverrides.begin(); it != m_perScreenOverrides.end();) {
        if (matches(it.key().screenId)) {
            it = m_perScreenOverrides.erase(it);
        } else {
            ++it;
        }
    }
    // Tab-strip teardown for every matching screen — the third step of the
    // sweepStatelessScreenBookkeeping helper this function open-codes, and
    // the one both sibling prunes get via that helper. Without it the dead
    // screen never receives its "[]" tabStripsChanged, so the departed screen
    // stays in TilingAdaptor::m_lastScrollTabStrips — the replay cache the
    // KWin effect reads on bring-up — and the effect would paint pills for an
    // output that is gone. Snapshot the matching ids first:
    // clearTabStripsForScreen mutates the set.
    const QSet<QString> tabStripScreens = m_screensWithTabStrips;
    for (const QString& stripScreen : tabStripScreens) {
        if (matches(stripScreen)) {
            clearTabStripsForScreen(stripScreen);
        }
    }
    // The payload cache can hold matching screens the latch set no longer
    // names; sweep those too so nothing keyed on the dead output survives.
    for (auto it = m_lastTabStripPayload.begin(); it != m_lastTabStripPayload.end();) {
        it = matches(it.key()) ? m_lastTabStripPayload.erase(it) : std::next(it);
    }
    // Strip identity goes with the output. setActiveScreens drops both of
    // these for a screen leaving the scrolling SET; an output being removed
    // outright bypasses that path entirely, so without this sweep a monitor
    // unplugged and plugged back in returns with its epoch still recorded as
    // announced, and the re-entry announcement is suppressed as unchanged.
    for (auto it = m_announcedStripEpoch.begin(); it != m_announcedStripEpoch.end();) {
        it = matches(it.key()) ? m_announcedStripEpoch.erase(it) : std::next(it);
    }
    for (auto it = m_forceEmitScreens.begin(); it != m_forceEmitScreens.end();) {
        it = matches(*it) ? m_forceEmitScreens.erase(it) : std::next(it);
    }
    for (auto it = m_pendingFocusEmitContexts.begin(); it != m_pendingFocusEmitContexts.end();) {
        it = matches(it->screenId) ? m_pendingFocusEmitContexts.erase(it) : std::next(it);
    }
    m_context.removeScreensIf(matches);
    // Drop the dead output from the active set and the deferred-apply queue
    // too: until the daemon's next setActiveScreens, isActiveOnScreen would
    // otherwise keep answering true for it and stateForKey(create) would
    // happily re-materialise a state for a screen that no longer exists.
    for (auto it = m_scrollingScreens.begin(); it != m_scrollingScreens.end();) {
        it = matches(*it) ? m_scrollingScreens.erase(it) : std::next(it);
    }
    for (auto it = m_burstPendingApplies.begin(); it != m_burstPendingApplies.end();) {
        it = matches(it.key().screenId) ? m_burstPendingApplies.erase(it) : std::next(it);
    }
    // Retile queue, completing the per-screen sweep.
    for (auto it = m_pendingRetiles.begin(); it != m_pendingRetiles.end();) {
        it = matches(*it) ? m_pendingRetiles.erase(it) : std::next(it);
    }
    // A dead screen id must not keep feeding the hint-less shortcut paths
    // (autotile's twin clears the same way).
    if (matches(m_activeScreen)) {
        m_activeScreen.clear();
    }
    if (!releasedWindows.isEmpty()) {
        announceReleasedFullscreenHolds();
        Q_EMIT windowsReleased(releasedWindows, releasedScreens);
    }
    // Only NOW may the per-window side maps go: the handler above has consumed
    // the float markers and the last-applied rects, and nothing else answers for
    // a departed screen's windows (releasedWindows is releaseScreenState's own list).
    for (const QString& windowId : std::as_const(releasedWindows)) {
        m_lastAppliedRect.remove(windowId);
        m_lastAppliedWindowedFs.remove(windowId);
        m_lastAppliedMaximizedToEdges.remove(windowId);
        m_parkedScrollEdge.remove(windowId);
        m_floatRestore.remove(windowId);
        m_scrollFloatedWindows.remove(windowId);
    }
}

void ScrollEngine::releaseScreenState(ScrollState* state, QStringList& releasedWindows)
{
    const QString screenId = state->screenId();
    const QStringList windows = state->managedWindows();
    // Snapshot each window's scrolling slot into the unified record BEFORE the
    // state is torn down — the record is the single source of truth for
    // cross-mode state, and stashStripStructure covers only the TILED
    // structure, so without this a window floated in scrolling loses its
    // floating slot across a mode round trip and comes back tiled with a stale
    // slot.order. AutotileEngine::releaseScreenStateForTeardown does the same.
    if (m_windowTracker) {
        for (const QString& windowId : windows) {
            if (auto record = capturePlacement(windowId)) {
                m_windowTracker->placementStore().record(*record);
            }
        }
    }
    // Only the unfloat-slot memory dies here. The float markers and the
    // last-applied rects are inputs to the daemon's windowsReleased handler,
    // which has not run yet — see the contract on the declaration.
    // The pending self-activation entries and the declined-open marks go too,
    // for windowClosed's reason: a released window's echo can never be answered
    // in another mode, and a stale entry would eat the first genuine focus
    // report on the way back. The parked-edge and windowed-fullscreen memories
    // go for eviction symmetry: neither feeds windowsReleased, and nothing can
    // sweep them later. A fullscreen HOLD is remembered for
    // announceReleasedFullscreenHolds, since its slot dies here.
    for (const QString& windowId : windows) {
        rememberReleasedFullscreenHold(windowId);
        m_floatRestore.remove(windowId);
        m_pendingSelfActivations.removeAll(windowId);
        m_pendingSelfActivationQueuedAt.remove(windowId);
        m_declinedOpenFocus.remove(windowId);
        m_parkedScrollEdge.remove(windowId);
        m_lastAppliedWindowedFs.remove(windowId);
        m_lastAppliedMaximizedToEdges.remove(windowId);
    }
    releasedWindows.append(windows);
    // Per-screen bookkeeping dies with the state: a stale seed must not
    // replay on re-entry, and the tab-strip overlay must be told to clear —
    // no relayout will ever run for a departed screen to do it.
    m_pendingInitialOrder.remove(screenId);
    m_consumedInitialOrder.remove(screenId);
    // Same reasoning for the focus seed: it is scoped to the transition that
    // captured it, so a state teardown ends its validity whether or not any
    // burst consumed it.
    m_pendingInitialFocus.remove(screenId);
    // Latch and payload cleared inline (plain containers, safe), but the
    // broadcast is DEFERRED: this function runs from inside
    // PerScreenStates::removeStatesIf's iteration over m_states, and a
    // consumer slot that touched the engine's state map synchronously would
    // invalidate the live iterator. All eight clearTabStripsForScreen call
    // sites are outside the state map's own iteration and emit directly.
    m_lastTabStripPayload.remove(screenId);
    if (m_screensWithTabStrips.remove(screenId)) {
        QMetaObject::invokeMethod(
            this,
            [this, screenId]() {
                // Re-checked at DELIVERY time. If the screen re-acquired a
                // strip between the inline latch clear and this callback (it
                // left scrolling and came back in the same daemon pass, then
                // any synchronous applyLayout ran ahead of the event queue),
                // applyLayout has already re-emitted the live payload and
                // re-set the latch. Firing the stale "[]" last would then be
                // final: m_lastTabStripPayload still holds the live payload,
                // so applyLayout's emit-on-change gate suppresses every
                // re-emit and the tab-strip indicator stays missing until the
                // payload genuinely changes.
                if (!m_screensWithTabStrips.contains(screenId)) {
                    Q_EMIT tabStripsChanged(screenId, QStringLiteral("[]"));
                }
            },
            Qt::QueuedConnection);
    }
    state->deleteLater();
}

} // namespace PhosphorScrollEngine
