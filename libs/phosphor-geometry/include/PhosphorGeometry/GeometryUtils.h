// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorgeometry_export.h>

#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>

namespace PhosphorGeometry {

namespace GeometryDefaults {
inline constexpr int MinRectSizePx = 50;
} // namespace GeometryDefaults

PHOSPHORGEOMETRY_EXPORT QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, const QRect& overlayGeometry);

PHOSPHORGEOMETRY_EXPORT QRect snapToRect(const QRectF& rf);

// Grows zones to accommodate per-window minimum sizes by stealing surplus
// from adjacent neighbors, then resolves any residual overlap.
//
// Vector tolerance (matches clampZonesToScreen): if minSizes is shorter than
// zones, missing entries are treated as no minimum (zero size). Extra entries
// past zones.size() are ignored. Empty minSizes is a no-op (nothing to enforce);
// empty zones is also a no-op.
PHOSPHORGEOMETRY_EXPORT void enforceMinSizes(QVector<QRect>& zones, const QVector<QSize>& minSizes, int gapThreshold,
                                             int innerGap = 0);

// Position-only bounds clamp. Shifts each zone so its effective rect (max of
// the zone's own size and the corresponding window's declared minSize) stays
// inside the screen. Sizes are preserved; only x/y move. Pass an empty or
// zero-filled minSizes vector to clamp purely against the zone's own size —
// unlike enforceMinSizes, an empty minSizes does NOT short-circuit the
// function: the zone's own dimensions are still clamped against the screen.
//
// Vector tolerance (matches enforceMinSizes): if minSizes is shorter
// than zones, missing entries are treated as no minimum (zero size). Extra
// entries past zones.size() are ignored. Empty zones is a no-op; an invalid
// screen (default-constructed QRect) is a no-op.
//
// Why position-only: this runs after enforceMinSizes, which is the only
// path allowed to grow/shrink zones. For any algorithm where
// producesOverlappingZones() is true (Deck, Stair, Cascade, Monocle, Paper,
// Spread, horizontal-deck — and any future algo opting in)
// enforceMinSizes is skipped because neighbor-stealing would destroy
// intentional overlap; a pure position shift is the only safe correction in
// that case. For non-overlapping algorithms a remaining overflow means the
// constraint solver couldn't satisfy the layout — shifting accepts that
// compromise rather than letting the window be pushed onto an adjacent
// monitor by the compositor's min-size enforcement.
PHOSPHORGEOMETRY_EXPORT void clampZonesToScreen(QVector<QRect>& zones, const QVector<QSize>& minSizes,
                                                const QRect& screen);

PHOSPHORGEOMETRY_EXPORT void removeRectOverlaps(QVector<QRect>& zones, const QVector<QSize>& minSizes = {},
                                                int innerGap = 0);

PHOSPHORGEOMETRY_EXPORT QString rectToJson(const QRect& rect);

} // namespace PhosphorGeometry
