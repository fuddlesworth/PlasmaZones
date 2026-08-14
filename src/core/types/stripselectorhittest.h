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
//   gapIndex g   → open a NEW column at strip position g (boundary g sits
//                  before card g; boundary count() trails the last card).
//   columnIndex i, half 0 → join column i at the TOP (tile index 0).
//   columnIndex i, half 1 → join column i at the BOTTOM (append).
//   columnIndex i, half 2 → whole-card join (tabbed column tab dock).
//
// Gaps are checked FIRST and derived from the cards themselves — the QML
// row renders no width-taking gap items, so the space between two adjacent
// cards (inflated by `gapInflate` into each neighbour, so the thin spacing
// strip is actually hittable) is the boundary band. The leading and
// trailing boundaries inflate symmetrically around the outer card edges.

#include <QPointF>
#include <QRectF>
#include <QVector>

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

/// @p cardRects is index-aligned with the snapshot's columns; a null/empty
/// rect means QML has not laid that card out (scrolled out or first frame)
/// and it can be neither hit nor used as a gap edge. @p tabbed is the
/// per-card tabbed flag, same indexing. @p gapInflate is how far a gap band
/// reaches into each neighbouring card (clamped so a card always keeps a
/// hittable middle).
inline StripSelectorHit classifyStripSelectorPoint(const QVector<QRectF>& cardRects, const QVector<bool>& tabbed,
                                                   const QPointF& pos, qreal gapInflate)
{
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
    // Outer boundaries: symmetric bands around the first laid-out card's
    // left edge and the last one's right edge.
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
