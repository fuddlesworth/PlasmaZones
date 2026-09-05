// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/PlacementMapParser.h>

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorZones/ZoneJsonKeys.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace PhosphorShell::PlacementMapParser {

namespace {

// getScreenStates keys. The daemon writes these inline in
// LayoutAdaptor::getScreenStates (src/dbus/layoutadaptor/assignment.cpp)
// and the XML docstring is the contract; there is no shared header for
// them, so they are pinned here beside the only reader in this library.
constexpr QLatin1String ScreenStatesScreenId("screenId");
constexpr QLatin1String ScreenStatesMode("mode");

// Zone::fromJson's ZoneGeometryMode: 0 relative, 1 fixed.
constexpr int GeometryModeFixed = 1;

QRectF readRect(const QJsonObject& obj)
{
    using namespace PhosphorZones::ZoneJsonKeys;
    return QRectF(obj[X].toDouble(), obj[Y].toDouble(), obj[Width].toDouble(), obj[Height].toDouble());
}

QRectF clampUnit(const QRectF& r)
{
    return r.intersected(QRectF(0.0, 0.0, 1.0, 1.0));
}

bool usable(const QRectF& r)
{
    return r.isValid() && r.width() > 0.0 && r.height() > 0.0;
}

Cell makeCell(const QString& id, const QRectF& rect)
{
    Cell cell;
    cell.id = id;
    cell.rect = rect;
    cell.t = hueFor(rect);
    return cell;
}

} // namespace

qreal hueFor(const QRectF& rect)
{
    return std::clamp(rect.center().x(), 0.0, 1.0);
}

QList<Cell> parseSnappingLayout(const QString& layoutJson, const QSize& workAreaSize)
{
    using namespace PhosphorZones::ZoneJsonKeys;
    QList<Cell> cells;
    const QJsonDocument doc = QJsonDocument::fromJson(layoutJson.toUtf8());
    if (!doc.isObject()) {
        return cells;
    }
    const QJsonArray zones = doc.object()[Zones].toArray();
    cells.reserve(zones.size());
    for (const QJsonValue& value : zones) {
        const QJsonObject zone = value.toObject();
        const QString id = zone[Id].toString();
        if (id.isEmpty()) {
            continue;
        }
        QRectF rect;
        if (zone[GeometryMode].toInt(0) == GeometryModeFixed && zone.contains(FixedGeometry)) {
            // Fixed zones are in work-area pixels. Without a work-area size
            // there is no scale, so the zone cannot be placed on the map.
            if (workAreaSize.isEmpty()) {
                continue;
            }
            const QRectF px = readRect(zone[FixedGeometry].toObject());
            rect = QRectF(px.x() / workAreaSize.width(), px.y() / workAreaSize.height(),
                          px.width() / workAreaSize.width(), px.height() / workAreaSize.height());
        } else {
            rect = readRect(zone[RelativeGeometry].toObject());
        }
        rect = clampUnit(rect);
        if (!usable(rect)) {
            continue;
        }
        Cell cell = makeCell(id, rect);
        cell.zoneNumber = zone[ZoneNumber].toInt(0);
        cell.label = zone[PhosphorZones::ZoneJsonKeys::Name].toString();
        cells.append(cell);
    }
    return cells;
}

QList<Cell> parseTileBatch(const QList<TileRect>& tiles, const QString& screenId, const QRect& workArea)
{
    QList<Cell> cells;
    if (workArea.width() <= 0 || workArea.height() <= 0) {
        return cells;
    }
    const qreal w = workArea.width();
    const qreal h = workArea.height();
    int monocleIndex = -1;
    for (const TileRect& tile : tiles) {
        if (tile.floating || tile.windowId.isEmpty() || tile.screenId != screenId) {
            continue;
        }
        if (tile.monocle && monocleIndex >= 0) {
            // Every monocle entry shares the full rect: one cell, counted.
            cells[monocleIndex].stack += 1;
            continue;
        }
        const QRectF rel((tile.rect.x() - workArea.x()) / w, (tile.rect.y() - workArea.y()) / h, tile.rect.width() / w,
                         tile.rect.height() / h);
        const QRectF rect = clampUnit(rel);
        if (!usable(rect)) {
            continue;
        }
        Cell cell = makeCell(tile.windowId, rect);
        cell.occupied = true;
        cells.append(cell);
        if (tile.monocle) {
            monocleIndex = cells.size() - 1;
        }
    }
    return cells;
}

StripParse parseVisibleStrip(const QString& stripJson)
{
    using namespace PhosphorZones::ZoneJsonKeys;
    StripParse parse;
    const QJsonDocument doc = QJsonDocument::fromJson(stripJson.toUtf8());
    if (!doc.isArray()) {
        return parse;
    }
    const QJsonArray tiles = doc.array();
    parse.cells.reserve(tiles.size());
    for (const QJsonValue& value : tiles) {
        const QJsonObject tile = value.toObject();
        const QRectF rect = clampUnit(readRect(tile));
        if (!usable(rect)) {
            continue;
        }
        const int zoneNumber = tile[ZoneNumber].toInt(0);
        // visibleStripJson carries no window id; the zone number is the only
        // stable handle the payload offers, so it keys the cell.
        Cell cell = makeCell(QStringLiteral("strip:%1").arg(zoneNumber), rect);
        cell.occupied = true;
        cell.stack = std::max(1, tile[PhosphorProtocol::Service::StripPreviewKey::TabCount].toInt(0));
        parse.cells.append(cell);
    }
    if (!parse.cells.isEmpty()) {
        // [NEW] Scrolling.stripModelJson would give stripExtentPx and the
        // view offset; until then the lens is the whole visible cut and the
        // hue axis is sampled inside it (A2 §8 fallback). Overflow counts
        // stay 0 for the same reason.
        parse.lens = QRectF(0.0, 0.0, 1.0, 1.0);
    }
    return parse;
}

void applyOccupancy(QList<Cell>& cells, const QHash<QString, QStringList>& occupancy, const QString& focusedWindowId)
{
    QSet<QString> occupied;
    QSet<QString> focused;
    for (auto it = occupancy.cbegin(); it != occupancy.cend(); ++it) {
        for (const QString& zoneId : it.value()) {
            if (zoneId.isEmpty()) {
                continue;
            }
            occupied.insert(zoneId);
            if (!focusedWindowId.isEmpty() && it.key() == focusedWindowId) {
                focused.insert(zoneId);
            }
        }
    }
    for (Cell& cell : cells) {
        cell.occupied = occupied.contains(cell.id);
        cell.focused = focused.contains(cell.id);
    }
}

void applyFocusByWindowId(QList<Cell>& cells, const QString& windowId)
{
    for (Cell& cell : cells) {
        cell.focused = !windowId.isEmpty() && cell.id == windowId;
    }
}

QVariantList toVariantList(const QList<Cell>& cells)
{
    QVariantList list;
    list.reserve(cells.size());
    for (const Cell& cell : cells) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), cell.id);
        map.insert(QStringLiteral("x"), cell.rect.x());
        map.insert(QStringLiteral("y"), cell.rect.y());
        map.insert(QStringLiteral("w"), cell.rect.width());
        map.insert(QStringLiteral("h"), cell.rect.height());
        map.insert(QStringLiteral("t"), cell.t);
        map.insert(QStringLiteral("occupied"), cell.occupied);
        map.insert(QStringLiteral("focused"), cell.focused);
        map.insert(QStringLiteral("label"), cell.label);
        map.insert(QStringLiteral("zoneNumber"), cell.zoneNumber);
        map.insert(QStringLiteral("stack"), cell.stack);
        list.append(map);
    }
    return list;
}

QVariantMap lensToVariant(const QRectF& lens)
{
    QVariantMap map;
    if (lens.isNull() || lens.width() <= 0.0) {
        return map;
    }
    map.insert(QStringLiteral("x"), lens.x());
    map.insert(QStringLiteral("w"), lens.width());
    return map;
}

int modeForScreen(const QString& statesJson, const QString& screenId)
{
    const QJsonDocument doc = QJsonDocument::fromJson(statesJson.toUtf8());
    if (!doc.isArray() || screenId.isEmpty()) {
        return -1;
    }
    const QJsonArray states = doc.array();
    for (const QJsonValue& value : states) {
        const QJsonObject state = value.toObject();
        if (state[ScreenStatesScreenId].toString() != screenId) {
            continue;
        }
        const int mode = state[ScreenStatesMode].toInt(-1);
        return (mode >= 0 && mode <= 2) ? mode : -1;
    }
    return -1;
}

} // namespace PhosphorShell::PlacementMapParser
