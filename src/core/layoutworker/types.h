// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QRectF>
#include <QUuid>
#include <QVector>

namespace PlasmaZones {

/// Immutable snapshot of a single zone's geometry inputs.
struct ZoneSnapshot
{
    QUuid id;
    QRectF relativeGeometry;
    bool fixedMode = false;
    QRectF fixedGeometry;
};

/// Immutable snapshot of a layout + screen, sufficient for geometry computation.
/// Built on the main thread from live Layout/Zone objects (all const reads).
struct LayoutSnapshot
{
    QUuid layoutId;
    QString screenId;
    QRectF screenGeometry;
    QVector<ZoneSnapshot> zones;
    bool useFullScreenGeometry = false;
};

/// Computed absolute geometry for a single zone.
struct ComputedZoneGeometry
{
    QUuid zoneId;
    QRectF absoluteGeometry;
};

/// Result of a layout geometry computation on the worker thread.
struct LayoutComputeResult
{
    QUuid layoutId;
    QString screenId;
    QRectF screenGeometry;
    QVector<ComputedZoneGeometry> zones;
    uint64_t generation = 0;
};

} // namespace PlasmaZones

Q_DECLARE_METATYPE(PlasmaZones::LayoutSnapshot)
Q_DECLARE_METATYPE(PlasmaZones::LayoutComputeResult)
