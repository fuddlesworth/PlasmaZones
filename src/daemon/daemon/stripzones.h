// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Shared builders for the synthetic "zones" a scrolling strip is rendered
// with. Two daemon producers push strip zones — the layout-OSD preview card
// (daemon/osd.cpp) and the navigation-OSD zone-number provider wired in
// daemon/init_engines.cpp — and both derive from the SAME visibleTiles walk.
// They used to hold their number space, their id namespacing and the
// empty-strip sketch in step by comment alone; here they share one
// definition of each.

#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScrollEngine/ScrollEngine.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLatin1String>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <functional>
#include <optional>

namespace PlasmaZones::StripZones {

using VisibleTile = PhosphorScrollEngine::ScrollEngine::VisibleTile;

/// Representative endless strip for a scrolling screen whose strip has no
/// visible tile: a full column with a clipped column at each edge, which
/// reads as a window onto a longer strip. Kept in step with
/// MonitorStatePage's scrollingFallbackZones, so the Monitors page and the
/// OSD card draw the same shape for the same empty strip.
inline QVector<QRectF> sketchRects()
{
    return {QRectF(0.0, 0.0, 0.1, 1.0), QRectF(0.115, 0.0, 0.5, 1.0), QRectF(0.63, 0.0, 0.37, 1.0)};
}

/// The zone maps the layout-OSD renderer consumes for the sketch.
///
/// No zoneNumber key: the sketch stands for "a strip, currently with nothing
/// in it", so its shapes are not tiles and nothing on screen is addressable.
/// A labelled 1/2/3 would offer digit targets that do not exist. Callers pass
/// zoneNumberDisplay "none" alongside, and omitting the key keeps the data
/// honest rather than relying on that display flag to hide it.
inline QVariantList sketchZoneMaps(const QString& screenId)
{
    const QVector<QRectF> rects = sketchRects();
    QVariantList zones;
    zones.reserve(rects.size());
    for (int i = 0; i < rects.size(); ++i) {
        const QRectF& r = rects.at(i);
        QVariantMap relGeo;
        relGeo[QLatin1String("x")] = r.x();
        relGeo[QLatin1String("y")] = r.y();
        relGeo[QLatin1String("width")] = r.width();
        relGeo[QLatin1String("height")] = r.height();
        QVariantMap zoneMap;
        zoneMap[QLatin1String("relativeGeometry")] = relGeo;
        zoneMap[QLatin1String("id")] = QStringLiteral("strip:%1:%2").arg(screenId).arg(i);
        zoneMap[QLatin1String("name")] = QString();
        zoneMap[QLatin1String("useCustomColors")] = false;
        zones.append(zoneMap);
    }
    return zones;
}

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

/// The overlay's tab-indicator model, parsed from ScrollEngine's
/// tabStripsChanged payload ({x, y, width, height, position, activeIndex,
/// tabs: [windowId, …]} per tabbed column that resolves an indicator,
/// documented on ScrollEngine.h).
///
/// This is the daemon's ONLY reader of that schema, so the wire format has one
/// producer (ScrollEngine::applyLayout) and one consumer rather than a copy in
/// the composition root that drifts from the emitter.
///
/// @p titleForWindow supplies each tab's display title for a canonical window
/// id; an empty answer falls back to the id's app id, so a window the registry
/// has not seen still labels its tab. @p urgentForWindow reports whether that
/// window is demanding attention; it may be empty, in which case no tab is
/// urgent, which is the correct reading of "the compositor never told us".
/// @p colorsForWindow supplies that window's per-window colour overrides (the
/// TabColor* window rules) as an activeColor / inactiveColor / urgentColor
/// map, absent keys meaning "no override"; it too may be empty.
///
/// Returns nullopt when the payload does not parse as an array — that is "we
/// know nothing about the strips", which is not the same as "there are none",
/// so the caller must leave the live indicators alone rather than clearing
/// them.
inline std::optional<QVariantList>
parseTabStripPayload(const QString& stripsJson, const std::function<QString(const QString&)>& titleForWindow,
                     const std::function<bool(const QString&)>& urgentForWindow,
                     const std::function<QVariantMap(const QString&)>& colorsForWindow, QJsonParseError* parseError)
{
    const QJsonDocument doc = QJsonDocument::fromJson(stripsJson.toUtf8(), parseError);
    if ((parseError && parseError->error != QJsonParseError::NoError) || !doc.isArray()) {
        return std::nullopt;
    }
    QVariantList strips;
    const QJsonArray arr = doc.array();
    strips.reserve(arr.size());
    for (const QJsonValue& stripValue : arr) {
        const QJsonObject stripObj = stripValue.toObject();
        QVariantMap strip;
        // qRound(toDouble()), not toInt(): QJsonValue::toInt() yields 0 for any
        // non-integral number, so a producer that ever writes a fractional
        // coordinate would silently park the whole strip at the screen origin.
        strip.insert(QLatin1String("x"), qRound(stripObj.value(QLatin1String("x")).toDouble()));
        strip.insert(QLatin1String("y"), qRound(stripObj.value(QLatin1String("y")).toDouble()));
        strip.insert(QLatin1String("width"), qRound(stripObj.value(QLatin1String("width")).toDouble()));
        strip.insert(QLatin1String("height"), qRound(stripObj.value(QLatin1String("height")).toDouble()));
        // The engine always writes the position; a missing key would mean a
        // producer/consumer version split, and Top is the least surprising
        // reading of a rect whose orientation we cannot confirm.
        strip.insert(QLatin1String("position"),
                     stripObj.value(QLatin1String("position"))
                         .toInt(static_cast<int>(PhosphorScrollEngine::TabIndicatorPosition::Top)));
        // -1, not 0, purely defensive: the producer always writes the key, but
        // a missing one must read as "no active tab" rather than lighting up
        // tab 0.
        const int activeIndex = stripObj.value(QLatin1String("activeIndex")).toInt(-1);
        QVariantList tabs;
        const QJsonArray tabIds = stripObj.value(QLatin1String("tabs")).toArray();
        tabs.reserve(tabIds.size());
        for (int i = 0; i < tabIds.size(); ++i) {
            const QString windowId = tabIds.at(i).toString();
            QString title = titleForWindow ? titleForWindow(windowId) : QString();
            if (title.isEmpty()) {
                title = PhosphorIdentity::WindowId::extractAppId(windowId);
            }
            QVariantMap tab;
            // The canonical id rides along so a click on the tab can name the
            // window it should focus. The overlay treats it as an opaque
            // token and hands it straight back.
            tab.insert(QLatin1String("windowId"), windowId);
            tab.insert(QLatin1String("title"), title);
            tab.insert(QLatin1String("active"), i == activeIndex);
            // Urgency rides per tab rather than per strip: any tab of a tabbed
            // column can be the one demanding attention, and the hidden ones
            // are exactly the tabs the user cannot otherwise see asking.
            tab.insert(QLatin1String("urgent"), urgentForWindow && urgentForWindow(windowId));
            // Per-window colour overrides, the top tier of niri's resolution
            // order. Inserted as a nested map rather than three flat keys so
            // the overlay can tell "this window overrode nothing" from "this
            // window overrode to empty" with one lookup.
            if (colorsForWindow) {
                const QVariantMap colors = colorsForWindow(windowId);
                if (!colors.isEmpty()) {
                    tab.insert(QLatin1String("colors"), colors);
                }
            }
            tabs.append(tab);
        }
        strip.insert(QLatin1String("tabs"), tabs);
        strips.append(strip);
    }
    return strips;
}

} // namespace PlasmaZones::StripZones
