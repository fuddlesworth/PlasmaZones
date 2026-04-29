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

PHOSPHORGEOMETRY_EXPORT QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, const QRect& overlayGeometry);

PHOSPHORGEOMETRY_EXPORT QRect snapToRect(const QRectF& rf);

// Grows zones to accommodate per-window minimum sizes by stealing surplus
// from adjacent neighbors, then resolves any residual overlap.
//
// Vector tolerance (matches clampZonesToScreen): if minSizes is shorter than
// zones, missing entries are treated as no minimum (zero size). Extra entries
// past zones.size() are ignored. Empty minSizes is a no-op.
PHOSPHORGEOMETRY_EXPORT void enforceWindowMinSizes(QVector<QRect>& zones, const QVector<QSize>& minSizes,
                                                   int gapThreshold, int innerGap = 0);

// Position-only bounds clamp. Shifts each zone so its effective rect (max of
// the zone's own size and the corresponding window's declared minSize) stays
// inside the screen. Sizes are preserved; only x/y move. Pass an empty or
// zero-filled minSizes vector to clamp purely against the zone's own size.
//
// Vector tolerance (matches enforceWindowMinSizes): if minSizes is shorter
// than zones, missing entries are treated as no minimum (zero size). Extra
// entries past zones.size() are ignored.
//
// Why position-only: this runs after enforceWindowMinSizes, which is the only
// path allowed to grow/shrink zones. For overlapping algorithms (Deck/Stair/
// Cascade/Monocle/Paper) enforceWindowMinSizes is skipped because neighbor-
// stealing would destroy intentional overlap; a pure position shift is the
// only safe correction in that case. For non-overlapping algorithms a
// remaining overflow means the constraint solver couldn't satisfy the layout
// — shifting accepts that compromise rather than letting the window be
// pushed onto an adjacent monitor by the compositor's min-size enforcement.
PHOSPHORGEOMETRY_EXPORT void clampZonesToScreen(QVector<QRect>& zones, const QVector<QSize>& minSizes,
                                                const QRect& screen);

PHOSPHORGEOMETRY_EXPORT void removeZoneOverlaps(QVector<QRect>& zones, const QVector<QSize>& minSizes = {},
                                                int innerGap = 0);

PHOSPHORGEOMETRY_EXPORT QString rectToJson(const QRect& rect);

} // namespace PhosphorGeometry
