// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutworker.h"

#include <PhosphorZones/Zone.h>

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

    for (const auto& zone : snapshot.zones) {
        ComputedZoneGeometry computed;
        computed.zoneId = zone.id;
        // Shared pure helper: same math PhosphorZones::Zone::calculateAbsoluteGeometry uses
        // on the main thread. Keeps the two paths byte-identical.
        computed.absoluteGeometry = PhosphorZones::Zone::computeAbsoluteGeometry(
            zone.geometryMode, zone.relativeGeometry, zone.fixedGeometry, snapshot.screenGeometry);
        result.zones.append(computed);
    }

    Q_EMIT geometriesReady(result);
}

} // namespace PlasmaZones
