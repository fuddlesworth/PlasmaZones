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
/// ## View detachment
///
/// Because the anchor is active-relative, the centering policy owns the view:
/// `updateViewForFocus` re-derives it at the top of every applyLayout, so a
/// view the POLICY did not choose cannot survive a layout pass. That is right
/// for every mutator except one. `scrollViewBy` moves the view because the user
/// pointed somewhere, not because focus, structure or policy changed, and a pan
/// the next layout pass undoes is not a pan at all.
///
/// So a successful `scrollViewBy` DETACHES the view, and `updateViewForFocus`
/// then leaves the anchor entirely alone — not even a re-clamp, for the reason
/// that function documents. `equalizeVisibleColumnWidths` detaches too: it
/// positions the group edge to edge on purpose, and a policy that re-centered
/// the active column afterwards would hand a second press a different group.
/// The next focus change, or either centering verb,
/// re-attaches it and the policy takes the view back. Detachment travels with the anchor through the
/// mode-round-trip stash and the persisted blob for the same reason the anchor
/// does: restoring the position while dropping the detachment hands the view
/// straight back to the policy that would move it.
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
    /// Give every FULLY visible column an equal share of the work area's MAIN
    /// extent (Karousel equalize; niri has no equivalent). Fully visible on
    /// the same terms as centerVisibleColumns: a column clipped by either
    /// viewport edge is what the split must not be dragged by. Written as
    /// Fixed pixels like adjustActiveColumnWidth, and the remainder of the
    /// division goes to the LAST column so the group still tiles the
    /// viewport edge to edge. A column whose minimum size (columnMinExtentPx)
    /// exceeds its share keeps that minimum and the rest share what is left.
    /// The group's lead column lands AT the viewport's lead edge: the anchor
    /// is re-derived after the rewrite so a column ahead of the active one
    /// growing does not slide the group under the active column, and a
    /// lead-edge straddler is pushed fully out of view. The view is then
    /// DETACHED like a pan's (see the class doc), so the centering policy
    /// does not undo the edge-to-edge position on the next pass and a second
    /// press finds nothing to change. Refuses when fewer than two columns
    /// are fully visible, when the active column is not one of them, when
    /// the minimums alone fill the viewport, or when every extent is already
    /// what it would become.
    bool equalizeVisibleColumnWidths(const ScrollLayoutParams& params);
    /// The active column at its narrowest: the smallest preset, or
    /// MinColumnWidthFraction when the preset list is empty (Karousel
    /// minimize-width). Refuses when already there.
    bool minimizeActiveColumnWidth(const ScrollLayoutParams& params);
    /// Every column back to @p defaultWidth and @p defaultDisplay, and every
    /// tile back to the even auto-split: the scrolling half of the Retile
    /// shortcut, whose other modes re-apply their layout the same way. Both
    /// defaults are arguments because the engine resolves them per screen:
    /// the display is not carried in params at all, and the width is
    /// std::nullopt when the context's default is "the client decides" (the
    /// engine's ClientDecides kind with no rule pinning a width), in which
    /// case every column keeps the width it has and only display and
    /// heights are reset. Returns true when any intent changed. Clears the
    /// single pre-maximize slot, since no column is maximized afterwards.
    bool resetToDefaults(const std::optional<ColumnWidth>& defaultWidth, ColumnDisplay defaultDisplay,
                         const ScrollLayoutParams& params);
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
    /// Whether an explicit pan owns the view instead of the centering policy
    /// (see the class doc). Exposed for the stash and the persisted blob,
    /// which must carry it beside the anchor.
    bool viewDetached() const
    {
        return m_viewDetached;
    }
    /// The other half of restoreViewAnchor: re-assert a captured detachment.
    /// Only meaningful alongside the anchor it was captured with, so a caller
    /// that drops that anchor (the stash's axis-mismatch arm) must drop this
    /// too, or the view stays pinned wherever the focus restore left it.
    void setViewDetached(bool detached)
    {
        m_viewDetached = detached;
    }
    /// Restore a previously captured view anchor, RAW: a centered anchor
    /// implies an out-of-range derived viewOffset by design (the same shape
    /// centerActiveColumn stores), so no clamp is applied here — later
    /// structural inserts re-clamp when the strip cannot honour the view.
    /// The stash-restore path re-applies the anchor AFTER re-focusing the
    /// stashed active window, overriding the focus change's own
    /// centering-policy reanchor with the user's actual view. It does NOT
    /// touch the detachment latch: that focus call has just cleared it, and
    /// re-asserting it is the caller's job through setViewDetached, so both
    /// halves of a captured view are restored by the same code.
    /// @p params is currently UNUSED (the raw restore needs no layout maths)
    /// but stays in the exported signature: every sibling anchor mutator
    /// takes it, and dropping it is an ABI break for no gain.
    void restoreViewAnchor(int anchor, const ScrollLayoutParams& params);
    /// Re-apply the centering policy to the current active column (settings
    /// change / work-area change) using the current anchor as the "no
    /// scroll" baseline. A DETACHED view is left entirely alone, not even
    /// re-clamped (see the class doc and the body's comment for why).
    void updateViewForFocus(const ScrollLayoutParams& params);
    /// Center the active column in the view (niri center-column).
    /// Returns true when the anchor actually moved.
    bool centerActiveColumn(const ScrollLayoutParams& params);
    /// Center the span of FULLY visible columns in the view (niri
    /// center-visible-columns). Falls back to centerActiveColumn when no
    /// column is fully visible. Returns true when the anchor actually moved.
    bool centerVisibleColumns(const ScrollLayoutParams& params);
    /// Scroll the view @p delta pixels forward ALONG THE STRIP (negative
    /// scrolls back towards its start) WITHOUT changing focus, clamped to the
    /// strip's ends. Forward is rightward on a horizontal strip and downward
    /// on a vertical one. The anchor is stored relative to the active column,
    /// so a forward view move shrinks it by the same amount. Returns true when
    /// the anchor actually moved — a caller sitting at either end gets false
    /// and can stop. A move DETACHES the view from the centering policy (see
    /// the class doc); a refusal leaves the latch as it found it. This is the
    /// only mutator that moves the view without a focus, structure or policy
    /// change behind it; the drag-insert edge auto-scroll and the engine's
    /// view-scroll verb (behind the wheel and the keyboard page pair) are its
    /// callers.
    bool scrollViewBy(int delta, const ScrollLayoutParams& params);

    // ── Relayout ─────────────────────────────────────────────────────────────
    /// Resolve every non-minimized tile's absolute pixel rect against
    /// @p params. Pure function of the current model state; does not mutate.
    ResolvedStrip relayout(const ScrollLayoutParams& params) const;

    /// True when the whole strip already fits the viewport, i.e. there is
    /// nothing off screen to scroll to. A degenerate work area counts as
    /// fitting, so a caller cannot scroll a screen that is going away.
    ///
    /// Exists so a per-frame caller can ask the question without paying for a
    /// full relayout: relayout() allocates a ResolvedColumn (with a nested
    /// tile vector) per column, and the edge auto-scroll's disarm gate needs
    /// only a predicate.
    bool stripFitsViewport(const ScrollLayoutParams& params) const;

    /// stripFitsViewport AND the derived view offset is settled inside
    /// [0, stripExtent - viewport] — i.e. genuinely nothing to reveal. The
    /// distinction matters because the centering mutators deliberately store
    /// an anchor whose derived viewOffset is out of range (their comments say
    /// so), which can leave a column hanging off one edge even though the
    /// strip FITS; the edge auto-scroll's clamped-delta walk is the one
    /// motion that brings it back, so its disarm gate must ask this, not the
    /// fits-only question. Degenerate work areas and empty strips answer
    /// true, same fail-closed reading as stripFitsViewport.
    bool stripSettledInViewport(const ScrollLayoutParams& params) const;

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
    /// The MAIN-extent floor columnExtentPx raises column @p c to under
    /// @p params: its visible tiles' min-MAIN plus the tab-indicator
    /// reservation when the indicator eats the main axis. 0 when minimum
    /// sizes are not respected. Shared with equalizeVisibleColumnWidths so a
    /// share the floor would overrule is never written as if it could render.
    int columnMinExtentPx(const Column& c, const ScrollLayoutParams& params) const;
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
    /// column re-centers, in which case RECENTER wins and the view is
    /// re-attached (the policy visibly took it, so a detach latch left set
    /// would only stop the next pass from re-deriving what it just chose).
    int keepOrRecenterAnchor(int oldViewOffset, const ScrollLayoutParams& params);
    /// Apply the center-focused-column policy after the active column moved
    /// from @p prevIdx at @p oldViewOffset (strip coords) to the current active.
    /// Clears the detach latch: this is the chokepoint the focus verbs and
    /// the structural inserts pass through, so "a focus change re-attaches"
    /// is enforced here. A writer of m_activeColumnIdx that does NOT route
    /// through it must clear the latch itself (noteActiveColumnChanged).
    void reanchorAfterFocusChange(int prevIdx, int oldViewOffset, const ScrollLayoutParams& params);
    /// The re-attach half of a focus change for the writers that bypass
    /// reanchorAfterFocusChange: the strip emptying, the first insert into an
    /// empty strip, and a removal taking the active column with it. The
    /// class doc promises a focus change ends a pan, and these are focus
    /// changes.
    void noteActiveColumnChanged()
    {
        m_viewDetached = false;
    }
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
    /// Whether an explicit pan owns the view instead of the centering policy
    /// (see the class doc's View detachment section).
    bool m_viewDetached = false;
    /// Pre-maximize width intent for the maximize toggle (single slot:
    /// maximize is a focused-column toggle).
    ColumnWidth m_preMaximizeWidth;
    int m_preMaximizeColumnIdx = -1;
};

} // namespace PhosphorScrollEngine
