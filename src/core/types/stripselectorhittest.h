// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Pure classification math for the strip-mode zone selector hit-test:
// rendered card rects + a cursor point → which drop target the popup is
// offering. Split out of selector_strip.cpp so tests/unit/daemon can pin
// the gap/half/whole rules without a QML scene.
//
// Vocabulary (mirrors IPlacementEngine::DragInsertTarget through
// OverlayService's int-only SelectorStripTarget):
// All positional words below are ALONG THE STRIP AXIS, not on the screen: a
// vertical strip reads "before/after" down the popup and "top/bottom" of a
// card as its left/right. The vocabulary itself is axis-neutral in meaning,
// which is what lets the vertical case transpose into the horizontal math.
//   gapIndex g   → open a NEW column at strip position g (boundary g sits
//                  before card g along the axis; boundary count() trails the
//                  last card).
//   columnIndex i, half 0 → join column i at its FIRST tile (tile index 0) —
//                  the top half of the card on a horizontal strip, the left
//                  half on a vertical one.
//   columnIndex i, half 1 → join column i at its LAST tile (append) — the
//                  bottom half horizontally, the right half vertically.
//   columnIndex i, half 2 → whole-card join (tabbed column tab dock).
//                  A distinct hit for highlight purposes only: the consumer
//                  encodes it as the same append (-1) DragInsertTarget as
//                  half 1, because appending to a Tabbed column IS the tab
//                  dock — there is no separate verb.
//
// Gaps are checked FIRST and derived from the cards themselves — the QML
// row renders no width-taking gap items, so the space between two adjacent
// cards (inflated by `gapInflate` into each neighbour, so the thin spacing
// strip is actually hittable) is the boundary band. The leading and
// trailing boundaries band around the outer card edges (raw gapInflate on
// the outside, the clamped per-card inflate on the inside — on a narrow
// card the two widths differ on purpose, keeping the middle hittable).

#include <QPointF>
#include <QRectF>
#include <QVector>

#include <algorithm>

namespace PlasmaZones {

struct StripSelectorHit
{
    int columnIndex = -1;
    int gapIndex = -1;
    /// 0 top half, 1 bottom half, 2 whole card; -1 when no card is hit.
    int half = -1;

    bool isValid() const
    {
        return columnIndex >= 0 || gapIndex >= 0;
    }
};

/// @p cardRects is index-aligned with the snapshot's columns, and the rects
/// must be monotonically increasing ALONG THE STRIP AXIS — in x when
/// @p verticalAxis is false, in y when it is true. The interior-band math
/// assumes index order is visual order along that axis (rects out of order
/// compute negative-extent bands, which silently never match). A null/empty
/// rect means QML has not laid that card out (scrolled out or first frame) and
/// it can be neither hit nor used as a gap edge; an un-laid-out card in the
/// MIDDLE of the row therefore silences both of its adjacent boundaries —
/// latent by construction, since a scrolled row/stack only ever un-lays the
/// ends. @p tabbed is the per-card tabbed flag, same indexing
/// (a shorter vector deliberately degrades the surplus cards to top/bottom
/// halves rather than asserting). @p gapInflate is how far a gap band
/// reaches into each neighbouring card (clamped so a card always keeps a
/// hittable middle). An EMPTY @p cardRects yields no hit at all: the
/// empty-strip "whole bar opens the first column" target is the CALLER's
/// branch (selector_strip.cpp), not this function's.
///
/// @p verticalAxis keeps its default because the horizontal case IS the
/// identity here (the vertical arm transposes into it), and the pinned tests
/// take the default deliberately to say so. The sole production caller passes
/// it explicitly, read back off the property the row was laid out with, and a
/// future caller should do the same: a wrong axis is silent, so guessing it
/// from the rects is not an option this function offers.
inline StripSelectorHit classifyStripSelectorPoint(const QVector<QRectF>& cardRects, const QVector<bool>& tabbed,
                                                   const QPointF& pos, qreal gapInflate, bool verticalAxis = false)
{
    // The popup is a MINIATURE of the strip, so it must mirror the axis. If
    // the strip runs vertically and this row stayed horizontal, half 0/1 would
    // still mean "top/bottom of the card" while a column has become a
    // horizontal band — which silently REVERSES insert order, with no error
    // and no visual tell.
    //
    // Transposed at the BOUNDARY rather than by forking the math below: swap
    // every rect and the point through the line y = x, run the existing
    // classification untouched, and return the hit as-is. The returned
    // vocabulary is already axis-neutral in meaning (gapIndex, columnIndex,
    // half 0 = first tile, 1 = append, 2 = whole card), so nothing downstream
    // changes — and the horizontal case stays byte-identical, which keeps
    // every pinned test meaningful as the identity case.
    if (verticalAxis) {
        // Unconditional: emptiness survives the transpose (a non-positive
        // width becomes a non-positive height), so an un-laid-out card stays
        // un-laid-out either way and the laidOut gate reads the same. Skipping
        // the swap for those would leave one rect in the untransposed space,
        // which is a shape this arm should never hand on.
        const auto flip = [](const QRectF& r) {
            return QRectF(r.y(), r.x(), r.height(), r.width());
        };
        QVector<QRectF> swapped;
        swapped.reserve(cardRects.size());
        for (const QRectF& r : cardRects) {
            swapped.append(flip(r));
        }
        return classifyStripSelectorPoint(swapped, tabbed, QPointF(pos.y(), pos.x()), gapInflate, false);
    }
    StripSelectorHit hit;
    const int count = cardRects.size();

    auto laidOut = [&](int i) {
        return i >= 0 && i < count && !cardRects.at(i).isEmpty();
    };
    // A gap band may not reach past a third of either neighbour: a single
    // narrow card must keep a hittable middle between its two boundaries.
    auto inflateFor = [&](int i) {
        return std::min(gapInflate, cardRects.at(i).width() / 3.0);
    };

    // Interior boundaries first: the strip spacing between adjacent
    // laid-out cards, inflated into both neighbours.
    for (int i = 1; i < count; ++i) {
        if (!laidOut(i - 1) || !laidOut(i)) {
            continue;
        }
        const QRectF& left = cardRects.at(i - 1);
        const QRectF& right = cardRects.at(i);
        const QRectF band(left.right() - inflateFor(i - 1), std::min(left.top(), right.top()),
                          (right.left() + inflateFor(i)) - (left.right() - inflateFor(i - 1)),
                          std::max(left.bottom(), right.bottom()) - std::min(left.top(), right.top()));
        if (band.contains(pos)) {
            hit.gapIndex = i;
            return hit;
        }
    }
    // Outer boundaries: bands around the first laid-out card's left edge
    // and the last one's right edge (raw gapInflate outside, clamped
    // inflate inside — see the header note).
    int first = 0;
    while (first < count && !laidOut(first)) {
        ++first;
    }
    int last = count - 1;
    while (last >= 0 && !laidOut(last)) {
        --last;
    }
    if (first < count) {
        const QRectF& firstRect = cardRects.at(first);
        const QRectF leadBand(firstRect.left() - gapInflate, firstRect.top(), gapInflate + inflateFor(first),
                              firstRect.height());
        if (leadBand.contains(pos)) {
            hit.gapIndex = first;
            return hit;
        }
        const QRectF& lastRect = cardRects.at(last);
        const QRectF trailBand(lastRect.right() - inflateFor(last), lastRect.top(), inflateFor(last) + gapInflate,
                               lastRect.height());
        if (trailBand.contains(pos)) {
            hit.gapIndex = last + 1;
            return hit;
        }
    }

    for (int i = 0; i < count; ++i) {
        if (!laidOut(i) || !cardRects.at(i).contains(pos)) {
            continue;
        }
        hit.columnIndex = i;
        if (i < tabbed.size() && tabbed.at(i)) {
            hit.half = 2;
        } else {
            hit.half = pos.y() <= cardRects.at(i).center().y() ? 0 : 1;
        }
        return hit;
    }
    return hit;
}

} // namespace PlasmaZones
