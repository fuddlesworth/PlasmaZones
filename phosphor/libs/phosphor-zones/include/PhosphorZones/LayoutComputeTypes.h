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
};

} // namespace PhosphorZones

Q_DECLARE_METATYPE(PhosphorZones::LayoutSnapshot)
Q_DECLARE_METATYPE(PhosphorZones::LayoutComputeResult)
