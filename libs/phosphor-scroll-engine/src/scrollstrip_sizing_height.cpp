// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// ScrollStrip — the window-HEIGHT vocabulary.
//
// Split out of scrollstrip_sizing.cpp when the width and height families
// together passed the file-size ceiling. The two halves are the same shape and
// deliberately mirror each other verb for verb, so read a height verb beside
// its width twin: the twin's comment usually carries the shared reasoning and
// the height one carries only what differs.
//
// The one difference worth knowing before reading any of them is the space
// each family measures in. A width verb measures against the RAW viewport, so
// the gaps between columns are space it must account for itself. A height verb
// measures against activeColumnCrossBudgetPx, which has ALREADY subtracted the
// gaps between the column's visible tiles, and relayout keeps those gaps
// outside the tile rects. Copying a width verb's gap arithmetic across the
// axis therefore subtracts the gaps twice.
//
// The shared helpers (activeTileCrossPx, activeColumnCrossBudgetPx,
// activeWindowHeightFloorPx, claimTabbedHeightOwnership) stay in
// scrollstrip_sizing.cpp, which is also where reconcileWindowSize lives — it
// claims tabbed height ownership on a cross-axis ack the same way these verbs
// do.

#include <PhosphorScrollEngine/ScrollStrip.h>

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace PhosphorScrollEngine {

bool ScrollStrip::setActiveWindowHeight(const WindowHeight& height)
{
    // Lone tiles included: relayout honors an explicit Fixed/Preset height
    // for a solo tile (niri parity), so the write is meaningful at any
    // stack size.
    Column* col = activeColumnMutable();
    if (!col || col->activeTileIdx < 0 || col->activeTileIdx >= col->tiles.size()) {
        return false;
    }
    const int ti = col->activeTileIdx;
    // Ownership BEFORE the no-change bail: moving the owner pointer moves the
    // tabbed column even when this tile already holds the height asked for
    // (the column resolves THROUGH the owner), so answering false there would
    // report "nothing happened" for a relayout that does.
    const bool claimed = claimTabbedHeightOwnership(*col, ti, height);
    // A height verb on the column drops a maximize-to-edges override (the
    // user is sizing the stack the flag was overriding).
    //
    // Unconditional while the flag is set, with no equality test against the
    // stored intent. Under the override the column does not RENDER its stored
    // intents at all — relayout resolves every tile as a weighted Auto share of
    // the raw cross extent — so a request that happens to equal the stored
    // value is still a real change: clearing the flag re-renders the intent.
    // Testing equality here made exactly that request a silent no-op, on the
    // one verb whose job is to take the stack back out of the override.
    const bool clearedEdges = col->maximizedToEdges;
    if (clearedEdges) {
        col->maximizedToEdges = false;
    }
    // Above the bail, unlike the adjust and cycle verbs. Theirs test "this
    // press moved no pixels", which is a refusal and not a countermand, so
    // clearing there would let a held-down key at its limit wipe the memory.
    // This one tests INTENT equality: the caller named a height and got it,
    // even when the tile already held it, so the maximize it may have been
    // sitting in is over. Reachable because a slot-bearing tile holds exactly
    // Fixed(the budget at maximize time), which a rule or a D-Bus caller can
    // name.
    col->tiles[ti].preMaximizeHeight.reset();
    if (col->tiles.at(ti).height == height) {
        return claimed || clearedEdges;
    }
    col->tiles[ti].height = height;
    return true;
}

bool ScrollStrip::cycleActiveWindowPresetHeight(int delta, const ScrollLayoutParams& params)
{
    // Lone tiles included: see setActiveWindowHeight.
    Tile* tile = activeTileMutable();
    if (!tile || params.presetWindowHeights.isEmpty() || (delta != -1 && delta != 1)) {
        return false;
    }
    // activeCol is dereferenced unguarded below, on the same by-construction
    // reasoning adjustActiveWindowHeight spells out: activeTileMutable answers
    // a tile only when activeColumnMutable answered a column, so the !tile
    // bail already covers the null case. Kept as one story rather than
    // guarding here and not there, which reads as a real nullability
    // difference between two halves of the same function.
    Column* activeCol = activeColumnMutable();
    // A tabbed column sizes ITSELF from this tile's intent
    // (tabbedColumnCrossPx), so the press works there as well; what changes is
    // the space the comparison happens in. See the reservation below.
    const bool tabbed = activeCol->display == ColumnDisplay::Tabbed;
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
    int currentPx = activeTileCrossPx(params);
    if (currentPx < 0) {
        return false;
    }
    if (tabbed) {
        // A tab is committed at the column's CONTENT rect, so what the
        // relayout measures is the column minus whatever the indicator
        // reserved across the strip, while the intent below sizes the whole
        // column. Compare in the column's space or the entry rule enters one
        // reservation-width off, and a press could answer the entry the
        // column already renders at.
        currentPx += tabbedCrossReservationPx(*activeCol, params);
    }
    // Each entry resolved the way relayout's Preset arm resolves it, so the
    // entry the cycle picks is the extent that will actually render. The
    // column's own Auto/Fixed budget (availH) caps it there and so does it
    // here; without the cap two entries taller than the budget would both
    // resolve past it and the walk could pick one that renders identically
    // to the current height.
    const int availH = activeColumnCrossBudgetPx(params);
    const int idx = cyclePresetIndexByExtent(params.presetWindowHeights.size(), currentPx, delta, [&](int i) {
        return qMin(availH, proportionalPx(params.presetWindowHeights.at(i), workH, params.gap));
    });
    const WindowHeight result = WindowHeight::makePreset(params.presetWindowHeights.at(idx));
    // Ownership before the no-change bail, setActiveWindowHeight's reason:
    // a sibling that still carried an intent was deciding this column's
    // extent, and taking it over is a move even when the step lands on the
    // entry this tile already held.
    //
    // Written through the COLUMN from here on, not the cached tile pointer:
    // the claim indexes the tile vector, and a strip that is implicitly
    // shared (drag_preview copies one to probe a drop) detaches on that
    // write, which would leave the cached pointer aimed at the old buffer.
    const int ti = activeCol->activeTileIdx;
    const bool claimed = claimTabbedHeightOwnership(*activeCol, ti, result);
    // Height verb rule: a maximize-to-edges override drops with any change,
    // the claim included (setActiveWindowHeight carries the reason).
    const bool clearedEdges = activeCol->maximizedToEdges && (claimed || !(activeCol->tiles.at(ti).height == result));
    if (clearedEdges) {
        activeCol->maximizedToEdges = false;
    }
    if (activeCol->tiles.at(ti).height == result) {
        return claimed || clearedEdges;
    }
    activeCol->tiles[ti].height = result;
    activeCol->tiles[ti].preMaximizeHeight.reset();
    return true;
}

bool ScrollStrip::adjustActiveWindowHeight(qreal deltaPercent, const ScrollLayoutParams& params)
{
    // Non-finite refused at the boundary, for adjustActiveColumnWidth's reason.
    if (!std::isfinite(deltaPercent)) {
        return false;
    }
    // Lone tiles included: see setActiveWindowHeight.
    Tile* tile = activeTileMutable();
    if (!tile || qFuzzyIsNull(deltaPercent)) {
        return false;
    }
    // A tabbed column adjusts too, in the same space the preset cycle
    // measures in: the intent sizes the COLUMN there, and the tab is
    // committed at the column minus the indicator's cross-axis reservation.
    Column* activeCol = activeColumnMutable();
    const bool tabbed = activeCol && activeCol->display == ColumnDisplay::Tabbed;
    const int reservationPx = tabbed ? tabbedCrossReservationPx(*activeCol, params) : 0;
    const int workH = params.axis.crossSize(params.workArea);
    if (workH <= 0) {
        return false; // degenerate area: qBound(1, …, workH) would invert
    }
    // Current pixel height: read off a fresh relayout so the adjustment
    // starts from what is actually on screen (activeTileCrossPx's doc carries
    // the why). The preset cycle enters from the same measurement.
    int currentPx = activeTileCrossPx(params);
    if (currentPx < 0) {
        // The active tile resolved to nothing — a minimized tile is dropped
        // from the relayout entirely. Seeding the full work area instead
        // would compute the delta from a height the window never had. Not
        // reachable while the daemon models minimize as a float; the test
        // seam can drive it.
        return false;
    }
    // Into the column's space while tabbed, the cycle's adjustment and its
    // reason: what gets written below is the extent tabbedColumnCrossPx will
    // resolve, and that is the COLUMN's.
    currentPx += reservationPx;
    // Clamp to the tile's own floor as well (niri clamps against the client
    // min size in the verb): without it a shrink below minHeight would
    // "succeed" here while relayout re-clamps, so every further press
    // reports success with nothing moving on screen. The floor is shared with
    // the minimize and expand verbs, which have to agree with this one or a
    // press would refuse where its sibling succeeded on the same tile; see
    // activeWindowHeightFloorPx for what goes into it.
    //
    // Worth naming here because it is the one number in the height family
    // that is gated twice: the rule channel's default window height is
    // range-CHECKED at the rules boundary against
    // PhosphorRules::MinColumnWidthRatio — the constant ScrollTypes.h keeps in
    // sync with MinWindowHeightFraction — where an out-of-range fraction is
    // rejected outright rather than clamped. So the engine-side floor is
    // defensive on an already-gated value.
    const int floorPx = activeWindowHeightFloorPx(params);
    if (floorPx < 0) {
        return false;
    }
    // Lowered to the current height when the tile already renders below the
    // floor, the twin of the guard adjustActiveColumnWidth documents: the
    // smallest LEGAL preset height resolves through proportionalPx to a gap's
    // worth under fractionFloor, and a crowded column's Auto share can land
    // under it too, so a bare floorPx would let a Shrink press grow the
    // window.
    const int lowerPx = qMin(floorPx, currentPx);
    const int target = qBound(lowerPx, currentPx + qRound(deltaPercent / 100.0 * workH), workH);
    const WindowHeight result = WindowHeight::makeFixed(target);
    // Ownership before the no-move bail, and written through the COLUMN
    // rather than the cached tile pointer — cycleActiveWindowPresetHeight
    // carries both reasons.
    // activeCol is non-null here by construction: activeTileMutable answers a
    // tile only when activeColumnMutable answered a column, and the !tile bail
    // at the top already took the other case.
    const int ti = activeCol->activeTileIdx;
    const bool claimed = claimTabbedHeightOwnership(*activeCol, ti, result);
    // Height verb rule: a maximize-to-edges override drops with any change,
    // the claim included (setActiveWindowHeight carries the reason).
    const bool clearedEdges = activeCol->maximizedToEdges && (claimed || target != currentPx);
    if (clearedEdges) {
        activeCol->maximizedToEdges = false;
    }
    if (target == currentPx) {
        return claimed || clearedEdges;
    }
    activeCol->tiles[ti].height = result;
    activeCol->tiles[ti].preMaximizeHeight.reset();
    return true;
}

bool ScrollStrip::toggleMaximizeActiveWindowHeight(const ScrollLayoutParams& params)
{
    // Lone tiles included: see setActiveWindowHeight.
    Tile* tile = activeTileMutable();
    if (!tile) {
        return false;
    }
    // Non-null by construction, adjustActiveWindowHeight's reasoning:
    // activeTileMutable answers a tile only when activeColumnMutable answered
    // a column.
    Column* activeCol = activeColumnMutable();
    const bool tabbed = activeCol->display == ColumnDisplay::Tabbed;
    const int workH = params.axis.crossSize(params.workArea);
    const int budget = activeColumnCrossBudgetPx(params);
    if (workH <= 0 || budget <= 0) {
        return false; // degenerate area, the sibling height verbs' bail
    }
    // Measured off a fresh relayout and lifted into the COLUMN's space while
    // tabbed, the cycle and adjust verbs' rule: what gets written below is the
    // extent tabbedColumnCrossPx will resolve, and that is the column's.
    int currentPx = activeTileCrossPx(params);
    if (currentPx < 0) {
        return false;
    }
    if (tabbed) {
        currentPx += tabbedCrossReservationPx(*activeCol, params);
    }
    // Intent OR pixels, and the intent test is the load-bearing half. A stack
    // whose other tiles are held up by their client minimums cannot give this
    // one the whole budget, so after a maximize it still renders short — and a
    // pixel-only test would read that as "not maximized" and maximize again,
    // forever, with no press ever reaching the un-maximize arm. The pixel test
    // stays for everything that reaches full height by another route (an Auto
    // lone tile, a preset cycled to the top, an adjust clamped at the budget),
    // which must un-maximize on the next press the way the width toggle's
    // rendered-pixel compare makes a full-width column un-maximize.
    // A Preset counts as maximized when the fraction RESOLVES to the budget,
    // resolved the way relayout's Preset arm resolves it (the form
    // cycleActiveWindowPresetHeight caps its entries with). Without this arm a
    // tile holding the top preset in a stack whose siblings are pinned at
    // their client minimums renders short, reads as "not maximized", and gets
    // its proportional anchor overwritten with a Fixed of the identical
    // extent: success reported, nothing moved, and the anchor a later
    // work-area change would have honoured is gone.
    const bool maximizedByIntent = tile->height.kind == WindowHeight::Fixed ? tile->height.fixedPx >= budget
                                                                            : tile->height.kind == WindowHeight::Preset
            && qMin(budget,
                    proportionalPx(nearestPresetValue(params.presetWindowHeights, tile->height.presetFraction), workH,
                                   params.gap))
                >= budget;
    const int ti = activeCol->activeTileIdx;
    const std::optional<WindowHeight> remembered = activeCol->tiles.at(ti).preMaximizeHeight;
    // A standing restore slot is the FIRST answer to "am I maximized", ahead of
    // both the pixel and the intent tests, because it is the only one that
    // cannot go stale. The budget is not a constant — it is the cross extent
    // less one gap per inter-tile seam, and it grows when a sibling closes or
    // the column is tabbed — so a tile left holding Fixed(oldBudget) reads as
    // NOT maximized against the larger budget. Re-entering the maximize arm
    // there overwrote the remembered height with the near-full one it had just
    // displaced, and the toggle then alternated between two full heights
    // forever with the user's real height gone and Auto unreachable. The slot
    // is set only by the arm below and cleared by every other height write, so
    // "it has a value" means exactly "this verb maximized this tile and nothing
    // has countermanded it since".
    const bool maximized = remembered.has_value() || currentPx >= budget || maximizedByIntent;
    // Un-maximizing puts back the height the maximize press displaced, and
    // falls to Auto only when there is nothing remembered — which is the case
    // for a tile that reached the budget by another route (an adjust clamped
    // there, a preset cycled to the top), where the user never asked this verb
    // for anything and Auto is the height family's "the column decides".
    //
    // A remembered height that no longer FITS is treated as nothing
    // remembered: the work area can shrink under a maximized tile, and putting
    // back a height at or above the current budget would render clamped, still
    // read as maximized, and cost a second press to reach Auto.
    const bool rememberedStillFits = remembered
        && (remembered->kind == WindowHeight::Auto
            || (remembered->kind == WindowHeight::Fixed && remembered->fixedPx < budget)
            || (remembered->kind == WindowHeight::Preset
                && proportionalPx(nearestPresetValue(params.presetWindowHeights, remembered->presetFraction), workH,
                                  params.gap)
                    < budget));
    const WindowHeight result =
        maximized ? (rememberedStillFits ? *remembered : WindowHeight::makeAuto()) : WindowHeight::makeFixed(budget);
    // Ownership before the no-change bail, and written through the COLUMN
    // rather than the cached tile pointer — cycleActiveWindowPresetHeight
    // carries both reasons.
    const bool claimed = claimTabbedHeightOwnership(*activeCol, ti, result);
    // Unconditional while the flag is set, with no equality test against the
    // stored intent — setActiveWindowHeight carries the full reason. Under the
    // override the column does not RENDER its stored intents at all, so a
    // result equal to the stored value is still a real change: clearing the
    // flag re-renders the intent. The equality test made a lone Auto tile in a
    // maximized-to-edges column a permanent refusal, on the one press whose
    // job is to take the stack back out of the override.
    const bool clearedEdges = activeCol->maximizedToEdges;
    if (clearedEdges) {
        activeCol->maximizedToEdges = false;
    }
    // The restore slot is settled on BOTH arms and BEFORE the no-change bail,
    // so a press that moves no pixels still leaves the memory right. The
    // maximize arm remembers only a height it is actually displacing: writing
    // the slot when the tile already holds the result would record the
    // maximized height as the height to go back to, and un-maximize would be a
    // permanent no-op.
    if (maximized) {
        activeCol->tiles[ti].preMaximizeHeight.reset();
    } else if (!(activeCol->tiles.at(ti).height == result)) {
        activeCol->tiles[ti].preMaximizeHeight = activeCol->tiles.at(ti).height;
    }
    if (activeCol->tiles.at(ti).height == result) {
        return claimed || clearedEdges;
    }
    activeCol->tiles[ti].height = result;
    return true;
}

bool ScrollStrip::minimizeActiveWindowHeight(const ScrollLayoutParams& params)
{
    // Lone tiles included: see setActiveWindowHeight.
    Tile* tile = activeTileMutable();
    if (!tile) {
        return false;
    }
    Column* activeCol = activeColumnMutable(); // non-null by construction, as above
    const bool tabbed = activeCol->display == ColumnDisplay::Tabbed;
    const int workH = params.axis.crossSize(params.workArea);
    const int budget = activeColumnCrossBudgetPx(params);
    if (workH <= 0 || budget <= 0) {
        return false; // degenerate area, the sibling height verbs' bail
    }
    // The smallest preset is the shortest height the user has NAMED, which
    // beats the engine floor when a list exists: a Preset intent follows the
    // vocabulary if the list is later edited, where a Fixed at the floor would
    // be stranded at a value nothing else uses. The list is NOT sorted (see
    // minimizeActiveColumnWidth for who does and does not normalise it), so
    // the minimum is searched for rather than read off the front.
    //
    // The empty-list fallback is Fixed pixels rather than a fraction because
    // height has no Proportion spelling — the one place this verb cannot
    // mirror its width twin exactly.
    const QList<qreal>& presets = params.presetWindowHeights;
    const int floorPx = activeWindowHeightFloorPx(params);
    if (floorPx < 0) {
        return false;
    }
    const WindowHeight target = presets.isEmpty()
        ? WindowHeight::makeFixed(floorPx)
        : WindowHeight::makePreset(*std::min_element(presets.cbegin(), presets.cend()));
    // RENDERED-pixel compare, minimizeActiveColumnWidth's reason: what the
    // user sees is the intent resolved against the column's budget and then
    // raised to the tile's floor. A tile whose floor already sits at or above
    // the smallest preset would move nothing on screen if the intent were
    // rewritten, and the verb would report success for a no-op (then refuse
    // the second press). Comparing the floored target against the rendered
    // height makes the first press refuse too.
    //
    // Preset entries resolve the way relayout's Preset arm resolves them, the
    // form cycleActiveWindowPresetHeight caps its entries with.
    // Capped at the budget on the OUTSIDE as well as the inside: floorPx is
    // bounded by workH (activeWindowHeightFloorPx's cap), not by the column's
    // budget, so in a crowded stack a client minimum can sit above the budget
    // and the bare qMax would answer a target no relayout can render — every
    // press would rewrite the intent and report success with nothing moving,
    // which is the exact no-op the rendered-pixel compare below exists to
    // prevent. The cap only ever lowers the target, so the floor still wins
    // against the preset.
    const int targetPx = qBound(1,
                                qMax(floorPx,
                                     target.kind == WindowHeight::Preset
                                         ? qMin(budget, proportionalPx(target.presetFraction, workH, params.gap))
                                         : qBound(1, target.fixedPx, budget)),
                                budget);
    int currentPx = activeTileCrossPx(params);
    if (currentPx < 0) {
        return false;
    }
    if (tabbed) {
        currentPx += tabbedCrossReservationPx(*activeCol, params);
    }
    // Ownership before the no-move bail, and written through the COLUMN —
    // cycleActiveWindowPresetHeight carries both reasons.
    const int ti = activeCol->activeTileIdx;
    const bool claimed = claimTabbedHeightOwnership(*activeCol, ti, target);
    // Unconditional while set, setActiveWindowHeight's reason: under the
    // override the stored intents are not rendered, so clearing the flag is
    // itself the change.
    const bool clearedEdges = activeCol->maximizedToEdges;
    if (clearedEdges) {
        activeCol->maximizedToEdges = false;
    }
    // The intent is written in BOTH arms below, and neither write is
    // redundant. Keep both.
    //
    // claimTabbedHeightOwnership moves the tabbed column's extent owner to
    // this tile and writes nothing else, so an owner must already hold the
    // height the claim was made for. Returning from the no-move arm without a
    // write left the new owner carrying whatever it had — Auto for a tab
    // inserted at the context default — and tabbedColumnCrossPx resolves an
    // Auto owner to the WHOLE work area, so a minimize press GREW the column
    // it was asked to shrink. Hence the write inside that arm. It is gated on
    // `claimed` because with no claim there is no owner to keep consistent,
    // and writing anyway would mutate the persisted intent on a press this
    // verb goes on to report as a refusal.
    //
    // The write AFTER the arm covers a case the claim declines: a tab that is
    // already heightOwnerId but still holds Auto answers false from the claim,
    // and its column then resolves to the whole work area, so targetPx differs
    // from currentPx and control reaches here. That is the same grow bug by
    // another route, and this write is the only thing that closes it.
    //
    // The verdict is the rendered pixels either way, so a tile already seated
    // at its floor reports no movement.
    if (targetPx == currentPx) {
        if (claimed) {
            activeCol->tiles[ti].height = target;
            activeCol->tiles[ti].preMaximizeHeight.reset();
        }
        return claimed || clearedEdges;
    }
    activeCol->tiles[ti].height = target;
    activeCol->tiles[ti].preMaximizeHeight.reset();
    return true;
}

bool ScrollStrip::expandActiveWindowToAvailableHeight(const ScrollLayoutParams& params)
{
    // Lone tiles included: see setActiveWindowHeight.
    Tile* tile = activeTileMutable();
    if (!tile) {
        return false;
    }
    Column* activeCol = activeColumnMutable(); // non-null by construction, as above
    const int budget = activeColumnCrossBudgetPx(params);
    if (budget <= 0) {
        return false; // degenerate area, the sibling height verbs' bail
    }
    // Stored-intent basis, expandActiveColumnToAvailableWidth's rule: without
    // the drop a maximized-to-edges column resolves every tile as a weighted
    // Auto share of the RAW cross extent, so the measurement below reads a
    // share of the raw area while the budget is the gapped one, and the
    // early-out refuses a verb whose promise is "grow". Dropped BEFORE the
    // measurement so what follows reads the intents the column will render,
    // and reported by every bail so a refusal that un-maximized still answers
    // true rather than mutating silently.
    //
    // This is the only drop on the path: the toggle routes below re-enter
    // with the flag already clear, so their own conditional drop is a no-op.
    const bool clearedEdges = activeCol->maximizedToEdges;
    activeCol->maximizedToEdges = false;
    int currentPx = activeTileCrossPx(params);
    if (currentPx < 0) {
        return clearedEdges;
    }
    // Lifted into the COLUMN's space while tabbed, the sibling height verbs'
    // rule: a tab renders at the column's content rect, and the budget a
    // tabbed column is measured against is the whole work area.
    const bool tabbed = activeCol->display == ColumnDisplay::Tabbed;
    if (tabbed) {
        currentPx += tabbedCrossReservationPx(*activeCol, params);
    }
    // Already filling the column: expandActiveColumnToAvailableWidth's
    // is_full_width early-out. Taken before EVERY toggle branch below, the
    // tabbed one included, so a tile that already has everything refuses
    // rather than un-maximizing — the toggle's other arm is the wrong answer
    // for a verb whose whole promise is "grow".
    if (currentPx >= budget) {
        return clearedEdges;
    }
    // A TABBED column has no leftover WITHIN it — every visible tab is
    // committed at the column's own content rect — so "fill what is left"
    // means the whole budget. Routed through the toggle rather than written as
    // Fixed pixels, expandActiveColumnToAvailableWidth's rule for the case
    // where the answer is "everything": the toggle leaves a way back out.
    if (tabbed) {
        return toggleMaximizeActiveWindowHeight(params) || clearedEdges;
    }
    // The active tile is the only visible one in its column, so it is about to
    // take the whole budget: through the toggle, for the reason above.
    if (activeCol->visibleTileCount() <= 1) {
        return toggleMaximizeActiveWindowHeight(params) || clearedEdges;
    }
    // What the column's tiles TAKE, summed from the same relayout the
    // measurement above reads, so the two agree: an Auto tile already absorbs
    // the leftover by weight, which is why a column holding one has nothing
    // for this verb to claim and falls out at the leftover test below.
    const ResolvedStrip resolved = relayout(params);
    int taken = -1;
    for (const ResolvedColumn& rc : resolved.columns) {
        if (rc.columnIndex != m_activeColumnIdx) {
            continue;
        }
        // Cross extents ONLY, with no gap term. This is where the height axis
        // parts company with expandActiveColumnToAvailableWidth, which seeds
        // its walk with gap * (visible - 1): that verb measures against workW,
        // the RAW viewport, so the inter-column gaps are space it must account
        // for. Here the budget comes from activeColumnCrossBudgetPx, which has
        // already subtracted the inter-tile gaps, and relayout keeps the gaps
        // OUTSIDE the tile rects (the emit loop advances the cursor by
        // height + gap). Seeding the gaps here would subtract them a second
        // time and strand exactly gap * (tiles - 1) pixels on every press.
        taken = 0;
        for (const ResolvedTile& rt : rc.tiles) {
            taken += params.axis.crossSize(rt.rect);
        }
        break;
    }
    if (taken < 0) {
        return clearedEdges; // the active column resolved to nothing
    }
    const int leftover = budget - taken;
    if (leftover <= 0) {
        return clearedEdges;
    }
    const int target = qMin(budget, currentPx + leftover);
    if (target == currentPx) {
        // Same pixels: rewriting the intent would move nothing on screen but
        // report success, expandActiveColumnToAvailableWidth's reason.
        return clearedEdges;
    }
    // A stacked column claims no tabbed ownership (the tabbed arm returned
    // above), so the write goes straight in — through the COLUMN rather than
    // the cached tile pointer, cycleActiveWindowPresetHeight's reason.
    activeCol->tiles[activeCol->activeTileIdx].height = WindowHeight::makeFixed(target);
    activeCol->tiles[activeCol->activeTileIdx].preMaximizeHeight.reset();
    return true;
}

WindowHeight ScrollStrip::windowHeightIntent(const QString& windowId) const
{
    const int ci = columnOfWindow(windowId);
    if (ci < 0) {
        return {};
    }
    const Column& col = m_columns.at(ci);
    const int ti = col.indexOfWindow(windowId);
    return ti >= 0 ? col.tiles.at(ti).height : WindowHeight{};
}

bool ScrollStrip::setTabbedHeightOwner(const QString& windowId)
{
    const int ci = columnOfWindow(windowId);
    if (ci < 0) {
        return false;
    }
    Column& col = m_columns[ci];
    if (col.display != ColumnDisplay::Tabbed || col.heightOwnerId == windowId) {
        return false;
    }
    // Assigned directly rather than through claimTabbedHeightOwnership: this
    // is a RESTORE of an ownership that was already decided, not a bid made by
    // writing a height, so the claim's "an Auto write claims nothing" rule
    // does not apply. A stashed owner whose tab is Auto is a legitimate state
    // — it means the column was showing full height — and routing it through
    // the claim would silently drop it and fall the column back to a scan.
    col.heightOwnerId = windowId;
    return true;
}

bool ScrollStrip::setWindowHeightIntent(const QString& windowId, const WindowHeight& height)
{
    const int colIdx = columnOfWindow(windowId);
    if (colIdx < 0) {
        return false;
    }
    Column& col = m_columns[colIdx];
    const int ti = col.indexOfWindow(windowId);
    // Ownership before the no-change bail, setActiveWindowHeight's reason.
    // Note this claims only for a non-Auto height: the restore and handoff
    // paths re-state a tile's remembered intent through here, and an Auto one
    // must not take a tabbed column's extent away from the tab that owns it.
    // The owner itself is restored through setTabbedHeightOwner.
    const bool claimed = claimTabbedHeightOwnership(col, ti, height);
    // Before the equality bail, because this is the addressed re-state the
    // restore paths use (migration, unfloat, stash, drag commit): whatever the
    // tile was doing before, it is now being told a height from outside, and a
    // maximize the user could still undo is not part of what those paths
    // carry. Clearing on the equal case too keeps that true for a re-state
    // that happens to match.
    col.tiles[ti].preMaximizeHeight.reset();
    if (col.tiles.at(ti).height == height) {
        return claimed;
    }
    col.tiles[ti].height = height;
    return true;
}

bool ScrollStrip::equalizeActiveColumnHeights()
{
    Column* col = activeColumnMutable();
    if (!col) {
        return false;
    }
    bool changed = false;
    // A height verb over the whole stack: a maximize-to-edges override drops
    // with it, and the drop is itself a change.
    if (col->maximizedToEdges) {
        col->maximizedToEdges = false;
        changed = true;
    }
    for (Tile& tile : col->tiles) {
        const WindowHeight even = WindowHeight::makeAuto();
        // Unconditional, and outside the height guard: an even split is the
        // user saying "no window in this column is special", so nothing is
        // left maximized to restore. Clearing costs nothing on a tile that
        // holds no slot, and doing it here rather than beside the write keeps
        // it true for a tile that already renders even.
        tile.preMaximizeHeight.reset();
        if (!(tile.height == even)) {
            tile.height = even;
            changed = true;
        }
    }
    return changed;
}

} // namespace PhosphorScrollEngine
