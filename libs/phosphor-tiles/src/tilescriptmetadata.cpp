// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTiles/TileScriptMetadata.h>

#include "tileslogging.h"

namespace PhosphorTiles {
namespace ScriptedHelpers {

QVector<QRect> clampZonesToArea(const QVector<QRect>& zones, const QRect& area, const QString& scriptId)
{
    QVector<QRect> clamped;
    clamped.reserve(zones.size());
    for (int i = 0; i < zones.size(); ++i) {
        const QRect& zone = zones[i];
        const QRect bounded = zone.intersected(area);
        if (bounded.isEmpty()) {
            qCWarning(PhosphorTiles::lcTilesLib)
                << "LuauTileAlgorithm: zone" << i << "outside area, using full area as fallback"
                << "zone=" << zone << "area=" << area << "script=" << scriptId;
            clamped.append(area);
            continue;
        }
        clamped.append(bounded);
    }
    return clamped;
}

} // namespace ScriptedHelpers
} // namespace PhosphorTiles
