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
};

} // namespace PhosphorPlacement
