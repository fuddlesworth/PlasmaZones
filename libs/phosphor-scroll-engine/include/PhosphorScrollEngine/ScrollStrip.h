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
/// unbounded strip, viewed through a viewport of the work area's extent.
///
/// This class is deliberately free of engine/compositor dependencies (Qt Core
/// value types only) so the full behavioural matrix is unit-testable in
/// isolation. The one invariant everything here serves: INSERTING OR REMOVING
/// A COLUMN NEVER RESIZES ANOTHER COLUMN. Only the viewport moves.
///
/// ## Coordinates
///
/// Strip coordinates run ALONG THE MAIN AXIS from the first column's leading
/// edge at 0. The viewport is the work area; its leading edge sits at
/// `viewOffset` in strip coordinates. The view anchor is stored RELATIVE TO
/// THE ACTIVE COLUMN (`viewAnchor` = active column's leading edge position
/// within the viewport), so structural changes before the focus along the
/// strip never make the focused window drift — `viewOffset` is derived, never
/// stored.
///
/// ## Naming exemption
///
/// The public verbs below keep their physical width/height spellings
/// (setActiveColumnWidth, adjustActiveWindowHeight, WindowHeight, and their
/// siblings) because they are shipped API carrying niri's own vocabulary.
/// Throughout this header `width` reads as the column's MAIN extent (along
/// the strip) and `height` as the tile's CROSS extent (across it), whichever
/// way the strip actually runs. StripAxis's role-naming rule governs NEW
/// internals; it does not ask for these names to be rewritten.
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
    /// The focused window: the active tile of the active column, or empty.
    ///
    /// If that tile is MINIMIZED it falls back to the column's first
    /// non-minimized tile, so this can name a different window than
    /// `activeColumn()->activeTileIdx` points at. The mutating verbs
    /// (moveActiveTile, expelWindowFromColumn, setActiveWindowHeight, and
    /// cycleActiveWindowPresetHeight — adjustActiveWindowHeight is NOT on
    /// this list; it self-guards by resolving the tile from the relayout)
    /// all act
    /// on activeTileIdx, so a caller pairing this accessor with one of them
    /// could report a window the operation did not touch. Not reachable in
    /// production today, where the daemon models minimize as float and a
    /// minimized window is not a strip tile at all.
    QString activeWindowId() const;
    /// Column index owning @p windowId, or -1.
    int columnOfWindow(const QString& windowId) const;
    bool containsWindow(const QString& windowId) const
    {
        return columnOfWindow(windowId) != -1;
    }
    /// All windows in strip order (column order along the strip, then each
    /// column's stack order across it), including minimized tiles (their slot
    /// is part of the order contract).
    QStringList windowsInOrder() const;
    int windowCount() const;
    // ── Structure: open / close / minimize ──────────────────────────────────
    /// Insert a new single-tile column for @p windowId at @p pos (default:
    /// immediately AFTER the active column along the strip, or at index 0 on an
    /// empty strip), focus it, and leave every other column untouched. The
    /// view scrolls only as the centering policy requires. Returns false
    /// when already present. IntoActiveColumn is NOT handled here — the
    /// engine routes it through insertWindowIntoActiveColumn so the strip's
    /// two insert verbs stay distinct; passed anyway, it degrades to
    /// RightOfActive.
    bool insertWindow(const QString& windowId, const ColumnWidth& width, ColumnDisplay display,
                      const ScrollLayoutParams& params, int minWidth = 0, int minHeight = 0,
                      ScrollInsertPosition pos = ScrollInsertPosition::RightOfActive);
    /// Insert @p windowId as a new tile at the END of the active column's
    /// stack (rule-driven "open consumed into the focused column"). Falls back to
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
    /// the persistence/restore path. Does not change focus, WITH ONE
    /// EXCEPTION: on an EMPTY strip the new column becomes the active one,
    /// because a strip with columns and no focus has no valid state to be
    /// in. The asymmetry is load-bearing for the restore paths, which lean
    /// on the first arrival taking the focus and on every later one leaving
    /// it alone until the stashed focus is re-asserted.
    /// NOTE: carries no min-size parameters; callers that know the
    /// window's minimum must follow up with setWindowMinimumSize (the
    /// open/restore/crossing sites all do).
    /// @p params re-clamps the view anchor after the positional insert (an
    /// insert BEFORE the active column along the strip grows it without moving the active
    /// column, and an unclamped anchor can strand the view past the strip
    /// end — the mode-transition seed bug).
    bool insertWindowAt(int columnIndex, const QString& windowId, const ColumnWidth& width, ColumnDisplay display,
                        const ScrollLayoutParams& params);
    /// Re-insert @p windowId as a TILE of the existing column at
    /// @p columnIndex (float/minimize round-trip of a stacked tile), at
    /// @p tileIndex clamped into the stack. Fails when the column index is
    /// out of range — callers fall back to a fresh column.
    ///
    /// TAKES FOCUS, unlike insertWindowAt: the new tile becomes its column's
    /// active one, the column becomes the strip's active column, and the view
    /// re-anchors. That is what the unfloat path wants (the window the user
    /// just restored is the one to look at), but it means a restore path
    /// driving several arrivals must re-assert its own focus afterwards —
    /// the last arrival would otherwise keep it.
    /// The tile is seeded with @p params.defaultWindowHeight; callers with a
    /// remembered intent overwrite it via setWindowHeightIntent.
    bool insertWindowIntoColumnAt(int columnIndex, int tileIndex, const QString& windowId,
                                  const ScrollLayoutParams& params, int minWidth = 0, int minHeight = 0);
    /// Remove @p windowId; a column left empty closes up. Keeps the view
    /// anchored so surviving neighbours don't jump, and selects a sensible
    /// new focus when the active tile/column vanished. Returns false when
    /// untracked.
    bool removeWindow(const QString& windowId, const ScrollLayoutParams& params);
    /// Mark a tile minimized (kept in order, excluded from layout) or restore
    /// it. Returns true when the flag actually changed.
    ///
    /// TEST SEAM, and the entry point to a whole test-driven domain: the
    /// daemon models minimize as a FLOAT (the effect reports it that way),
    /// so in production a minimized window leaves the strip entirely and
    /// nothing here ever sets the flag. The minimized branches in relayout,
    /// focus, move and the height budget are nonetheless real behaviour the
    /// model owes its callers — kept, exercised by test_scrollstrip_core /
    /// _ops, and documented as unreached rather than deleted, because the
    /// alternative is a model that silently mislays a tile the day the
    /// daemon does drive minimize directly.
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
    /// Focus the first (@p last false) or last non-minimized tile of the
    /// active column (niri focus-window-top/bottom). False when already there.
    bool focusTileAtEnd(bool last);
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
    /// Reorder the active tile within its column by @p delta positions.
    ///
    /// Any magnitude is accepted, not just -1/+1: the walk steps over
    /// minimized tiles, so a single-step request can cross several slots. The
    /// move is a REMOVE-AND-INSERT (QList::move), not a swap, so for
    /// |delta| > 1 the intervening tiles shift by one rather than the two
    /// endpoints exchanging places. A delta of 0 is a no-op.
    bool moveActiveTile(int delta);
    /// Pull the next column's active tile onto the END of the active
    /// column's stack (niri consume-window-into-column).
    bool consumeWindowIntoColumn(const ScrollLayoutParams& params);
    /// Push the active tile out into its own new column immediately AFTER
    /// the current one along the strip (niri expel-window-from-column).
    bool expelWindowFromColumn(const ScrollLayoutParams& params);
    /// niri consume-or-expel: when the active tile is alone in its column it
    /// is consumed into the neighbour column in @p delta's direction
    /// (appended); otherwise it is expelled into its own new column on that
    /// side. @p delta is -1 (towards the strip's start) / +1 (towards its end).
    bool consumeOrExpel(int delta, const ScrollLayoutParams& params);
    /// Remove @p windowId with no focus policy beyond index fixups — the
    /// cross-context transfer / float path (the caller re-homes the window).
    bool takeWindow(const QString& windowId, const ScrollLayoutParams& params);

    // ── Sizing ───────────────────────────────────────────────────────────────
    /// Set the active column's width intent, verbatim. The direct write the
    /// cycle/adjust/maximize verbs below are built on, and the absolute
    /// set-column-width verb's (and the tests') way to reach an exact intent.
    /// Callers own validation — nothing here clamps. The width is the
    /// column's extent ALONG the strip.
    bool setActiveColumnWidth(const ColumnWidth& width);
    /// Cycle the active column through the preset width list. @p delta -1/+1.
    /// Enters the cycle at the nearest preset when the current width is not
    /// a preset.
    bool cycleActiveColumnPresetWidth(int delta, const ScrollLayoutParams& params);
    /// Adjust the active column's width by @p deltaPercent of the work area's
    /// MAIN extent (niri set-column-width "+10%"/"-10%").
    bool adjustActiveColumnWidth(qreal deltaPercent, const ScrollLayoutParams& params);
    /// Full work-area MAIN extent, still tiled (niri maximize-column). Toggles
    /// back to the pre-maximize intent when already maximized.
    bool toggleMaximizeActiveColumn(const ScrollLayoutParams& params);
    /// Grow the active column into the on-screen MAIN-axis space not covered by
    /// any column at the current view (niri expand-column-to-available-width).
    bool expandActiveColumnToAvailableWidth(const ScrollLayoutParams& params);
    /// Set the active tile's height intent, verbatim. The height twin of
    /// setActiveColumnWidth: the direct write under the cycle/adjust verbs,
    /// the restore paths' setWindowHeightIntent, and the absolute
    /// set-window-height verb. Callers own validation. The height is the
    /// tile's extent ACROSS the strip.
    bool setActiveWindowHeight(const WindowHeight& height);
    /// Cycle the active tile through the preset height list. @p delta -1/+1.
    bool cycleActiveWindowPresetHeight(int delta, const ScrollLayoutParams& params);
    /// Adjust the active tile's height by @p deltaPercent of the work area's
    /// CROSS extent.
    bool adjustActiveWindowHeight(qreal deltaPercent, const ScrollLayoutParams& params);
    /// Back to the even auto-split for EVERY tile in the active column.
    bool resetActiveColumnHeights();
    /// Record the size a client/user resize actually settled on. The acked
    /// MAIN extent becomes the column's Fixed width intent only when
    /// @p mainChanged (the engine compares against the last applied rect) — a
    /// resize purely ACROSS the strip must not pin a Proportion/Preset column
    /// to pixels. The acked CROSS extent becomes the tile's Fixed height
    /// intent symmetrically, only when @p crossChanged; a lone tile is
    /// included, because relayout honours a solo tile's Fixed height (niri
    /// parity). Other columns are untouched.
    ///
    /// These two bools were spelled @c widthChanged / @c heightChanged before
    /// the strip gained a second axis, and they now mean main/cross. The
    /// signature did not change shape, so an out-of-tree caller still passing
    /// physical width/height flags compiles and is silently wrong on a
    /// vertical strip: each guard then protects the intent it was not written
    /// for.
    /// @p mainChanged / @p crossChanged say which axis the interactive resize
    /// actually moved, in ROLE terms. @p ackedSize is the compositor's
    /// physical QSize and is decoded through @p params.axis, so the caller
    /// must derive the two flags against that same axis or each guard ends up
    /// protecting the intent it was not written for.
    bool reconcileWindowSize(const QString& windowId, const QSize& ackedSize, bool mainChanged, bool crossChanged,
                             const ScrollLayoutParams& params);

    // ── Display ──────────────────────────────────────────────────────────────
    /// Toggle the active column between Normal and Tabbed presentation.
    bool toggleActiveColumnTabbed();

    /// Toggle windowed fullscreen on the active tile of the active column.
    /// Layout-neutral: the tile keeps its column slot; the flag only rides
    /// the apply payload so the compositor flips the client's fullscreen
    /// state. Returns false when there is no active tile.
    bool toggleActiveWindowedFullscreen();
    /// Direct flag write for @p windowId (any tile, not just the active
    /// one) — compositor-driven reconciliation clears through this, and the
    /// mode-round-trip restore path re-applies stashed flags through it.
    /// Returns false for an unknown window or an unchanged flag.
    bool setWindowedFullscreen(const QString& windowId, bool on);
    bool isWindowedFullscreen(const QString& windowId) const;

    /// Direct height-intent write for @p windowId (any tile, not just the
    /// active one) — the mode-round-trip restore path re-applies stashed
    /// heights through this. Returns false for an unknown window or an
    /// unchanged intent.
    bool setWindowHeightIntent(const QString& windowId, const WindowHeight& height);

    /// Strip indices of the columns currently intersecting the viewport, in
    /// strip order — a viewport-intersection helper, NOT the zone-number
    /// space. Zone numbers are per-TILE and live one layer up, in
    /// ScrollEngine::visibleTiles; rotateVisibleColumns is this helper's
    /// only consumer.
    QVector<int> visibleColumnIndices(const ScrollLayoutParams& params) const;

    /// Rotate the window contents of the VISIBLE columns through their
    /// slots (clockwise = every stack shifts one slot ALONG the strip,
    /// towards its end, and the last visible wraps to the first). Width and
    /// display INTENTS stay with the SLOT, like autotile's rotate through
    /// fixed zones, so the strip's geometry holds still for the ordinary
    /// case. It is not an absolute: a column's resolved width also honours
    /// its tiles' min-MAIN clamp, so rotating a window with a large minimum
    /// into a small slot does grow that slot. The anchor is re-clamped afterwards for exactly
    /// that reason. The active column index stays put (focus follows the
    /// slot; callers activate its new window). Returns the number of
    /// windows rotated, 0 when fewer than two columns are visible.
    int rotateVisibleColumns(bool clockwise, const ScrollLayoutParams& params);

    // ── View ─────────────────────────────────────────────────────────────────
    /// The stored active-relative view anchor (see class doc). Exposed for
    /// the engine's mode-round-trip stash — pixels derived from it are not.
    int viewAnchor() const
    {
        return m_viewAnchor;
    }
    /// Restore a previously captured view anchor, RAW: a centered anchor
    /// implies an out-of-range derived viewOffset by design (the same shape
    /// centerActiveColumn stores), so no clamp is applied here — later
    /// structural inserts re-clamp when the strip cannot honour the view.
    /// The stash-restore path re-applies the anchor AFTER re-focusing the
    /// stashed active window, overriding the focus change's own
    /// centering-policy reanchor with the user's actual view.
    /// @p params is currently UNUSED (the raw restore needs no layout maths)
    /// but stays in the exported signature: every sibling anchor mutator
    /// takes it, and dropping it is an ABI break for no gain.
    void restoreViewAnchor(int anchor, const ScrollLayoutParams& params);
    /// Re-apply the centering policy to the current active column (settings
    /// change / work-area change) using the current anchor as the "no
    /// scroll" baseline.
    void updateViewForFocus(const ScrollLayoutParams& params);
    /// Center the active column in the view (niri center-column).
    /// Returns true when the anchor actually moved.
    bool centerActiveColumn(const ScrollLayoutParams& params);
    /// Center the span of FULLY visible columns in the view (niri
    /// center-visible-columns). Falls back to centerActiveColumn when no
    /// column is fully visible. Returns true when the anchor actually moved.
    bool centerVisibleColumns(const ScrollLayoutParams& params);

    // ── Relayout ─────────────────────────────────────────────────────────────
    /// Resolve every non-minimized tile's absolute pixel rect against
    /// @p params. Pure function of the current model state; does not mutate.
    ResolvedStrip relayout(const ScrollLayoutParams& params) const;

    // ── Pixel resolution helpers (shared with the engine/tests) ─────────────
    /// The pixel MAIN extent @p width resolves to under @p params (gap-aware
    /// proportions, preset lookup; no min-extent clamp).
    static int resolveColumnWidthPx(const ColumnWidth& width, const ScrollLayoutParams& params);

private:
    // scrollstrip_structure.cpp
    void removeColumnAt(int columnIndex);
    /// Shared body of removeWindow/takeWindow: drop the tile, close up an
    /// emptied column, fix every index, and keep the view anchored. When
    /// @p refocus is true the niri close-focus policy picks the new focus.
    bool removeWindowInternal(const QString& windowId, const ScrollLayoutParams& params, bool refocus);
    // scrollstrip_relayout.cpp
    /// Pixel MAIN extent of column @p c under @p params including its tiles'
    /// min-MAIN clamp (a fully-minimized column resolves to 0).
    int columnExtentPx(const Column& c, const ScrollLayoutParams& params) const;
    /// Strip-coordinate LEADING edge of @p columnIndex under @p params.
    int columnStripPos(int columnIndex, const ScrollLayoutParams& params) const;
    /// Total strip MAIN extent under @p params.
    int stripExtentPx(const ScrollLayoutParams& params) const;
    /// The derived viewport leading edge in strip coordinates.
    int viewOffsetFor(const ScrollLayoutParams& params) const;
    /// Anchor value that centers column @p columnIndex in the viewport.
    int centeredAnchorFor(int columnIndex, const ScrollLayoutParams& params) const;
    /// Clamp @p anchor so the derived viewOffset stays within
    /// [0, stripMain - workMain] (pinned at the strip's START when the strip
    /// fits the viewport entirely).
    int clampedAnchor(int anchor, const ScrollLayoutParams& params) const;
    /// The anchor a structural mutation ends on: KEEP the view where it was
    /// (an anchor reproducing @p oldViewOffset, clamped at BOTH strip edges —
    /// deliberately stricter than removeWindowInternal's leading-edge-only
    /// clamp; see that function's comment for why a removal does not reclaim
    /// trailing dead space) unless the centering policy says the focused
    /// column re-centers, in which case RECENTER wins.
    int keepOrRecenterAnchor(int oldViewOffset, const ScrollLayoutParams& params) const;
    /// Apply the center-focused-column policy after the active column moved
    /// from @p prevIdx at @p oldViewOffset (strip coords) to the current active.
    void reanchorAfterFocusChange(int prevIdx, int oldViewOffset, const ScrollLayoutParams& params);
    // scrollstrip_sizing.cpp
    /// The tile's current height as a fraction of the column's CROSS extent, or -1
    /// when it has no determinate fraction (Auto weight). Preset anchors
    /// answer their SNAPPED value (nearestPresetValue), matching relayout.
    qreal currentHeightFraction(const Tile& t, const ScrollLayoutParams& params) const;

    Column* activeColumnMutable();
    Tile* activeTileMutable();
    void clampActiveIndices();

    QVector<Column> m_columns;
    int m_activeColumnIdx = -1;
    /// Active column's leading edge position within the viewport (see class doc).
    int m_viewAnchor = 0;
    /// Pre-maximize width intent for the maximize toggle (single slot:
    /// maximize is a focused-column toggle).
    ColumnWidth m_preMaximizeWidth;
    int m_preMaximizeColumnIdx = -1;
};

} // namespace PhosphorScrollEngine
