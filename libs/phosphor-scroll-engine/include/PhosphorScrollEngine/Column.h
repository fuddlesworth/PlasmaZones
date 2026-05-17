// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorScrollEngine/ScrollTypes.h>
#include <phosphorscrollengine_export.h>

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace PhosphorScrollEngine {

/// One window inside a column. Pure data — geometry is resolved elsewhere.
struct Tile
{
    QString windowId;
    WindowHeight height;
};

/// A vertical stack of one or more tiles — one cell of the horizontal strip
/// owned by ScrollScreenState. A column carries width *intent* (ColumnWidth);
/// its tiles carry height intent. A column may be transiently empty during a
/// mutation; ScrollScreenState drops empty columns so the strip never
/// exposes one.
class PHOSPHORSCROLLENGINE_EXPORT Column
{
public:
    Column() = default;

    // ── Tiles ───────────────────────────────────────────────────────────
    const QVector<Tile>& tiles() const
    {
        return m_tiles;
    }
    int tileCount() const
    {
        return static_cast<int>(m_tiles.size());
    }
    bool isEmpty() const
    {
        return m_tiles.isEmpty();
    }
    bool containsWindow(const QString& windowId) const;
    int indexOfWindow(const QString& windowId) const;
    QStringList windowIds() const;

    void appendTile(const Tile& tile);
    void insertTile(int index, const Tile& tile);
    /// Remove the tile for @p windowId. Returns true if a tile was removed.
    bool removeWindow(const QString& windowId);
    /// Reorder a tile. Returns true if @p from and @p to are valid and differ;
    /// tile focus follows the moved tile.
    bool moveTile(int from, int to);

    // ── Active tile ─────────────────────────────────────────────────────
    /// Index of the focused tile, or -1 when the column is empty.
    int activeTileIndex() const
    {
        return m_activeTileIndex;
    }
    void setActiveTileIndex(int index);
    const Tile* activeTile() const;

    // ── Width intent ────────────────────────────────────────────────────
    ColumnWidth width() const
    {
        return m_width;
    }
    void setWidth(ColumnWidth width)
    {
        m_width = width;
    }
    /// Index into the configured preset-width list, or -1 when the width is
    /// not currently one of the presets.
    int presetWidthIndex() const
    {
        return m_presetWidthIndex;
    }
    void setPresetWidthIndex(int index)
    {
        m_presetWidthIndex = index;
    }

    // ── Serialization ───────────────────────────────────────────────────
    QJsonObject toJson() const;
    static Column fromJson(const QJsonObject& obj);

private:
    void clampActiveTileIndex();

    QVector<Tile> m_tiles;
    int m_activeTileIndex = -1;
    ColumnWidth m_width = ColumnWidth::proportion(0.5);
    int m_presetWidthIndex = -1;
};

} // namespace PhosphorScrollEngine
