// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/LayoutWorker.h>
#include <PhosphorZones/Zone.h>

namespace PhosphorZones {

LayoutWorker::LayoutWorker(QObject* parent)
    : QObject(parent)
{
}

LayoutComputeResult LayoutWorker::compute(const LayoutSnapshot& snapshot, uint64_t generation)
{
    LayoutComputeResult result;
    result.layoutId = snapshot.layoutId;
    result.screenId = snapshot.screenId;
    result.screenGeometry = snapshot.screenGeometry;
    result.generation = generation;
    result.screenScale = snapshot.screenScale;
    result.zoneGeometryEpoch = snapshot.zoneGeometryEpoch;
    result.syncRecalcEpoch = snapshot.syncRecalcEpoch;
    result.zones.reserve(snapshot.zones.size());

    for (const auto& zone : snapshot.zones) {
        ComputedZoneGeometry computed;
        computed.zoneId = zone.id;
        computed.absoluteGeometry =
            Zone::computeAbsoluteGeometry(zone.geometryMode, zone.relativeGeometry, zone.fixedGeometry,
                                          snapshot.screenGeometry, snapshot.fixedDivisor);
        result.zones.append(computed);
    }
    return result;
}

void LayoutWorker::computeGeometries(const LayoutSnapshot& snapshot, uint64_t generation)
{
    Q_EMIT geometriesReady(compute(snapshot, generation));
}

} // namespace PhosphorZones
