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

namespace {

/// A tile's rect inside its column, in ROLE space: x/width run ALONG the
/// strip and y/height run ACROSS it (the within-column stack), whichever way
/// the strip physically runs.
///
/// Role rather than physical because the snapshot's consumers draw it in role
/// space and transpose for display themselves — the selector's strip card
/// swaps x with y when its screen's strip is vertical. Emitting physical
/// rects here would make that a double transpose. On a horizontal strip this
/// is the identity mapping.
QRectF relRectInColumn(const QRect& tile, const QRect& column, StripAxis axis)
{
    return QRectF(static_cast<qreal>(axis.mainPos(tile) - axis.mainPos(column)) / axis.mainSize(column),
                  static_cast<qreal>(axis.crossPos(tile) - axis.crossPos(column)) / axis.crossSize(column),
                  static_cast<qreal>(axis.mainSize(tile)) / axis.mainSize(column),
                  static_cast<qreal>(axis.crossSize(tile)) / axis.crossSize(column));
}

/// The one snapshot walk both ScrollEngine::stripSnapshot overloads share.
/// @p excluded is the already-canonicalized window to emulate detached
/// (empty for none). @p fillAbsolute additionally records every resolved
/// rect and the view offset, which only the key overload's consumer (the
/// workspace overview) reads. Everything here is a const read of @p strip:
/// relayout is a pure function of the model, and nothing below touches
/// focus, anchor or view.
ScrollStripSnapshot buildSnapshot(const ScrollStrip& strip, const ScrollLayoutParams& params, const QString& excluded,
                                  bool fillAbsolute)
{
    ScrollStripSnapshot snap;
    if (!params.workArea.isValid()) {
        return snap;
    }
    snap.valid = true;
    if (strip.isEmpty()) {
        return snap;
    }

    const ResolvedStrip resolved = strip.relayout(params);
    // Resolved columns keyed by model index: fully-minimized columns resolve
    // no entry, and the model walk below must not skew when one is absent.
    QHash<int, const ResolvedColumn*> resolvedByIndex;
    resolvedByIndex.reserve(resolved.columns.size());
    for (const ResolvedColumn& column : resolved.columns) {
        resolvedByIndex.insert(column.columnIndex, &column);
    }

    const QVector<Column>& modelColumns = strip.columns();
    snap.columns.reserve(modelColumns.size());
    int activeColumnDroppedAt = -1;
    for (int ci = 0; ci < modelColumns.size(); ++ci) {
        const Column& modelColumn = modelColumns.at(ci);
        ScrollStripSnapshotColumn outColumn;
        outColumn.tabbed = modelColumn.display == ColumnDisplay::Tabbed;

        const ResolvedColumn* resolvedColumn = resolvedByIndex.value(ci, nullptr);
        const QRect columnRect = resolvedColumn ? resolvedColumn->rect : QRect();
        if (fillAbsolute) {
            outColumn.absRect = columnRect;
        }
        // Real on-screen share ALONG THE STRIP, driving the variable-extent
        // card that IS its column at preview scale. A column longer than the
        // work area (over-wide preset) clamps to 1 — the preview shows the
        // visible share, matching the committed clip.
        //
        // Along the strip, not physically wide: on a vertical strip a column
        // spans the full screen width and its share of the strip is its
        // height. Consumers draw the snapshot in role space and transpose it
        // themselves (ZoneSelectorStripCard), so emitting a physical fraction
        // here would report 1.0 for every column on a vertical screen.
        if (!columnRect.isEmpty() && params.axis.mainSize(params.workArea) > 0) {
            outColumn.widthFraction = qBound(
                0.0, static_cast<qreal>(params.axis.mainSize(columnRect)) / params.axis.mainSize(params.workArea), 1.0);
        }

        // Output position the excluded ACTIVE tile would have occupied, for
        // the position-faithful promotion below. -1 = the exclusion did not
        // remove this column's active tile.
        int excludedActiveOutPos = -1;
        for (int ti = 0; ti < modelColumn.tiles.size(); ++ti) {
            const Tile& modelTile = modelColumn.tiles.at(ti);
            if (!excluded.isEmpty() && modelTile.windowId == excluded) {
                if (ti == modelColumn.activeTileIdx) {
                    excludedActiveOutPos = static_cast<int>(outColumn.tiles.size());
                }
                continue;
            }
            ScrollStripSnapshotTile outTile;
            outTile.windowId = modelTile.windowId;
            outTile.minimized = modelTile.minimized;
            outTile.activeTab = ti == modelColumn.activeTileIdx;
            outTile.hidden = outColumn.tabbed && !modelTile.minimized && !outTile.activeTab;
            // A rel rect only for tiles a renderer stacks: not minimized
            // (none resolves), not a hidden tab (drawn as a tab, and its
            // resolved rect is just the active tile's — see
            // ResolvedTile::rect). The absolute rect, when asked for, is
            // recorded for hidden tabs too: the overview draws every window
            // it has a rect for, and the shared rect is the honest answer.
            if (!outTile.minimized && resolvedColumn && !columnRect.isEmpty()) {
                for (const ResolvedTile& resolvedTile : resolvedColumn->tiles) {
                    if (resolvedTile.windowId == modelTile.windowId) {
                        const QRect& r = resolvedTile.rect;
                        if (fillAbsolute) {
                            outTile.absRect = r;
                        }
                        if (!outTile.hidden) {
                            outTile.relRect = relRectInColumn(r, columnRect, params.axis);
                        }
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
            if (ci == strip.activeColumnIndex()) {
                activeColumnDroppedAt = snap.columns.size();
            }
            continue;
        }
        // Excluding the active tile leaves the survivors with no activeTab
        // (the model index comparison above ran before the drop). Promote by
        // POSITION, the way the real detach does: removeWindowInternal keeps
        // activeTileIdx's numeric value, so the next tile DOWN inherits the
        // slot, and only a last-tile removal decrements — a first-survivor
        // scan disagreed with the real strip whenever the active tile was
        // not tile 0. The minimized skip (forward, then backward) is a
        // DELIBERATE divergence from the real detach, which does no such
        // skip: a preview highlighting an invisible tile would be a lie, so
        // when the inheriting slot is minimized the promotion moves to the
        // nearest visible survivor, and when every survivor is minimized
        // (embedder-only state) the flag stays unset. Gated on THIS column
        // having lost its active tile, so untouched columns pay nothing and
        // the model's own invariant (activeTileIdx always names a live tile
        // — setWindowMinimized re-points it) is never second-guessed.
        if (excludedActiveOutPos >= 0 && !outColumn.tiles.isEmpty()) {
            const int start = qMin(excludedActiveOutPos, static_cast<int>(outColumn.tiles.size()) - 1);
            int promoted = -1;
            for (int i = start; i < outColumn.tiles.size(); ++i) {
                if (!outColumn.tiles.at(i).minimized) {
                    promoted = i;
                    break;
                }
            }
            for (int i = start - 1; promoted < 0 && i >= 0; --i) {
                if (!outColumn.tiles.at(i).minimized) {
                    promoted = i;
                }
            }
            if (promoted >= 0) {
                ScrollStripSnapshotTile& tile = outColumn.tiles[promoted];
                tile.activeTab = true;
                tile.hidden = false;
                // A promoted HIDDEN TAB skipped the rect walk above (it ran
                // while the tile was still hidden). Back-fill from the
                // resolve so the tile is not handed to renderers visible but
                // rect-less; for a tabbed column the resolved rect is the
                // active tile's, which is exactly what the promotion means.
                if (tile.relRect.isNull() && resolvedColumn && !columnRect.isEmpty()) {
                    for (const ResolvedTile& resolvedTile : resolvedColumn->tiles) {
                        if (resolvedTile.windowId == tile.windowId) {
                            const QRect& r = resolvedTile.rect;
                            tile.relRect = relRectInColumn(r, columnRect, params.axis);
                            break;
                        }
                    }
                }
            }
        }
        if (ci == strip.activeColumnIndex()) {
            snap.activeColumnIndex = snap.columns.size();
        }
        snap.columns.append(outColumn);
    }
    // Post-loop fixup for the dropped-active-column case: the final column
    // count is not known at the point the drop happens.
    if (activeColumnDroppedAt >= 0 && !snap.columns.isEmpty()) {
        snap.activeColumnIndex = qMin(activeColumnDroppedAt, static_cast<int>(snap.columns.size()) - 1);
    }
    if (fillAbsolute) {
        snap.viewX = resolved.viewOffset;
    }
    return snap;
}

} // namespace

ScrollStripSnapshot ScrollEngine::stripSnapshot(const QString& screenId, const QString& excludeWindowId) const
{
    // Preview-owned screens resolve against the preview's CAPTURED key for
    // the same reason computeDragInsertTargetAtPoint does: a desktop or
    // activity switch mid-drag must not swap the strip under indices a
    // commit will apply to the captured one.
    const bool previewOwnsScreen = m_dragInsertPreview
        && PhosphorScreens::ScreenIdentity::screensMatch(m_dragInsertPreview->targetScreenId, screenId);
    const ScrollState* state = previewOwnsScreen ? m_states.stateForKey(m_dragInsertPreview->targetKey)
                                                 : m_states.stateForKey(currentKeyForScreen(screenId));
    if (!state) {
        return ScrollStripSnapshot();
    }
    const ScrollLayoutParams params =
        layoutParamsForScreen(previewOwnsScreen ? m_dragInsertPreview->targetScreenId : screenId);

    // Exclusion only matters while the drag window is still ATTACHED (no
    // live preview). Once a preview detached it, the strip resolved below no
    // longer contains it and a second removal would be a no-op by id anyway;
    // skipping the canonicalization keeps the common per-show path cheap.
    // PRECONDITION this rests on: callers pass only the window the live
    // preview detached (the daemon passes its active-drag id) — the drop is
    // unconditional on WHICH id arrives while a preview owns the screen.
    // TEST SEAM NOTE: the snapshot suite's fixtures run with a null window
    // registry, where canonicalizeForLookup returns its argument verbatim,
    // so the canonicalization itself is exercised only by the daemon's real
    // ids, not by these tests.
    const QString excluded =
        (!previewOwnsScreen && !excludeWindowId.isEmpty()) ? canonicalizeForLookup(excludeWindowId) : QString();
    return buildSnapshot(state->strip(), params, excluded, /*fillAbsolute=*/false);
}

ScrollStripSnapshot ScrollEngine::stripSnapshot(const PhosphorEngine::PlacementStateKey& key) const
{
    // Straight to the keyed state, never created and never redirected: the
    // overview asks about contexts that are not on screen, and a live
    // drag-preview's captured key is a fact about the current context's
    // popup, not about the key asked for here.
    const ScrollState* state = m_states.stateForKey(key);
    if (!state) {
        return ScrollStripSnapshot();
    }
    // layoutParamsForScreen resolves the context gap overrides for the
    // screen's CURRENT context (overridesForScreen keys on
    // currentKeyForScreen), so a non-current key is laid out with the
    // current context's gaps. Accepted: the overview draws thumbnails, a
    // per-context gap difference is a few pixels at that scale, and a
    // key-taking params resolver would need a second override path through
    // every gap reader to remove it.
    const ScrollLayoutParams params = layoutParamsForScreen(key.screenId);
    return buildSnapshot(state->strip(), params, QString(), /*fillAbsolute=*/true);
}

} // namespace PhosphorScrollEngine
