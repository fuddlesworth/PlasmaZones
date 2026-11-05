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

    virtual int resolveInnerGap(PhosphorZones::Layout* layout, const QString& screenId) const = 0;
    virtual PhosphorLayout::EdgeGaps resolveOuterGaps(PhosphorZones::Layout* layout, const QString& screenId) const = 0;
    virtual int defaultBorderWidth() const = 0;
    virtual int defaultBorderRadius() const = 0;

    /// Per-side inset (in logical px) applied to a snapped window's frame.
    /// Returns 0 in all configurations today: the KWin effect's border shader
    /// recolours the window's OWN outermost band (inside the frame) for
    /// decorated and borderless windows alike, so the border never extends past
    /// the frame edge into the neighbour and a snapped window fills its zone
    /// exactly. Any visible separation between tiles comes from the zone
    /// gap/padding settings, not from a border-width inset. This is a reserved
    /// seam — implementations return 0 and the inset is pinned to 0 by
    /// test_daemongeometryresolver_inset — kept so a future per-window-border
    /// design that draws OUTSIDE the frame can reintroduce a non-zero inset
    /// here without re-threading the call sites.
    virtual int snapBorderInset() const = 0;
};

} // namespace PhosphorPlacement
