// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/Column.h>
// kDefaultColumnWidthFraction / kMinSizeFraction / kMaxSizeFraction live in
// ScrollTypes.h (transitively included via Column.h's include of it).

#include <QJsonArray>

#include <cmath>

namespace PhosphorScrollEngine {

namespace {

QJsonObject columnWidthToJson(const ColumnWidth& width)
{
    QJsonObject obj;
    obj.insert(QLatin1String("kind"),
               width.kind == ColumnWidth::Kind::Fixed ? QStringLiteral("fixed") : QStringLiteral("proportion"));
    obj.insert(QLatin1String("value"), width.value);
    return obj;
}

ColumnWidth columnWidthFromJson(const QJsonObject& obj)
{
    ColumnWidth width;
    width.kind = obj.value(QLatin1String("kind")).toString() == QLatin1String("fixed") ? ColumnWidth::Kind::Fixed
                                                                                       : ColumnWidth::Kind::Proportion;
    width.value = obj.value(QLatin1String("value")).toDouble(kDefaultColumnWidthFraction);
    // Defence-in-depth at the persistence boundary: a corrupt or hand-edited
    // scroll-session.json with a 100.0 proportion would otherwise produce a
    // column 100x the screen width on restore. Reject NaN/Inf first (qBound
    // is unordered against NaN and would let the value through), then clamp:
    // proportions into [kMinSizeFraction, kMaxSizeFraction]; fixed-pixel
    // widths floored to a positive value (resolveColumnWidth itself enforces
    // a >=1 floor too, but rejecting negative pixel intent at the read site
    // keeps the data model honest).
    if (!std::isfinite(width.value)) {
        width.value = kDefaultColumnWidthFraction;
    }
    if (width.kind == ColumnWidth::Kind::Proportion) {
        width.value = qBound(kMinSizeFraction, width.value, kMaxSizeFraction);
    } else {
        width.value = qMax(width.value, qreal(1.0));
    }
    return width;
}

QJsonObject windowHeightToJson(const WindowHeight& height)
{
    QString kind = QStringLiteral("auto");
    if (height.kind == WindowHeight::Kind::Fixed) {
        kind = QStringLiteral("fixed");
    } else if (height.kind == WindowHeight::Kind::Preset) {
        kind = QStringLiteral("preset");
    }

    QJsonObject obj;
    obj.insert(QLatin1String("kind"), kind);
    // Only the field meaningful for the active kind is serialised; the others
    // would be persisted at their default values and bloat scroll-session.json.
    // fromJson defaults each field individually when missing, so a strip with
    // many tiles compacts notably.
    switch (height.kind) {
    case WindowHeight::Kind::Auto:
        obj.insert(QLatin1String("weight"), height.weight);
        break;
    case WindowHeight::Kind::Fixed:
        obj.insert(QLatin1String("fixedPx"), height.fixedPx);
        break;
    case WindowHeight::Kind::Preset:
        obj.insert(QLatin1String("presetIndex"), height.presetIndex);
        break;
    }
    return obj;
}

WindowHeight windowHeightFromJson(const QJsonObject& obj)
{
    WindowHeight height;
    const QString kind = obj.value(QLatin1String("kind")).toString();
    if (kind == QLatin1String("fixed")) {
        height.kind = WindowHeight::Kind::Fixed;
    } else if (kind == QLatin1String("preset")) {
        height.kind = WindowHeight::Kind::Preset;
    } else {
        height.kind = WindowHeight::Kind::Auto;
    }
    // Same defence-in-depth as columnWidthFromJson: reject NaN/Inf, clamp
    // persisted intent into a non-negative range so a corrupt scroll-
    // session.json can't drive the layout resolver into pathological
    // geometry.
    const qreal rawWeight = obj.value(QLatin1String("weight")).toDouble(1.0);
    height.weight = std::isfinite(rawWeight) ? qMax(rawWeight, qreal(0.0)) : qreal(1.0);
    const qreal rawFixed = obj.value(QLatin1String("fixedPx")).toDouble(0.0);
    height.fixedPx = std::isfinite(rawFixed) ? qMax(rawFixed, qreal(0.0)) : qreal(0.0);
    // presetIndex out-of-range falls back to Auto inside resolveConcreteTileHeight,
    // so we don't need to clamp it here — but rejecting negative values at
    // read keeps the in-memory invariant clean.
    height.presetIndex = qMax(obj.value(QLatin1String("presetIndex")).toInt(0), 0);
    return height;
}

} // namespace

bool Column::containsWindow(const QString& windowId) const
{
    return indexOfWindow(windowId) >= 0;
}

int Column::indexOfWindow(const QString& windowId) const
{
    for (int i = 0; i < m_tiles.size(); ++i) {
        if (m_tiles.at(i).windowId == windowId) {
            return i;
        }
    }
    return -1;
}

QStringList Column::windowIds() const
{
    QStringList ids;
    ids.reserve(static_cast<int>(m_tiles.size()));
    for (const Tile& tile : m_tiles) {
        ids.append(tile.windowId);
    }
    return ids;
}

void Column::appendTile(const Tile& tile)
{
    m_tiles.append(tile);
    if (m_activeTileIndex < 0) {
        m_activeTileIndex = 0;
    }
}

void Column::insertTile(int index, const Tile& tile)
{
    index = qBound(0, index, static_cast<int>(m_tiles.size()));
    m_tiles.insert(index, tile);
    if (m_activeTileIndex < 0) {
        m_activeTileIndex = 0;
    } else if (index <= m_activeTileIndex) {
        ++m_activeTileIndex;
    }
}

bool Column::removeWindow(const QString& windowId)
{
    const int index = indexOfWindow(windowId);
    if (index < 0) {
        return false;
    }
    m_tiles.removeAt(index);
    if (index < m_activeTileIndex) {
        --m_activeTileIndex;
    }
    clampActiveTileIndex();
    return true;
}

bool Column::moveTile(int from, int to)
{
    if (from < 0 || from >= m_tiles.size() || to < 0 || to >= m_tiles.size() || from == to) {
        return false;
    }
    const Tile tile = m_tiles.takeAt(from);
    m_tiles.insert(to, tile);
    // Recompute the active-tile index arithmetically rather than relinking by
    // windowId: a takeAt(from)+insert(to) shifts a contiguous span by exactly
    // one slot, and the active tile's new index is fully determined by the
    // (from, to, prevActive) triple — no linear scan needed. The cases:
    //
    //   * the moved tile WAS the active one (m_activeTileIndex == from):
    //     focus follows it to its new position `to`.
    //   * the active tile sat strictly between `from` and `to` going
    //     forward (from < m_activeTileIndex <= to): takeAt removed an
    //     earlier slot, every tile from from+1..to shifts left by one, so
    //     the active index decrements.
    //   * the active tile sat strictly between `to` and `from` going
    //     backward (to <= m_activeTileIndex < from): insert pushed every
    //     tile from to..from-1 right by one, so the active index
    //     increments.
    //   * otherwise (the active tile is on the far side of both `from` and
    //     `to`, or the column had no active tile): the index is unchanged.
    if (m_activeTileIndex == from) {
        m_activeTileIndex = to;
    } else if (m_activeTileIndex >= 0) {
        if (from < m_activeTileIndex && m_activeTileIndex <= to) {
            --m_activeTileIndex;
        } else if (to <= m_activeTileIndex && m_activeTileIndex < from) {
            ++m_activeTileIndex;
        }
    }
    return true;
}

bool Column::hasVisibleTiles() const
{
    for (const Tile& tile : m_tiles) {
        if (!tile.minimized) {
            return true;
        }
    }
    return false;
}

bool Column::isWindowMinimized(const QString& windowId) const
{
    const int index = indexOfWindow(windowId);
    return index >= 0 && m_tiles.at(index).minimized;
}

bool Column::setWindowMinimized(const QString& windowId, bool minimized)
{
    const int index = indexOfWindow(windowId);
    if (index < 0 || m_tiles.at(index).minimized == minimized) {
        return false;
    }
    m_tiles[index].minimized = minimized;
    return true;
}

void Column::setActiveTileIndex(int index)
{
    m_activeTileIndex = index;
    clampActiveTileIndex();
}

void Column::setActiveTileHeight(const WindowHeight& height)
{
    if (m_activeTileIndex >= 0 && m_activeTileIndex < m_tiles.size()) {
        m_tiles[m_activeTileIndex].height = height;
    }
}

void Column::toggleFullWidth()
{
    if (m_fullWidth) {
        // Leaving full-width: restore the width remembered on toggle-on.
        m_width = m_restoreWidth;
        m_presetWidthIndex = m_restorePresetWidthIndex;
        m_fullWidth = false;
    } else {
        // Entering full-width: remember the current width, then fill the
        // working area. m_width is set directly — setWidth() would clear the
        // flag this method is setting.
        m_restoreWidth = m_width;
        m_restorePresetWidthIndex = m_presetWidthIndex;
        m_width = ColumnWidth::proportion(1.0);
        m_presetWidthIndex = -1;
        m_fullWidth = true;
    }
}

const Tile* Column::activeTile() const
{
    if (m_activeTileIndex < 0 || m_activeTileIndex >= m_tiles.size()) {
        return nullptr;
    }
    return &m_tiles.at(m_activeTileIndex);
}

void Column::clampActiveTileIndex()
{
    if (m_tiles.isEmpty()) {
        m_activeTileIndex = -1;
    } else {
        m_activeTileIndex = qBound(0, m_activeTileIndex, static_cast<int>(m_tiles.size()) - 1);
    }
}

QJsonObject Column::toJson() const
{
    QJsonArray tiles;
    for (const Tile& tile : m_tiles) {
        QJsonObject tileObj;
        tileObj.insert(QLatin1String("windowId"), tile.windowId);
        tileObj.insert(QLatin1String("height"), windowHeightToJson(tile.height));
        tileObj.insert(QLatin1String("minimized"), tile.minimized);
        tiles.append(tileObj);
    }

    QJsonObject obj;
    obj.insert(QLatin1String("tiles"), tiles);
    obj.insert(QLatin1String("activeTileIndex"), m_activeTileIndex);
    obj.insert(QLatin1String("width"), columnWidthToJson(m_width));
    obj.insert(QLatin1String("presetWidthIndex"), m_presetWidthIndex);
    // Full-width state and the restore-width fields it stores are only
    // meaningful when m_fullWidth is true; serialise them conditionally so a
    // strip whose every column is at its native width writes a noticeably
    // smaller scroll-session.json. fromJson() defaults the missing fields to
    // a half-width Proportion, matching the constructor defaults.
    if (m_fullWidth) {
        obj.insert(QLatin1String("fullWidth"), m_fullWidth);
        obj.insert(QLatin1String("restoreWidth"), columnWidthToJson(m_restoreWidth));
        obj.insert(QLatin1String("restorePresetWidthIndex"), m_restorePresetWidthIndex);
    }
    return obj;
}

Column Column::fromJson(const QJsonObject& obj)
{
    Column column;
    const QJsonArray tiles = obj.value(QLatin1String("tiles")).toArray();
    for (const QJsonValue& value : tiles) {
        const QJsonObject tileObj = value.toObject();
        Tile tile;
        tile.windowId = tileObj.value(QLatin1String("windowId")).toString();
        tile.height = windowHeightFromJson(tileObj.value(QLatin1String("height")).toObject());
        tile.minimized = tileObj.value(QLatin1String("minimized")).toBool(false);
        if (!tile.windowId.isEmpty()) {
            column.m_tiles.append(tile);
        }
    }
    column.m_width = columnWidthFromJson(obj.value(QLatin1String("width")).toObject());
    column.m_presetWidthIndex = obj.value(QLatin1String("presetWidthIndex")).toInt(-1);
    column.m_fullWidth = obj.value(QLatin1String("fullWidth")).toBool(false);
    column.m_restoreWidth = columnWidthFromJson(obj.value(QLatin1String("restoreWidth")).toObject());
    column.m_restorePresetWidthIndex = obj.value(QLatin1String("restorePresetWidthIndex")).toInt(-1);
    column.m_activeTileIndex = obj.value(QLatin1String("activeTileIndex")).toInt(-1);
    column.clampActiveTileIndex();
    return column;
}

} // namespace PhosphorScrollEngine
