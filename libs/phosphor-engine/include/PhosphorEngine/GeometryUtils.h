// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorGeometry/GeometryUtils.h>
#include <phosphorengine_export.h>

#include <QString>
#include <QVector>

namespace PhosphorEngine {
namespace GeometryUtils {

using PhosphorGeometry::availableAreaToOverlayCoordinates;
using PhosphorGeometry::clampZonesToScreen;
using PhosphorGeometry::enforceWindowMinSizes;
using PhosphorGeometry::rectToJson;
using PhosphorGeometry::removeZoneOverlaps;
using PhosphorGeometry::snapToRect;

PHOSPHORENGINE_EXPORT QString serializeZoneAssignments(const QVector<ZoneAssignmentEntry>& entries);

} // namespace GeometryUtils
} // namespace PhosphorEngine
