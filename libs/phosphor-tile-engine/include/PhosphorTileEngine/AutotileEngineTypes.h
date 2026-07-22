// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <memory>

namespace PhosphorTiles {
class SplitTree;
}

namespace PhosphorTileEngine {

/**
 * @brief An already-managed window ARRIVING in a state via
 *        migrateWindowBetweenKeys, with the float state it held on the
 *        source, captured before the source removed it.
 *
 * Set only for the duration of that migration's synchronous
 * onWindowAdded() → insertWindow() call, so insertWindow can tell a
 * migration apart from a genuine open. m_floatPredicate answers "should
 * this app OPEN floating" — a pure app-rule match with no memory of a later
 * Meta+F — so re-running it on a migration would silently re-float a
 * float-ruled window the user had explicitly tiled. A migrating window
 * carries this live float state across instead.
 */
struct MigrationArrival
{
    QString windowId;
    bool wasFloating = false;
};

/**
 * @brief Script-state bag rescued from a TilingState that a teardown destroys,
 *        so a re-created state for the same key can pick its bag back up.
 *
 * Toggling autotile off destroys the current context's state outright (see
 * setAutotileScreens), taking with it the only copy of an opaque bag the
 * algorithm's script authored — e.g. an aligned grid's column fractions from
 * a manual resize. Toggling back on then lays out from scratch, which reads
 * to the user as their adjustments being thrown away.
 *
 * Tagged with the EFFECTIVE algorithm id at harvest time, and handed back
 * only when that still matches, because TilingStateKey does not include the
 * algorithm. That tag is what preserves the invariant the wipe sites enforce
 * for live states: bags never cross algorithms. A tag mismatch refuses
 * rather than migrates; script state has no cross-algorithm meaning.
 *
 * Algorithm-change sites DO drop entries, but only through the resolver,
 * which is the only holder of a trustworthy reading. Deriving "did this
 * screen's algorithm move" from the in-memory override map cannot work at
 * these moments: a toggle-off has already dropped that map, so a screen
 * pinned to its own algorithm reads as following the global one, and
 * dropping on that reading erases the bag the teardown just rescued. That
 * failure has been produced twice. The resolver's remembered "built under"
 * id is what answers it instead, so both the global switch
 * (applyGlobalAlgorithmChange) and per-screen changes go through
 * wipeStateBagsOnEffectiveAlgorithmChange rather than dropping inline. The
 * per-key tag then adjudicates lazily at the moment a state takes a bag,
 * which is the second line of defence rather than the only one.
 *
 * Four unconditional drops exist, none of them an algorithm comparison. Two
 * are screen-id-scoped: orphaned virtual screens in setAutotileScreens, and
 * that same method's purge of screens no longer connected. The other two key
 * on the part of the context that died rather than on the screen, in
 * pruneStatesForDesktop and pruneStatesForActivities.
 *
 * Entries are reclaimed by a harvest that finds an EMPTY bag, which erases
 * rather than inserts (see stashScriptState). That is also what stops a bag
 * a live wipe cleared from being shadowed by a stale entry and handed back.
 *
 * One gap is known and accepted: an entry only dies on a switch away from
 * its algorithm if some teardown happens to harvest the emptied state while
 * the other algorithm is live — with no teardown, the entry outlives the
 * switch. It stays unreachable behind the tag until then, so this costs
 * memory rather than correctness.
 *
 * Within-session only: nothing writes it to disk, so a daemon restart still
 * starts from a clean layout.
 * The split tree rides along because it is the same kind of thing: per-context
 * state holding the user's manual resizes, in its per-node ratios rather than
 * in an opaque bag. Losing it on a toggle rebuilt a uniform layout, which is
 * the same complaint the bag's loss produced.
 *
 * The tree moves in and out whole, and there is no serialization to move it
 * through: this is a rescue across a teardown, entirely within one session,
 * so a JSON round trip would buy nothing and could only lose. The tree's
 * serializer used to impose depth, node-count and ratio-clamping caps, which
 * are bounds for untrusted on-disk input rather than for a tree the engine
 * owned a moment ago. It was removed with the rest of the state
 * serialization once nothing persisted a TilingState.
 *
 * Owning a unique_ptr makes the entry move-only, which is why the stash is a
 * std::unordered_map: Qt's containers are implicitly shared and require
 * copyable values, so this cannot be a QHash.
 *
 * The tree is NOT governed by the algorithm tag. Bags never cross algorithms,
 * but trees deliberately do carry between two MEMORY algorithms — that is the
 * live rule (wipeStateBagsOnEffectiveAlgorithmChange clears a tree only when
 * the incoming algorithm lacks memory), and a stashed tree follows it.
 */
struct StashedScriptState
{
    QJsonObject scriptState;
    std::unique_ptr<PhosphorTiles::SplitTree> splitTree;
    QString algorithmId;
};

/**
 * @brief Active drag-insert preview state.
 *
 * When set, applyTiling() filters the dragged window out of the windowsTiled
 * batch so the KWin interactive move isn't fought, while the remaining
 * windows animate into their new positions. Supports three entry modes
 * (captured at begin() time for cancel restoration):
 *   - Same-screen reorder: window was already tiled/floating on targetScreenId
 *   - Cross-screen adoption: window was tracked on a different autotile screen
 *   - Fresh adoption: window was not tracked by the engine at all
 */
struct DragInsertPreview
{
    QString windowId;
    QString targetScreenId;
    int lastInsertIndex = -1;

    // Prior-state restoration info (used on cancel)
    bool hadPriorState = false; // True if m_states contained windowId at begin
    PhosphorEngine::TilingStateKey
        priorKey; // Key of the prior PhosphorTiles::TilingState (meaningful iff hadPriorState)
    int priorRawIndex = -1; // Raw index in priorState->windowOrder() at begin
    bool priorFloating = false; // Prior floating flag in priorState
    bool priorSameScreen = false; // priorKey == currentKeyForScreen(targetScreenId)

    // Eviction info (used when the target stack is already at maxWindows
    // and the dragged window is a new member). The last tiled neighbour
    // is floated to make room; on cancel the eviction is undone, on
    // commit the evicted window is sent through the batch-float path so
    // its pre-tile geometry is restored.
    QString evictedWindowId;
};

} // namespace PhosphorTileEngine
