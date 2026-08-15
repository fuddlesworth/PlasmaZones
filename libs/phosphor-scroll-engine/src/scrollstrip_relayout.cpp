// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollStrip.h>

#include <QtGlobal>

namespace PhosphorScrollEngine {

namespace {

/// A proportion resolves against the work extent PLUS one gap so that
/// proportions summing to 1 tile edge-to-edge with the gaps between them
/// (0.5 + 0.5 across a 1000px area with a 10px gap → 495 + 10 + 495).
int proportionalPx(qreal proportion, int workExtent, int gap)
{
    return qMax(1, qRound(proportion * (workExtent + gap)) - gap);
}

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
    int px = resolveColumnWidthPx(c.width, params);
    if (params.respectMinimumSize) {
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
        // CROSS axis needs nothing: the tabbed branch applies no minimum floor
        // across the column, so there is no contradicted clamp there.
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
            if (!tile.minimized && tileMinMain + reservationFloor > px) {
                px = tileMinMain + reservationFloor;
            }
        }
    }
    return qMin(px, mainExtent(params));
}

int ScrollStrip::columnStripPos(int columnIndex, const ScrollLayoutParams& params) const
{
    int x = 0;
    const int end = qBound(0, columnIndex, m_columns.size());
    for (int i = 0; i < end; ++i) {
        const int w = columnExtentPx(m_columns.at(i), params);
        if (w > 0) {
            x += w + params.gap;
        }
    }
    return x;
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
    // there): a centered anchor computed against a 0-width area is 0.
    if (mainExtent(params) <= 0) {
        return m_viewAnchor;
    }
    const int colW = columnExtentPx(m_columns.at(columnIndex), params);
    return (mainExtent(params) - colW) / 2;
}

int ScrollStrip::keepOrRecenterAnchor(int oldViewOffset, const ScrollLayoutParams& params) const
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
    const bool centerLone = params.alwaysCenterSingleColumn && m_columns.size() == 1;
    if (centerLone || params.centerFocusedColumn == CenterFocusedColumn::Always) {
        return centeredAnchorFor(m_activeColumnIdx, params);
    }
    return clampedAnchor(columnStripPos(m_activeColumnIdx, params) - oldViewOffset, params);
}

int ScrollStrip::clampedAnchor(int anchor, const ScrollLayoutParams& params) const
{
    if (m_activeColumnIdx < 0) {
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
    const int workW = mainExtent(params);
    const int stripW = stripExtentPx(params);
    const int stripX = columnStripPos(m_activeColumnIdx, params);
    // anchor = stripX - viewOffset; viewOffset must stay within [0, max(0, stripW - workW)].
    const int maxViewOffset = qMax(0, stripW - workW);
    const int viewOffset = qBound(0, stripX - anchor, maxViewOffset);
    return stripX - viewOffset;
}

void ScrollStrip::reanchorAfterFocusChange(int prevIdx, int oldViewOffset, const ScrollLayoutParams& params)
{
    if (m_activeColumnIdx < 0) {
        m_viewAnchor = 0;
        return;
    }
    // Degenerate-area guard, same as clampedAnchor's (the rationale lives
    // there): every arm below would write 0 over the persisted anchor.
    if (mainExtent(params) <= 0) {
        return;
    }
    const int workW = mainExtent(params);
    const int colW = columnExtentPx(m_columns.at(m_activeColumnIdx), params);
    const int stripX = columnStripPos(m_activeColumnIdx, params);

    const bool centerLoneColumn = params.alwaysCenterSingleColumn && m_columns.size() == 1;
    bool center = centerLoneColumn || params.centerFocusedColumn == CenterFocusedColumn::Always;

    if (!center && params.centerFocusedColumn == CenterFocusedColumn::OnOverflow && prevIdx >= 0
        && prevIdx < m_columns.size() && prevIdx != m_activeColumnIdx) {
        const int prevW = columnExtentPx(m_columns.at(prevIdx), params);
        if (colW + params.gap + prevW > workW) {
            center = true;
        }
    }

    if (center) {
        m_viewAnchor = centeredAnchorFor(m_activeColumnIdx, params);
        return;
    }

    // CenterFocusedColumn::Never — scroll the minimum amount that makes the
    // focused column fully visible, pinning it to the edge it entered from.
    int pos = stripX - oldViewOffset;
    if (colW >= workW) {
        // Wider than the viewport: pin the entering edge.
        pos = (prevIdx >= 0 && m_activeColumnIdx < prevIdx) ? workW - colW : 0;
    } else if (pos < 0) {
        pos = 0;
    } else if (pos + colW > workW) {
        pos = workW - colW;
    }
    m_viewAnchor = clampedAnchor(pos, params);
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
    // Policy re-application only: when the active column is already fully
    // visible under a non-centering policy, leave the anchor alone — this
    // runs at the top of every applyLayout, and re-clamping there would
    // silently undo an explicit centerActiveColumn at the strip's edges
    // (whose centered anchor implies out-of-range viewOffset by design) and
    // reclaim removeWindowInternal's deliberate right-edge dead space.
    const bool centerLone = params.alwaysCenterSingleColumn && m_columns.size() == 1;
    if (!centerLone && params.centerFocusedColumn != CenterFocusedColumn::Always && m_activeColumnIdx >= 0) {
        const int workW = mainExtent(params);
        const int colW = columnExtentPx(m_columns.at(m_activeColumnIdx), params);
        const int pos = columnStripPos(m_activeColumnIdx, params) - viewOffsetFor(params);
        if (pos >= 0 && pos + colW <= workW) {
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
    const int workW = mainExtent(params);
    const int viewOffset = viewOffsetFor(params);
    // FULLY visible columns only (niri center-visible-columns): a partially
    // clipped edge column is exactly what the verb pushes out of the way, so
    // it must not drag the span. Zero-width (fully minimized) columns carry
    // no strip position and are skipped the same way stripExtentPx skips them.
    int spanStart = -1;
    int spanEnd = -1;
    for (int i = 0; i < m_columns.size(); ++i) {
        const int w = columnExtentPx(m_columns.at(i), params);
        if (w <= 0) {
            continue;
        }
        const int stripX = columnStripPos(i, params);
        const int pos = stripX - viewOffset;
        if (pos < 0 || pos + w > workW) {
            continue;
        }
        if (spanStart < 0) {
            spanStart = stripX;
        }
        spanEnd = stripX + w;
    }
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
    const int newViewOffset = spanStart - (workW - (spanEnd - spanStart)) / 2;
    const int anchor = columnStripPos(m_activeColumnIdx, params) - newViewOffset;
    if (anchor == m_viewAnchor) {
        return false;
    }
    m_viewAnchor = anchor;
    return true;
}

ResolvedStrip ScrollStrip::relayout(const ScrollLayoutParams& params) const
{
    ResolvedStrip out;
    out.stripExtent = stripExtentPx(params);
    out.viewOffset = viewOffsetFor(params);

    const QRect area = params.workArea;
    const StripAxis axis = params.axis;
    const int gap = params.gap;
    // Walks ALONG the strip. The view offset only ever slides on this axis,
    // which is what makes one spring per output enough to carry the whole
    // strip rigidly.
    int mainCursor = axis.mainLow(area) - out.viewOffset;

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
        // A column spans the FULL cross extent; only its main extent varies.
        rc.rect = axis.makeRect(mainCursor, axis.crossLow(area), colW, crossExtent(params));

        QVector<int> visible;
        visible.reserve(col.tiles.size());
        for (int ti = 0; ti < col.tiles.size(); ++ti) {
            if (!col.tiles.at(ti).minimized) {
                visible.append(ti);
            }
        }

        if (rc.tabbed) {
            // Only the active tile is laid out, at full column height; the
            // others share its rect but are reported hidden.
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
            const int gapsTotal = gap * (n - 1);
            const int availH = qMax(n, crossExtent(params) - gapsTotal);

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
                                            crossExtent(params), gap));
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

            int crossCursor = axis.crossLow(area);
            for (int vi = 0; vi < n; ++vi) {
                ResolvedTile rt;
                rt.windowId = col.tiles.at(visible.at(vi)).windowId;
                rt.rect = axis.makeRect(mainCursor, crossCursor, colW, heights[vi]);
                rt.windowedFullscreen = col.tiles.at(visible.at(vi)).windowedFullscreen;
                rc.tiles.append(rt);
                crossCursor += heights[vi] + gap;
            }
        }

        out.columns.append(rc);
        mainCursor += colW + gap;
    }
    return out;
}

} // namespace PhosphorScrollEngine
