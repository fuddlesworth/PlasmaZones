// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutworker.h"

namespace PlasmaZones {

LayoutWorker::LayoutWorker(QObject* parent)
    : QObject(parent)
{
}

void LayoutWorker::computeGeometries(const LayoutSnapshot& snapshot, uint64_t generation)
{
    LayoutComputeResult result;
    result.layoutId = snapshot.layoutId;
    result.screenId = snapshot.screenId;
    result.screenGeometry = snapshot.screenGeometry;
    result.generation = generation;
    result.zones.reserve(snapshot.zones.size());

    const QRectF& screen = snapshot.screenGeometry;

    for (const auto& zone : snapshot.zones) {
        ComputedZoneGeometry computed;
        computed.zoneId = zone.id;

        if (zone.fixedMode) {
            computed.absoluteGeometry = QRectF(screen.x() + zone.fixedGeometry.x(), screen.y() + zone.fixedGeometry.y(),
                                               zone.fixedGeometry.width(), zone.fixedGeometry.height());
        } else {
            computed.absoluteGeometry = QRectF(screen.x() + zone.relativeGeometry.x() * screen.width(),
                                               screen.y() + zone.relativeGeometry.y() * screen.height(),
                                               zone.relativeGeometry.width() * screen.width(),
                                               zone.relativeGeometry.height() * screen.height());
        }

        result.zones.append(computed);
    }

    Q_EMIT geometriesReady(result);
}

} // namespace PlasmaZones
