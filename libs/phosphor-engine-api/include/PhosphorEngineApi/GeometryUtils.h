// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngineApi/EngineTypes.h>
#include <phosphorengineapi_export.h>

#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>

class QScreen;

namespace PhosphorEngineApi {
namespace GeometryUtils {

PHOSPHORENGINEAPI_EXPORT QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, QScreen* screen);
PHOSPHORENGINEAPI_EXPORT QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, const QRect& overlayGeometry);

PHOSPHORENGINEAPI_EXPORT QRect snapToRect(const QRectF& rf);

PHOSPHORENGINEAPI_EXPORT void enforceWindowMinSizes(QVector<QRect>& zones, const QVector<QSize>& minSizes,
                                                    int gapThreshold, int innerGap = 0);

PHOSPHORENGINEAPI_EXPORT void removeZoneOverlaps(QVector<QRect>& zones, const QVector<QSize>& minSizes = {},
                                                 int innerGap = 0);

PHOSPHORENGINEAPI_EXPORT QString rectToJson(const QRect& rect);

PHOSPHORENGINEAPI_EXPORT QString serializeZoneAssignments(const QVector<ZoneAssignmentEntry>& entries);

} // namespace GeometryUtils
} // namespace PhosphorEngineApi
