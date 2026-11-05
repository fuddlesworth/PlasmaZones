// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Shared builders for the synthetic "zones" a scrolling strip is rendered
// with. Two daemon producers push strip zones — the layout-OSD preview card
// (daemon/osd.cpp) and the navigation-OSD zone-number provider wired in
// daemon/init_engines.cpp — and both derive from the SAME visibleTiles walk.
// They used to hold their number space and their id namespacing in step by
// comment alone; here they share one definition of each.
//
// There is deliberately NO empty-strip builder here any more. A strip with no
// visible tile used to be drawn as a three-column sketch with its outer two
// clipped at the screen edges, mirrored by a hand-kept twin in the settings
// app. Drawn by the zone renderer in the zone fills, it read as three real
// windows on a strip that held none. The empty case is now the shared
// StripEmptyState component (an axis arrow and a caption naming the actual
// cause), so a caller with no tiles renders that instead of asking for zones.

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorScrollEngine/ScrollEngine.h>

#include <QLatin1String>
#include <QRect>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <algorithm>

namespace PlasmaZones::StripZones {

using VisibleTile = PhosphorScrollEngine::ScrollEngine::VisibleTile;

/// The zone maps the layout-OSD renderer consumes for a LIVE strip.
///
/// @p screenGeometry is the target screen's FULL geometry, and it is what the
/// rects are renormalized against. The engine clips its tiles to the WORK
/// area, so a work-area basis fed into the OSD's screen-shaped preview box
/// would stretch the strip by the panel's share of the screen (5% or more
/// vertically with a standard panel). Renormalizing against the full screen
/// puts the tiles where they actually sit on the output, panel gap included —
/// the same basis ScrollEngine::visibleTileRectsRelative uses for the D-Bus
/// strip payload, so the settings thumbnail draws the same shape.
///
/// Zone numbers come from the tile, never from the loop index: the engine
/// stamps VisibleTile::zoneNumber on the same walk the Snap-to-Zone digits
/// resolve against, so the card labels exactly what the digits target.
///
/// Ids are namespaced, never a bare index, matching the settings app's twin
/// (settingscontroller_session.cpp): these are render-only synthetic zones
/// with no persisted identity, and a bare "0"/"1"/"2" is indistinguishable
/// from a real zone id to any consumer that starts keying on zone.id.
/// CLAUDE.md: zone IDs everywhere, never indices.
inline QVariantList zoneMapsForTiles(const QString& screenId, const QVector<VisibleTile>& tiles,
                                     const QRect& screenGeometry)
{
    QVariantList zones;
    if (screenGeometry.width() <= 0 || screenGeometry.height() <= 0) {
        return zones;
    }
    const qreal originX = screenGeometry.x();
    const qreal originY = screenGeometry.y();
    const qreal spanX = screenGeometry.width();
    const qreal spanY = screenGeometry.height();
    zones.reserve(tiles.size());
    for (const VisibleTile& tile : tiles) {
        const QRect& r = tile.rect;
        QVariantMap relGeo;
        relGeo[QLatin1String("x")] = (r.x() - originX) / spanX;
        relGeo[QLatin1String("y")] = (r.y() - originY) / spanY;
        relGeo[QLatin1String("width")] = r.width() / spanX;
        relGeo[QLatin1String("height")] = r.height() / spanY;
        QVariantMap zoneMap;
        zoneMap[QLatin1String("zoneNumber")] = tile.zoneNumber;
        zoneMap[QLatin1String("relativeGeometry")] = relGeo;
        // 1-based like the settings app's twin (settingscontroller_session.cpp
        // counts emitted tiles from 1), so the two synthetic id spaces really
        // do match entry-for-entry. Taken off the TILE rather than re-derived
        // as i + 1: the engine stamps zoneNumber densely over the same walk,
        // so the two are equal today, and reading the field is what keeps them
        // equal if that walk ever stops being dense — which is the hazard the
        // note above this function names.
        zoneMap[QLatin1String("id")] = QStringLiteral("strip:%1:%2").arg(screenId).arg(tile.zoneNumber);
        zoneMap[QLatin1String("name")] = QString();
        zoneMap[QLatin1String("useCustomColors")] = false;
        // The column's tab indicator, so the card draws a tabbed column as
        // tabbed. Written only for a tile whose column resolves one, matching
        // the wire twin: on this side the renderer reads the same absent-or-
        // zero gate, so the two paths reach ZonePreview identically shaped.
        if (tile.tabCount > 0) {
            zoneMap[PhosphorProtocol::Service::StripPreviewKey::TabCount] = tile.tabCount;
            // Clamped into the pill row like the wire twin's read. The engine
            // types activeTabIndex as -1 for a column with no indicator, which
            // a tabCount above zero already rules out — so this cannot bite
            // today, and it is here so the two producers stay the same shape
            // rather than one of them relying on a caller's invariant.
            zoneMap[PhosphorProtocol::Service::StripPreviewKey::ActiveTab] =
                std::clamp(tile.activeTabIndex, 0, tile.tabCount - 1);
            zoneMap[PhosphorProtocol::Service::StripPreviewKey::TabPosition] =
                static_cast<int>(tile.tabIndicatorPosition);
            zoneMap[PhosphorProtocol::Service::StripPreviewKey::TabLength] = tile.tabLengthProportion;
        }
        zones.append(zoneMap);
    }
    return zones;
}

/// The navigation OSD's zone-number lookup list: each visible tile's window
/// id paired with its zone number. A window with no visible tile gets no
/// entry, so the OSD falls back to direction-only copy for off-screen
/// columns, the hidden tabs of a tabbed column, parked columns, and any tile
/// whose work-area intersection comes out empty.
///
/// The ids are the engine's CANONICAL window ids, so any lookup against this
/// list must pass a canonical id too — a raw effect-side id silently misses.
inline QVariantList numberMapsForTiles(const QVector<VisibleTile>& tiles)
{
    QVariantList zones;
    zones.reserve(tiles.size());
    for (const VisibleTile& tile : tiles) {
        QVariantMap zone;
        zone[QLatin1String("id")] = tile.windowId;
        zone[QLatin1String("zoneNumber")] = tile.zoneNumber;
        zones.append(zone);
    }
    return zones;
}

} // namespace PlasmaZones::StripZones
