// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/LayoutWorker.h>
#include <PhosphorZones/Zone.h>

namespace PhosphorZones {

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

    for (const auto& zone : snapshot.zones) {
        ComputedZoneGeometry computed;
        computed.zoneId = zone.id;
        computed.absoluteGeometry = Zone::computeAbsoluteGeometry(zone.geometryMode, zone.relativeGeometry,
                                                                  zone.fixedGeometry, snapshot.screenGeometry);
        result.zones.append(computed);
    }

    Q_EMIT geometriesReady(result);
}

} // namespace PhosphorZones
