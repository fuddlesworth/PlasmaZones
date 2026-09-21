// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The scroll engine's arm of the workspace overview: the two
// IOverviewModelSource reads, which describe ANY context by key (the overview
// shows every workspace at once, most of them not on screen), and the pan a
// non-current strip takes from the overview's scroll gesture. Both reads are
// thin views over the key-taking stripSnapshot (engine_snapshot.cpp), which
// owns the walk and its rect contract; nothing here resolves geometry on its
// own, so the overview cannot disagree with the popup about where a tile is.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/IOverviewModelSource.h>

namespace PhosphorScrollEngine {

std::optional<QList<PhosphorEngine::OverviewWindowEntry>>
ScrollEngine::overviewWindowsFor(const PhosphorEngine::PlacementStateKey& key) const
{
    // The interface contract: no state is std::nullopt, never a created
    // state and never an empty list, so the builder can tell "this engine
    // has never seen the context" from "the context is empty".
    const ScrollState* state = m_states.stateForKey(key);
    if (!state) {
        return std::nullopt;
    }
    QList<PhosphorEngine::OverviewWindowEntry> entries;

    // Tiled windows in strip model order, carrying their column and tile
    // indices. The snapshot is invalid (no columns) when the screen has no
    // work area, and the entries then simply carry null rects: the builder
    // fills those from the daemon's tracked geometry.
    const ScrollStripSnapshot snap = stripSnapshot(key);
    for (int ci = 0; ci < snap.columns.size(); ++ci) {
        const ScrollStripSnapshotColumn& column = snap.columns.at(ci);
        for (int ti = 0; ti < column.tiles.size(); ++ti) {
            const ScrollStripSnapshotTile& tile = column.tiles.at(ti);
            PhosphorEngine::OverviewWindowEntry entry;
            entry.windowId = tile.windowId;
            // A hidden tab shares the active tile's rect and a minimized
            // tile has none; both are what the resolve says (see
            // ScrollStripSnapshotTile::absRect).
            entry.rect = tile.absRect;
            entry.minimized = tile.minimized;
            entry.column = ci;
            entry.tile = ti;
            entries.append(entry);
        }
    }

    // Floats, exactly once each. lastManagedRect answers the last rect THIS
    // engine applied, which the float paths remove at the moment a window
    // leaves the strip (floatWindowInternal drops the entry so the poison
    // guard cannot restore a stale tile rect), so a float normally has none
    // here and the builder fills it from tracked geometry. Asked anyway
    // rather than hard-wired null, so a float that does retain one (the
    // handoff and close paths keep it deliberately) answers with it.
    const QStringList floating = state->floatingWindows();
    for (const QString& windowId : floating) {
        PhosphorEngine::OverviewWindowEntry entry;
        entry.windowId = windowId;
        entry.rect = lastManagedRect(windowId);
        entry.floating = true;
        entries.append(entry);
    }
    return entries;
}

std::optional<PhosphorEngine::OverviewStripEntry>
ScrollEngine::overviewStripFor(const PhosphorEngine::PlacementStateKey& key) const
{
    const ScrollState* state = m_states.stateForKey(key);
    if (!state) {
        return std::nullopt;
    }
    const ScrollStripSnapshot snap = stripSnapshot(key);
    PhosphorEngine::OverviewStripEntry out;
    out.viewOffset = snap.viewX;
    out.columns.reserve(snap.columns.size());
    for (const ScrollStripSnapshotColumn& column : snap.columns) {
        PhosphorEngine::OverviewStripColumn outColumn;
        // Rects straight through, axis and all: the resolve already laid
        // the column out along whichever axis the screen's strip runs, and
        // the interface carries rects precisely so no consumer has to know
        // which axis that was.
        outColumn.rect = column.absRect;
        outColumn.tabbed = column.tabbed;
        outColumn.tiles.reserve(column.tiles.size());
        for (int ti = 0; ti < column.tiles.size(); ++ti) {
            const ScrollStripSnapshotTile& tile = column.tiles.at(ti);
            if (tile.activeTab) {
                outColumn.activeTab = ti;
            }
            PhosphorEngine::OverviewStripTile outTile;
            outTile.windowId = tile.windowId;
            // Minimized tiles stay listed (the model order is what the
            // overview's drop resolver indexes by) with the null rect the
            // resolve gave them.
            outTile.rect = tile.absRect;
            outColumn.tiles.append(outTile);
        }
        out.columns.append(outColumn);
    }
    return out;
}

bool ScrollEngine::panStoredView(const PhosphorEngine::PlacementStateKey& key, int deltaPx)
{
    // Non-creating: a key the engine has no strip for has nothing to pan,
    // and inventing a state for an invisible workspace would leave an
    // empty context behind that the overview then lists as real.
    ScrollState* state = stateForKey(key, /*createIfMissing=*/false);
    if (!state) {
        return false;
    }
    // Same current-context gap approximation as the key-taking snapshot,
    // and for the same reason; the strip clamps the pan against these
    // params' work area, which is the screen's, and that part is exact.
    const ScrollLayoutParams params = layoutParamsForScreen(key.screenId);
    // The strip's own pan verb, so the view detaches from the centering
    // policy exactly as a wheel scroll on the live strip does and the next
    // focus change re-attaches it the same way. Deliberately no applyLayout
    // and no placementChanged: the context is not on screen (the daemon
    // routes the current context to scrollViewByPercent), so there is no
    // geometry to emit, and placementChanged fans out to consumers that
    // read the CURRENT context (the popup refresh, the tilingChanged
    // broadcast, the focus-scroll cap republish). The pan lands through the
    // ordinary relayout the next switch to this context runs. Persistence
    // is the caller's: the strip snapshot's dirty bit is marked by the
    // daemon after a true return, the same shape as its reapDesktopState
    // handler, because this engine's only save trigger is the signal this
    // verb must not raise.
    return state->strip().scrollViewBy(deltaPx, params);
}

} // namespace PhosphorScrollEngine
