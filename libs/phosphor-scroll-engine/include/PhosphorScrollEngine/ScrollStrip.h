// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorScrollEngine/ScrollTypes.h>
#include <phosphorscrollengine_export.h>

#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace PhosphorScrollEngine {

/// The pure scrolling-strip model: an ordered list of columns on an
/// unbounded horizontal strip, viewed through a fixed-width viewport.
///
/// This class is deliberately free of engine/compositor dependencies (Qt Core
/// value types only) so the full behavioural matrix is unit-testable in
/// isolation. The one invariant everything here serves: INSERTING OR REMOVING
/// A COLUMN NEVER RESIZES ANOTHER COLUMN. Only the viewport moves.
///
/// ## Coordinates
///
/// Strip coordinates run left-to-right from the first column's left edge at 0.
/// The viewport is the work area; its left edge sits at `viewX` in strip
/// coordinates. The view anchor is stored RELATIVE TO THE ACTIVE COLUMN
/// (`viewAnchor` = active column's left edge position within the viewport),
/// so structural changes left of the focus never make the focused window
/// drift — `viewX` is derived, never stored.
///
/// Mutators that can change which column is focused (or shift pixel
/// positions under the anchor) take the current ScrollLayoutParams so they
/// can keep the view anchored / apply the center-focused-column policy at
/// the mutation site. Structural mutators return true when they changed
/// anything; the owning engine relayouts and applies geometry afterwards.
class PHOSPHORSCROLLENGINE_EXPORT ScrollStrip
{
public:
    ScrollStrip() = default;

    // ── Introspection ────────────────────────────────────────────────────────
    const QVector<Column>& columns() const
    {
        return m_columns;
    }
    int columnCount() const
    {
        return m_columns.size();
    }
    bool isEmpty() const
    {
        return m_columns.isEmpty();
    }
    int activeColumnIndex() const
    {
        return m_activeColumnIdx;
    }
    const Column* activeColumn() const;
    /// The focused window (active tile of the active column), or empty.
    QString activeWindowId() const;
    /// Column index owning @p windowId, or -1.
    int columnOfWindow(const QString& windowId) const;
    bool containsWindow(const QString& windowId) const
    {
        return columnOfWindow(windowId) != -1;
    }
    /// All windows in strip order (left-to-right, top-to-bottom), including
    /// minimized tiles (their slot is part of the order contract).
    QStringList windowsInOrder() const;
    int windowCount() const;
    // ── Structure: open / close / minimize ──────────────────────────────────
    /// Insert a new single-tile column for @p windowId immediately to the
    /// right of the active column (or at index 0 on an empty strip), focus
    /// it, and leave every other column untouched. The view scrolls only as
    /// the centering policy requires. Returns false when already present.
    bool insertWindow(const QString& windowId, const ColumnWidth& width, ColumnDisplay display,
                      const ScrollLayoutParams& params, int minWidth = 0, int minHeight = 0);
    /// Insert @p windowId as a new tile at the bottom of the active column
    /// (rule-driven "open consumed into the focused column"). Falls back to
    /// insertWindow when the strip is empty.
    /// @p width is honoured ONLY on the empty-strip fallback (which routes
    /// through insertWindow); joining an existing column keeps the host
    /// column's width intent — an override would resize every sibling.
    /// @p displayOverride applies only when ENGAGED (an explicit openTabbed
    /// rule): a plain consume-open must not overwrite the host column's
    /// user-toggled display with the config default. The empty-strip
    /// fallback opens with the override when engaged, else Normal.
    bool insertWindowIntoActiveColumn(const QString& windowId, const ColumnWidth& width,
                                      std::optional<ColumnDisplay> displayOverride, const ScrollLayoutParams& params,
                                      int minWidth = 0, int minHeight = 0);
    /// Insert a restored single-tile column at @p columnIndex (clamped) —
    /// the persistence/restore path. Does not change focus.
    /// NOTE: carries no min-size parameters; callers that know the
    /// window's minimum must follow up with setWindowMinimumSize (the
    /// open/restore/crossing sites all do).
    /// @p params re-clamps the view anchor after the positional insert (a
    /// left-of-active insert grows the strip without moving the active
    /// column, and an unclamped anchor can strand the view past the strip
    /// end — the mode-transition seed bug).
    bool insertWindowAt(int columnIndex, const QString& windowId, const ColumnWidth& width, ColumnDisplay display,
                        const ScrollLayoutParams& params);
    /// Re-insert @p windowId as a TILE of the existing column at
    /// @p columnIndex (float/minimize round-trip of a stacked tile), at
    /// @p tileIndex clamped into the stack. Fails when the column index is
    /// out of range — callers fall back to a fresh column.
    bool insertWindowIntoColumnAt(int columnIndex, int tileIndex, const QString& windowId,
                                  const ScrollLayoutParams& params, int minWidth = 0, int minHeight = 0);
    /// Remove @p windowId; a column left empty closes up. Keeps the view
    /// anchored so surviving neighbours don't jump, and selects a sensible
    /// new focus when the active tile/column vanished. Returns false when
    /// untracked.
    bool removeWindow(const QString& windowId, const ScrollLayoutParams& params);
    /// Mark a tile minimized (kept in order, excluded from layout) or restore
    /// it. Returns true when the flag actually changed.
    bool setWindowMinimized(const QString& windowId, bool minimized, const ScrollLayoutParams& params);
    bool isWindowMinimized(const QString& windowId) const;
    /// Update a tile's stored minimum size. Returns true when it changed.
    bool setWindowMinimumSize(const QString& windowId, int minWidth, int minHeight);
    /// The tile's client-reported minimum size, or 0x0 when untracked.
    QSize windowMinimumSize(const QString& windowId) const;

    // ── Focus ────────────────────────────────────────────────────────────────
    /// Focus a column by index (clamped; active tile within it unchanged).
    bool focusColumn(int columnIndex, const ScrollLayoutParams& params);
    /// Focus the adjacent non-fully-minimized column. @p delta is -1/+1.
    bool focusAdjacentColumn(int delta, const ScrollLayoutParams& params);
    bool focusFirstColumn(const ScrollLayoutParams& params);
    bool focusLastColumn(const ScrollLayoutParams& params);
    /// Focus the previous/next non-minimized tile within the active column
    /// (cycles tabs in a tabbed column exactly the same way). @p delta -1/+1.
    bool focusAdjacentTile(int delta);
    /// Make @p windowId the active tile of its (newly active) column.
    /// Externally-driven focus (compositor activation). Returns false when
    /// untracked or already the focused window.
    bool focusWindow(const QString& windowId, const ScrollLayoutParams& params);

    // ── Structure: move / consume / expel ───────────────────────────────────
    /// Swap the active column with its neighbour. @p delta is -1/+1.
    bool moveActiveColumn(int delta, const ScrollLayoutParams& params);
    /// Move the active column directly to @p target (one list move + one
    /// reanchor — a positional move must not pay per-step swap costs).
    bool moveActiveColumnTo(int target, const ScrollLayoutParams& params);
    bool moveActiveColumnToFirst(const ScrollLayoutParams& params);
    bool moveActiveColumnToLast(const ScrollLayoutParams& params);
    /// Reorder the active tile within its column. @p delta is -1/+1.
    bool moveActiveTile(int delta);
    /// Pull the next column's active tile into the bottom of the active
    /// column (niri consume-window-into-column).
    bool consumeWindowIntoColumn(const ScrollLayoutParams& params);
    /// Push the active tile out into its own new column immediately to the
    /// right of the current one (niri expel-window-from-column).
    bool expelWindowFromColumn(const ScrollLayoutParams& params);
    /// niri consume-or-expel: when the active tile is alone in its column it
    /// is consumed into the neighbour column in @p delta's direction
    /// (appended); otherwise it is expelled into its own new column on that
    /// side. @p delta is -1 (left) / +1 (right).
    bool consumeOrExpel(int delta, const ScrollLayoutParams& params);
    /// Remove @p windowId with no focus policy beyond index fixups — the
    /// cross-context transfer / float path (the caller re-homes the window).
    bool takeWindow(const QString& windowId, const ScrollLayoutParams& params);

    // ── Sizing ───────────────────────────────────────────────────────────────
    /// Set the active column's width intent.
    bool setActiveColumnWidth(const ColumnWidth& width);
    /// Cycle the active column through the preset width list. @p delta -1/+1.
    /// Enters the cycle at the nearest preset when the current width is not
    /// a preset.
    bool cycleActiveColumnPresetWidth(int delta, const ScrollLayoutParams& params);
    /// Adjust the active column's width by @p deltaPercent of the work-area
    /// width (niri set-column-width "+10%"/"-10%").
    bool adjustActiveColumnWidth(qreal deltaPercent, const ScrollLayoutParams& params);
    /// Full work-area width, still tiled (niri maximize-column). Toggles back
    /// to the pre-maximize intent when already maximized.
    bool toggleMaximizeActiveColumn(const ScrollLayoutParams& params);
    /// Grow the active column into the on-screen space not covered by any
    /// column at the current view (niri expand-column-to-available-width).
    bool expandActiveColumnToAvailableWidth(const ScrollLayoutParams& params);
    /// Set the active tile's height intent.
    bool setActiveWindowHeight(const WindowHeight& height);
    /// Cycle the active tile through the preset height list. @p delta -1/+1.
    bool cycleActiveWindowPresetHeight(int delta, const ScrollLayoutParams& params);
    /// Adjust the active tile's height by @p deltaPercent of the work-area
    /// height.
    bool adjustActiveWindowHeight(qreal deltaPercent, const ScrollLayoutParams& params);
    /// Back to the even auto-split for EVERY tile in the active column.
    bool resetActiveColumnHeights();
    /// Record the size a client/user resize actually settled on. The width
    /// becomes the column's Fixed intent only when @p widthChanged (the
    /// engine compares against the last applied rect) — a vertical-only
    /// resize must not pin a Proportion/Preset column to pixels. The height
    /// becomes the tile's Fixed intent symmetrically, only when
    /// @p heightChanged; a lone tile is included, because relayout honours a
    /// solo tile's Fixed height (niri parity). Other columns are untouched.
    bool reconcileWindowSize(const QString& windowId, const QSize& ackedSize, bool widthChanged = true,
                             bool heightChanged = true);

    // ── Display ──────────────────────────────────────────────────────────────
    /// Toggle the active column between Normal and Tabbed presentation.
    bool toggleActiveColumnTabbed();

    /// Direct height-intent write for @p windowId (any tile, not just the
    /// active one) — the mode-round-trip restore path re-applies stashed
    /// heights through this. Returns false for an unknown window or an
    /// unchanged intent.
    bool setWindowHeightIntent(const QString& windowId, const WindowHeight& height);

    /// Strip indices of the columns currently intersecting the viewport,
    /// in strip order — the scroll "zone" space: visible columns are
    /// numbered 1..k left to right, off-screen columns carry no number.
    QVector<int> visibleColumnIndices(const ScrollLayoutParams& params) const;

    /// Rotate the window contents of the VISIBLE columns through their
    /// slots (clockwise = every stack shifts one slot right, the last
    /// visible wraps to the first). Widths and display stay with the SLOT,
    /// like autotile's rotate through fixed zones, so the strip's geometry
    /// does not move — only the windows do. The active column index stays
    /// put (focus follows the slot; callers activate its new window).
    /// Returns the number of windows rotated, 0 when fewer than two
    /// columns are visible.
    int rotateVisibleColumns(bool clockwise, const ScrollLayoutParams& params);

    // ── View ─────────────────────────────────────────────────────────────────
    /// The stored active-relative view anchor (see class doc). Exposed for
    /// the engine's mode-round-trip stash — pixels derived from it are not.
    int viewAnchor() const
    {
        return m_viewAnchor;
    }
    /// Restore a previously captured view anchor, RAW: a centered anchor
    /// implies an out-of-range derived viewX by design (the same shape
    /// centerActiveColumn stores), so no clamp is applied here — later
    /// structural inserts re-clamp when the strip cannot honour the view.
    /// The stash-restore path re-applies the anchor AFTER re-focusing the
    /// stashed active window, overriding the focus change's own
    /// centering-policy reanchor with the user's actual view.
    void restoreViewAnchor(int anchor, const ScrollLayoutParams& params);
    /// Re-apply the centering policy to the current active column (settings
    /// change / work-area change) using the current anchor as the "no
    /// scroll" baseline.
    void updateViewForFocus(const ScrollLayoutParams& params);
    /// Center the active column in the view (niri center-column).
    /// Returns true when the anchor actually moved.
    bool centerActiveColumn(const ScrollLayoutParams& params);

    // ── Relayout ─────────────────────────────────────────────────────────────
    /// Resolve every non-minimized tile's absolute pixel rect against
    /// @p params. Pure function of the current model state; does not mutate.
    ResolvedStrip relayout(const ScrollLayoutParams& params) const;

    // ── Pixel resolution helpers (shared with the engine/tests) ─────────────
    /// The pixel width @p width resolves to under @p params (gap-aware
    /// proportions, preset lookup; no min-width clamp).
    static int resolveColumnWidthPx(const ColumnWidth& width, const ScrollLayoutParams& params);

private:
    // scrollstrip_structure.cpp
    void removeColumnAt(int columnIndex);
    /// Shared body of removeWindow/takeWindow: drop the tile, close up an
    /// emptied column, fix every index, and keep the view anchored. When
    /// @p refocus is true the niri close-focus policy picks the new focus.
    bool removeWindowInternal(const QString& windowId, const ScrollLayoutParams& params, bool refocus);
    // scrollstrip_relayout.cpp
    /// Pixel width of column @p c under @p params including its tiles'
    /// min-width clamp (a fully-minimized column resolves to 0).
    int columnWidthPx(const Column& c, const ScrollLayoutParams& params) const;
    /// Strip-coordinate left edge of @p columnIndex under @p params.
    int columnStripX(int columnIndex, const ScrollLayoutParams& params) const;
    /// Total strip width under @p params.
    int stripWidthPx(const ScrollLayoutParams& params) const;
    /// The derived viewport left edge in strip coordinates.
    int viewXFor(const ScrollLayoutParams& params) const;
    /// Anchor value that centers column @p columnIndex in the viewport.
    int centeredAnchorFor(int columnIndex, const ScrollLayoutParams& params) const;
    /// Clamp @p anchor so the derived viewX stays within [0, stripW - workW]
    /// (left-pinned when the strip fits the viewport entirely).
    int clampedAnchor(int anchor, const ScrollLayoutParams& params) const;
    int keepOrRecenterAnchor(int oldViewX, const ScrollLayoutParams& params) const;
    /// Apply the center-focused-column policy after the active column moved
    /// from @p prevIdx at @p oldViewX (strip coords) to the current active.
    void reanchorAfterFocusChange(int prevIdx, int oldViewX, const ScrollLayoutParams& params);
    // scrollstrip_sizing.cpp
    /// Nearest preset index to the current pixel width of @p c (for entering
    /// the preset cycle from a non-preset width).
    int nearestPresetWidthIdx(const Column& c, const ScrollLayoutParams& params) const;
    int nearestPresetHeightIdx(const Tile& t, const ScrollLayoutParams& params) const;
    /// The tile's current height as a fraction of the column height, or -1
    /// when it has no determinate fraction (Auto weight).
    qreal currentHeightFraction(const Tile& t, const ScrollLayoutParams& params) const;

    Column* activeColumnMutable();
    Tile* activeTileMutable();
    void clampActiveIndices();

    QVector<Column> m_columns;
    int m_activeColumnIdx = -1;
    /// Active column's left edge position within the viewport (see class doc).
    int m_viewAnchor = 0;
    /// Pre-maximize width intent for the maximize toggle (single slot:
    /// maximize is a focused-column toggle).
    ColumnWidth m_preMaximizeWidth;
    int m_preMaximizeColumnIdx = -1;
};

} // namespace PhosphorScrollEngine
