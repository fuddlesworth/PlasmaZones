// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Context lifetime and reaping: which (screen, desktop, activity) key the
// engine answers for right now, the sticky-desktop pin that keeps an
// all-sticky screen's strip reachable across a switch, and every prune that
// reaps state — by aliveness (pruneStaleWindows) or because a context or an
// output died. Split out of engine_core.cpp, which was over its size ceiling;
// the two halves share nothing but the members.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorIdentity/VirtualScreenId.h>

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
    // (with the fs memory above) at close, and every path that drops
    // m_lastAppliedRect for a still-alive window (float, handoff,
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
    QStringList dead;
    const auto& windowKeys = m_states.windowKeys();
    for (auto it = windowKeys.cbegin(); it != windowKeys.cend(); ++it) {
        if (!aliveWindowIds.contains(it.key())) {
            dead.append(it.key());
        }
    }
    QSet<QString> affectedScreens;
    // Per-screen params cache: layoutParamsForScreen costs a ScreenManager
    // query plus a context-gap provider invocation, and a batch prune of N
    // dead windows on one screen needs it once, not N times. The params are
    // content-dependent (the smart-gaps arm reads the live column count), so a
    // batch that empties a screen down to one column resolves its later
    // removals against the pre-batch gap verdict; the scheduled retile below
    // re-resolves and re-anchors before anything paints, which is the same
    // borrow layoutParamsForScreen already documents for a background context.
    QHash<QString, ScrollLayoutParams> paramsByScreen;
    for (const QString& windowId : dead) {
        // Before any state mutation, mirroring windowClosed (and autotile's
        // prune): a preview naming a dead id must not survive to re-add or
        // float it at commit/cancel — the prune is the backstop for exactly
        // the windows that died WITHOUT a windowClosed signal.
        dropClosedWindowFromDragPreview(windowId);
        PhosphorEngine::PlacementStateKey key;
        ScrollState* state = stateForWindow(windowId, &key);
        if (state) {
            auto paramsIt = paramsByScreen.find(key.screenId);
            if (paramsIt == paramsByScreen.end()) {
                paramsIt = paramsByScreen.insert(key.screenId, layoutParamsForScreen(key.screenId));
            }
            state->strip().removeWindow(windowId, *paramsIt);
            state->removeFloating(windowId);
            affectedScreens.insert(key.screenId);
        }
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

    // The strip stash needs the same treatment, but keyed on ALIVENESS rather
    // than on `dead` — same reasoning as the m_lastAppliedRect sweep above.
    // A stash tile is unreachable by the `dead` loop above, which only walks
    // the seed lists. (Not because a stashed window is never tracked — one
    // stashed for context K1 can be live and tracked in a different context
    // K2, and would then appear in `dead`. That duplication is exactly what
    // serializeStripState's live-wins filter exists for.) And the stash is
    // keyed by CONTEXT, so the existing
    // sweepStripStash calls (desktop, activity and output prunes) never reach
    // a stash whose context is still perfectly live. Between the two, a window
    // that closes while its screen sits in another mode was reachable by
    // nothing: its tile could never satisfy the all-consumed drop condition,
    // so the entry was immortal — re-walked on every later open for that
    // context, written out by serializeStripState, and, through the
    // cross-session appId claim, able to hand an unrelated same-app window the
    // dead tile's slot, width and display.
    //
    // The empty-alive-set bail below protects the STASH specifically — the
    // one collection that cannot be rebuilt once wiped. It is a second,
    // narrower belt: the boundary that actually refuses an empty alive set
    // is the adaptor (WindowTrackingAdaptor::pruneStaleWindows returns
    // before reaching this function), so in production the destructive dead
    // loop above never runs against an empty set. An embedder calling this
    // exported API directly with an empty set WOULD tear down every strip
    // while the stash survived; hoisting the bail to cover the dead loop is
    // a public-API behaviour change deliberately not taken here.
    //
    // A tile staged straight from the persisted blob is EXEMPT until it is
    // claimed. Its id belongs to last session, so no alive set can contain it
    // and the sweep would read it as closed — wiping the structure/focus/
    // anchor restore on the very first prune after login, since the effect
    // fires one at bringup right after the daemon stages it. The exemption is
    // PER TILE and lives in the removeIf below: an entry-wide lift on the
    // first claim let this prune erase an unclaimed co-tenant's slot, which is
    // precisely the case StashedTile::unclaimedSessions exists to lease out
    // gradually instead. See StashedTile::stagedFromPersistence.
    if (!aliveWindowIds.isEmpty()) {
        for (auto stashIt = m_stripStash.begin(); stashIt != m_stripStash.end();) {
            // Captured before the sweep below can empty the entry, because the
            // zero-tile reap needs to tell two look-alike shapes apart: an
            // entry BORN cursor-only (stashStripStructure's all-floated arm,
            // which never had columns) is a live carrier and must survive to
            // the arrival that consumes it, while an entry that DECAYED to
            // zero tiles here is one whose windows are demonstrably gone —
            // that context comes back to an empty strip and is supposed to
            // restart its blueprint, so retaining its cursor would be the bug.
            const bool bornCursorOnly = stashIt->columns.isEmpty();
            // Dead tiles are erased outright, consumed ones included. Retaining
            // a consumed-then-closed tile to protect restoreFromStripStash's
            // positional math is NOT necessary: both of its counters test LIVE
            // STRIP membership (columnOfWindow / indexOfWindow), so a dead tile
            // answers -1 and contributes 0 either way — erasing it removes a
            // zero-contribution entry and decrements colIdx by the same one.
            // Retaining them only inflates `remaining` below, which is what
            // keeps an entry from ever reaching the all-consumed drop.
            for (auto colIt = stashIt->columns.begin(); colIt != stashIt->columns.end();) {
                colIt->tiles.removeIf([&aliveWindowIds](const StashedTile& tile) {
                    // The per-tile persistence exemption, and the ONLY one:
                    // an unclaimed staged tile names last session's id, which
                    // no alive set can hold.
                    return !tile.stagedFromPersistence && !aliveWindowIds.contains(tile.windowId);
                });
                colIt = colIt->tiles.isEmpty() ? stashIt->columns.erase(colIt) : std::next(colIt);
            }
            if (!stashIt->focusedWindowId.isEmpty()) {
                // The focus is dropped only when NO surviving tile still
                // names it — an id that is merely absent from the alive set
                // may belong to a staged tile the sweep just exempted.
                bool focusSurvives = false;
                for (const StashedColumn& col : std::as_const(stashIt->columns)) {
                    for (const StashedTile& tile : col.tiles) {
                        if (tile.windowId == stashIt->focusedWindowId) {
                            focusSurvives = true;
                            break;
                        }
                    }
                    if (focusSurvives) {
                        break;
                    }
                }
                if (!focusSurvives) {
                    stashIt->focusedWindowId.clear();
                }
            }
            // Keep the consumed set a subset of the (now shorter) tile list so
            // the all-consumed drop condition below stays exact.
            auto stashConsumedIt = m_stripStashConsumed.find(stashIt.key());
            if (stashConsumedIt != m_stripStashConsumed.end()) {
                stashConsumedIt->removeIf([&aliveWindowIds](const QString& consumed) {
                    return !aliveWindowIds.contains(consumed);
                });
            }
            const int remaining = stashIt->tileCount();
            // A cursor-only carrier has no tiles by construction, so the
            // zero-tile arm would reap it on the bring-up prune the effect
            // fires before any window has announced — retiring the entry
            // before the arrival it exists for. Scoped to entries that were
            // ALREADY column-less on entry AND actually carry a cursor;
            // everything else reaps exactly as before.
            const bool cursorOnlyCarrier = bornCursorOnly && stashIt->blueprintCursor > 0;
            if (!cursorOnlyCarrier
                && (remaining == 0
                    || (stashConsumedIt != m_stripStashConsumed.end() && stashConsumedIt->size() >= remaining))) {
                m_stripStashConsumed.remove(stashIt.key());
                stashIt = m_stripStash.erase(stashIt);
            } else {
                ++stashIt;
            }
        }
    }
    for (const QString& screenId : affectedScreens) {
        scheduleRetileForScreen(screenId);
        // The strip structure mutated durably (a column may have closed) and
        // placementChanged is the sole producer of DirtyScrollStrips — the
        // prune path is exactly the no-windowClosed case, so nothing else
        // marks the save.
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
    m_isDesktopContextSwitch |= m_context.setCurrentDesktopForScreen(screenId, desktop).armSwitch;
}

void ScrollEngine::clearCurrentDesktopForScreen(const QString& screenId)
{
    m_context.clearCurrentDesktopForScreen(screenId);
}

void ScrollEngine::updateStickyScreenPins(const std::function<bool(const QString&)>& isWindowSticky)
{
    // Windows displaced by an unpin migration, collected across the loop and
    // released AFTER it: windowsReleased is a synchronous signal, and a slot
    // that re-entered the engine (setActiveScreens) from inside the
    // m_scrollingScreens iteration would invalidate the live iterator.
    QStringList displacedWindows;
    QSet<QString> displacedScreens;
    // Screens whose resolved context key MOVED because a pin was released.
    // Collected here and announced after the loop for the same reason the
    // releases are: announceStripContextIfChanged emits synchronously, and a
    // consumer that re-entered the engine from inside this iteration would
    // invalidate the snapshot's premise.
    QSet<QString> contextChangedScreens;
    // Iterate a SNAPSHOT, not the member: the unpin-migration arm cancels a
    // live drag-insert preview, whose synchronous placementChanged reaches
    // the daemon's tiled-count gate and can re-enter setActiveScreens, which
    // REASSIGNS m_scrollingScreens mid-loop. QSet is implicitly shared, so
    // the copy is O(1) and keeps the iterated node hash alive. The per-
    // iteration membership re-check skips a screen such a re-entrant pass
    // removed, instead of migrating it into a torn-down state.
    const QSet<QString> scrollingSnapshot = m_scrollingScreens;
    for (const QString& screenId : scrollingSnapshot) {
        if (!m_scrollingScreens.contains(screenId)) {
            continue;
        }
        const PhosphorEngine::PlacementStateKey key = currentKeyForScreen(screenId);
        ScrollState* state = m_states.stateForKey(key);
        if (!state) {
            continue;
        }
        const QStringList managed = state->managedWindows();
        if (managed.isEmpty()) {
            continue;
        }
        bool allSticky = true;
        for (const QString& wid : managed) {
            if (!isWindowSticky(wid)) {
                allSticky = false;
                break;
            }
        }
        if (allSticky) {
            if (!m_context.hasStickyPin(screenId)) {
                m_context.setStickyPin(screenId, key.desktop);
                qCInfo(lcScrollEngine) << "Pinning screen" << screenId << "to desktop" << key.desktop << "(all"
                                       << managed.size() << "windows sticky)";
            }
        } else if (m_context.hasStickyPin(screenId)) {
            const int pinnedDesktop = m_context.takeStickyPin(screenId);
            qCInfo(lcScrollEngine) << "Unpinning screen" << screenId << "from desktop" << pinnedDesktop;
            // Migrate the strip to the screen's CURRENT desktop key (the pin
            // was just removed, so currentKeyForScreen resolves the true
            // effective desktop). Same terms as AutotileEngine: the pinned
            // state holds the actual windows, so a placeholder created at
            // the target key by a transient lookup is discarded.
            const PhosphorEngine::PlacementStateKey newKey = currentKeyForScreen(screenId);
            if (pinnedDesktop != newKey.desktop) {
                // The strip under this screen has just been replaced, and
                // nothing else will say so: this path does not go through
                // setActiveScreens, which is where the only other announce
                // lives. Gated on the KEY moving rather than on the migration
                // below finding a state, because the epoch is derived from the
                // key — a screen whose old-key state was absent has still
                // changed strips and its consumer still has to be told.
                //
                // The acquire arm above deliberately does NOT announce: it
                // pins the screen to the desktop currentKeyForScreen already
                // resolves, so the key does not move and the announcement
                // would be a guaranteed no-op. A pin only changes what the key
                // resolves to LATER, on a switch, and the announce there
                // correctly stays silent for a pinned screen.
                contextChangedScreens.insert(screenId);
                const PhosphorEngine::PlacementStateKey oldKey{screenId, pinnedDesktop, m_context.currentActivity()};
                // A live preview's captured keys are plain copies that
                // rekeyWindows cannot rewrite — migrating under it would
                // strand the preview on the dead key and commit would then
                // materialise a fresh empty state there. Every sibling
                // context-mutating path unwinds the preview the same way.
                if (m_dragInsertPreview
                    && (m_dragInsertPreview->targetKey == oldKey
                        || (m_dragInsertPreview->hadPriorState && m_dragInsertPreview->priorKey == oldKey))) {
                    cancelDragInsertPreview();
                }
                if (ScrollState* migrated = m_states.stateForKey(oldKey)) {
                    if (ScrollState* existing = m_states.stateForKey(newKey)) {
                        // Normally a placeholder a transient lookup created,
                        // and empty. If it is NOT, its windows are real and
                        // discarding the state silently would leave them
                        // tracked at a key nothing holds, with no
                        // windowsReleased for the daemon's restore consumers.
                        // Hand them back through the FULL release, matching
                        // pruneStatesForRemovedScreen: the placement-record
                        // snapshot and the unfloat-slot drop happen inside
                        // releaseScreenState, the float markers and
                        // last-applied rects survive for the handler, and the
                        // emit + final sweep run after the loop.
                        if (!existing->managedWindows().isEmpty()) {
                            qCWarning(lcScrollEngine)
                                << "updateStickyScreenPins: releasing" << existing->managedWindows().size()
                                << "window(s) held by the state the unpin migration displaced on" << screenId;
                            displacedScreens.insert(screenId);
                        }
                        // RELEASE FIRST, then unhook. releaseScreenState's
                        // placement snapshot goes through capturePlacement ->
                        // stateForWindow, which resolves the window's reverse-map
                        // key and then looks THAT key up in the forward map. Take
                        // the state out first and every one of those lookups
                        // misses, so capturePlacement returns nullopt for every
                        // displaced window and not one record is written — the
                        // exact loss the comment above says this arm prevents.
                        // removeStatesIf documents the same ordering ("invoke
                        // onRemove BEFORE dropping the entry") for its callers.
                        releaseScreenState(existing, displacedWindows);
                        m_states.takeState(newKey);
                    }
                    m_states.takeState(oldKey);
                    m_states.insertState(newKey, migrated);
                    m_states.rekeyWindows(oldKey, newKey);
                    // The stash maps are keyed by context too — left at the
                    // old key they become unreachable (no live context
                    // resolves it) until a prune reaps them, and a restore
                    // for the new key finds nothing. Move-only-if-vacant:
                    // the new key can already hold a stash awaiting
                    // re-adoption, and clobbering it would lose those
                    // pending restores — in that case the old-key entry
                    // stays for the prunes, exactly the pre-fix behaviour.
                    // "Vacant" means no STRUCTURE: a cursor-only carrier at
                    // the new key (an all-floating exit's stash) holds no
                    // windows to lose, so a structural stash moves over it and
                    // absorbs its cursor with qMax rather than dropping it —
                    // and the blueprint IDENTITY that cursor was counting
                    // against, when the moved entry hands none over of its own.
                    // The two are only meaningful together (StashedStrip::
                    // blueprintIdentity): keeping the displaced entry's cursor
                    // while dropping its identity leaves the consumption site
                    // unable to tell a resumed template from a swapped one.
                    if (m_stripStash.contains(oldKey) && m_stripStash.value(newKey).isEmpty()) {
                        StashedStrip moved = m_stripStash.take(oldKey);
                        const StashedStrip displaced = m_stripStash.value(newKey);
                        moved.blueprintCursor = qMax(moved.blueprintCursor, displaced.blueprintCursor);
                        if (!moved.blueprintIdentity.isValid() && displaced.blueprintIdentity.isValid()) {
                            moved.blueprintIdentity = displaced.blueprintIdentity;
                        }
                        m_stripStash.insert(newKey, moved);
                        if (m_stripStashConsumed.contains(oldKey)) {
                            m_stripStashConsumed.insert(newKey, m_stripStashConsumed.take(oldKey));
                        }
                    }
                    // The mid-burst deferred-apply marker is context-keyed too:
                    // left at the old key it can never drain (endArrivalBurst
                    // resolves live keys), silently dropping the deferred apply
                    // and its focusWindowAfter. Same move-only-if-vacant rule
                    // as the stash.
                    if (m_burstPendingApplies.contains(oldKey) && !m_burstPendingApplies.contains(newKey)) {
                        m_burstPendingApplies.insert(newKey, m_burstPendingApplies.take(oldKey));
                    }
                    // The per-context rule/template overrides move with the
                    // state for the same reason: left at the old key the
                    // migrated strip resolves no template, no preset
                    // vocabulary and no axis override until the daemon's next
                    // per-pass push re-seeds them, and its blueprint identity
                    // compare meets an empty blueprint in the meantime. Same
                    // move-only-if-vacant rule as the stash above — a map
                    // already sitting at the new key was resolved FOR that
                    // context and outranks the one being migrated into it.
                    // An EMPTY map counts as vacant: the daemon pushes {} for
                    // every scrolling context that resolves nothing, so
                    // presence alone no longer says the context was resolved
                    // to anything worth outranking the migrated map.
                    if (m_perScreenOverrides.contains(oldKey) && m_perScreenOverrides.value(newKey).isEmpty()) {
                        m_perScreenOverrides.insert(newKey, m_perScreenOverrides.take(oldKey));
                    }
                    qCInfo(lcScrollEngine) << "Migrated screen" << screenId << "strip from desktop" << pinnedDesktop
                                           << "to" << newKey.desktop;
                }
            }
        }
    }
    if (!displacedWindows.isEmpty()) {
        // A re-entrant setActiveScreens (see the snapshot note above) may
        // already have released some collected windows through its own
        // teardown; re-announcing those would double-release. Keep only the
        // ids the engine still tracks.
        displacedWindows.removeIf([this](const QString& wid) {
            return !m_states.windowKeys().contains(wid);
        });
    }
    if (!displacedWindows.isEmpty()) {
        const QSet<QString> displacedSet(displacedWindows.cbegin(), displacedWindows.cend());
        m_states.removeWindowsIf([&displacedSet](const QString& wid, const PhosphorEngine::PlacementStateKey&) {
            return displacedSet.contains(wid);
        });
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
    }
    // Strip identity last, AFTER the release above, so a consumer sees the
    // windows leave before it is told the screen is showing a different strip.
    // Re-check membership: a re-entrant setActiveScreens may have taken the
    // screen out of the set since the loop collected it, and the announce is
    // emit-on-change anyway, so a key that ended up where it started is free.
    for (const QString& screenId : std::as_const(contextChangedScreens)) {
        if (!m_scrollingScreens.contains(screenId)) {
            continue;
        }
        // Arm and retile with the announcement, the same pairing
        // setActiveScreens makes: the consumer has just been told to retire
        // this screen's strip-scoped state, so something has to carry the
        // batch that repopulates it. The migration moved the strip to a
        // context whose rects may already match what was last applied, which
        // is precisely the case the emit-on-change gate would suppress.
        if (announceStripContextIfChanged(screenId)) {
            m_forceEmitScreens.insert(screenId);
            scheduleRetileForScreen(screenId);
        }
    }
}

void ScrollEngine::setCurrentActivity(const QString& activity)
{
    m_isDesktopContextSwitch |= m_context.setCurrentActivity(activity).armSwitch;
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

void ScrollEngine::dropWindowBookkeeping(const ScrollState* state)
{
    // Shared sweep for every state-destruction path: the per-window side
    // maps must die with the state or they grow unbounded and
    // lastManagedRect keeps answering for windows whose context is gone —
    // the float-back poison-guard input (mirrors the autotile prunes'
    // in-callback drops).
    const QStringList windows = state->managedWindows();
    for (const QString& windowId : windows) {
        m_lastAppliedRect.remove(windowId);
        m_lastAppliedWindowedFs.remove(windowId);
        m_lastAppliedMaximizedToEdges.remove(windowId);
        m_parkedScrollEdge.remove(windowId);
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
            dropWindowBookkeeping(state);
            state->deleteLater();
        });
    m_states.removeWindowsIf([removedDesktop](const QString&, const PhosphorEngine::PlacementStateKey& key) {
        return key.desktop == removedDesktop;
    });
    m_context.pruneDesktop(removedDesktop);
    sweepStatelessScreenBookkeeping(touchedScreens);
    sweepStripStash([removedDesktop](const PhosphorEngine::PlacementStateKey& key) {
        return key.desktop == removedDesktop;
    });
    // The override map is keyed per CONTEXT, so a destroyed desktop leaves
    // entries no live context can ever resolve. They are not merely dead
    // weight: KWin renumbers desktops on removal, so the index is handed out
    // again, and the next desktop to take it would resolve the template of the
    // one the user deleted. Same predicate as the state prune above.
    for (auto it = m_perScreenOverrides.begin(); it != m_perScreenOverrides.end();) {
        it = it.key().desktop == removedDesktop ? m_perScreenOverrides.erase(it) : std::next(it);
    }
}

void ScrollEngine::pruneStatesForActivities(const QStringList& validActivities)
{
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
            dropWindowBookkeeping(state);
            state->deleteLater();
        });
    m_states.removeWindowsIf([&stale](const QString&, const PhosphorEngine::PlacementStateKey& key) {
        return stale(key.activity);
    });
    sweepStatelessScreenBookkeeping(touchedScreens);
    sweepStripStash([&stale](const PhosphorEngine::PlacementStateKey& key) {
        return stale(key.activity);
    });
    // Per-context override entries for a deleted activity, on the same terms
    // as the desktop prune's sweep: no live context resolves them again, and
    // an activity id can be reused by a restore from backup.
    for (auto it = m_perScreenOverrides.begin(); it != m_perScreenOverrides.end();) {
        it = stale(it.key().activity) ? m_perScreenOverrides.erase(it) : std::next(it);
    }
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
    sweepStripStash([&matches](const PhosphorEngine::PlacementStateKey& key) {
        return matches(key.screenId);
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
    for (auto it = m_pendingFocusEmitByScreen.begin(); it != m_pendingFocusEmitByScreen.end();) {
        it = matches(it.key()) ? m_pendingFocusEmitByScreen.erase(it) : std::next(it);
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
    // A dead screen id must not keep feeding the hint-less shortcut paths
    // (autotile's twin clears the same way).
    if (matches(m_activeScreen)) {
        m_activeScreen.clear();
    }
    if (!releasedWindows.isEmpty()) {
        Q_EMIT windowsReleased(releasedWindows, releasedScreens);
    }
    // Only NOW may the per-window side maps go: the handler above has consumed
    // the float markers and the last-applied rects, and nothing else answers
    // for a departed screen's windows. releasedWindows is the same list
    // releaseScreenState built from each state's managedWindows, so there is
    // no second collection to keep in step with it.
    for (const QString& windowId : std::as_const(releasedWindows)) {
        m_lastAppliedRect.remove(windowId);
        m_lastAppliedWindowedFs.remove(windowId);
        m_lastAppliedMaximizedToEdges.remove(windowId);
        m_parkedScrollEdge.remove(windowId);
        m_floatRestore.remove(windowId);
        m_scrollFloatedWindows.remove(windowId);
    }
}

} // namespace PhosphorScrollEngine
