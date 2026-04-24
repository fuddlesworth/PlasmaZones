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

PHOSPHORGEOMETRY_EXPORT void enforceWindowMinSizes(QVector<QRect>& zones, const QVector<QSize>& minSizes,
                                                   int gapThreshold, int innerGap = 0);

PHOSPHORGEOMETRY_EXPORT void removeZoneOverlaps(QVector<QRect>& zones, const QVector<QSize>& minSizes = {},
                                                int innerGap = 0);

PHOSPHORGEOMETRY_EXPORT QString rectToJson(const QRect& rect);

} // namespace PhosphorGeometry
