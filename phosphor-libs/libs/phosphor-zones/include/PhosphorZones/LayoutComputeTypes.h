// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorzones_export.h>

#include <QMetaType>
#include <QRectF>
#include <QString>
#include <QUuid>
#include <QVector>

namespace PhosphorZones {

enum class ZoneGeometryMode : int;

struct ZoneSnapshot
{
    QUuid id;
    ZoneGeometryMode geometryMode{};
    QRectF relativeGeometry;
    QRectF fixedGeometry;
};

struct LayoutSnapshot
{
    QUuid layoutId;
    QString screenId;
    QRectF screenGeometry;
    QVector<ZoneSnapshot> zones;
    /// Logical scale of the output, carried to the result so the applier
    /// stamps the frame the geometries were computed in (0 = not known).
    qreal screenScale = 0.0;
    /// What fixed geometries are divided by (Layout::fixedGeometryDivisor),
    /// resolved on the GUI thread: the worker cannot read the layout.
    qreal fixedDivisor = 1.0;
};

struct ComputedZoneGeometry
{
    QUuid zoneId;
    QRectF absoluteGeometry;
};

struct LayoutComputeResult
{
    QUuid layoutId;
    QString screenId;
    QRectF screenGeometry;
    QVector<ComputedZoneGeometry> zones;
    uint64_t generation = 0;
    qreal screenScale = 0.0;
};

} // namespace PhosphorZones

Q_DECLARE_METATYPE(PhosphorZones::LayoutSnapshot)
Q_DECLARE_METATYPE(PhosphorZones::LayoutComputeResult)
