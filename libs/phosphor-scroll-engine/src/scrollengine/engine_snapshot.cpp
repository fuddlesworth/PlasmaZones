// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// ScrollEngine::stripSnapshot — the column-aware read surface behind the
// daemon's strip-mode drag popup. Kept out of engine_apply.cpp because the
// snapshot's contract is drag-insert index fidelity, not the zone-number
// space the visible-tile walks there serve; the two must be free to evolve
// separately.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorScreens/ScreenIdentity.h>

#include <QHash>

#include <utility>

namespace PhosphorScrollEngine {

ScrollStripSnapshot ScrollEngine::stripSnapshot(const QString& screenId, const QString& excludeWindowId) const
{
    ScrollStripSnapshot snap;
    // Preview-owned screens resolve against the preview's CAPTURED key for
    // the same reason computeDragInsertTargetAtPoint does: a desktop or
    // activity switch mid-drag must not swap the strip under indices a
    // commit will apply to the captured one.
    const bool previewOwnsScreen = m_dragInsertPreview
        && PhosphorScreens::ScreenIdentity::screensMatch(m_dragInsertPreview->targetScreenId, screenId);
    const ScrollState* state = previewOwnsScreen ? m_states.stateForKey(m_dragInsertPreview->targetKey)
                                                 : m_states.stateForKey(currentKeyForScreen(screenId));
    if (!state) {
        return snap;
    }
    const ScrollLayoutParams params =
        layoutParamsForScreen(previewOwnsScreen ? m_dragInsertPreview->targetScreenId : screenId);
    if (!params.workArea.isValid()) {
        return snap;
    }
    snap.valid = true;
    if (state->strip().isEmpty()) {
        return snap;
    }

    // Exclusion only matters while the drag window is still ATTACHED (no
    // live preview). Once a preview detached it, the strip resolved below no
    // longer contains it and a second removal would be a no-op by id anyway;
    // skipping the canonicalization keeps the common per-show path cheap.
    const QString excluded =
        (!previewOwnsScreen && !excludeWindowId.isEmpty()) ? canonicalizeForLookup(excludeWindowId) : QString();

    const ResolvedStrip resolved = state->strip().relayout(params);
    // Resolved columns keyed by model index: fully-minimized columns resolve
    // no entry, and the model walk below must not skew when one is absent.
    QHash<int, const ResolvedColumn*> resolvedByIndex;
    resolvedByIndex.reserve(resolved.columns.size());
    for (const ResolvedColumn& column : resolved.columns) {
        resolvedByIndex.insert(column.columnIndex, &column);
    }

    const QVector<Column>& modelColumns = state->strip().columns();
    snap.columns.reserve(modelColumns.size());
    int activeColumnDroppedAt = -1;
    for (int ci = 0; ci < modelColumns.size(); ++ci) {
        const Column& modelColumn = modelColumns.at(ci);
        ScrollStripSnapshotColumn outColumn;
        outColumn.tabbed = modelColumn.display == ColumnDisplay::Tabbed;

        const ResolvedColumn* resolvedColumn = resolvedByIndex.value(ci, nullptr);
        const QRect columnRect = resolvedColumn ? resolvedColumn->rect : QRect();

        for (int ti = 0; ti < modelColumn.tiles.size(); ++ti) {
            const Tile& modelTile = modelColumn.tiles.at(ti);
            if (!excluded.isEmpty() && modelTile.windowId == excluded) {
                continue;
            }
            ScrollStripSnapshotTile outTile;
            outTile.windowId = modelTile.windowId;
            outTile.minimized = modelTile.minimized;
            outTile.activeTab = ti == modelColumn.activeTileIdx;
            outTile.hidden = outColumn.tabbed && !modelTile.minimized && !outTile.activeTab;
            // A rect only for tiles a renderer stacks: not minimized (none
            // resolves), not a hidden tab (drawn as a tab, and its resolved
            // rect is just the active tile's — see ResolvedTile::rect).
            if (!outTile.minimized && !outTile.hidden && resolvedColumn && !columnRect.isEmpty()) {
                for (const ResolvedTile& resolvedTile : resolvedColumn->tiles) {
                    if (resolvedTile.windowId == modelTile.windowId) {
                        const QRect& r = resolvedTile.rect;
                        outTile.relRect = QRectF(static_cast<qreal>(r.x() - columnRect.x()) / columnRect.width(),
                                                 static_cast<qreal>(r.y() - columnRect.y()) / columnRect.height(),
                                                 static_cast<qreal>(r.width()) / columnRect.width(),
                                                 static_cast<qreal>(r.height()) / columnRect.height());
                        break;
                    }
                }
            }
            outColumn.tiles.append(outTile);
        }

        // A column emptied by the exclusion is dropped WITH renumbering —
        // that is the detach a commit will have performed (takeWindow removes
        // the emptied column), so keeping a placeholder would offset every
        // later card's index against the strip the target indices name. When
        // the dropped column WAS the active one, remember where it stood in
        // the output: the real detach re-points the active index to the
        // column that takes its place (removeWindowInternal's
        // qMin(colIdx, size - 1)), and the emulation must agree or the popup
        // shows no active-column highlight for the common drag-a-solo-window
        // case until the trigger is held.
        if (outColumn.tiles.isEmpty()) {
            if (ci == state->strip().activeColumnIndex()) {
                activeColumnDroppedAt = snap.columns.size();
            }
            continue;
        }
        // Excluding the active tile leaves the survivors with no activeTab
        // (the model index comparison above ran before the drop); the real
        // detach re-points activeTileIdx to a surviving tile, so promote one
        // the same way — the first non-minimized survivor, or none when
        // every survivor is minimized. Only an exclusion can strip the
        // flag: the model's activeTileIdx always names a live tile
        // (setWindowMinimized re-points it).
        if (!excluded.isEmpty()) {
            bool hasActive = false;
            for (const ScrollStripSnapshotTile& t : std::as_const(outColumn.tiles)) {
                if (t.activeTab) {
                    hasActive = true;
                    break;
                }
            }
            if (!hasActive) {
                // First non-minimized survivor; when every survivor is
                // minimized (embedder-only state), leave the flag unset —
                // highlighting an invisible tile would be a lie.
                for (int i = 0; i < outColumn.tiles.size(); ++i) {
                    if (!outColumn.tiles.at(i).minimized) {
                        outColumn.tiles[i].activeTab = true;
                        outColumn.tiles[i].hidden = false;
                        break;
                    }
                }
            }
        }
        if (ci == state->strip().activeColumnIndex()) {
            snap.activeColumnIndex = snap.columns.size();
        }
        snap.columns.append(outColumn);
    }
    // Post-loop fixup for the dropped-active-column case: the final column
    // count is not known at the point the drop happens.
    if (activeColumnDroppedAt >= 0 && !snap.columns.isEmpty()) {
        snap.activeColumnIndex = qMin(activeColumnDroppedAt, static_cast<int>(snap.columns.size()) - 1);
    }
    return snap;
}

} // namespace PhosphorScrollEngine
