// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollStrip.h>

#include <QtGlobal>

namespace PhosphorScrollEngine {

qreal ScrollStrip::currentHeightFraction(const Tile& t, const ScrollLayoutParams& params) const
{
    switch (t.height.kind) {
    case WindowHeight::Preset:
        // Snapped, matching relayout's resolution of the same anchor.
        return nearestPresetValue(params.presetWindowHeights, t.height.presetFraction, -1);
    case WindowHeight::Fixed: {
        // A tile's Fixed extent is pixels ACROSS the strip, so it becomes a
        // fraction of the cross extent — via the EXACT inverse of relayout's
        // proportionalPx (px = round(f * (cross + gap)) - gap), the same
        // (px + gap) / (extent + gap) form the width cycle uses. The bare
        // px / cross form was biased by roughly gap/cross and could enter
        // the preset cycle one entry off when the vocabulary's stops sit
        // closer together than the gap.
        const int cross = params.axis.crossSize(params.workArea);
        return cross > 0 ? static_cast<qreal>(t.height.fixedPx + params.gap) / (cross + params.gap) : -1;
    }
    case WindowHeight::Auto:
        return -1;
    }
    return -1;
}

bool ScrollStrip::setActiveColumnWidth(const ColumnWidth& width)
{
    Column* col = activeColumnMutable();
    if (!col || col->width == width) {
        return false;
    }
    col->width = width;
    if (m_preMaximizeColumnIdx == m_activeColumnIdx) {
        m_preMaximizeColumnIdx = -1;
    }
    return true;
}

bool ScrollStrip::cycleActiveColumnPresetWidth(int delta, const ScrollLayoutParams& params)
{
    Column* col = activeColumnMutable();
    if (!col || params.presetColumnWidths.isEmpty() || (delta != -1 && delta != 1)) {
        return false;
    }
    // ONE path for every current kind — the F30 fix: there is no index to
    // read back, so a short template vocabulary can never rewrite the
    // anchor's original intent. Enter the cycle at the vocabulary entry
    // nearest the CURRENT on-screen size (a Preset anchor RESOLVES to that
    // entry, so the equality test below is true for it and the press steps,
    // same stepping the index cycle had); a non-preset width steps only when
    // the nearest entry already matches, so the first press always lands on
    // a visible change. proportionalPx is monotone in the fraction, so the
    // exact-inverse fraction below selects the same nearest entry the old
    // pixel-space probe did.
    const int count = params.presetColumnWidths.size();
    const int workW = params.axis.mainSize(params.workArea);
    if (workW <= 0) {
        return false;
    }
    const int currentPx = resolveColumnWidthPx(col->width, params);
    const qreal currentFraction = qreal(currentPx + params.gap) / (workW + params.gap);
    int idx = nearestPresetIndex(params.presetColumnWidths, currentFraction);
    const int nearPx = resolveColumnWidthPx(ColumnWidth::makePreset(params.presetColumnWidths.at(idx)), params);
    if (qAbs(nearPx - currentPx) <= 1) {
        idx = (idx + delta + count) % count;
    }
    const ColumnWidth result = ColumnWidth::makePreset(params.presetColumnWidths.at(idx));
    if (col->width == result) {
        // Single-entry preset list (or a step that landed where we already
        // are): report no change so the engine skips a pointless relayout.
        return false;
    }
    col->width = result;
    if (m_preMaximizeColumnIdx == m_activeColumnIdx) {
        m_preMaximizeColumnIdx = -1;
    }
    return true;
}

bool ScrollStrip::adjustActiveColumnWidth(qreal deltaPercent, const ScrollLayoutParams& params)
{
    Column* col = activeColumnMutable();
    if (!col || qFuzzyIsNull(deltaPercent)) {
        return false;
    }
    const int workW = params.axis.mainSize(params.workArea);
    if (workW <= 0) {
        return false; // degenerate area: qBound(1, …, workW) would invert
    }
    const int current = resolveColumnWidthPx(col->width, params);
    const int target = qBound(1, current + qRound(deltaPercent / 100.0 * workW), workW);
    if (target == current) {
        return false;
    }
    col->width = ColumnWidth::makeFixed(target);
    if (m_preMaximizeColumnIdx == m_activeColumnIdx) {
        m_preMaximizeColumnIdx = -1;
    }
    return true;
}

bool ScrollStrip::toggleMaximizeActiveColumn(const ScrollLayoutParams& params)
{
    Column* col = activeColumnMutable();
    if (!col) {
        return false;
    }
    // Degenerate work area, the same bail its three sibling width verbs take.
    // This one writes PERSISTED intent: with a zero main extent the
    // full-width compare below reads true for anything, and the branch would
    // overwrite the column's stored width with a half-work-area proportion
    // and report success against a viewport that does not exist.
    if (params.axis.mainSize(params.workArea) <= 0) {
        return false;
    }
    const ColumnWidth full = ColumnWidth::makeProportion(1.0);
    if (m_preMaximizeColumnIdx == m_activeColumnIdx && col->width == full) {
        col->width = m_preMaximizeWidth;
        m_preMaximizeColumnIdx = -1;
        return true;
    }
    if (col->width == full) {
        // Full-width without a stored intent for THIS column (maximized in
        // an earlier session, or another column's maximize discarded the
        // single stored slot): fall back to the default width so the toggle
        // can always un-maximize instead of dead-ending.
        //
        // A user whose DEFAULT is itself full width would dead-end on that
        // fallback, so take half the work area in that case. The point of the
        // branch is that the toggle always does something.
        //
        // Compared in RESOLVED PIXELS, not on the intent value. ColumnWidth's
        // operator== compares kind first, so a default spelled Fixed(<work
        // area width>) is not == Proportion(1.0) even though it renders
        // identically: the old value compare took the "not full" arm, left
        // the column visually unchanged, and then reported TRUE — a false
        // success OSD on the exact dead-end this branch exists to remove.
        const bool defaultIsFullWidth =
            resolveColumnWidthPx(params.defaultColumnWidth, params) >= params.axis.mainSize(params.workArea);
        col->width = defaultIsFullWidth ? ColumnWidth::makeProportion(0.5) : params.defaultColumnWidth;
        // Unconditionally true: both arms leave the column narrower than the
        // full width it had on entry, so the toggle always did something.
        return true;
    }
    m_preMaximizeWidth = col->width;
    m_preMaximizeColumnIdx = m_activeColumnIdx;
    col->width = full;
    return true;
}

bool ScrollStrip::expandActiveColumnToAvailableWidth(const ScrollLayoutParams& params)
{
    Column* col = activeColumnMutable();
    if (!col) {
        return false;
    }
    // On-screen leftover: the columns are contiguous in strip space, so the
    // occupied viewport region is one interval — everything outside it is
    // reclaimable.
    const int workW = params.axis.mainSize(params.workArea);
    const int viewOffset = viewOffsetFor(params);
    const int stripW = stripExtentPx(params);
    const int covered = qMax(0, qMin(workW, stripW - viewOffset) - qMax(0, -viewOffset));
    const int leftover = workW - covered;
    if (leftover <= 0) {
        return false;
    }
    const int current = resolveColumnWidthPx(col->width, params);
    const int target = qMin(workW, current + leftover);
    if (target == current) {
        // Same pixels: rewriting the intent (a Proportion(1.0) becoming a
        // Fixed of the identical extent) moves nothing on screen but would
        // report success and destroy the proportional anchor a later
        // work-area change would have honoured.
        return false;
    }
    col->width = ColumnWidth::makeFixed(target);
    if (m_preMaximizeColumnIdx == m_activeColumnIdx) {
        m_preMaximizeColumnIdx = -1;
    }
    return true;
}

bool ScrollStrip::equalizeVisibleColumnWidths(const ScrollLayoutParams& params)
{
    const int workW = params.axis.mainSize(params.workArea);
    if (workW <= 0) {
        return false; // degenerate area, the sibling width verbs' bail
    }
    // FULLY visible columns, the walk centerVisibleColumns performs: a
    // column clipped by either edge is exactly what must not be dragged into
    // the split. Zero-extent (fully minimized) columns carry no strip
    // position and are skipped the way stripExtentPx skips them.
    const int viewOffset = viewOffsetFor(params);
    QVector<int> visible;
    int colMainPos = 0;
    for (int i = 0; i < m_columns.size(); ++i) {
        const int colMain = columnExtentPx(m_columns.at(i), params);
        if (colMain <= 0) {
            continue;
        }
        const int pos = colMainPos - viewOffset;
        if (pos >= 0 && pos + colMain <= workW) {
            visible.append(i);
        }
        colMainPos += colMain + params.gap;
    }
    // One column has nothing to equalize against; "equal" would mean "full
    // width", which is maximize's job.
    const int n = visible.size();
    if (n < 2) {
        return false;
    }
    // Shares of the MAIN extent net of the gaps BETWEEN the group, so the
    // group still tiles the viewport edge to edge afterwards. The remainder
    // of the division goes to the last column rather than being dropped:
    // dropping it would leave a sliver of dead space that expand-column
    // would then report as reclaimable.
    const int usable = workW - (n - 1) * params.gap;
    if (usable < n) {
        return false; // gaps alone outrun the viewport: no share is a pixel
    }
    const int share = usable / n;
    const int remainder = usable - share * n;
    bool changed = false;
    for (int k = 0; k < n; ++k) {
        Column& col = m_columns[visible.at(k)];
        const ColumnWidth target = ColumnWidth::makeFixed(share + (k == n - 1 ? remainder : 0));
        // Compared in RESOLVED pixels, for toggleMaximizeActiveColumn's
        // reason: a Proportion that already renders to the share must not be
        // rewritten as a Fixed of the identical extent, which would move
        // nothing on screen while reporting success and destroying the
        // proportional anchor a later work-area change would honour.
        if (resolveColumnWidthPx(col.width, params) == resolveColumnWidthPx(target, params)) {
            continue;
        }
        col.width = target;
        if (m_preMaximizeColumnIdx == visible.at(k)) {
            m_preMaximizeColumnIdx = -1;
        }
        changed = true;
    }
    return changed;
}

bool ScrollStrip::minimizeActiveColumnWidth(const ScrollLayoutParams& params)
{
    Column* col = activeColumnMutable();
    if (!col) {
        return false;
    }
    if (params.axis.mainSize(params.workArea) <= 0) {
        return false; // degenerate area, same bail as the sibling verbs
    }
    // The smallest preset is the narrowest width the user has NAMED, which
    // beats the engine floor when a list exists: a Preset intent follows the
    // vocabulary if the list is later edited, where a Proportion at the floor
    // would be stranded at a value nothing else uses. The list is sorted and
    // deduplicated at the boundary (ScrollTypes.h's preset-list contract), so
    // the first entry is the minimum.
    const ColumnWidth target = params.presetColumnWidths.isEmpty()
        ? ColumnWidth::makeProportion(MinColumnWidthFraction)
        : ColumnWidth::makePreset(params.presetColumnWidths.first());
    // Resolved-pixel compare, toggleMaximizeActiveColumn's reason.
    if (resolveColumnWidthPx(col->width, params) == resolveColumnWidthPx(target, params)) {
        return false;
    }
    col->width = target;
    if (m_preMaximizeColumnIdx == m_activeColumnIdx) {
        m_preMaximizeColumnIdx = -1;
    }
    return true;
}

bool ScrollStrip::resetToDefaults(ColumnDisplay defaultDisplay, const ScrollLayoutParams& params)
{
    if (params.axis.mainSize(params.workArea) <= 0) {
        return false; // degenerate area: writes persisted intent, so bail
    }
    bool changed = false;
    for (Column& col : m_columns) {
        // Intent compare, not resolved pixels: this verb's promise is "back
        // to what the layout says", and a Fixed that happens to render to the
        // default's extent is still not the default intent — it would not
        // follow a later work-area change the way the default would.
        if (!(col.width == params.defaultColumnWidth)) {
            col.width = params.defaultColumnWidth;
            changed = true;
        }
        if (col.display != defaultDisplay) {
            col.display = defaultDisplay;
            changed = true;
        }
        for (Tile& tile : col.tiles) {
            const WindowHeight even = WindowHeight::makeAuto();
            if (!(tile.height == even)) {
                tile.height = even;
                changed = true;
            }
        }
    }
    // Nothing is maximized once every column is at the default, so the slot
    // would otherwise hand a stale intent to the next un-maximize.
    m_preMaximizeColumnIdx = -1;
    return changed;
}

bool ScrollStrip::setActiveWindowHeight(const WindowHeight& height)
{
    // Lone tiles included: relayout honors an explicit Fixed/Preset height
    // for a solo tile (niri parity), so the write is meaningful at any
    // stack size.
    Tile* tile = activeTileMutable();
    if (!tile || tile->height == height) {
        return false;
    }
    tile->height = height;
    return true;
}

bool ScrollStrip::cycleActiveWindowPresetHeight(int delta, const ScrollLayoutParams& params)
{
    // Lone tiles included: see setActiveWindowHeight.
    Tile* tile = activeTileMutable();
    if (!tile || params.presetWindowHeights.isEmpty() || (delta != -1 && delta != 1)) {
        return false;
    }
    // ONE path, mirroring the width cycle's value-anchored shape (see its
    // comment): enter at the nearest vocabulary entry to the current height
    // fraction, step when it already matches (a Preset anchor resolves to
    // it, so a press on a preset tile always steps; Auto has no determinate
    // fraction and enters at the first entry without stepping).
    const int count = params.presetWindowHeights.size();
    const qreal curFrac = currentHeightFraction(*tile, params);
    int idx = curFrac < 0 ? 0 : nearestPresetIndex(params.presetWindowHeights, curFrac);
    if (curFrac >= 0 && qAbs(params.presetWindowHeights.at(idx) - curFrac) < 0.01) {
        idx = (idx + delta + count) % count;
    }
    const WindowHeight result = WindowHeight::makePreset(params.presetWindowHeights.at(idx));
    if (tile->height == result) {
        return false;
    }
    tile->height = result;
    return true;
}

bool ScrollStrip::adjustActiveWindowHeight(qreal deltaPercent, const ScrollLayoutParams& params)
{
    // Lone tiles included: see setActiveWindowHeight.
    Tile* tile = activeTileMutable();
    if (!tile || qFuzzyIsNull(deltaPercent)) {
        return false;
    }
    const int workH = params.axis.crossSize(params.workArea);
    if (workH <= 0) {
        return false; // degenerate area: qBound(1, …, workH) would invert
    }
    // Current pixel height: read it off a fresh relayout so the adjustment
    // starts from what is actually on screen. A targeted per-tile helper
    // cannot replace this: an Auto height only gets a pixel value from the
    // full column distribution (floors, budget rebalance), so the relayout
    // IS the resolution. Shortcut-rate path, not per-frame.
    int currentPx = -1;
    const ResolvedStrip resolved = relayout(params);
    for (const ResolvedColumn& rc : resolved.columns) {
        if (rc.columnIndex != m_activeColumnIdx) {
            continue;
        }
        for (const ResolvedTile& rt : rc.tiles) {
            if (rt.windowId == tile->windowId) {
                currentPx = params.axis.crossSize(rt.rect);
            }
        }
    }
    if (currentPx < 0) {
        // The active tile resolved to nothing — a minimized tile is dropped
        // from the relayout entirely. Seeding the full work area instead
        // would compute the delta from a height the window never had. Not
        // reachable while the daemon models minimize as a float; the test
        // seam can drive it.
        return false;
    }
    // Clamp to the tile's own floor as well (niri clamps against the client
    // min size in the verb): without it a shrink below minHeight would
    // "succeed" here while relayout re-clamps, so every further press
    // reports success with nothing moving on screen. With
    // respectMinimumSize off, relayout stops re-clamping too, so the floor
    // drops to 1 — keeping it would invert the failure (the verb refusing a
    // shrink relayout would happily apply).
    // minCross, matching the clamp relayout applies to this same value.
    // Capped at workH as well: a client cross-minimum LARGER than the work
    // area (reachable through a work-area shrink after the minimum was
    // recorded) would otherwise invert the qBound below (min > max is UB).
    const int floorPx = params.respectMinimumSize ? qBound(1, tile->minCross(params.axis), workH) : 1;
    const int target = qBound(floorPx, currentPx + qRound(deltaPercent / 100.0 * workH), workH);
    if (target == currentPx) {
        return false;
    }
    tile->height = WindowHeight::makeFixed(target);
    return true;
}

bool ScrollStrip::setWindowHeightIntent(const QString& windowId, const WindowHeight& height)
{
    const int colIdx = columnOfWindow(windowId);
    if (colIdx < 0) {
        return false;
    }
    Column& col = m_columns[colIdx];
    Tile& tile = col.tiles[col.indexOfWindow(windowId)];
    if (tile.height == height) {
        return false;
    }
    tile.height = height;
    return true;
}

bool ScrollStrip::resetActiveColumnHeights()
{
    Column* col = activeColumnMutable();
    if (!col) {
        return false;
    }
    bool changed = false;
    for (Tile& tile : col->tiles) {
        const WindowHeight even = WindowHeight::makeAuto();
        if (!(tile.height == even)) {
            tile.height = even;
            changed = true;
        }
    }
    return changed;
}

bool ScrollStrip::reconcileWindowSize(const QString& windowId, const QSize& ackedSize, bool mainChanged,
                                      bool crossChanged, const ScrollLayoutParams& params)
{
    const int colIdx = columnOfWindow(windowId);
    // isEmpty (not merely isValid): a 0x0 ack is "valid" to QSize but would
    // reconcile into a 1px column.
    if (colIdx < 0 || ackedSize.isEmpty()) {
        return false;
    }
    Column& col = m_columns[colIdx];
    bool changed = false;
    // Only take the column's extent when the resize actually MOVED it along
    // the strip (the engine compares against the last applied rect): a resize
    // purely ACROSS the strip must not convert a Proportion/Preset intent into
    // Fixed pixels — that would stop the column reflowing on work-area,
    // preset-list, and DPI changes.
    //
    // The acked size is a PHYSICAL QSize from the compositor, so it has to be
    // decoded by role here. Reading .width() unconditionally would, on a
    // vertical strip, feed the cross extent into the column's intent while the
    // guard above still spoke about the main one — the resize would appear to
    // work and then relayout into the wrong shape, with both intents pinned to
    // Fixed pixels on the wrong axes.
    if (mainChanged) {
        const ColumnWidth acked = ColumnWidth::makeFixed(params.axis.mainSize(ackedSize));
        if (!(col.width == acked)) {
            col.width = acked;
            changed = true;
            // Same invariant as every other width mutator in this file: a
            // width write invalidates a pending maximize-toggle restore for
            // this column. Unlike those mutators this one is keyed on the
            // RESIZED column, which need not be the active one.
            if (m_preMaximizeColumnIdx == colIdx) {
                m_preMaximizeColumnIdx = -1;
            }
        }
    }
    // Same guard as the column's: a resize purely ALONG the strip must not
    // convert the tile's cross-axis intent into Fixed pixels. Lone tiles
    // included — relayout honors a solo tile's Fixed height (niri parity), so
    // an interactive cross-axis resize of a lone window sticks instead of
    // snapping back.
    if (crossChanged) {
        Tile& tile = col.tiles[col.indexOfWindow(windowId)];
        const WindowHeight ackedH = WindowHeight::makeFixed(params.axis.crossSize(ackedSize));
        if (!(tile.height == ackedH)) {
            tile.height = ackedH;
            changed = true;
        }
    }
    return changed;
}

} // namespace PhosphorScrollEngine
