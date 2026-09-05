// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QHash>
#include <QList>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace PhosphorShell::PlacementMapParser {

/**
 * @brief One cell of the placement map, in work-area fractions.
 *
 * `rect` is 0..1 of the work area on both axes. `t` is the hue axis
 * (A2 §1.2): the cell's horizontal centre in work-area space for snapping
 * and tiling, and the visible-cut fallback for scrolling until the daemon
 * exposes the strip extent.
 */
struct PHOSPHORSHELL_EXPORT Cell
{
    QString id;
    QRectF rect;
    qreal t = 0.0;
    bool occupied = false;
    bool focused = false;
    QString label;
    /// Snapping only: the zone's 1-based number, used by the click path
    /// (`Snap.snapToZoneByNumber`). 0 for tiling and scrolling cells.
    int zoneNumber = 0;
    /// Tiling monocle and scrolling columns: how many windows share this
    /// rect (a tabbed or stacked column's tile count). 1 otherwise.
    int stack = 1;
    /// Scrolling only: the column's start as a fraction of the whole strip
    /// (`stripPosPx / stripExtentPx`), the structure axis of A2 §1.2. It
    /// is the hue for a strip cell, kept apart from `t` so a reader can
    /// tell a real strip position from the visible-cut fallback. 0 for
    /// snapping and tiling cells.
    qreal stripT = 0.0;
    /// Scrolling only: the column's index in the strip, the argument to
    /// `Scrolling.focusColumnAt`. -1 for every other cell.
    int columnIndex = -1;
};

/// A tile from a `Tiling.windowsTileRequested` batch, reduced to what the
/// map needs so the parser stays free of the D-Bus wire types.
struct PHOSPHORSHELL_EXPORT TileRect
{
    QString windowId;
    QString screenId;
    QRect rect;
    bool floating = false;
    bool monocle = false;
};

/// The scrolling parse: the visible columns as cells plus the lens band.
/// The three pixel fields come from `stripModelJson` only; the
/// `visibleStripJson` fallback leaves them 0.
struct PHOSPHORSHELL_EXPORT StripParse
{
    QList<Cell> cells;
    QRectF lens;
    int overflowLeft = 0;
    int overflowRight = 0;
    int viewOffsetPx = 0;
    int viewportPx = 0;
    int stripExtentPx = 0;
};

/// Hue for a cell rect: its horizontal centre, clamped to 0..1.
PHOSPHORSHELL_EXPORT qreal hueFor(const QRectF& rect);

/**
 * @brief Parse a `LayoutRegistry.getLayout` JSON document into cells.
 *
 * Relative zones (`relativeGeometry`) map straight through. Fixed-mode
 * zones (`geometryMode` 1 with `fixedGeometry` in px) are scaled by the
 * work-area size; when that size is unknown (empty) they are dropped,
 * since there is nothing to scale against. Zones with a degenerate rect
 * are dropped as well.
 */
PHOSPHORSHELL_EXPORT QList<Cell> parseSnappingLayout(const QString& layoutJson, const QSize& workAreaSize);

/**
 * @brief Normalise a tile batch for one screen against the work area.
 *
 * Entries naming another screen and `floating` entries are dropped.
 * `monocle` entries collapse into one cell (the first window's id) whose
 * `stack` is the number of windows sharing it. Every tiling cell is
 * occupied by definition.
 */
PHOSPHORSHELL_EXPORT QList<Cell> parseTileBatch(const QList<TileRect>& tiles, const QString& screenId,
                                                const QRect& workArea);

/**
 * @brief Parse `Scrolling.visibleStripJson` into cells plus the lens.
 *
 * The payload only carries the visible cut (0..1 rects with `zoneNumber`),
 * so the lens is the whole band and the hue axis is the visible cut.
 * That is the fallback for a daemon without `stripModelJson`.
 */
PHOSPHORSHELL_EXPORT StripParse parseVisibleStrip(const QString& stripJson);

/**
 * @brief Parse `Scrolling.stripModelJson` into cells plus the lens.
 *
 * One cell per column, keyed by the column's first tile window id, with
 * `stack` the tile count and `focused` for `activeColumn`. The rect's
 * strip axis is the column's position relative to the VIEWPORT window
 * (`[viewOffsetPx, viewOffsetPx + viewportPx]` maps to 0..1, and the
 * cross axis is the full 0..1), so a column straddling an edge is cut
 * and a column wholly outside is dropped and counted in
 * `overflowLeft` / `overflowRight`. `t` and `stripT` are the column's
 * start over the whole strip extent, the structure axis of A2 §1.2. The
 * lens is `{0, 1}` when the whole strip fits the viewport and the
 * viewport's fraction of the strip otherwise. `axis` 1 (vertical) puts
 * the strip axis on y. An empty object (`{}`, the daemon's answer for a
 * screen that is not scrolling) or a malformed document parses to
 * nothing.
 */
PHOSPHORSHELL_EXPORT StripParse parseStripModel(const QString& modelJson);

/**
 * @brief Read a `Tiling.currentTilesJson` array into tile rects.
 *
 * Entries are `{windowId, x, y, width, height, screenId, monocle,
 * floating, zoneId}` in screen pixels, the same shape as a
 * `windowsTileRequested` batch. Entries without a window id are dropped;
 * nothing else is filtered here so the result can stand in for a batch.
 */
PHOSPHORSHELL_EXPORT QList<TileRect> tileRectsFromJson(const QString& tilesJson);

/// `parseTileBatch(tileRectsFromJson(tilesJson), screenId, workArea)`.
PHOSPHORSHELL_EXPORT QList<Cell> parseCurrentTiles(const QString& tilesJson, const QString& screenId,
                                                   const QRect& workArea);

/**
 * @brief Mark cells occupied from a window → zone-ids map.
 *
 * `occupancy` is windowId → the zone ids that window fills (a multi-zone
 * span lists every zone it covers). Floating windows must already be
 * filtered out by the caller. A cell whose id appears for
 * `focusedWindowId` is also marked focused.
 */
PHOSPHORSHELL_EXPORT void applyOccupancy(QList<Cell>& cells, const QHash<QString, QStringList>& occupancy,
                                         const QString& focusedWindowId = {});

/// Mark the cell whose id equals `windowId` focused (tiling cells are
/// keyed by window id). No-op when nothing matches.
PHOSPHORSHELL_EXPORT void applyFocusByWindowId(QList<Cell>& cells, const QString& windowId);

/// The QML-facing shape: one QVariantMap per cell with the documented keys
/// (id, x, y, w, h, t, occupied, focused, label, zoneNumber, stack,
/// stripT, columnIndex).
PHOSPHORSHELL_EXPORT QVariantList toVariantList(const QList<Cell>& cells);

/// `{x, w}` for a lens band, or an empty map for a null rect.
PHOSPHORSHELL_EXPORT QVariantMap lensToVariant(const QRectF& lens);

/// Look up one screen's `mode` in a `getScreenStates` JSON array.
/// Returns -1 when the screen is not listed or the document is malformed.
PHOSPHORSHELL_EXPORT int modeForScreen(const QString& statesJson, const QString& screenId);

} // namespace PhosphorShell::PlacementMapParser
