// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorScrollEngine/StripAxis.h>

#include <QRect>
#include <QSize>
#include <QString>

#include <optional>

namespace PhosphorScrollEngine::Detail {

/// Distance a parked window sits below the union of all outputs. Small on
/// purpose: "just past the bottom-most monitor" keeps coordinates sane for
/// KWin (extreme coordinates are never committed — the stuck-off-screen
/// folklore guard), while still separating parked rects from the union's
/// bottom edge itself. The park position carries NO direction meaning; the
/// scrollEdge field on the tile request does.
constexpr int kParkMargin = 16;

/// Minimum visible sliver a straddling tile keeps on screen, on BOTH axes:
/// the main-axis edge clamps and the cross-axis stack-overflow clamp share
/// this floor (each raises it to the client's declared minimum under
/// respectMinimumSize).
constexpr int kMinVisiblePeekPx = 48;

/// Everything the per-tile placement decision reads. Gathered into a struct
/// so the decision itself is a PURE function: it was previously ~95 lines
/// inline in applyLayout's emit loop, reachable only by driving a whole
/// engine batch, which is why it had no test path of its own.
struct ParkInputs
{
    /// The tile's resolved rect, before any park or clamp rewrites it.
    QRect tileRect;
    /// Its column's rect. Read only by the hidden-tab arm, which follows the
    /// COLUMN off-screen rather than the tile.
    QRect columnRect;
    QRect workArea;
    QRect screenRect;
    /// The client's declared minimum, or a null size when minimum sizes are
    /// not being respected. NEVER transposed: it is a fact about the window
    /// in screen coordinates, not a strip-axis quantity.
    QSize tileMin;
    StripAxis axis;
    /// Where a park lands: below the union of every output.
    int parkTop = 0;
    /// Non-active tile of a tabbed column.
    bool hidden = false;
    bool cropStraddlers = false;
};

/// The decision. The caller owns m_parkedScrollEdge and applies @ref
/// rememberedEdge to it, which keeps this function pure and testable.
struct ParkResult
{
    QRect rect;
    bool parked = false;
    /// What goes on the wire. Empty means the tile is emitting no departure.
    QString emittedEdge;
    /// What to do with this window's REMEMBERED departure edge: nullopt
    /// erases it, a value stores it. Every path decides one or the other, so
    /// there is no "leave it alone" case for a caller to get wrong.
    std::optional<QString> rememberedEdge;
};

/// Park a rect: move it below every output, keeping it within its own
/// screen's horizontal span.
///
/// DELIBERATELY PHYSICAL, on both axes, and it must stay that way. The safety
/// argument is "no point below the union of all outputs belongs to any
/// monitor", which is a claim about the desktop, not about which way the strip
/// runs. This will read as a missed transposition in review; it is not.
void parkRect(QRect& rect, const QRect& screenRect, int parkTop);

/// Resolve one tile's committed placement: off-viewport parking, the
/// main-axis straddle clamp, and the cross-axis stack-overflow clamp.
///
/// THE INVARIANT, in one sentence: a park on the MAIN (strip) axis always
/// carries a scrollEdge naming the end the column departed by and is the only
/// park crop mode can opt out of; a park on the CROSS (stack) axis never
/// carries one, is enforced in both crop and clamp mode, and preserves any
/// main-axis edge already consumed this pass by handing it back through
/// ParkResult::rememberedEdge.
///
/// Keying either arm on a PHYSICAL axis rather than a role silently swaps
/// those behaviours on a vertical strip: a stack overflow would emit a
/// departure edge the effect then animates as strip motion, and a genuine
/// strip departure would be suppressed by the absence of crop mode.
///
/// ORDER IS LOAD-BEARING: the main-axis arms run first, and the cross-axis arm
/// is reachable only by a tile whose main-axis position is already inside the
/// screen. On a vertical strip the main-axis arms run on y, so a main-axis
/// park is decided BEFORE the cross-axis test could misread the same rect.
///
/// @param remembered the departure edge this window is currently carrying, or
///        an empty string when it carries none.
ParkResult resolveTilePlacement(const ParkInputs& in, const QString& remembered);

} // namespace PhosphorScrollEngine::Detail
