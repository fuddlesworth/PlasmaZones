// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/Column.h>

#include <QJsonArray>

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
    width.value = obj.value(QLatin1String("value")).toDouble(0.5);
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
    obj.insert(QLatin1String("weight"), height.weight);
    obj.insert(QLatin1String("fixedPx"), height.fixedPx);
    obj.insert(QLatin1String("presetIndex"), height.presetIndex);
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
    height.weight = obj.value(QLatin1String("weight")).toDouble(1.0);
    height.fixedPx = obj.value(QLatin1String("fixedPx")).toDouble(0.0);
    height.presetIndex = obj.value(QLatin1String("presetIndex")).toInt(0);
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
    const QString activeId = (m_activeTileIndex >= 0) ? m_tiles.at(m_activeTileIndex).windowId : QString();
    const Tile tile = m_tiles.takeAt(from);
    m_tiles.insert(to, tile);
    if (!activeId.isEmpty()) {
        m_activeTileIndex = indexOfWindow(activeId);
    }
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
        tiles.append(tileObj);
    }

    QJsonObject obj;
    obj.insert(QLatin1String("tiles"), tiles);
    obj.insert(QLatin1String("activeTileIndex"), m_activeTileIndex);
    obj.insert(QLatin1String("width"), columnWidthToJson(m_width));
    obj.insert(QLatin1String("presetWidthIndex"), m_presetWidthIndex);
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
        if (!tile.windowId.isEmpty()) {
            column.m_tiles.append(tile);
        }
    }
    column.m_width = columnWidthFromJson(obj.value(QLatin1String("width")).toObject());
    column.m_presetWidthIndex = obj.value(QLatin1String("presetWidthIndex")).toInt(-1);
    column.m_activeTileIndex = obj.value(QLatin1String("activeTileIndex")).toInt(-1);
    column.clampActiveTileIndex();
    return column;
}

} // namespace PhosphorScrollEngine
