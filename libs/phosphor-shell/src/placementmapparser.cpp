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
constexpr QLatin1String ScreenStatesDesktop("virtualDesktop");
constexpr QLatin1String ScreenStatesActivity("activity");
constexpr QLatin1String ScreenStatesLayoutId("layoutId");
constexpr QLatin1String ScreenStatesAlgorithmId("algorithmId");
constexpr QLatin1String ScreenStatesTemplateId("scrollingTemplateId");

// WindowDrag.registerDropProxy payload keys (the phase-3 daemon contract):
// the miniature's rect and one entry per zone cell, both in screen-local
// pixels.
namespace DropProxyKey {
constexpr QLatin1String Rect("rect");
constexpr QLatin1String Cells("cells");
constexpr QLatin1String Id("id");
} // namespace DropProxyKey

// Zone::fromJson's ZoneGeometryMode: 0 relative, 1 fixed.
constexpr int GeometryModeFixed = 1;

// Scrolling.stripModelJson keys: a straight serialisation of the scroll
// engine's strip (A2 §1.3). Pinned beside the only reader, like the
// screen-state keys above.
namespace StripModelKey {
constexpr QLatin1String Axis("axis");
constexpr QLatin1String ViewOffsetPx("viewOffsetPx");
constexpr QLatin1String ViewportPx("viewportPx");
constexpr QLatin1String StripExtentPx("stripExtentPx");
constexpr QLatin1String ActiveColumn("activeColumn");
constexpr QLatin1String Columns("columns");
constexpr QLatin1String Index("index");
constexpr QLatin1String StripPosPx("stripPosPx");
constexpr QLatin1String ExtentPx("extentPx");
constexpr QLatin1String Tiles("tiles");
constexpr QLatin1String WindowId("windowId");
constexpr QLatin1String Minimized("minimized");
} // namespace StripModelKey

// Tiling.currentTilesJson keys: one entry per tile in screen pixels, the
// wire shape of a windowsTileRequested batch.
namespace CurrentTilesKey {
constexpr QLatin1String WindowId("windowId");
constexpr QLatin1String ScreenId("screenId");
constexpr QLatin1String X("x");
constexpr QLatin1String Y("y");
constexpr QLatin1String Width("width");
constexpr QLatin1String Height("height");
constexpr QLatin1String Monocle("monocle");
constexpr QLatin1String Floating("floating");
} // namespace CurrentTilesKey

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
        cell.windowId = tile.windowId;
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
    int index = -1;
    for (const QJsonValue& value : tiles) {
        ++index;
        const QJsonObject tile = value.toObject();
        const QRectF rect = clampUnit(readRect(tile));
        if (!usable(rect)) {
            continue;
        }
        // visibleStripJson carries no window id; the zone number is the only
        // stable handle the payload offers, so it keys the cell. It defaults
        // to 0 when absent, though, and two such tiles would both key
        // "strip:0" — cellById returns the first, so a click on the second
        // acts on the first, and the change gate sees a list that looks
        // legitimate. Fall back to the position in the payload, which is
        // unique by construction.
        const int zoneNumber = tile[ZoneNumber].toInt(0);
        const QString cellId =
            zoneNumber > 0 ? QStringLiteral("strip:%1").arg(zoneNumber) : QStringLiteral("strip:@%1").arg(index);
        Cell cell = makeCell(cellId, rect);
        cell.occupied = true;
        cell.stack = std::max(1, tile[PhosphorProtocol::Service::StripPreviewKey::TabCount].toInt(0));
        parse.cells.append(cell);
    }
    if (!parse.cells.isEmpty()) {
        // Older daemon without stripModelJson: no strip extent or view
        // offset, so the lens is the whole visible cut and the hue axis is
        // sampled inside it (A2 §8 fallback). Overflow counts stay 0 for
        // the same reason.
        parse.lens = QRectF(0.0, 0.0, 1.0, 1.0);
    }
    return parse;
}

StripParse parseStripModel(const QString& modelJson)
{
    using namespace StripModelKey;
    StripParse parse;
    const QJsonDocument doc = QJsonDocument::fromJson(modelJson.toUtf8());
    if (!doc.isObject()) {
        return parse;
    }
    const QJsonObject model = doc.object();
    const int extent = model[StripExtentPx].toInt(0);
    const int viewport = model[ViewportPx].toInt(0);
    const int viewOffset = model[ViewOffsetPx].toInt(0);
    const int activeColumn = model[ActiveColumn].toInt(-1);
    const bool vertical = model[Axis].toInt(0) == 1;
    if (extent <= 0 || viewport <= 0) {
        return parse;
    }
    parse.viewOffsetPx = viewOffset;
    parse.viewportPx = viewport;
    parse.stripExtentPx = extent;

    const QJsonArray columns = model[Columns].toArray();
    parse.cells.reserve(columns.size());
    for (const QJsonValue& value : columns) {
        const QJsonObject column = value.toObject();
        const QJsonArray tiles = column[Tiles].toArray();
        if (tiles.isEmpty()) {
            continue;
        }
        const QString id = tiles.first().toObject()[WindowId].toString();
        if (id.isEmpty()) {
            continue;
        }
        const int pos = column[StripPosPx].toInt(0);
        const int len = column[ExtentPx].toInt(0);
        if (len <= 0) {
            continue;
        }
        // Strip axis relative to the viewport window: 0..1 is what is on
        // screen, so a column entirely before or after it is off-lens.
        const qreal start = qreal(pos - viewOffset) / viewport;
        const qreal length = qreal(len) / viewport;
        if (start + length <= 0.0) {
            ++parse.overflowLeft;
            continue;
        }
        if (start >= 1.0) {
            ++parse.overflowRight;
            continue;
        }
        const QRectF rect = clampUnit(vertical ? QRectF(0.0, start, 1.0, length) : QRectF(start, 0.0, length, 1.0));
        if (!usable(rect)) {
            continue;
        }
        Cell cell = makeCell(id, rect);
        cell.windowId = id;
        cell.stripT = std::clamp(qreal(pos) / extent, 0.0, 1.0);
        cell.t = cell.stripT;
        cell.columnIndex = column[Index].toInt(-1);
        cell.occupied = true;
        cell.focused = activeColumn >= 0 && cell.columnIndex == activeColumn;
        // The model lists minimized tiles too; the map draws only what is
        // shown, matching visibleStripJson.
        int shown = 0;
        for (const QJsonValue& tile : tiles) {
            if (!tile.toObject()[Minimized].toBool(false)) {
                ++shown;
            }
        }
        cell.stack = std::max(1, shown);
        parse.cells.append(cell);
    }
    // The lens is the viewport's window onto the strip's structure axis.
    if (extent <= viewport) {
        parse.lens = QRectF(0.0, 0.0, 1.0, 1.0);
    } else {
        const qreal x = std::clamp(qreal(viewOffset) / extent, 0.0, 1.0);
        const qreal w = std::clamp(qreal(viewport) / extent, 0.0, 1.0 - x);
        parse.lens = QRectF(x, 0.0, w, 1.0);
    }
    return parse;
}

QList<TileRect> tileRectsFromJson(const QString& tilesJson)
{
    using namespace CurrentTilesKey;
    QList<TileRect> tiles;
    const QJsonDocument doc = QJsonDocument::fromJson(tilesJson.toUtf8());
    if (!doc.isArray()) {
        return tiles;
    }
    const QJsonArray entries = doc.array();
    tiles.reserve(entries.size());
    for (const QJsonValue& value : entries) {
        const QJsonObject entry = value.toObject();
        TileRect tile;
        tile.windowId = entry[WindowId].toString();
        if (tile.windowId.isEmpty()) {
            continue;
        }
        tile.screenId = entry[ScreenId].toString();
        tile.rect = QRect(entry[X].toInt(0), entry[Y].toInt(0), entry[Width].toInt(0), entry[Height].toInt(0));
        tile.floating = entry[Floating].toBool(false);
        tile.monocle = entry[Monocle].toBool(false);
        tiles.append(tile);
    }
    return tiles;
}

QList<Cell> stackedColumns(const QStringList& windowIds)
{
    QStringList ids;
    for (const QString& id : windowIds) {
        if (!id.isEmpty()) {
            ids.append(id);
        }
    }
    QList<Cell> cells;
    cells.reserve(ids.size());
    const qreal w = ids.isEmpty() ? 0.0 : 1.0 / ids.size();
    for (int i = 0; i < ids.size(); ++i) {
        Cell c;
        c.id = ids[i];
        c.windowId = ids[i];
        c.rect = QRectF(i * w, 0.0, w, 1.0);
        c.t = hueFor(c.rect);
        c.occupied = true;
        cells.append(c);
    }
    return cells;
}

QList<Cell> parseCurrentTiles(const QString& tilesJson, const QString& screenId, const QRect& workArea)
{
    return parseTileBatch(tileRectsFromJson(tilesJson), screenId, workArea);
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

void applyOccupancy(QList<Cell>& cells, const QList<Occupant>& occupants, const QString& focusedWindowId)
{
    // Topmost occupant per zone: the last one listed, unless the focused
    // window is among them (focus raises).
    QHash<QString, QString> topmost;
    QSet<QString> focused;
    QSet<QString> urgent;
    for (const Occupant& o : occupants) {
        if (o.windowId.isEmpty()) {
            continue;
        }
        for (const QString& zoneId : o.zoneIds) {
            if (zoneId.isEmpty()) {
                continue;
            }
            const bool isFocused = !focusedWindowId.isEmpty() && o.windowId == focusedWindowId;
            if (isFocused || !focused.contains(zoneId)) {
                topmost.insert(zoneId, o.windowId);
            }
            if (isFocused) {
                focused.insert(zoneId);
            }
            if (o.urgent) {
                urgent.insert(zoneId);
            }
        }
    }
    for (Cell& cell : cells) {
        cell.windowId = topmost.value(cell.id);
        cell.occupied = !cell.windowId.isEmpty();
        cell.focused = focused.contains(cell.id);
        cell.urgent = urgent.contains(cell.id);
    }
}

void applyMetadata(QList<Cell>& cells, const QHash<QString, WindowMeta>& meta)
{
    for (Cell& cell : cells) {
        const auto it = meta.constFind(cell.windowId);
        if (cell.windowId.isEmpty() || it == meta.cend()) {
            cell.appId.clear();
            cell.title.clear();
            continue;
        }
        cell.appId = it->appId;
        cell.title = it->title;
    }
}

void applyUrgency(QList<Cell>& cells, const QSet<QString>& urgent)
{
    for (Cell& cell : cells) {
        cell.urgent = !cell.windowId.isEmpty() && urgent.contains(cell.windowId);
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
        map.insert(QStringLiteral("stripT"), cell.stripT);
        map.insert(QStringLiteral("columnIndex"), cell.columnIndex);
        map.insert(QStringLiteral("windowId"), cell.windowId);
        map.insert(QStringLiteral("appId"), cell.appId);
        map.insert(QStringLiteral("title"), cell.title);
        map.insert(QStringLiteral("urgent"), cell.urgent);
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
    return screenStateFor(statesJson, screenId).mode;
}

ScreenState screenStateFor(const QString& statesJson, const QString& screenId)
{
    ScreenState out;
    const QJsonDocument doc = QJsonDocument::fromJson(statesJson.toUtf8());
    if (!doc.isArray() || screenId.isEmpty()) {
        return out;
    }
    const QJsonArray states = doc.array();
    for (const QJsonValue& value : states) {
        const QJsonObject state = value.toObject();
        if (state[ScreenStatesScreenId].toString() != screenId) {
            continue;
        }
        const int mode = state[ScreenStatesMode].toInt(-1);
        out.mode = (mode >= 0 && mode <= 2) ? mode : -1;
        out.virtualDesktop = std::max(0, state[ScreenStatesDesktop].toInt(0));
        out.activity = state[ScreenStatesActivity].toString();
        out.layoutId = state[ScreenStatesLayoutId].toString();
        out.algorithmId = state[ScreenStatesAlgorithmId].toString();
        out.scrollingTemplateId = state[ScreenStatesTemplateId].toString();
        return out;
    }
    return out;
}

QString dropProxyJson(const QRect& rect, const QVariantList& cells)
{
    const auto rectArray = [](const QRect& r) {
        return QJsonArray{r.x(), r.y(), r.width(), r.height()};
    };
    QJsonArray entries;
    for (const QVariant& value : cells) {
        const QVariantMap cell = value.toMap();
        const QString id = cell.value(DropProxyKey::Id).toString();
        const QRect r(cell.value(QStringLiteral("x")).toInt(), cell.value(QStringLiteral("y")).toInt(),
                      cell.value(QStringLiteral("w")).toInt(), cell.value(QStringLiteral("h")).toInt());
        if (id.isEmpty() || r.isEmpty()) {
            continue;
        }
        QJsonObject entry;
        entry[DropProxyKey::Id] = id;
        entry[DropProxyKey::Rect] = rectArray(r);
        entries.append(entry);
    }
    QJsonObject root;
    root[DropProxyKey::Rect] = rectArray(rect);
    root[DropProxyKey::Cells] = entries;
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

} // namespace PhosphorShell::PlacementMapParser
