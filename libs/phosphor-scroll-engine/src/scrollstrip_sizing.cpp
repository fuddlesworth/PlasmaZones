// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollStrip.h>

#include <QtGlobal>

#include <algorithm>

namespace PhosphorScrollEngine {

int ScrollStrip::activeTileCrossPx(const ScrollLayoutParams& params) const
{
    // activeTileIdx, NOT activeWindowId(): the height verbs write through
    // activeTileMutable, which indexes activeTileIdx, and activeWindowId()
    // deliberately falls back to another tile when that one is minimized.
    // Measuring a different tile than the one about to be written is exactly
    // the mismatch that makes a verb report success while nothing moves.
    const Column* col = activeColumn();
    if (!col || col->activeTileIdx < 0 || col->activeTileIdx >= col->tiles.size()) {
        return -1;
    }
    const QString windowId = col->tiles.at(col->activeTileIdx).windowId;
    const ResolvedStrip resolved = relayout(params);
    for (const ResolvedColumn& rc : resolved.columns) {
        if (rc.columnIndex != m_activeColumnIdx) {
            continue;
        }
        for (const ResolvedTile& rt : rc.tiles) {
            if (rt.windowId == windowId) {
                return params.axis.crossSize(rt.rect);
            }
        }
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
    // anchor's original intent. Where the cycle ENTERS is niri's rule
    // (cyclePresetIndexByExtent, whose doc carries the parity note and the
    // wrap's reason): the nearest entry wider than what the column renders at
    // going forward, the nearest narrower going back, wrapping at each end to
    // the vocabulary's own narrowest and widest. The old "nearest
    // entry, step when it already matches" rule could answer a NARROWER
    // preset for a forward press — a column resized to sit just above one
    // entry entered at that entry and shrank.
    const int count = params.presetColumnWidths.size();
    const int workW = params.axis.mainSize(params.workArea);
    if (workW <= 0) {
        return false;
    }
    // The RENDERED extent, matching adjustActiveColumnWidth and the entry
    // rule's premise: a column pinned to its client minimum resolves to a
    // narrower intent than it draws, and entering from the intent would step
    // to a preset it is already wider than, so the press moved nothing.
    const int currentPx = columnExtentPx(*col, params);
    if (currentPx <= 0) {
        return false; // empty or fully minimized column: nothing to size
    }
    const int idx = cyclePresetIndexByExtent(count, currentPx, delta, [&](int i) {
        return resolveColumnWidthPx(ColumnWidth::makePreset(params.presetColumnWidths.at(i)), params);
    });
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
    // Measure from what is ON SCREEN, not from the bare intent: a column
    // sitting at its minimum-size floor resolves to a narrower intent than it
    // renders, and stepping from the intent would let a shrink write ever
    // smaller values while nothing moved, so the matching grow presses then
    // did nothing visible until they climbed back out of that hole.
    const int current = columnExtentPx(*col, params);
    if (current <= 0) {
        return false; // empty or fully minimized column: nothing to size
    }
    // Floor, the twin of the one adjustActiveWindowHeight applies across the
    // strip: the engine's declared narrowest column, raised to the column's
    // own client minimum when minimum sizes are respected. Every producer that
    // spells a width as a FRACTION already clamps against it (the config
    // read's Proportion arm, the rule overrides, the preset list parser, the
    // persisted blob), and without this the repeatable verb was the one that
    // could walk a column down to a single pixel and commit that extent to the
    // client. The px-valued spellings (the config read's Fixed arm, a px rule
    // override, a client-supplied open or handoff width) still floor at 1 by
    // design: those are one-shot values a user or a client asked for by
    // number, not a key that repeats while held.
    const int fractionFloor = qMax(1, qRound(MinColumnWidthFraction * workW));
    const int floorPx = qBound(1, qMax(fractionFloor, columnMinExtentPx(*col, params)), workW);
    // Lowered to the current extent when the column already renders BELOW the
    // floor, so a shrink can never widen it. That state is ordinary, not
    // pathological: every producer that clamps as a FRACTION resolves through
    // proportionalPx (round(f * (work + gap)) - gap), which lands a gap's
    // worth under fractionFloor's bare round(f * work) — minimizeActiveColumnWidth
    // writing Proportion(MinColumnWidthFraction) is exactly such a column. A
    // bare floorPx there would make the Shrink shortcut grow the column and
    // report success. A grow press is unaffected: its target clears the
    // current extent either way.
    const int lowerPx = qMin(floorPx, current);
    const int target = qBound(lowerPx, current + qRound(deltaPercent / 100.0 * workW), workW);
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
    // Degenerate work area, the same bail its sibling width verbs (cycle,
    // adjust, equalize, minimize) take.
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
    const int workW = params.axis.mainSize(params.workArea);
    if (workW <= 0) {
        return false; // degenerate area, the sibling width verbs' bail
    }
    // Measured from what is ON SCREEN, matching adjustActiveColumnWidth and
    // matching the `taken` walk below, which sums columnExtentPx. Stepping
    // from the bare intent would mismatch the two: a column held at its
    // client minimum resolves to a narrower intent than it renders, so target
    // could land BELOW the rendered extent, leaving the screen unchanged
    // while the verb reported success and buried the proportional anchor
    // under a smaller Fixed value.
    const int current = columnExtentPx(*col, params);
    if (current <= 0) {
        return false; // empty or fully minimized column: nothing to expand
    }
    // Already filling the viewport: niri's `col.is_full_width` early-out.
    // Taken before either toggle branch below so both of them MAXIMIZE — the
    // toggle's un-maximize arm is the wrong answer for a verb whose whole
    // promise is "grow".
    if (current >= workW) {
        return false;
    }
    // A centering policy pins the active column to the middle of the
    // viewport, so its position after the resize is not ours to choose and
    // "fill what is left" has no stable answer. niri takes the simple way out
    // here and so do we: maximize, which the user can back out of.
    if (isCenteringActiveColumn(params)) {
        return toggleMaximizeActiveColumn(params);
    }
    // niri's accounting: only the columns lying ENTIRELY in the viewport are
    // counted as taking space. A straddler's on-screen pixels are reclaimable
    // — the expansion pushes it out of view — which is the whole difference
    // between this and measuring the strip's covered interval, the form that
    // let a clipped neighbour eat the leftover it was about to be pushed out
    // of. The active column must itself be fully visible: for a straddler
    // there is no meaningful answer, since the leftover is measured against a
    // position the column does not fully occupy.
    const QVector<int> visible = fullyVisibleColumnIndices(params);
    if (!visible.contains(m_activeColumnIdx)) {
        return false;
    }
    int taken = params.gap * (visible.size() - 1);
    for (int idx : visible) {
        taken += columnExtentPx(m_columns.at(idx), params);
    }
    const int leftover = workW - taken;
    if (leftover <= 0) {
        return false;
    }
    // The active column is the only one fully on screen, so it is about to
    // take the whole viewport. niri routes that through the maximize toggle
    // rather than writing the width, "as it lets you back out of it more
    // intuitively" — a Fixed(workW) would strand the column at full width
    // with no un-maximize to undo it.
    if (visible.size() == 1) {
        return toggleMaximizeActiveColumn(params);
    }
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
    // FULLY visible columns, the walk centerVisibleColumns performs and the
    // one expandActiveColumnToAvailableWidth shares: a column clipped by
    // either edge is exactly what must not be dragged into the split.
    const QVector<int> visible = fullyVisibleColumnIndices(params);
    // One column has nothing to equalize against; "equal" would mean "full
    // width", which is maximize's job.
    const int n = visible.size();
    if (n < 2) {
        return false;
    }
    // The active column must be IN the group: the anchor is re-derived
    // below to put the group's first column at the lead edge, and an active
    // column that straddles an edge (a pan leaves it there) would be pushed
    // fully off screen by that, where a detached view would then keep it.
    if (!visible.contains(m_activeColumnIdx)) {
        return false;
    }
    // Shares of the MAIN extent net of the gaps BETWEEN the group, so the
    // group still tiles the viewport edge to edge afterwards.
    const int usable = workW - (n - 1) * params.gap;
    if (usable < n) {
        return false; // gaps alone outrun the viewport: no share is a pixel
    }
    // A column whose tiles' minimum exceeds its share cannot take it: what
    // renders is columnExtentPx, which floors at that minimum, so writing the
    // share would push the group past the trailing edge and then read back
    // as "already there" on the next press. Such a column keeps its floor
    // and the others split what is left, repeated until every share clears
    // the floors it was measured against.
    QVector<int> extents(n);
    QVector<bool> floorBound(n, false);
    for (int k = 0; k < n; ++k) {
        extents[k] = columnMinExtentPx(m_columns.at(visible.at(k)), params);
    }
    int pool = usable;
    int free = n;
    // Each round pins every free column whose floor exceeds the current
    // share; a round that pins nothing means the remaining shares clear
    // their floors, and a round that pins the last free column ends it.
    for (bool pinned = true; pinned && free > 0;) {
        pinned = false;
        const int share = pool / free;
        for (int k = 0; k < n; ++k) {
            if (!floorBound.at(k) && extents.at(k) > share) {
                floorBound[k] = true;
                pool -= extents.at(k);
                --free;
                pinned = true;
            }
        }
    }
    if (pool < free) {
        return false; // the floors alone outrun the viewport: nothing to share
    }
    // The remainder of the division goes to the LAST free column rather
    // than being dropped: dropping it would leave a sliver of dead space that
    // expand-column would then report as reclaimable. With every column
    // floor-bound there is nothing to equalize.
    if (free == 0) {
        return false;
    }
    const int share = pool / free;
    int remainder = pool - share * free;
    for (int k = n - 1; k >= 0; --k) {
        if (!floorBound.at(k)) {
            extents[k] = share + remainder;
            remainder = 0;
        }
    }
    bool changed = false;
    for (int k = 0; k < n; ++k) {
        Column& col = m_columns[visible.at(k)];
        const ColumnWidth target = ColumnWidth::makeFixed(extents.at(k));
        // Compared in RENDERED pixels, for toggleMaximizeActiveColumn's
        // reason: a Proportion that already renders to the share must not be
        // rewritten as a Fixed of the identical extent, which would move
        // nothing on screen while reporting success and destroying the
        // proportional anchor a later work-area change would honour.
        if (columnExtentPx(col, params) == extents.at(k)) {
            continue;
        }
        col.width = target;
        if (m_preMaximizeColumnIdx == visible.at(k)) {
            m_preMaximizeColumnIdx = -1;
        }
        changed = true;
    }
    // The anchor is ACTIVE-relative (viewOffset = columnStripPos(active) -
    // anchor), so rewriting a column LEAD of the active one would slide the
    // whole group under the active column's pinned screen position and clip
    // the first column at the lead edge. The shares were measured against
    // the whole viewport, so the group's lead column now belongs AT the lead
    // edge: re-derive the anchor to put it there. A lead-edge straddler is
    // pushed fully out of view by that, which is the only place a column
    // the group did not absorb can go.
    //
    // And DETACH the view, the way a pan does: edge to edge is this verb's
    // promise, and under Always the next layout pass would otherwise
    // re-center the active column, clip the group's last column, and hand a
    // second press a different group to split. A detached view is left
    // alone until the next focus change, so the second press finds the
    // group exactly as the first left it and refuses. Deliberately NOT a
    // centering verb (those re-attach): it chose a view position the policy
    // did not.
    if (changed) {
        m_viewAnchor = columnStripPos(m_activeColumnIdx, params) - columnStripPos(visible.first(), params);
        m_viewDetached = true;
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
    // would be stranded at a value nothing else uses. The list is deduplicated
    // at the boundary but NOT sorted: every producer (the settings schema's
    // canonicalProportionList, refreshConfigFromSettings' parsePresets, the
    // template override list) preserves the order the user typed, so the
    // minimum has to be searched for rather than read off the front.
    const QList<qreal>& presets = params.presetColumnWidths;
    const ColumnWidth target = presets.isEmpty()
        ? ColumnWidth::makeProportion(MinColumnWidthFraction)
        : ColumnWidth::makePreset(*std::min_element(presets.cbegin(), presets.cend()));
    // RENDERED-pixel compare, equalizeVisibleColumnWidths' reason: what the
    // user sees is columnExtentPx, the intent raised to the column's minimum
    // size floor. A column whose floor already sits at or above the smallest
    // preset would move nothing on screen if the intent were rewritten, and
    // the verb would report success for a no-op (then refuse the second
    // press). Comparing the floored target against the floored current width
    // makes the first press refuse too.
    const int floorPx = columnMinExtentPx(*col, params);
    if (columnExtentPx(*col, params) == qMax(resolveColumnWidthPx(target, params), floorPx)) {
        return false;
    }
    col->width = target;
    if (m_preMaximizeColumnIdx == m_activeColumnIdx) {
        m_preMaximizeColumnIdx = -1;
    }
    return true;
}

bool ScrollStrip::resetToDefaults(const std::optional<ColumnWidth>& defaultWidth, ColumnDisplay defaultDisplay,
                                  const ScrollLayoutParams& params)
{
    if (params.axis.mainSize(params.workArea) <= 0) {
        return false; // degenerate area: writes persisted intent, so bail
    }
    bool changed = false;
    for (Column& col : m_columns) {
        // Intent compare, not resolved pixels: this verb's promise is "back
        // to what the layout says", and a Fixed that happens to render to the
        // default's extent is still not the default intent — it would not
        // follow a later work-area change the way the default would. No
        // default width at all means the client's own size IS the default,
        // and the width the column opened at is the closest thing to it.
        if (defaultWidth && !(col.width == *defaultWidth)) {
            col.width = *defaultWidth;
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
    // would otherwise hand a stale intent to the next un-maximize. Only when
    // widths were actually rewritten: under "the client decides" the widths
    // stay as they are, a maximized column stays maximized, and clearing the
    // slot would make the next un-maximize fall back to the half-width
    // proportion instead of the width the user had before.
    if (defaultWidth) {
        m_preMaximizeColumnIdx = -1;
    }
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
    // Tabbed columns have no per-tile height, adjustActiveWindowHeight's
    // guard and its reason. It binds here too now that the entry rule reads
    // the RENDERED extent: relayout draws every tab at the column's whole
    // content rect, so the measurement is the same on every press and taller
    // than any preset — a forward press would answer the smallest entry
    // whatever is on screen, and the press after it would find that intent
    // already written and decline. Neither press moves a pixel.
    const Column* activeCol = activeColumn();
    if (activeCol && activeCol->display == ColumnDisplay::Tabbed) {
        return false;
    }
    const int workH = params.axis.crossSize(params.workArea);
    if (workH <= 0) {
        return false; // degenerate area, the sibling verbs' bail
    }
    // ONE path, mirroring the width cycle's shape and its niri entry rule
    // (see cyclePresetIndexByExtent). Measured off a fresh relayout, which is
    // what an AUTO tile needs: it has no fraction of its own, and the old
    // "no determinate fraction" arm entered at vocabulary entry 0 regardless
    // of the height on screen, so the first press on an evenly-split column
    // could shrink or grow the tile depending only on where entry 0 sat.
    const int currentPx = activeTileCrossPx(params);
    if (currentPx < 0) {
        return false;
    }
    // Each entry resolved the way relayout's Preset arm resolves it, so the
    // entry the cycle picks is the extent that will actually render. The
    // column's own Auto/Fixed budget (availH) caps it there and so does it
    // here; without the cap two entries taller than the budget would both
    // resolve past it and the walk could pick one that renders identically
    // to the current height.
    int visibleTiles = 0;
    if (activeCol) {
        for (const Tile& t : activeCol->tiles) {
            if (!t.minimized) {
                ++visibleTiles;
            }
        }
    }
    const int availH = qMax(qMax(1, visibleTiles), workH - params.gap * qMax(0, visibleTiles - 1));
    const int idx = cyclePresetIndexByExtent(params.presetWindowHeights.size(), currentPx, delta, [&](int i) {
        return qMin(availH, proportionalPx(params.presetWindowHeights.at(i), workH, params.gap));
    });
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
    // Tabbed columns have no per-tile height to adjust: relayout lays every
    // visible tile out at the column's whole content rect and never reads
    // Tile::height there, so the measured extent is the same on every press
    // and a write would move nothing while the verb reported success forever.
    // Refuse instead. cycleActiveWindowPresetHeight carries the same guard,
    // for the same reason once its entry rule reads the rendered extent.
    // setActiveWindowHeight deliberately does NOT: it promises an INTENT
    // change, which stays true while tabbed, and the restore and handoff
    // paths write intent into tabbed columns on purpose.
    const Column* activeCol = activeColumn();
    if (activeCol && activeCol->display == ColumnDisplay::Tabbed) {
        return false;
    }
    const int workH = params.axis.crossSize(params.workArea);
    if (workH <= 0) {
        return false; // degenerate area: qBound(1, …, workH) would invert
    }
    // Current pixel height: read off a fresh relayout so the adjustment
    // starts from what is actually on screen (activeTileCrossPx's doc carries
    // the why). The preset cycle enters from the same measurement.
    const int currentPx = activeTileCrossPx(params);
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
    // respectMinimumSize off, relayout stops re-clamping too, so the CLIENT
    // half of the floor drops out (keeping it would invert the failure: the
    // verb would refuse a shrink relayout would happily apply). The engine's
    // own fraction floor, below, is what remains underneath.
    // minCross, matching the clamp relayout applies to this same value.
    // Capped at workH as well: a client cross-minimum LARGER than the work
    // area (reachable through a work-area shrink after the minimum was
    // recorded) would otherwise invert the qBound below (min > max is UB).
    //
    // The engine's own declared shortest tile is the floor under that, so the
    // verb cannot walk a window down to a single pixel with minimum sizes
    // off. The producers that spell a height as a FRACTION clamp against
    // MinWindowHeightFraction (the preset list parser, the per-screen preset
    // override list, the persisted blob's Preset arm, the open rule's height
    // fraction), and this one did not. Height has no Proportion spelling at
    // all. The rule channel's default window height is gated differently but
    // to the same number: it is range-CHECKED at the rules boundary, where an
    // out-of-range fraction is rejected outright rather than clamped, against
    // PhosphorRules::MinColumnWidthRatio — the constant ScrollTypes.h keeps in
    // sync with MinWindowHeightFraction. So the engine-side 1px floor it
    // commits with is defensive on an already-gated value.
    const int fractionFloor = qMax(1, qRound(MinWindowHeightFraction * workH));
    const int clientFloor = params.respectMinimumSize ? tile->minCross(params.axis) : 0;
    const int floorPx = qBound(1, qMax(fractionFloor, clientFloor), workH);
    // Lowered to the current height when the tile already renders below the
    // floor, the twin of the guard adjustActiveColumnWidth documents: the
    // smallest LEGAL preset height resolves through proportionalPx to a gap's
    // worth under fractionFloor, and a crowded column's Auto share can land
    // under it too, so a bare floorPx would let a Shrink press grow the
    // window.
    const int lowerPx = qMin(floorPx, currentPx);
    const int target = qBound(lowerPx, currentPx + qRound(deltaPercent / 100.0 * workH), workH);
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
