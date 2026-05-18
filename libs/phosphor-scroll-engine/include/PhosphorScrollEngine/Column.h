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
    /// A minimized tile keeps its slot in the column/strip order but is
    /// excluded from the visible layout — Karousel's `TiledMinimized` state.
    /// Cleared on unminimize, restoring the window in place.
    bool minimized = false;
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

    // ── Minimize state ──────────────────────────────────────────────────
    /// Whether the column has at least one non-minimized tile. A column with
    /// none collapses out of the visible strip while keeping its slot order.
    bool hasVisibleTiles() const;
    /// Whether @p windowId's tile is minimized. False if the window is not
    /// in this column.
    bool isWindowMinimized(const QString& windowId) const;
    /// Set the minimized flag of @p windowId's tile. Returns true only if the
    /// flag actually changed.
    bool setWindowMinimized(const QString& windowId, bool minimized);

    // ── Active tile ─────────────────────────────────────────────────────
    /// Index of the focused tile, or -1 when the column is empty.
    int activeTileIndex() const
    {
        return m_activeTileIndex;
    }
    void setActiveTileIndex(int index);
    /// Set the height intent of the focused tile (no-op when the column is empty).
    void setActiveTileHeight(const WindowHeight& height);
    const Tile* activeTile() const;

    // ── Width intent ────────────────────────────────────────────────────
    ColumnWidth width() const
    {
        return m_width;
    }
    void setWidth(ColumnWidth width)
    {
        m_width = width;
        m_fullWidth = false; // an explicit width change leaves full-width mode
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
    /// Whether the column is in toggle-induced full-width mode (its width
    /// intent fills the whole working area). Cleared by any explicit width
    /// change through setWidth().
    bool isFullWidth() const
    {
        return m_fullWidth;
    }
    /// Toggle full-width: when entering, the current width is remembered and
    /// the width intent becomes the whole working area; when leaving, the
    /// remembered width is restored. Works for any width kind — a Fixed-pixel
    /// width is remembered and restored verbatim (unlike adjustColumnWidth,
    /// which no-ops on Fixed widths since it cannot do fraction arithmetic).
    void toggleFullWidth();

    // ── Serialization ───────────────────────────────────────────────────
    QJsonObject toJson() const;
    static Column fromJson(const QJsonObject& obj);

private:
    void clampActiveTileIndex();

    QVector<Tile> m_tiles;
    int m_activeTileIndex = -1;
    ColumnWidth m_width = ColumnWidth::proportion(0.5);
    int m_presetWidthIndex = -1;
    /// Full-width toggle state: when m_fullWidth is set, m_restoreWidth /
    /// m_restorePresetWidthIndex hold the width to return to on toggle-off.
    bool m_fullWidth = false;
    ColumnWidth m_restoreWidth = ColumnWidth::proportion(0.5);
    int m_restorePresetWidthIndex = -1;
};

} // namespace PhosphorScrollEngine
