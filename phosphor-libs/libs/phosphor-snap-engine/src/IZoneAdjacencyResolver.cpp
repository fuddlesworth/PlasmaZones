// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSnapEngine/IZoneAdjacencyResolver.h>

namespace PhosphorSnapEngine {

IZoneAdjacencyResolver::~IZoneAdjacencyResolver() = default;

QString IZoneAdjacencyResolver::getAdjacentZoneOutside(const QStringList& zoneIds, const QString& direction,
                                                       const QString& screenId) const
{
    return zoneIds.isEmpty() ? QString() : getAdjacentZone(zoneIds.first(), direction, screenId);
}

} // namespace PhosphorSnapEngine
