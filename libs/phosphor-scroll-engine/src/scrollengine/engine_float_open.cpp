// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The float-at-open helpers of the scroll engine, split out of
// engine_lifecycle.cpp by concern: the float-restore seed (shared with the
// handoff receive), the gated remembered-position restore a floated open
// performs, the scroll arm of the shared free-size restore (#1106) for a
// float that moved nowhere, and the tail insertOpenedWindow's two float
// exits share.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>

#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/WindowPlacementStore.h>

#include "scrollenginelogging.h"

namespace PhosphorScrollEngine {

void ScrollEngine::seedFloatRestoreForOpen(const QString& windowId, int minWidth, int minHeight)
{
    // windowMinimumSize reads the clamp out of this entry for every window
    // that is floated rather than tiled, and the cross-engine handoff queries
    // it whatever state the window is in: with no entry the answer is the
    // "unknown" one, and the receiving engine gets an unclamped window. That
    // bites hardest on exactly the windows these paths float — the oversized
    // ones, whose clamp is why they could not take a column in the first
    // place.
    const auto existing = m_floatRestore.find(windowId);
    if (existing != m_floatRestore.end()) {
        // A real remembered slot is worth more than a slotless seed; only
        // the clamp is refreshed.
        existing->minWidth = qMax(0, minWidth);
        existing->minHeight = qMax(0, minHeight);
        return;
    }
    FloatRestore restore;
    restore.column = -1; // no slot to go back to; unfloat opens a fresh column
    restore.minWidth = qMax(0, minWidth);
    restore.minHeight = qMax(0, minHeight);
    m_floatRestore.insert(windowId, restore);
}

void ScrollEngine::finishFloatedOpen(ScrollState* state, const QString& windowId, const QString& screenId, int minWidth,
                                     int minHeight, bool migration, bool oversized, bool placedBefore,
                                     const PhosphorEngine::WindowPlacement* record)
{
    state->addFloating(windowId);
    seedFloatRestoreForOpen(windowId, minWidth, minHeight);
    // Engine-decided or record-decided, the float carries the mode marker
    // like every other float this engine makes: isModeSpecificFloated has to
    // answer true or the daemon captures the scroll-mode float into the snap
    // slot at the next mode transition (presaveSnapFloats skips exactly the
    // marked windows).
    m_scrollFloatedWindows.insert(windowId);
    // A floated arrival consumes its seed entry too, or the screen's list
    // never empties and the stale entry survives every later mode
    // transition. Its stash tile likewise: the tile path consumes it on the
    // claim, and a float that left it standing was fuzzy-claimable by the
    // next same-app window and persisted as a ghost slot after the close.
    consumePendingInitialOrder(screenId, windowId);
    consumeStripStashTileForFloat(state, currentKeyForScreen(screenId), windowId);
    // A migration re-entry (the window changed screen or desktop while this
    // engine tracked it) re-establishes the float mark for a window that is
    // already placed: the user is looking at it, and neither the remembered
    // position nor the remembered size may move it. The record consume is
    // what keeps a user-floated window floating across the move, so it runs
    // either way; only the geometry follows the gate.
    // A migration WITH a record needs neither arm: the caller already
    // consumed it, and a migration moves nothing.
    if (!migration) {
        // Resolved ONCE for the move gate and the size arm, which ask the
        // same question of it. Keyed to screenId, which is the record's own
        // screen in both arms — takeForReopen's accept requires the match.
        const QList<QSize> managedSizes = managedSizesOnScreen(screenId);
        const bool moved = record ? emitGatedFloatGeometryRestore(windowId, *record, screenId, managedSizes)
                                  : restoreFloatRecordForOpen(windowId, screenId, managedSizes);
        // A float that moved nowhere still gets its free SIZE back where it
        // stands, unless the window is oversized: the clamp would then ask
        // for less than the client's own minimum on the axis that made it
        // oversized, and the client keeps the frame it has.
        if (!moved && !oversized) {
            restoreFreeSizeForFloatedOpen(windowId, screenId, placedBefore, managedSizes);
        }
    } else if (!record) {
        // Consume the record for the mode marker's sake without applying
        // its position: restoreFloatRecordForOpen's move is gated by the
        // same helper, so a migration takes the consume and declines the
        // emit through the migration flag it cannot see. Read the record
        // and drop the geometry here instead.
        const QString appId = currentAppIdFor(windowId);
        if (m_windowTracker && PhosphorEngine::hasStableAppIdFor(appId, windowId)) {
            const PhosphorEngine::PlacementStateKey key = currentKeyForScreen(screenId);
            (void)m_windowTracker->placementStore().takeForReopen(engineId(), windowId, appId, key.screenId);
        }
    }
    // Announced AFTER the geometry or size emit, so the effect's float
    // handler sees the reposition already in flight before it decides
    // whether first-frame suppression has anything left to wait for.
    Q_EMIT windowFloatingStateSynced(windowId, true, screenId);
}

bool ScrollEngine::emitGatedFloatGeometryRestore(const QString& windowId, const PhosphorEngine::WindowPlacement& record,
                                                 const QString& screenId, const QList<QSize>& managedSizes)
{
    // SCREEN-LOCAL recorded position only, for autotile's documented reason: a
    // rect captured on a different screen would teleport the window while the
    // float tracking points elsewhere. The move itself is gated (daemon-wired
    // scrollingRestoreFloatedWindowsOnLogin setting + per-window
    // RestorePosition rule) while the floating MARK is not — the callers mark
    // unconditionally and only the geometry comes through here.
    //
    // Shared by the two restore paths (insertOpenedWindow's record-float branch
    // and restoreFloatRecordForOpen) because they are one rule with two entry
    // points, and a change to the gate that reached only one of them would
    // restore the position on one open path and not the other.
    const QString restoreScreen = record.screenId.isEmpty() ? screenId : record.screenId;
    const QRect freeGeo = record.freeGeometryFor(restoreScreen);
    const bool restorePosition = !m_restorePositionPredicate || m_restorePositionPredicate(windowId);
    // Do not trust the KEY. This reads the shared free-geometry map directly
    // rather than through validatedUnmanagedGeometry, so it gets none of that
    // resolver's validation — and the rect goes straight out as an absolute
    // geometry to apply. A record filed under one screen whose coordinates
    // describe another would teleport the window to that monitor, which is
    // exactly what the comment above says this restore must never do.
    // A recorded rect of a live column's size is a spawn frame the window
    // never chose (it missed its first size restore and closed column-sized):
    // re-applying it would keep that record alive for every later reopen, so
    // the move is refused and the size arm finds a real source instead.
    if (freeGeo.isValid() && restorePosition
        && (!m_windowTracker || m_windowTracker->geometryBelongsToScreen(freeGeo, restoreScreen))
        && !isManagedSize(managedSizes, freeGeo.size())) {
        Q_EMIT geometryRestoreRequested(windowId, freeGeo, restoreScreen);
        return true;
    }
    return false;
}

bool ScrollEngine::restoreFloatRecordForOpen(const QString& windowId, const QString& screenId,
                                             const QList<QSize>& managedSizes)
{
    // Registry answer, not a parse: a canonical id frozen before KWin resolved
    // the class has no appId to parse, and this gate would then silently skip
    // the float restore for the window's whole life.
    const QString appId = currentAppIdFor(windowId);
    if (!m_windowTracker || !PhosphorEngine::hasStableAppIdFor(appId, windowId)) {
        return false;
    }
    // The window floats regardless (the caller already decided that), so only
    // a FLOATING record is consumed — takeForReopen's contract. The accept
    // itself is the store's shared predicate.
    const PhosphorEngine::PlacementStateKey key = currentKeyForScreen(screenId);
    const auto record = m_windowTracker->placementStore().takeForReopen(engineId(), windowId, appId, key.screenId);
    if (!record) {
        return false;
    }
    return emitGatedFloatGeometryRestore(windowId, *record, screenId, managedSizes);
}

QList<QSize> ScrollEngine::managedSizesOnScreen(const QString& screenId) const
{
    // Keys first, params second: layoutParamsForKey runs the injected
    // geometry and gap providers, which must not run mid-walk over m_states
    // (the stashStripStructure precondition), so the walk only collects.
    QList<PhosphorEngine::PlacementStateKey> keys;
    for (auto it = m_states.states().cbegin(); it != m_states.states().cend(); ++it) {
        if (it.key().screenId == screenId && it.value() && !it.value()->strip().isEmpty()) {
            keys.append(it.key());
        }
    }
    const PhosphorEngine::PlacementStateKey currentKey = currentKeyForScreen(screenId);
    QList<QSize> sizes;
    for (const PhosphorEngine::PlacementStateKey& key : std::as_const(keys)) {
        const ScrollState* state = m_states.stateForKey(key);
        if (!state) {
            continue;
        }
        // The rects the strip resolves to, tile by tile (a column's own rect
        // spans the whole cross axis and is not a frame size). Covers a
        // sibling that arrived in the same burst, whose apply is deferred to
        // the burst end and whose applied memo is therefore still empty.
        const ScrollLayoutParams params = layoutParamsForKey(key);
        if (params.workArea.isValid()) {
            const ResolvedStrip resolved = state->strip().relayout(params);
            for (const ResolvedColumn& column : resolved.columns) {
                for (const ResolvedTile& tile : column.tiles) {
                    if (tile.rect.isValid()) {
                        sizes.append(tile.rect.size());
                    }
                }
            }
        }
        // The rects the compositor was actually told, where one exists: the
        // apply path clamps a straddler at the screen edge, and a spawn frame
        // copies that applied frame, not the resolved tile. The current
        // context's memo is window-keyed; a background context's is parked
        // under its key.
        const QHash<QString, QRect> applied = key == currentKey ? m_lastAppliedRect : m_contextRectMemory.value(key);
        for (const QString& member : state->strip().windowsInOrder()) {
            const QRect rect = applied.value(member);
            if (rect.isValid()) {
                sizes.append(rect.size());
            }
        }
    }
    return sizes;
}

void ScrollEngine::restoreFreeSizeForFloatedOpen(const QString& windowId, const QString& screenId, bool placedBefore,
                                                 const QList<QSize>& managedSizes)
{
    // A window this engine leaves floating at open, and did not move to its
    // remembered free spot, stays where the compositor put it at the size the
    // client asked for: for a KDE app whose sibling is a column that is the
    // column's size (#1106). The tiling wire carries no restore reason, so
    // the base's lineage snapshot (placedBefore) is what tells a restart or
    // mode-swap re-announce of a visible window from a first placement. The
    // clamp is the tracker's available area, the same bound the other
    // engines use, not the gap-inset strip work area.
    restoreFreeSizeWhereItStands(m_windowTracker, windowId, screenId, PhosphorEngine::RestoreReason::Open, placedBefore,
                                 managedSizes);
}

void ScrollEngine::consumeStripStashTileForFloat(ScrollState* state, const PhosphorEngine::PlacementStateKey& key,
                                                 const QString& windowId)
{
    const auto it = m_stripStash.find(key);
    if (it == m_stripStash.end()) {
        return;
    }
    if (const auto consumedIt = m_stripStashConsumed.constFind(key);
        consumedIt != m_stripStashConsumed.cend() && consumedIt->contains(windowId)) {
        return;
    }
    StashedStrip& stashStrip = it.value();
    // The blueprint carry comes BEFORE the tile claim, exactly as
    // restoreFromStripStash does it and for the same reason: the cursor is
    // owed to the state whether or not this arrival claims a tile, and the
    // claim below can retire the whole entry. A float that consumed the
    // entry's last tile would otherwise take the stashed cursor down with it
    // and leave the next fresh open to restart the template from the live
    // column count. Raised, never assigned: a window that opened fresh
    // alongside the restore has already advanced the cursor past the stash.
    if (state) {
        state->setBlueprintCursor(qMax(state->blueprintCursor(), stashStrip.blueprintCursor));
        // Only a VALID stash identity, and only onto a state that has none:
        // a null identity is what a persistence-staged entry carries, and
        // stamping it would make the consumption site read a blueprint swap
        // and reset the cursor just handed over (see restoreFromStripStash).
        if (stashStrip.blueprintIdentity.isValid() && !state->hasBlueprintIdentity()) {
            state->setBlueprintIdentity(stashStrip.blueprintIdentity);
        }
    }
    // A cursor-only entry has no tile to consume; restoreFromStripStash
    // retires it on the next tiled arrival, which is the payload it exists
    // for, so a float leaves it alone.
    if (stashStrip.isEmpty()) {
        return;
    }
    // EXACT id only: a float never spends the cross-session fuzzy claim,
    // which renames a stashed tile to a live id and is the tile path's to
    // make.
    for (StashedColumn& column : stashStrip.columns) {
        for (StashedTile& tile : column.tiles) {
            if (tile.windowId != windowId) {
                continue;
            }
            const int total = stashStrip.tileCount();
            tile.stagedFromPersistence = false;
            tile.unclaimedSessions = 0;
            QSet<QString>& consumed = m_stripStashConsumed[key];
            consumed.insert(windowId);
            if (consumed.size() >= total) {
                m_stripStash.remove(key);
                m_stripStashConsumed.remove(key);
            }
            return;
        }
    }
}

} // namespace PhosphorScrollEngine
