// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollStrip.h>

#include "scrollengine/scrollenginelogging.h"

#include <QtGlobal>

namespace PhosphorScrollEngine {

namespace {

/// The work area measured along the strip, and across it. Every "how much
/// room is there" question in this file is one of these two, and naming them
/// keeps the anchor math readable once the physical axis stops being fixed.
int mainExtent(const ScrollLayoutParams& params)
{
    return params.axis.mainSize(params.workArea);
}

int crossExtent(const ScrollLayoutParams& params)
{
    return params.axis.crossSize(params.workArea);
}

/// The rect a maximized-to-edges column resolves against: the pre-gap work
/// area when the caller supplied one, else the gapped work area. The fallback
/// keeps every params construction that never sets rawWorkArea (tests, pure
/// verb math) meaning "fill the work area, gap-free" instead of collapsing to
/// the degenerate-area arms on a null rect.
QRect rawAreaFor(const ScrollLayoutParams& params)
{
    return params.rawWorkArea.isValid() ? params.rawWorkArea : params.workArea;
}

/// Where a column's content starts on the CROSS axis given the extent it
/// actually resolved to. With the centre policy off, or with a column that
/// fills (or overflows) the cross extent, this is the area's start edge and
/// the layout is unchanged. With it on and slack left over, the content is
/// centred in that slack, which is what puts a half-height solo window in the
/// middle of the screen instead of against the top.
///
/// Takes the whole RECT rather than a start edge, so the low edge and the
/// extent it is centred within always come from the same place and cannot
/// disagree. That matters here beyond tidiness: a maximized-to-edges column
/// resolves against the RAW work area, which is wider than the gapped one, so
/// deriving the extent from params would centre it in the wrong slack.
int crossStartFor(int contentCross, const ScrollLayoutParams& params, const QRect& areaRect)
{
    const int crossLow = params.axis.crossLow(areaRect);
    if (!params.centerShortColumns) {
        return crossLow;
    }
    return crossLow + qMax(0, params.axis.crossSize(areaRect) - contentCross) / 2;
}

} // namespace

int ScrollStrip::resolveColumnWidthPx(const ColumnWidth& width, const ScrollLayoutParams& params)
{
    const int workW = mainExtent(params);
    // Degenerate work area (unknown/removed screen, or outer gaps wider
    // than the screen): resolve every column to 1px instead of feeding a
    // negative bound into qBound below — its Q_ASSERT(!(max < min)) would
    // abort a debug daemon exactly during screen teardown, when
    // windowClosed/handoffRelease still build params for the dying screen.
    if (workW <= 0) {
        return 1;
    }
    switch (width.kind) {
    case ColumnWidth::Proportion:
        return qMin(workW, proportionalPx(width.proportion, workW, params.gap));
    case ColumnWidth::Fixed:
        return qBound(1, width.fixedPx, workW);
    case ColumnWidth::Preset:
        // Snap-at-resolve: the fraction anchor lands on the nearest entry of
        // the CURRENT vocabulary, so a template swap reflows preset columns
        // and clearing the template restores the anchor's original entry.
        return qMin(
            workW,
            proportionalPx(nearestPresetValue(params.presetColumnWidths, width.presetFraction), workW, params.gap));
    }
    return qMin(workW, proportionalPx(0.5, workW, params.gap));
}

int ScrollStrip::tabbedCrossReservationPx(const Column& c, const ScrollLayoutParams& params)
{
    if (c.display != ColumnDisplay::Tabbed) {
        return 0;
    }
    // The inverse of columnMinExtentPx's indicatorEatsMainAxis gate, and the
    // same reasoning: a Left/Right indicator is thick along x, which is the
    // CROSS axis exactly while the strip runs vertically.
    if (isVerticalTabIndicator(params.tabIndicator.position) == params.axis.isHorizontal()) {
        return 0;
    }
    int visibleTiles = 0;
    for (const Tile& tile : c.tiles) {
        if (!tile.minimized) {
            ++visibleTiles;
        }
    }
    return params.tabIndicator.reservedThickness(visibleTiles);
}

int ScrollStrip::tabbedColumnCrossPx(const Column& c, const ScrollLayoutParams& params)
{
    const int fullCross = crossExtent(params);
    if (fullCross <= 0) {
        return 1; // degenerate work area, resolveColumnWidthPx's bail
    }
    if (c.tiles.isEmpty()) {
        return fullCross;
    }
    // ONE tab decides, NOT the tab on show. niri's rule verbatim
    // (scrolling.rs: "All tiles have the same height, equal to the height of
    // the only fixed tile (if any)"), and the reason is that the alternative
    // is unstable: reading the SHOWN tab makes the column's extent a function
    // of which tab is focused, so a plain tab switch resizes the column. That
    // is both a shape change nobody asked for and a broken premise for the
    // compositor's tab-switch cross-fade, which is built on the arriving tab
    // occupying the rect the outgoing one just vacated
    // (kwin-effect/tilinghandler/tiling.cpp).
    //
    // Which tab owns it is EXPLICIT state (Column::heightOwnerId), set when
    // the column becomes tabbed and moved by a height write. The tabs that do
    // not own it keep their own intents untouched, so untabbing restores the
    // stack the user built rather than the flattened one an ownership wipe
    // would leave behind.
    //
    // This is where the parity ends, and deliberately. niri keeps the
    // invariant that at most ONE tile per column is non-Auto — it normalizes
    // the others to weighted Auto at the height-write sites, preserving their
    // apparent heights — so a scan for "the fixed one" is safe there and its
    // display toggle needs no bookkeeping at all. A column here may legitimately
    // carry several sized tiles (the stack branch renormalizes them
    // proportionally), so that invariant does not hold and the scan cannot
    // stand in for an owner.
    const Tile* ownerTile = c.heightOwner();
    const WindowHeight* owner = ownerTile ? &ownerTile->height : nullptr;
    if (owner && owner->kind == WindowHeight::Auto) {
        // An owner that asked for Auto is asking for the whole work area,
        // which is the no-owner answer. Resolved here rather than falling
        // through to the scan: the owner has spoken, and scanning past it
        // would hand the column a height its own owner did not choose.
        owner = nullptr;
    } else if (!owner && c.heightOwnerId.isEmpty()) {
        // NO ownership recorded at all: a column tabbed before this field
        // existed (an older persisted blob), or one built by the rule,
        // template and handoff seams. Infer it the way it used to be
        // inferred — the first non-Auto tab — which is a deterministic answer
        // rather than a focus-dependent one.
        //
        // Deliberately NOT the fallback when an owner IS named but cannot be
        // resolved (its tab closed, or is minimized and so dropped from the
        // layout). The tabs that do not own the column now keep their own
        // heights, so scanning there would adopt some other tab's extent the
        // moment the owner minimizes — a resize the user did not ask for, and
        // one that did not happen under the old model because the siblings had
        // all been flattened to Auto. An unresolvable owner leaves the column
        // at the no-owner answer, the full work area, until something claims
        // it again.
        for (const Tile& tile : c.tiles) {
            if (!tile.minimized && tile.height.kind != WindowHeight::Auto) {
                owner = &tile.height;
                break;
            }
        }
    }
    // No owner: every tab is Auto, which means the whole work area — what
    // every tabbed column was pinned at before the intent was read here at
    // all.
    int px = fullCross;
    if (owner) {
        switch (owner->kind) {
        case WindowHeight::Fixed:
            px = qBound(1, owner->fixedPx, fullCross);
            break;
        case WindowHeight::Preset:
            // Resolved through the same snap-at-resolve path the stack branch
            // takes, so a template swap reflows a tabbed column the way it
            // reflows a stacked one.
            px = qMin(fullCross,
                      proportionalPx(nearestPresetValue(params.presetWindowHeights, owner->presetFraction), fullCross,
                                     params.gap));
            break;
        case WindowHeight::Auto:
            // Unreachable: the scan above only ever takes a non-Auto tab.
            // Kept as a labelled arm with no default, so a new WindowHeight
            // kind is a compiler warning here rather than silently resolving
            // to the work area.
            break;
        }
    }
    if (params.respectMinimumSize) {
        // Every visible tab is committed at this column's content rect, the
        // hidden ones included, so the floor is the tallest minimum in the
        // set and not just the shown tab's. Capped at the work area for the
        // same reason the stack branch caps its own floors: a client minimum
        // larger than the output must not size the column past the screen.
        int floorPx = 0;
        for (const Tile& tile : c.tiles) {
            if (!tile.minimized) {
                floorPx = qMax(floorPx, tile.minCross(params.axis));
            }
        }
        if (floorPx > 0) {
            px = qMax(px, qMin(floorPx + tabbedCrossReservationPx(c, params), fullCross));
        }
    }
    return qMax(1, px);
}

int ScrollStrip::columnExtentPx(const Column& c, const ScrollLayoutParams& params) const
{
    // isEmpty first: isFullyMinimized answers FALSE for an empty column, so
    // without this the width/offset walks (columnStripPos, stripExtentPx,
    // visibleColumnIndices) would count a phantom column's width+gap while
    // relayout's own accumulator skips it — every column right of the
    // phantom would render shifted from the position the anchor math
    // computed. No mutation site produces an empty column today (each
    // either closes the emptied column or is guarded on tiles.size() >= 2);
    // this is the single chokepoint all four walks share, kept here so the
    // invariant does not depend on every future mutation site remembering
    // it.
    if (c.isEmpty() || c.isFullyMinimized()) {
        return 0;
    }
    // Maximize-to-edges: the extent is DECLARED, not resolved from the width
    // intent (which stays stored, untouched, for the un-maximize restore).
    // No minimum floor and no work-area cap — the raw main extent already
    // exceeds the capped answer, and a client minimum wider than the output
    // overhangs under the compositor's own enforcement, the same outcome the
    // respectMinimumSize=false arm accepts everywhere else.
    if (c.maximizedToEdges) {
        const int rawMain = params.axis.mainSize(rawAreaFor(params));
        if (rawMain > 0) {
            return rawMain;
        }
    }
    const int px = qMax(resolveColumnWidthPx(c.width, params), columnMinExtentPx(c, params));
    return qMin(px, mainExtent(params));
}

int ScrollStrip::columnMinExtentPx(const Column& c, const ScrollLayoutParams& params) const
{
    if (!params.respectMinimumSize) {
        return 0;
    }
    int floor = 0;
    {
        // A tabbed column with a LEFT/RIGHT within-column indicator hands
        // its tiles contentRectFor(rect) = rect minus reservedThickness, so
        // when the min-width clamp is what sets the column width the
        // committed TILE width would land below the client's declared
        // minimum — the exact commit-a-width-the-client-refuses hazard the
        // peek floor documents (KWin then regrows the frame from x). Raise
        // the floor by the reservation so the tile, not the column, honours
        // the minimum (best-effort: the workArea cap at the tail still wins
        // when minWidth plus the reservation exceeds the output, the same
        // way it already overrode a bare minWidth wider than the screen).
        // Applied HERE, in the one function every consumer
        // (columnStripPos / stripExtentPx / relayout / the anchor math) goes
        // through, so the strip widens consistently for such columns. The
        // CROSS axis has its own copy of this, in tabbedColumnCrossPx:
        // that branch raises a tabbed column to its tabs' minimum plus the
        // cross-axis reservation for exactly the same reason.
        //
        // The gate asks whether the indicator's THICKNESS eats the MAIN axis,
        // which is not the same question as whether the indicator sits on a
        // vertical screen edge. Those two coincide on a horizontal strip and
        // invert on a vertical one: a Left/Right indicator is thick along x,
        // which is the main axis only while the strip runs horizontally.
        // TabIndicatorPosition stays a SCREEN-edge vocabulary on purpose (a
        // user who asked for the indicator on the left means the left of their
        // screen, and rotating a monitor must not move it), so the axis has to
        // be brought in here rather than folded into the enum.
        int reservationFloor = 0;
        const bool indicatorEatsMainAxis =
            isVerticalTabIndicator(params.tabIndicator.position) == params.axis.isHorizontal();
        if (c.display == ColumnDisplay::Tabbed && indicatorEatsMainAxis) {
            int visibleTiles = 0;
            for (const Tile& tile : c.tiles) {
                if (!tile.minimized) {
                    ++visibleTiles;
                }
            }
            reservationFloor = params.tabIndicator.reservedThickness(visibleTiles);
        }
        for (const Tile& tile : c.tiles) {
            // minMain, not minWidth: this is the column's floor ALONG the
            // strip, which is the client's minimum height on a vertical one.
            const int tileMinMain = tile.minMain(params.axis);
            if (!tile.minimized && tileMinMain + reservationFloor > floor) {
                floor = tileMinMain + reservationFloor;
            }
        }
    }
    return floor;
}

int ScrollStrip::columnStripPos(int columnIndex, const ScrollLayoutParams& params) const
{
    int mainPos = 0;
    const int end = qBound(0, columnIndex, m_columns.size());
    for (int i = 0; i < end; ++i) {
        const int extent = columnExtentPx(m_columns.at(i), params);
        if (extent > 0) {
            mainPos += extent + params.gap;
        }
    }
    return mainPos;
}

int ScrollStrip::stripExtentPx(const ScrollLayoutParams& params) const
{
    int total = 0;
    for (const Column& c : m_columns) {
        const int w = columnExtentPx(c, params);
        if (w > 0) {
            total += (total > 0 ? params.gap : 0) + w;
        }
    }
    return total;
}

int ScrollStrip::viewOffsetFor(const ScrollLayoutParams& params) const
{
    if (m_activeColumnIdx < 0) {
        return 0;
    }
    return columnStripPos(m_activeColumnIdx, params) - m_viewAnchor;
}

int ScrollStrip::centeredAnchorFor(int columnIndex, const ScrollLayoutParams& params) const
{
    if (columnIndex < 0 || columnIndex >= m_columns.size()) {
        return 0;
    }
    // Degenerate-area guard, same as clampedAnchor's (the rationale lives
    // there): a centered anchor computed against a 0-extent area is 0.
    if (mainExtent(params) <= 0) {
        return m_viewAnchor;
    }
    const int colMain = columnExtentPx(m_columns.at(columnIndex), params);
    return (mainExtent(params) - colMain) / 2;
}

int ScrollStrip::keepOrRecenterAnchor(int oldViewOffset, const ScrollLayoutParams& params)
{
    // Degenerate-area guard, same as clampedAnchor's (the rationale lives
    // there). Checked here too so the centering arms cannot bypass it.
    if (mainExtent(params) <= 0) {
        return m_viewAnchor;
    }
    // Structural changes (minimize collapse, consume) keep the view where it
    // was UNLESS the centering policy pins the focused column to the middle:
    // Always / lone-column centering must survive a strip-width change, or
    // the focused column drifts off-center until the next focus move.
    if (isCenteringActiveColumn(params)) {
        // The policy took the view back; a pan that stayed latched here would
        // leave the next pass refusing to re-derive the position it has.
        m_viewDetached = false;
        return centeredAnchorFor(m_activeColumnIdx, params);
    }
    return clampedAnchor(columnStripPos(m_activeColumnIdx, params) - oldViewOffset, params);
}

int ScrollStrip::clampedAnchor(int anchor, const ScrollLayoutParams& params) const
{
    return clampedAnchorFor(m_activeColumnIdx, anchor, params);
}

int ScrollStrip::clampedAnchorFor(int columnIndex, int anchor, const ScrollLayoutParams& params) const
{
    if (columnIndex < 0 || columnIndex >= m_columns.size()) {
        return 0;
    }
    // Degenerate work area (unknown/removed screen, or outer gaps wider than
    // the output — resolveColumnWidthPx documents the same state): every
    // column width resolves to 0/1 here, so the clamp math below collapses
    // ANY anchor to 0 — and m_viewAnchor is PERSISTED state
    // (serializeStripState), so a verb or float round trip running against a
    // dying screen would permanently reset the strip's restored scroll
    // position. Preserve the current anchor instead; the first relayout
    // against a real area re-clamps it. This is the chokepoint most anchor
    // walks share; keepOrRecenterAnchor, reanchorAfterFocusChange,
    // centeredAnchorFor, centerVisibleColumns and removeWindowInternal
    // (scrollstrip_structure.cpp) carry their own copies of the guard for
    // the arms that do not pass through here.
    if (mainExtent(params) <= 0) {
        return m_viewAnchor;
    }
    const int viewMain = mainExtent(params);
    const int stripMain = stripExtentPx(params);
    const int activeMainPos = columnStripPos(columnIndex, params);
    // anchor = activeMainPos - viewOffset; viewOffset must stay within
    // [0, max(0, stripMain - viewMain)].
    const int maxViewOffset = qMax(0, stripMain - viewMain);
    const int viewOffset = qBound(0, activeMainPos - anchor, maxViewOffset);
    return activeMainPos - viewOffset;
}

void ScrollStrip::reanchorAfterFocusChange(int prevIdx, int oldViewOffset, const ScrollLayoutParams& params)
{
    if (m_activeColumnIdx < 0) {
        m_viewAnchor = 0;
        m_viewDetached = false;
        return;
    }
    // This is the one chokepoint every focus-driven re-anchor passes through
    // (the focus verbs, the structural paths, and updateViewForFocus's own
    // tail), which makes it the place a pan stops owning the view: the policy
    // is about to choose a position, so the latch that exists to stop it must
    // go. updateViewForFocus never reaches here while detached — it returns
    // above — so clearing here cannot undo a live pan.
    //
    // ABOVE the degenerate-area guard, unlike every other write here. The
    // guard's subject is the anchor, which cannot be computed against nothing;
    // the latch is not a position but an answer to "who owns the view", and
    // the focus DID move whether or not this screen can lay out right now.
    // Clearing here lets the first relayout against a real work area re-derive
    // the position, which is exactly what the guard promises; keeping it would
    // leave the pan owning the view across a focus change forever, since no
    // later pass revisits the question.
    m_viewDetached = false;
    // Degenerate-area guard, same as clampedAnchor's (the rationale lives
    // there): every arm below would write 0 over the persisted anchor.
    if (mainExtent(params) <= 0) {
        return;
    }
    m_viewAnchor = focusAnchorFor(m_activeColumnIdx, prevIdx, oldViewOffset, params);
    qCDebug(lcScrollEngine) << "reanchorAfterFocusChange: prevIdx" << prevIdx << "active" << m_activeColumnIdx
                            << "oldViewOffset" << oldViewOffset << "policy" << int(params.centerFocusedColumn)
                            << "verdict anchor" << m_viewAnchor;
}

int ScrollStrip::focusAnchorFor(int targetIdx, int prevIdx, int oldViewOffset, const ScrollLayoutParams& params) const
{
    if (targetIdx < 0 || targetIdx >= m_columns.size()) {
        return 0;
    }
    // Degenerate-area guard, same as clampedAnchor's (the rationale lives
    // there): every arm below would answer 0, and the callers write that
    // answer over a PERSISTED anchor.
    if (mainExtent(params) <= 0) {
        return m_viewAnchor;
    }
    const int viewMain = mainExtent(params);
    const int colMain = columnExtentPx(m_columns.at(targetIdx), params);
    const int activeMainPos = columnStripPos(targetIdx, params);

    bool center = isCenteringActiveColumn(params);

    if (!center && params.centerFocusedColumn == CenterFocusedColumn::OnOverflow && prevIdx >= 0
        && prevIdx < m_columns.size() && prevIdx != targetIdx) {
        // niri parity (compute_new_view_offset_for_column, the OnOverflow arm:
        // "Always take the left or right neighbor of the target as the
        // source"). The overflow test measures the target against the column
        // that will sit NEXT TO IT on screen, never against the column focus
        // happened to come from. Those are the same column for an adjacent
        // step and different for every jump of more than one — focusFirstColumn
        // and focusLastColumn, focusWindow onto a distant column, or focus
        // following a window that moved. Measuring against a far-off prevIdx
        // reads a width that has nothing to do with the resulting view, which
        // let a jump land flush against the entering edge in exactly the cases
        // niri centers.
        //
        // Zero-extent (fully minimized) columns hold no strip position, so
        // they cannot be the neighbour that crowds the target; walk past them
        // to the first column that actually occupies space, the same skip
        // columnStripPos and stripExtentPx apply. niri has no such state and
        // so no opinion here. When that side holds no such column the target
        // has the viewport to itself and the fit arm is right, which is also
        // what niri does when it is handed no source at all.
        const int dir = prevIdx > targetIdx ? 1 : -1;
        int sourceIdx = targetIdx + dir;
        while (sourceIdx >= 0 && sourceIdx < m_columns.size() && columnExtentPx(m_columns.at(sourceIdx), params) <= 0) {
            sourceIdx += dir;
        }
        if (sourceIdx >= 0 && sourceIdx < m_columns.size()) {
            const int sourceMain = columnExtentPx(m_columns.at(sourceIdx), params);
            const int sourcePos = columnStripPos(sourceIdx, params);
            // Source's leading edge to the target's trailing one, or the
            // mirror when the source is trailward, expressed through strip
            // positions exactly as niri's total_width is.
            //
            // WITHOUT niri's `+ gaps * 2`, deliberately. That term is the
            // edge padding niri keeps between the outermost column and the
            // viewport: its working_area still contains that padding, so it
            // has to be added to the span before the two are comparable. Ours
            // is already gone — engine_query subtracts the outer gaps from
            // params.workArea, and both relayout (first column flush at
            // mainLow) and the fit arm below (pinning to viewMain - colMain)
            // lay columns edge to edge inside what is left. viewMain IS the
            // room the pair has, so adding the term back would compare the
            // padding twice and center a pair that fits by up to two gaps.
            const int span = sourcePos < activeMainPos ? activeMainPos - sourcePos + colMain
                                                       : sourcePos - activeMainPos + sourceMain;
            if (span > viewMain) {
                center = true;
            }
        }
    }

    if (center) {
        return centeredAnchorFor(targetIdx, params);
    }

    // CenterFocusedColumn::Never — scroll the minimum amount that makes the
    // focused column fully visible, pinning it to the edge it entered from.
    int pos = activeMainPos - oldViewOffset;
    if (colMain >= viewMain) {
        // Exactly the viewport's length along the strip, never longer:
        // columnWidthPx caps every column at the work area, so this is the
        // equality case and both arms resolve to the same zero offset. Kept
        // as a branch because the cap lives in another function and a future
        // width kind that opted out of it would land here needing the
        // entering-edge pin.
        pos = (prevIdx >= 0 && targetIdx < prevIdx) ? viewMain - colMain : 0;
    } else if (pos < 0) {
        pos = 0;
    } else if (pos + colMain > viewMain) {
        pos = viewMain - colMain;
    }
    return clampedAnchorFor(targetIdx, pos, params);
}

int ScrollStrip::predictedFocusScrollPx(int columnIndex, const ScrollLayoutParams& params) const
{
    // Every bail answers 0, which reads as "focusing this costs no scroll" and
    // so can never trip a cap. That is the fail-open direction on purpose: a
    // caller asking the question is deciding whether to REFUSE a focus, and a
    // question we cannot answer must not become a refusal.
    if (columnIndex < 0 || columnIndex >= m_columns.size() || m_activeColumnIdx < 0
        || columnIndex == m_activeColumnIdx) {
        return 0;
    }
    if (mainExtent(params) <= 0) {
        return 0;
    }
    const int oldViewOffset = viewOffsetFor(params);
    const int anchor = focusAnchorFor(columnIndex, m_activeColumnIdx, oldViewOffset, params);
    // The anchor is stored relative to the column it belongs to, so the view
    // offset it implies is that column's strip position minus it — the same
    // derivation viewOffsetFor makes for the active column.
    const int newViewOffset = columnStripPos(columnIndex, params) - anchor;
    return qAbs(newViewOffset - oldViewOffset);
}

void ScrollStrip::restoreViewAnchor(int anchor, const ScrollLayoutParams& params)
{
    Q_UNUSED(params)
    // Raw, deliberately UNCLAMPED: a captured centered anchor implies an
    // out-of-range derived viewOffset by design (centerActiveColumn stores the
    // same shape), and clamping here mangled exactly those anchors. Later
    // structural inserts re-clamp when the strip genuinely cannot honour
    // the view (insertWindowAt's anchor re-clamp).
    m_viewAnchor = anchor;
}

void ScrollStrip::updateViewForFocus(const ScrollLayoutParams& params)
{
    // A detached view belongs to the user's pan, not to the policy (class
    // doc), and this runs at the top of every applyLayout — so re-deriving
    // here is exactly what would undo the pan on the very next pass: under
    // Always unconditionally, and under Never/OnOverflow as soon as the pan
    // carried the focused column off the viewport, which is the only pan
    // worth making.
    //
    // Nothing is re-clamped on the way out, deliberately. A pan can INHERIT an
    // out-of-range anchor (the centering mutators store one by design) and
    // scrollViewBy walks such a view back one clamped delta at a time rather
    // than snapping it — running clampedAnchor here would perform in one
    // layout pass the very snap that comment forbids, hundreds of pixels
    // against the direction the user just scrolled. The only way a view that
    // started IN range leaves it is a viewport growth, which strands it at the
    // TRAILING end as dead space: the same state removeWindowInternal creates
    // on purpose and the early-return below already preserves. It heals on the
    // next pan or focus change.
    if (m_viewDetached) {
        return;
    }
    // Policy re-application only: when the active column is already fully
    // visible under a non-centering policy, leave the anchor alone — this
    // runs at the top of every applyLayout, and re-clamping there would
    // silently undo an explicit centerActiveColumn at the strip's edges
    // (whose centered anchor implies out-of-range viewOffset by design) and
    // reclaim removeWindowInternal's deliberate right-edge dead space.
    // BOTH ends, like every sibling bounds test in this file: this one indexes
    // m_columns directly rather than delegating to a qBound, so an
    // out-of-range active index would be an out-of-bounds read in release
    // rather than an assertion. Not reachable today — every writer of the
    // index clamps — so this is consistency, not a latent crash.
    if (!isCenteringActiveColumn(params) && m_activeColumnIdx >= 0 && m_activeColumnIdx < m_columns.size()) {
        const int viewMain = mainExtent(params);
        const int colMain = columnExtentPx(m_columns.at(m_activeColumnIdx), params);
        const int pos = columnStripPos(m_activeColumnIdx, params) - viewOffsetFor(params);
        if (pos >= 0 && pos + colMain <= viewMain) {
            return;
        }
    }
    reanchorAfterFocusChange(m_activeColumnIdx, viewOffsetFor(params), params);
}

bool ScrollStrip::centerActiveColumn(const ScrollLayoutParams& params)
{
    if (m_activeColumnIdx < 0) {
        return false;
    }
    // Asking to center is asking for the policy's view back, so the pan stops
    // owning it — and unconditionally, ahead of the no-move bail below: a pan
    // that happens to land on the centered anchor still has to re-attach, or
    // the verb reports "nothing to do" while leaving the view detached and
    // the next focus change behaving differently than it does everywhere else.
    m_viewDetached = false;
    const int centered = centeredAnchorFor(m_activeColumnIdx, params);
    if (m_viewAnchor == centered) {
        return false;
    }
    m_viewAnchor = centered;
    return true;
}

bool ScrollStrip::centerVisibleColumns(const ScrollLayoutParams& params)
{
    if (m_activeColumnIdx < 0) {
        return false;
    }
    // Degenerate-area guard, same rationale as clampedAnchor's: the span
    // math below would write a garbage anchor over persisted state. Refuse
    // (the verb reports no_target) rather than centering against nothing.
    if (mainExtent(params) <= 0) {
        return false;
    }
    // Re-attach for centerActiveColumn's reason, but BELOW this verb's own
    // refusal, unlike reanchorAfterFocusChange's clear: a centering verb that
    // bails changes nothing at all, where a focus change has already happened
    // by the time its guard is reached.
    m_viewDetached = false;
    const int viewMain = mainExtent(params);
    // FULLY visible columns only (niri center-visible-columns): a partially
    // clipped edge column is exactly what the verb pushes out of the way, so
    // it must not drag the span. The ONE spelling of that walk, shared with
    // the width-distribution verbs — zero-extent (fully minimized) columns
    // carry no strip position and are skipped there the same way
    // stripExtentPx skips them.
    const QVector<int> visible = fullyVisibleColumnIndices(params);
    // The span in STRIP coordinates. Two columnStripPos calls rather than one
    // per column: the per-column form re-walks the prefix each time, which is
    // quadratic in the column count.
    const int spanStart = visible.isEmpty() ? -1 : columnStripPos(visible.first(), params);
    const int spanEnd = visible.isEmpty()
        ? -1
        : columnStripPos(visible.last(), params) + columnExtentPx(m_columns.at(visible.last()), params);
    if (spanStart < 0) {
        // Nothing fully visible (a lone over-wide column, or a viewport mid
        // scroll) — centering the active column is the closest sensible act.
        return centerActiveColumn(params);
    }
    // Anchor the ACTIVE column relative to the centered span: the anchor is
    // active-relative state (see class doc), so the span center has to be
    // expressed through it. Deliberately unclamped, like centerActiveColumn:
    // a centered span near the strip's edge implies out-of-range viewOffset by
    // design, and later structural inserts re-clamp when needed.
    const int newViewOffset = spanStart - (viewMain - (spanEnd - spanStart)) / 2;
    const int anchor = columnStripPos(m_activeColumnIdx, params) - newViewOffset;
    if (anchor == m_viewAnchor) {
        return false;
    }
    m_viewAnchor = anchor;
    return true;
}

bool ScrollStrip::scrollViewBy(int delta, const ScrollLayoutParams& params)
{
    if (m_activeColumnIdx < 0 || delta == 0) {
        return false;
    }
    // Degenerate-area guard, same as clampedAnchor's (the rationale lives
    // there). Checked here too because this mutator runs from a timer: a
    // screen dying mid-drag must not walk the persisted anchor.
    if (mainExtent(params) <= 0) {
        return false;
    }
    // viewOffset = columnStripPos(active) - anchor, so moving the view
    // forward along the strip by delta means shrinking the anchor by delta.
    // Clamped, unlike the centering mutators: this one scrolls to a place the
    // user pointed at rather than to a computed position, so running past
    // either end must simply stop.
    //
    // The clamp is on the DELTA, not on the absolute position, and that
    // distinction is load-bearing. centerActiveColumn and centerVisibleColumns
    // deliberately store an anchor whose derived viewOffset is out of range
    // (their comments say so), and applyLayout skips updateViewForFocus for
    // the whole drag, so such an anchor survives untouched into the first
    // tick. Running it through clampedAnchor would snap the view the entire
    // way back into range in one tick — hundreds of pixels, in the direction
    // OPPOSITE to the requested scroll on the leading band. Clamping the delta
    // instead lets an out-of-range view walk back one tick's worth at a time
    // and never snaps.
    const int viewMain = mainExtent(params);
    const int activeMainPos = columnStripPos(m_activeColumnIdx, params);
    const int maxViewOffset = qMax(0, stripExtentPx(params) - viewMain);
    const int viewOffset = activeMainPos - m_viewAnchor;
    // qMax/qMin against viewOffset itself so an already-out-of-range view is
    // never pushed FURTHER out, but is free to travel back towards the range.
    const int target = delta > 0 ? qMin(viewOffset + delta, qMax(viewOffset, maxViewOffset))
                                 : qMax(viewOffset + delta, qMin(viewOffset, 0));
    const int anchor = activeMainPos - target;
    if (anchor == m_viewAnchor) {
        return false;
    }
    m_viewAnchor = anchor;
    // The view is now where the USER put it, so take it out of the centering
    // policy's hands until a focus change or a centering verb gives it back
    // (class doc). Set only on a real move: a refusal at either end must not
    // detach a view the policy still owns, or holding a scroll key against the
    // strip's end would silently change what the next layout pass does.
    m_viewDetached = true;
    return true;
}

bool ScrollStrip::stripFitsViewport(const ScrollLayoutParams& params) const
{
    if (mainExtent(params) <= 0) {
        return true;
    }
    return stripExtentPx(params) <= mainExtent(params);
}

bool ScrollStrip::stripSettledInViewport(const ScrollLayoutParams& params) const
{
    if (!stripFitsViewport(params)) {
        return false;
    }
    // Degenerate area or empty strip: nothing a scroll could reveal, and
    // viewOffsetFor needs a live active column to derive from.
    if (mainExtent(params) <= 0 || m_activeColumnIdx < 0) {
        return true;
    }
    const int viewOffset = viewOffsetFor(params);
    return viewOffset >= 0 && viewOffset <= qMax(0, stripExtentPx(params) - mainExtent(params));
}

ResolvedStrip ScrollStrip::relayout(const ScrollLayoutParams& params) const
{
    ResolvedStrip out;
    out.viewOffset = viewOffsetFor(params);

    const QRect area = params.workArea;
    const StripAxis axis = params.axis;
    const int gap = params.gap;
    // Walks ALONG the strip. The view offset only ever slides on this axis,
    // which is what makes one spring per output enough to carry the whole
    // strip rigidly.
    const int stripStart = axis.mainLow(area) - out.viewOffset;
    int mainCursor = stripStart;

    for (int ci = 0; ci < m_columns.size(); ++ci) {
        const Column& col = m_columns.at(ci);
        const int colW = columnExtentPx(col, params);
        // col.isEmpty() is unreachable by construction (every removal path
        // closes an emptied column) but kept local so the invariant does
        // not depend on every future mutation site remembering it.
        if (colW <= 0 || col.isEmpty()) {
            continue; // fully minimized — occupies no strip width
        }

        ResolvedColumn rc;
        rc.columnIndex = ci;
        rc.tabbed = col.display == ColumnDisplay::Tabbed;
        // Maximize-to-edges: the column resolves against the RAW work area on
        // both axes with the inner gap suppressed inside it. Its strip-space
        // position stays cursor-derived like every other column (the strip
        // still scrolls it), but once its span fully covers the viewport the
        // emitted rect snaps to the raw area exactly — cursor math is
        // workArea-based, and any anchor policy (pin, center, overflow) then
        // differs from the raw rect only by outer-gap slivers that would
        // otherwise peek through at the screen edges.
        const bool toEdges = col.maximizedToEdges;
        const QRect colArea = toEdges ? rawAreaFor(params) : area;
        const int colGap = toEdges ? 0 : gap;
        const bool coversViewport =
            toEdges && mainCursor <= axis.mainLow(area) && mainCursor + colW >= axis.mainLow(area) + mainExtent(params);
        const int mainStart = coversViewport ? axis.mainLow(colArea) : mainCursor;
        // Published so a consumer can tell "the user asked for this extent"
        // from "this column cannot be any narrower" — columnExtentPx takes the
        // max of the two and the answer is indistinguishable afterwards. A
        // maximized-to-edges extent is declared, never minimum-pinned.
        rc.extentPinnedByMinimum =
            !toEdges && columnMinExtentPx(col, params) >= resolveColumnWidthPx(col.width, params);
        rc.maximizedToEdges = toEdges;
        // The default: a column spans the FULL cross extent and only its main
        // extent varies. The tabbed branch below is the one exception and
        // rewrites this rect from the height intent of the tab that OWNS the
        // column (see tabbedColumnCrossPx), which is not necessarily the tab
        // on show and is not the only tab that may carry an intent.
        rc.rect = axis.makeRect(mainStart, axis.crossLow(colArea), colW, axis.crossSize(colArea));

        QVector<int> visible;
        visible.reserve(col.tiles.size());
        for (int ti = 0; ti < col.tiles.size(); ++ti) {
            if (!col.tiles.at(ti).minimized) {
                visible.append(ti);
            }
        }

        if (rc.tabbed) {
            // A tabbed column takes its CROSS extent from the height intent
            // of the tab that OWNS it (Column::heightOwnerId), not from
            // whichever tab is on show. niri parity for the SHAPE (a tabbed
            // column may be shorter than the work area) but not for the
            // bookkeeping: niri holds at most one non-Auto tile per column
            // and so can scan for it, while a column here may carry several
            // sized tiles and names its owner instead. So the rect the
            // full-cross default above wrote
            // is replaced before anything derives from it. Everything below —
            // the indicator rect, the content rect the tabs are committed at —
            // then follows the shortened column on its own.
            //
            // Maximize-to-edges wins over the owner's height intent: the flag
            // declares the full raw cross extent, so the default rect above
            // stands and the intent survives untouched for the restore. A
            // column that IS short then takes the centre policy, which is why
            // the start edge comes from crossStartFor rather than the area.
            if (!toEdges) {
                const int tabbedCross = tabbedColumnCrossPx(col, params);
                rc.rect = axis.makeRect(mainCursor, crossStartFor(tabbedCross, params, area), colW, tabbedCross);
            }
            // Only the active tile is laid out, at the column's content rect;
            // the others share its rect but are reported hidden.
            int activeTi = qBound(0, col.activeTileIdx, col.tiles.size() - 1);
            if (col.tiles.at(activeTi).minimized && !visible.isEmpty()) {
                activeTi = visible.first();
            }
            // The indicator resolves against the count of VISIBLE tiles, not
            // the column's total: a column whose extra tiles are all minimized
            // presents as a single tab, so hideWhenSingleTab must hide it. The
            // tiles then take whatever the indicator did not reserve, which is
            // the whole column unless placeWithinColumn is set.
            rc.tabIndicatorPosition = params.tabIndicator.position;
            rc.tabIndicatorRect = params.tabIndicator.indicatorRectFor(rc.rect, visible.size());
            const QRect full = params.tabIndicator.contentRectFor(rc.rect, visible.size());
            for (int ti : visible) {
                ResolvedTile rt;
                rt.windowId = col.tiles.at(ti).windowId;
                rt.rect = full;
                rt.hidden = (ti != activeTi);
                rt.windowedFullscreen = col.tiles.at(ti).windowedFullscreen;
                rc.tiles.append(rt);
            }
        } else if (!visible.isEmpty()) {
            // The stack, which runs ACROSS the strip: Fixed/Preset tiles take
            // their px, Auto tiles share the remainder proportionally to
            // weight. Every quantity from here to the end of the branch is a
            // cross-axis extent, which is why "height" keeps its name — it is
            // the role, not the screen dimension.
            const int n = visible.size();
            // colGap is 0 for a maximized-to-edges column: its stack divides
            // the raw cross extent with no separation, matching the gap-free
            // look the flag promises on every side.
            const int gapsTotal = colGap * (n - 1);
            const int availH = qMax(n, axis.crossSize(colArea) - gapsTotal);

            QVector<int> heights(n, 0);
            qreal autoWeightTotal = 0;
            int fixedTotal = 0;
            for (int vi = 0; vi < n; ++vi) {
                const Tile& t = col.tiles.at(visible.at(vi));
                switch (t.height.kind) {
                case WindowHeight::Fixed:
                    heights[vi] = qBound(1, t.height.fixedPx, availH);
                    fixedTotal += heights[vi];
                    break;
                case WindowHeight::Preset:
                    heights[vi] =
                        qMin(availH,
                             proportionalPx(nearestPresetValue(params.presetWindowHeights, t.height.presetFraction),
                                            axis.crossSize(colArea), colGap));
                    fixedTotal += heights[vi];
                    break;
                case WindowHeight::Auto:
                    autoWeightTotal += qMax<qreal>(0.01, t.height.weight);
                    break;
                }
            }
            // A lone tile fills the column height on Auto intent only; an
            // explicit Fixed/Preset height is honored (niri parity — a solo
            // window can be shorter than the column, leaving empty space
            // below it). The explicit heights were already resolved and
            // clamped to the work area in the switch above, and the
            // min-height clamp below still applies.
            if (n == 1) {
                if (col.tiles.at(visible.at(0)).height.kind == WindowHeight::Auto) {
                    heights[0] = availH;
                }
            } else {
                // Renormalize when the Fixed/Preset heights alone overflow
                // the column: scale them down proportionally so the last
                // window is not laid out past the bottom of the work area
                // (each tile was only clamped individually above). Auto
                // tiles keep a 1px floor below, so the Fixed/Preset budget
                // is availH minus one pixel per Auto tile — without the
                // reservation those floors would overflow the column again.
                int autoCount = 0;
                for (int vi = 0; vi < n; ++vi) {
                    if (col.tiles.at(visible.at(vi)).height.kind == WindowHeight::Auto) {
                        ++autoCount;
                    }
                }
                const int fixedBudget = qMax(0, availH - autoCount);
                if (fixedTotal > fixedBudget && fixedTotal > 0) {
                    int scaledTotal = 0;
                    for (int vi = 0; vi < n; ++vi) {
                        if (heights[vi] > 0) {
                            heights[vi] =
                                qMax(1, static_cast<int>(static_cast<qint64>(heights[vi]) * fixedBudget / fixedTotal));
                            scaledTotal += heights[vi];
                        }
                    }
                    fixedTotal = scaledTotal;
                }
                int autoAvail = qMax(0, availH - fixedTotal);
                qreal weightLeft = autoWeightTotal;
                for (int vi = 0; vi < n; ++vi) {
                    const Tile& t = col.tiles.at(visible.at(vi));
                    if (t.height.kind != WindowHeight::Auto) {
                        continue;
                    }
                    const qreal w = qMax<qreal>(0.01, t.height.weight);
                    const int h = (weightLeft > 0) ? qRound(autoAvail * (w / weightLeft)) : 0;
                    heights[vi] = qMax(1, h);
                    autoAvail -= heights[vi];
                    weightLeft -= w;
                }
            }
            // Min-height clamp after distribution. Soft constraint: when
            // even the floors overflow the column, the overflow stands and
            // the trailing tiles lay out below the work area (no float-out
            // policy exists for this case — the open-time oversized check
            // is the only min-size-driven float). The whole clamp (and the
            // rebalance it necessitates) is the respect-minimum-size arm:
            // off, the distributed heights stand and the compositor's own
            // min-size enforcement decides what overhangs.
            if (params.respectMinimumSize) {
                for (int vi = 0; vi < n; ++vi) {
                    // minCross: heights[] divides the column ACROSS the
                    // strip, so the floor is the client minimum on that axis.
                    const Tile& t = col.tiles.at(visible.at(vi));
                    const int floorPx = t.minCross(params.axis);
                    if (floorPx > 0 && heights[vi] < floorPx) {
                        heights[vi] = qMin(floorPx, availH);
                    }
                }
                // The clamp can push the stack past availH again (it has no
                // budget of its own). Rebalance by shrinking tiles that still
                // have slack above their own floor, proportionally to that
                // slack; when every tile sits at its floor the overflow stands
                // and the caller's cannot-fit policy applies.
                if (n > 1) {
                    int total = 0;
                    for (int vi = 0; vi < n; ++vi) {
                        total += heights[vi];
                    }
                    int excess = total - availH;
                    while (excess > 0) {
                        int slackTotal = 0;
                        for (int vi = 0; vi < n; ++vi) {
                            const Tile& t = col.tiles.at(visible.at(vi));
                            slackTotal += qMax(0, heights[vi] - qMax(1, t.minCross(params.axis)));
                        }
                        if (slackTotal <= 0) {
                            break;
                        }
                        for (int vi = 0; vi < n && excess > 0; ++vi) {
                            const Tile& t = col.tiles.at(visible.at(vi));
                            const int slack = qMax(0, heights[vi] - qMax(1, t.minCross(params.axis)));
                            if (slack <= 0) {
                                continue;
                            }
                            const int cut = qMin(
                                slack, qMax(1, static_cast<int>(static_cast<qint64>(excess) * slack / slackTotal)));
                            heights[vi] -= cut;
                            excess -= cut;
                        }
                    }
                }
            } // respectMinimumSize

            // The stack is laid out from the column's start edge unless the
            // centre policy is on and it came out short — see crossStartFor.
            // Measured from the RESOLVED heights, so every arm above (the
            // Fixed/Preset renormalize, the Auto share, the min-size clamp
            // and its rebalance) is already folded in, and an overflowing
            // stack yields no offset rather than a negative one.
            //
            // Against colArea, not area: a maximized-to-edges column is laid
            // out in the RAW work area, and centring it in the gapped one
            // would offset it by half the outer gap.
            int stackCross = gapsTotal;
            for (int vi = 0; vi < n; ++vi) {
                stackCross += heights[vi];
            }
            int crossCursor = crossStartFor(stackCross, params, colArea);
            for (int vi = 0; vi < n; ++vi) {
                ResolvedTile rt;
                rt.windowId = col.tiles.at(visible.at(vi)).windowId;
                rt.rect = axis.makeRect(mainStart, crossCursor, colW, heights[vi]);
                rt.windowedFullscreen = col.tiles.at(visible.at(vi)).windowedFullscreen;
                rc.tiles.append(rt);
                crossCursor += heights[vi] + colGap;
            }
        }

        out.columns.append(rc);
        mainCursor += colW + gap;
    }
    // Derived from this walk's own accumulation rather than a second
    // stripExtentPx pass: the cursor advanced colW + gap per contributing
    // column, so dropping the trailing gap reproduces stripExtentPx exactly
    // — this runs per tick on the auto-scroll and indicator paths, and the
    // duplicate O(columns x tiles) walk was pure cost.
    out.stripExtent = (mainCursor > stripStart) ? mainCursor - stripStart - gap : 0;
    return out;
}

} // namespace PhosphorScrollEngine
