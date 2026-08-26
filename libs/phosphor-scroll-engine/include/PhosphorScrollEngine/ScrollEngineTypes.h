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
// columns). The types here describe the engine's API surface around that
// model, which is why they live apart from it.

// The one crossing into the strip model's vocabulary: a visible tile reports
// the tab indicator its column draws, and TabIndicatorPosition is that
// model's enum. Not re-declared here — a second spelling of the four edges
// is exactly the kind of parallel vocabulary the split above exists to avoid.
#include <PhosphorScrollEngine/ScrollTypes.h>

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
    /// How many tabs the owning column's indicator shows, and 0 when it draws
    /// no indicator at all. The gate is the resolved indicator rect, where the
    /// compositor's tab-strip payload starts too (engine_apply.cpp): that one
    /// rect already folds in the master switch, the single-tab skip and "this
    /// column is not tabbed". The emitter's gate is a SUPERSET of this one —
    /// it also drops out-of-view and fully-parked columns — so a preview is
    /// never missing an indicator the screen draws, but a parked column can
    /// carry one the screen does not. Counts the column's tiles including the
    /// hidden ones — those ARE the tabs; only minimized tiles are absent, and
    /// they have no pill on screen either.
    int tabCount = 0;
    /// Which tab of @c tabCount this tile is, 0-based, and -1 when the column
    /// draws no indicator. A resolved tabbed column has exactly one non-hidden
    /// tile, so exactly one visible tile carries the column's tab data.
    int activeTabIndex = -1;
    /// Which edge of the column the indicator runs along. Meaningful only
    /// while @c tabCount is above zero; the default matches
    /// ResolvedColumn::tabIndicatorPosition's.
    TabIndicatorPosition tabIndicatorPosition = TabIndicatorPosition::Left;
    /// How much of that edge the indicator covers, 0 to 1, centered on it
    /// (TabIndicatorParams::indicatorRectFor centers on the long axis). The
    /// RESOLVED proportion, measured off the two rects rather than copied from
    /// the setting, so the rounding and the 1px floor the resolve applied are
    /// already in it.
    ///
    /// The indicator's THICKNESS deliberately does not ride along. It is a
    /// handful of pixels, which is under half a pixel once a preview scales a
    /// screen into a thumbnail, so every renderer of this has to floor it to
    /// something legible and a true value would only invite one of them to
    /// draw an invisible indicator faithfully.
    qreal tabLengthProportion = 0.0;
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
/// resolved hits back through Column::indexOfWindow. Note the minimized flag
/// serves embedders that deliver a real minimize signal
/// (ScrollStrip::setWindowMinimized); the PlasmaZones daemon reports minimize
/// as a float, so its own snapshots never carry a minimized tile.
struct ScrollStripSnapshotTile
{
    QString windowId;
    /// Rect relative to the owning column's resolved bounds, 0..1 on x. On y
    /// it MAY exceed 1.0: when even the min-height floors overflow the
    /// column, the trailing tiles lay out below the work area and the
    /// overflow stands (renderers clip). A tile can also resolve a
    /// zero-height rect (the overflow tail squeezed to nothing). Null for
    /// minimized tiles (they resolve no rect), for the hidden tabs of a
    /// tabbed column (a renderer draws those as tabs, not stacked rects),
    /// and for a tab the exclusion emulation promoted to activeTab (its rect
    /// was resolved while it was hidden; tab renderers draw segments from
    /// the flag, not the rect).
    QRectF relRect;
    bool minimized = false;
    /// Non-active tab of a tabbed column. The shipped renderer branches on
    /// the column's tabbed flag instead; carried for direct consumers.
    bool hidden = false;
    /// The column's active tile (for a tabbed column: the visible tab).
    bool activeTab = false;
};

/// One column of a strip snapshot, in strip order. Popup cards render
/// VARIABLE-WIDTH off @c widthFraction — each card is its column at preview
/// scale (a half-screen column is a half-width card) — and the bar width
/// sums the same fractions. Tile relRects are column-relative, so without
/// the fraction every column would render as a full-width mini-screen
/// regardless of its actual width.
struct ScrollStripSnapshotColumn
{
    bool tabbed = false;
    /// Resolved column extent ALONG THE STRIP as a fraction of the work
    /// area's extent on that same axis (engine_snapshot computes both in
    /// main-axis role terms, so on a vertical strip this is a HEIGHT share),
    /// clamped to (0, 1]. 0 when the column resolves no rect (fully
    /// minimized) — renderers fall back to a full-extent preview there.
    qreal widthFraction = 0.0;
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
/// dropped, later positions renumbered). GEOMETRY is pre-detach on the
/// emulated path: widthFraction and the relRects come from the strip's
/// relayout WITH the drag window still in it (its column previews the
/// survivor at half height, siblings keep pre-detach widths) — the honest
/// "what you are dragging out of" picture; only STRUCTURE (indices) is
/// post-detach. The shipped consumer builds targets only from the column
/// endpoints (tile 0 / append -1); per-tile indices are carried so richer
/// targets stay expressible.
struct ScrollStripSnapshot
{
    QVector<ScrollStripSnapshotColumn> columns;
    /// Snapshot position of the strip's active column. When the exclusion
    /// empties the active column, this re-points to the column that takes
    /// its place (the right neighbour, or the new last column), matching the
    /// real detach; -1 only when the snapshot ends up with no columns.
    int activeColumnIndex = -1;
    /// False when the screen has no strip state or no valid work area —
    /// distinct from a valid, empty strip (zero columns). A test /
    /// introspection seam: the daemon's serializer collapses both states to
    /// an empty card list, which the popup renders identically on purpose.
    bool valid = false;
};

} // namespace PhosphorScrollEngine
