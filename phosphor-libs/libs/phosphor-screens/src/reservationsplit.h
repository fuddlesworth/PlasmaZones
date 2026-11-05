// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QtGlobal>

namespace PhosphorScreens::Detail {

/// Per-axis split of a reserved strut across the axis' two edges.
/// `first` is top (vertical axis) or left (horizontal axis).
struct EdgeSplit
{
    int first = 0;
    int second = 0;
};

/// Distribute `reserved` px (the layer-shell sensor's authoritative total for
/// one axis) across that axis' two edges, using the panel source's per-edge
/// claims for direction.
///
/// A claim is the thickness of the thickest Plasma panel on that edge
/// (PlasmaPanelSource keeps the per-edge maximum), reported whether or not
/// that panel currently reserves space: an auto-hidden or floating panel
/// still reports a thickness but contributes nothing to the sensor total. A
/// straight proportional split therefore smears a non-reserving panel's
/// thickness onto the edge that does reserve, and both edges come out wrong.
/// A top panel plus a hidden bottom dock ended up with zones pushed under the
/// top panel and a matching gap at the bottom of the screen.
///
/// So attribute exactly when we can: if exactly one combination of edges adds
/// up to `reserved` (within a pixel or two of rounding slack), that
/// combination is the one actually reserving and takes the whole total. Only
/// when no single combination stands out do we fall back to the proportional
/// split, which is the best available guess when the claims and the total
/// disagree in a way we cannot attribute.
inline EdgeSplit splitReservation(int reserved, int claimFirst, int claimSecond)
{
    if (reserved <= 0) {
        return {};
    }
    claimFirst = qMax(0, claimFirst);
    claimSecond = qMax(0, claimSecond);
    const int claimTotal = claimFirst + claimSecond;
    if (claimTotal <= 0) {
        return {};
    }

    // Proportional split of `reserved` restricted to the given edges. Always
    // sums to exactly `reserved` — the second edge takes the remainder, so
    // rounding never loses or invents a pixel.
    const auto proportional = [reserved](int a, int b) {
        EdgeSplit s;
        s.first = (a + b) > 0 ? qRound(a * double(reserved) / (a + b)) : 0;
        s.second = reserved - s.first;
        return s;
    };

    // Rounding slack: panel thicknesses come from plasmashell's rounded
    // geometry, the total from the compositor's configure. On fractionally
    // scaled outputs the two can disagree by a pixel without meaning
    // different panels.
    constexpr int kSlack = 2;

    struct Candidate
    {
        int sum;
        bool useFirst;
        bool useSecond;
    };
    const Candidate candidates[] = {
        {claimFirst, true, false},
        {claimSecond, false, true},
        {claimTotal, true, true},
    };

    int bestError = -1;
    int bestCount = 0;
    const Candidate* best = nullptr;
    for (const Candidate& c : candidates) {
        if (c.sum <= 0) {
            continue;
        }
        const int error = qAbs(c.sum - reserved);
        if (error > kSlack) {
            continue;
        }
        if (bestError < 0 || error < bestError) {
            bestError = error;
            bestCount = 1;
            best = &c;
        } else if (error == bestError) {
            ++bestCount;
        }
    }

    if (best && bestCount == 1) {
        return proportional(best->useFirst ? claimFirst : 0, best->useSecond ? claimSecond : 0);
    }

    return proportional(claimFirst, claimSecond);
}

} // namespace PhosphorScreens::Detail
