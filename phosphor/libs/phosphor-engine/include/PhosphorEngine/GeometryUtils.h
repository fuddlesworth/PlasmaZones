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
using PhosphorGeometry::enforceMinSizes;
using PhosphorGeometry::rectToJson;
using PhosphorGeometry::removeRectOverlaps;
using PhosphorGeometry::snapToRect;

PHOSPHORENGINE_EXPORT QString serializeZoneAssignments(const QVector<ZoneAssignmentEntry>& entries);

/// Parse the wire format serializeZoneAssignments produces back into entries —
/// the two functions are the single serializer/deserializer pair for the batch
/// resnap payload, sharing the same JsonKeys constants so the sides cannot
/// drift. Entries missing a windowId or targetZoneId are dropped; a missing
/// VirtualDesktop key (or a negative wire value) yields 0, the current-desktop
/// default. On malformed JSON returns an empty vector and, when @p errorString
/// is non-null, stores a human-readable parse diagnostic there (empty on
/// success — an empty RESULT with an empty errorString is a legitimate empty
/// batch).
PHOSPHORENGINE_EXPORT QVector<ZoneAssignmentEntry> deserializeZoneAssignments(const QString& json,
                                                                              QString* errorString = nullptr);

} // namespace GeometryUtils
} // namespace PhosphorEngine
