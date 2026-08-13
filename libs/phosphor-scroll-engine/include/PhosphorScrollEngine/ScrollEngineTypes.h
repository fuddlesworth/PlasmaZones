// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// The ScrollEngine's own public VALUE types: what a per-window open rule can
// say about an arriving window, and what one visible tile of a strip is.
// Split out of ScrollEngine.h at its file-size ceiling, the same hoist
// ScrollStashTypes.h made for the stash types. ScrollEngine.h includes this
// and aliases both tile types back into the class, so ScrollEngine::VisibleTile
// keeps naming ScrollVisibleTile for every existing consumer.
//
// ScrollTypes.h stays the STRIP MODEL's vocabulary (widths, heights, resolved
// columns). These three describe the engine's API surface around that model,
// which is why they live apart from it.

#include <QRect>
#include <QRectF>
#include <QString>
#include <QVector>

#include <optional>

namespace PhosphorScrollEngine {

/// Per-window open-behaviour overrides resolved from window rules
/// (openColumnWidth / openTabbed / openColumnPlacement). Unset fields fall
/// through to the engine's config-backed defaults — config stays the
/// authoritative base, rules layer on top.
struct ScrollOpenParams
{
    std::optional<qreal> widthFraction;
    /// Work-area height fraction (openWindowHeight rule), committed as a
    /// Fixed pixel intent against the live work area after the insert.
    std::optional<qreal> heightFraction;
    std::optional<bool> tabbed;
    /// True: join the focused column instead of opening a new one.
    std::optional<bool> consume;
    /// True: the new column opens at the full work-area width (openMaximized
    /// rule). Outranks widthFraction and the template blueprint — a maximized
    /// open IS a width verdict, the stronger one.
    ///
    /// It only reaches a column that this open CREATES, which is what the
    /// three width-dropping arrival shapes have in common: a consume open
    /// (the openColumnPlacement rule or an IntoActiveColumn insert position)
    /// joins an existing column and keeps the HOST's width, since resizing
    /// the host would resize every sibling in the stack, and a strip-stash
    /// restore rebuilds the column's remembered width. widthFraction is
    /// dropped by the same three, for the same reason.
    std::optional<bool> maximized;
    /// Per-window override of IScrollSettings::scrollingFocusNewWindows
    /// (openFocused rule). Consumed by windowOpened's focus arm, not by the
    /// insert itself; the engine's FLOAT exits ignore it (a floated open
    /// keeps the compositor's own focus verdict).
    std::optional<bool> focused;
};

/// One visible tile of a strip: the unit of the scroll "zone number" space.
/// Zone number N is ScrollEngine::visibleTiles(screenId).at(N - 1) —
/// sequential in strip order (columns left to right, tiles top to bottom).
/// That is the ADDRESS space, and it is single-sourced: previews label it and
/// the Snap-to-Zone digits resolve against it through moveFocusedToPosition,
/// both from that one walk, so they always name the same tile.
///
/// The ACTION a digit performs is COARSER than the address it resolves. A
/// digit naming a stack-mate of the operated window reorders that window
/// inside its column, but a digit naming a tile in ANOTHER column moves the
/// whole active COLUMN to that column's strip position — the deliberate
/// pre-tile-numbering behaviour, kept because the column is the strip's unit
/// of travel. The consequence is real and is not a bug: stack-mates travel
/// along, and after a cross-column move the operated window's own number may
/// differ from the digit pressed (any stacked column shifts the walk, and
/// Always/OnOverflow centering re-derives the visible set around the new
/// active column).
struct ScrollVisibleTile
{
    QString windowId;
    /// Strip index of the owning column. Not the zone number, and not unique
    /// across the walk — a stacked column contributes one entry per visible
    /// tile, all carrying the same index. Read by callers that need to tell
    /// stack-mates apart from separate columns; the engine's own tests and
    /// the D-Bus adaptor test are the only such callers today.
    int columnIndex = -1;
    /// The tile's 1-based zone number, stamped by the walk. THE number space:
    /// every consumer (preview labels, the OSD, the Snap-to-Zone digits)
    /// reads this rather than re-deriving an ordinal from its own iteration,
    /// so no consumer can drift out of step with another.
    int zoneNumber = 0;
    /// Absolute pixel rect, clipped to the work area.
    QRect rect;
};

/// A tile paired with its screen-normalized rect, from ONE strip walk (see
/// ScrollEngine::visibleTilesWithRects).
struct ScrollVisibleTileWithRect
{
    ScrollVisibleTile tile;
    /// Same basis and fallback as ScrollEngine::visibleTileRectsRelative.
    QRectF relativeRect;
};

/// One tile of a strip snapshot (see ScrollEngine::stripSnapshot). Unlike
/// ResolvedTile this keeps MINIMIZED tiles, because the snapshot's tile
/// positions must stay valid DragInsertTarget.secondary indices — the same
/// model-order rule computeDragInsertTargetAtPoint enforces by mapping
/// resolved hits back through Column::indexOfWindow.
struct ScrollStripSnapshotTile
{
    QString windowId;
    /// Rect relative to the owning column's resolved bounds (0..1 on both
    /// axes). Null for minimized tiles (they resolve no rect) and for the
    /// hidden tabs of a tabbed column (a renderer draws those as tabs, not
    /// stacked rects).
    QRectF relRect;
    bool minimized = false;
    /// Non-active tab of a tabbed column.
    bool hidden = false;
    /// The column's active tile (for a tabbed column: the visible tab).
    bool activeTab = false;
};

/// One column of a strip snapshot, in strip order.
struct ScrollStripSnapshotColumn
{
    bool tabbed = false;
    /// Resolved column width as a fraction of the work-area width. 0 when
    /// the column resolves no rect (every tile minimized) — renderers clamp
    /// to their own minimum card width.
    qreal relWidth = 0.0;
    /// Resolved column height as a fraction of the work-area height.
    qreal relHeight = 0.0;
    /// MODEL tile order, minimized tiles included.
    QVector<ScrollStripSnapshotTile> tiles;
};

/// A column-aware strip snapshot for drag-popup renderers. INDEX CONTRACT:
/// column positions in @c columns and tile positions in each column are the
/// indices a DragInsertTarget built from this snapshot must carry — they
/// name slots in the strip AS A COMMIT WILL SEE IT (the dragged window
/// detached). When a drag-insert preview is live the resolve already runs
/// against the preview's detached strip; when it is not, the accessor's
/// excludeWindowId emulates the detach (tile removed, an emptied column
/// dropped, later positions renumbered).
struct ScrollStripSnapshot
{
    QVector<ScrollStripSnapshotColumn> columns;
    /// Snapshot position of the strip's active column, -1 when it was
    /// excluded or the strip is empty.
    int activeColumnIndex = -1;
    /// False when the screen has no strip state or no valid work area —
    /// distinct from a valid, empty strip (zero columns).
    bool valid = false;
};

} // namespace PhosphorScrollEngine
