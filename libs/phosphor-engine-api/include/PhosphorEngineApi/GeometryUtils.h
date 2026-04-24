// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngineApi/EngineTypes.h>
#include <PhosphorGeometry/GeometryUtils.h>
#include <phosphorengineapi_export.h>

#include <QRectF>
#include <QString>
#include <QVector>

class QScreen;

namespace PhosphorEngineApi {
namespace GeometryUtils {

using PhosphorGeometry::availableAreaToOverlayCoordinates;
using PhosphorGeometry::enforceWindowMinSizes;
using PhosphorGeometry::rectToJson;
using PhosphorGeometry::removeZoneOverlaps;
using PhosphorGeometry::snapToRect;

PHOSPHORENGINEAPI_EXPORT QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, QScreen* screen);

PHOSPHORENGINEAPI_EXPORT QString serializeZoneAssignments(const QVector<ZoneAssignmentEntry>& entries);

} // namespace GeometryUtils
} // namespace PhosphorEngineApi
