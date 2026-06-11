// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorplacement_export.h>
#include <PhosphorLayoutApi/EdgeGaps.h>
#include <QString>

namespace PhosphorZones {
class Layout;
}

namespace PhosphorPlacement {

class PHOSPHORPLACEMENT_EXPORT IGeometryResolver
{
public:
    virtual ~IGeometryResolver() = default;

    virtual int resolveZonePadding(PhosphorZones::Layout* layout, const QString& screenId) const = 0;
    virtual PhosphorLayout::EdgeGaps resolveOuterGaps(PhosphorZones::Layout* layout, const QString& screenId) const = 0;
    virtual int defaultBorderWidth() const = 0;
    virtual int defaultBorderRadius() const = 0;

    /// Per-side inset (in logical px) to shrink a snapped window's frame by so
    /// the snap border the KWin effect draws on the window's own edge sits
    /// INSIDE the zone, leaving a border-width gap between adjacent tiles.
    /// Returns 0 when the snapping show-border setting is off (no border → no
    /// inset, current behavior). The width mirrors exactly what the effect
    /// borders snapped windows with: the global snapping border width
    /// (snappingBorderWidth), NOT a zone's custom width — the effect's snap
    /// BorderState carries a single per-mode width for every snapped window.
    /// Per-window SetBorderVisible rules on an otherwise-borderless window are
    /// out of scope: they're resolved compositor-side per window and aren't
    /// reachable from this daemon-side geometry layer, so such windows are not
    /// inset.
    virtual int snapBorderInset() const = 0;
};

} // namespace PhosphorPlacement
