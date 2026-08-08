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
    // The remembered park edge is written only while a window sits parked and
    // consumed when it scrolls back on screen, so a window that DIES parked
    // never consumes its entry; this aliveness sweep reclaims those. Every
    // path that drops m_lastAppliedRect for a still-alive window (float,
    // handoff, cross-screen move, drag commit/cancel, the context sweeps)
    // drops the edge beside it, so this sweep only ever sees dead ids.
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
    // dead windows on one screen needs it once, not N times.
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
        ++pruned;
    }
    // Seed lists hold dead ids too (a captured order whose window died before
    // arriving); left behind they would pin insert positions against ghosts
    // forever. One sweep for the whole batch — per dead window it would
    // re-walk every screen's list again.
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
    // The empty-alive-set bail below is this BOUNDARY's own fail-closed, not
    // a house rule the sweeps above share: the seed and rect sweeps run
    // unconditionally, because re-deriving a seed or a rect costs nothing,
    // while a wiped stash cannot be rebuilt. A premature one-shot report at
    // login must not take the structure with it.
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
                        focusSurvives = focusSurvives || tile.windowId == stashIt->focusedWindowId;
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
            if (remaining == 0
                || (stashConsumedIt != m_stripStashConsumed.end() && stashConsumedIt->size() >= remaining)) {
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
                    if (m_stripStash.contains(oldKey) && !m_stripStash.contains(newKey)) {
                        m_stripStash.insert(newKey, m_stripStash.take(oldKey));
                        if (m_stripStashConsumed.contains(oldKey)) {
                            m_stripStashConsumed.insert(newKey, m_stripStashConsumed.take(oldKey));
                        }
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
            m_parkedScrollEdge.remove(windowId);
            m_scrollFloatedWindows.remove(windowId);
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
        m_parkedScrollEdge.remove(windowId);
        m_floatRestore.remove(windowId);
        m_scrollFloatedWindows.remove(windowId);
        // The dying context's windows can never answer their queued echoes;
        // a stale entry would swallow the first genuine focus of a reused
        // id (windowClosed and releaseScreenState sweep the same way).
        m_pendingSelfActivations.removeAll(windowId);
    }
}

void ScrollEngine::sweepStatelessScreenBookkeeping(const QSet<QString>& screenIds)
{
    // Per-screen (not per-state) bookkeeping outlives an individual context
    // prune only while ANOTHER context still exists for the screen. Once
    // the last state is gone, a stale seed or tab-strip latch would replay
    // against whatever state is built there next. m_perScreenOverrides
    // deliberately survives (per-screen rule config re-applies on
    // re-entry); only a physical removal purges it.
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
            // Removed screen: the per-screen rule overrides go with the state
            // too — this prune is their documented purger, and releaseScreenState
            // does not touch them because a mode exit must keep them.
            m_perScreenOverrides.remove(state->screenId());
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
    for (auto it = m_perScreenOverrides.begin(); it != m_perScreenOverrides.end();) {
        if (matches(it.key())) {
            it = m_perScreenOverrides.erase(it);
        } else {
            ++it;
        }
    }
    // Tab-strip teardown for every matching screen — the third step of the
    // sweepStatelessScreenBookkeeping helper this function open-codes, and
    // the one both sibling prunes get via that helper. Without it the dead
    // screen never receives its "[]" tabStripsChanged, so the daemon's
    // m_lastScrollTabStripsJson keeps the departed screen and every later
    // enrichment tick re-pushes its strips into the overlay. Snapshot the
    // matching ids first: clearTabStripsForScreen mutates the set.
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
        m_parkedScrollEdge.remove(windowId);
        m_floatRestore.remove(windowId);
        m_scrollFloatedWindows.remove(windowId);
    }
}

} // namespace PhosphorScrollEngine
