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
/// Any focus-driven or structural re-anchor (every reanchorAfterFocusChange
/// caller: the focus and move verbs, the inserts, an active column that
/// vanished), the Always policy's own re-centering, or either centering verb
/// re-attaches it and the policy takes the view back. A bystander's removal
/// that leaves focus where it was does not. One re-attach lives OUTSIDE this
/// class, in ScrollEngine::windowFocused: a compositor report naming the
/// window the strip already calls active reaches no re-anchor at all (it is
/// refused, or it moves a tile inside the active column), and the engine
/// clears the latch through `setViewDetached` there so the next layout pass
/// lets the policy answer. Detachment travels with the anchor through the
/// mode-round-trip stash and the persisted blob for the
/// same reason the anchor does: restoring the position while dropping the
/// detachment hands the view straight back to the policy that would move it.
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
    /// (moveActiveTile, expelWindowFromColumn, setActiveWindowHeight, consumeWindowIntoColumn, consumeOrExpel) all act
    /// on activeTileIdx, so a caller pairing this accessor with one of them
    /// could report a window the operation did not touch. Not reachable in
    /// production today, where the daemon models minimize as float and a
    /// minimized window is not a strip tile at all.
    ///
    /// The two height SIZING verbs are not on that list: both measure through
    /// activeTileCrossPx, which resolves activeTileIdx's tile from a relayout
    /// and answers -1 when it is minimized, so they refuse rather than act on
    /// a window this accessor did not name.
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
    /// Entry follows niri's rule (cyclePresetIndexByExtent): a forward press
    /// takes the narrowest preset WIDER than what the column renders at and
    /// wraps to the narrowest of all when nothing is wider; a backward press
    /// takes the widest preset narrower and wraps to the widest of all,
    /// so every entry is reachable in both directions even when the
    /// vocabulary was typed out of size order. Measured from the
    /// RENDERED extent, so a column held at its client minimum enters where
    /// it looks like it should.
    bool cycleActiveColumnPresetWidth(int delta, const ScrollLayoutParams& params);
    /// Adjust the active column's width by @p deltaPercent of the work area's
    /// MAIN extent (niri set-column-width "+10%"/"-10%"). Measured from the
    /// RENDERED extent rather than the bare intent, and floored at the greater
    /// of MinColumnWidthFraction and the column's own client minimum (the
    /// client half drops out while @c respectMinimumSize is off, the same way
    /// it does for the height verb), so a repeated shrink stops at the floor
    /// instead of burying an ever smaller
    /// intent under it. Refuses when the target lands on the current extent,
    /// so a column already rendering below the floor refuses a shrink rather
    /// than growing.
    bool adjustActiveColumnWidth(qreal deltaPercent, const ScrollLayoutParams& params);
    /// Full work-area MAIN extent, still tiled (niri maximize-column). Toggles
    /// back to the pre-maximize intent when already maximized.
    ///
    /// "Already maximized" is decided on RESOLVED PIXELS, not on the stored
    /// intent value, so every route to a full-width column un-maximizes —
    /// preset cycling, expand, equalize, a restore from disk. Three arms sit
    /// behind the simple description:
    ///
    ///  - The stored pre-maximize width is re-validated against the current
    ///    work area and axis. One captured on a wider output resolves clamped
    ///    back to full width, and restoring it would spend the slot and move
    ///    nothing, so that case falls through instead.
    ///  - With no usable stored width (maximized in an earlier session, or
    ///    another column's maximize took the single slot) it un-maximizes to
    ///    the context default width rather than dead-ending.
    ///  - If the default is ITSELF full width, it takes half the work area,
    ///    because otherwise that user could never un-maximize.
    ///
    /// Refuses (false) only for a column pinned at or past the work area by
    /// its client minimum, which is a dead end the verb cannot resolve; the
    /// definition documents why.
    bool toggleMaximizeActiveColumn(const ScrollLayoutParams& params);
    /// The same verb aimed at the column OWNING @p windowId rather than at the
    /// active one. Refuses (false) when this strip does not hold the window.
    ///
    /// The distinction is load-bearing for the compositor's maximize
    /// interception: that arrives for ONE named window (a titlebar click, a
    /// client's own request from a window that never took focus) and the
    /// active column is frequently a different one, so aiming at the active
    /// column would cancel the clicked window's maximize and resize somebody
    /// else's column.
    bool toggleMaximizeColumnForWindow(const QString& windowId, const ScrollLayoutParams& params);
    /// Toggle the active column's maximize-to-edges state (niri
    /// maximize-window-to-edges, generalized to the column): full raw work
    /// area on both axes, gap-free (Column::maximizedToEdges carries the
    /// contract). Pure declared state — the stored width intent is untouched,
    /// so the un-maximize arm is just "stop overriding". Refuses only on a
    /// degenerate work area (the sibling verbs' bail) and on a missing
    /// column.
    bool toggleMaximizeToEdgesActiveColumn(const ScrollLayoutParams& params);
    /// The same verb aimed at the column OWNING @p windowId, for the
    /// compositor's maximize interception — toggleMaximizeColumnForWindow's
    /// doc carries why the named form is load-bearing.
    bool toggleMaximizeToEdgesForWindow(const QString& windowId, const ScrollLayoutParams& params);
    /// Restore-path setter for the column owning @p windowId: a stash claim
    /// re-asserting the state it captured, not a user verb, so none of the
    /// toggle's addressing or feedback applies. No-op (false) when the strip
    /// does not hold the window or the state already matches.
    bool setMaximizedToEdgesForWindow(const QString& windowId, bool maximized);
    /// Grow the active column into the on-screen MAIN-axis space not taken by
    /// the FULLY visible columns at the current view (niri
    /// expand-column-to-available-width).
    ///
    /// Fully visible on fullyVisibleColumnIndices' terms, which is niri's
    /// accounting: a column clipped by either viewport edge is not counted, so
    /// the expansion reclaims its on-screen pixels and pushes it out of view.
    /// Refuses when the active column is not itself fully visible (there is no
    /// meaningful answer for a straddler), and when the column already fills
    /// the viewport.
    ///
    /// Two cases route through toggleMaximizeActiveColumn instead of writing
    /// Fixed pixels, both niri's: a centering policy that pins the active
    /// column to the middle (the column's position is not ours to choose, so
    /// "fill what is left" has no stable answer), and an active column that is
    /// the ONLY fully visible one (the result is full width, and going through
    /// the toggle leaves the user a way back out of it).
    ///
    /// The centering branch is taken BEFORE the straddling-active refusal, so
    /// under a centering policy a straddling active column maximizes rather
    /// than refusing: the policy already owns that column's position, which
    /// is the whole reason the branch exists.
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
    /// minimize-width; the empty-list arm is reachable only from a test or an
    /// embedder, since the engine never hands down an empty vocabulary).
    /// Refuses when already there, measured in RENDERED pixels so a column
    /// whose minimum size already pins it at or above the target refuses too.
    bool minimizeActiveColumnWidth(const ScrollLayoutParams& params);
    /// Every column back to @p defaultWidth and @p defaultDisplay, and every
    /// tile back to @p defaultHeight: the scrolling half of the Retile
    /// shortcut, whose other modes re-apply their layout the same way. All
    /// three defaults are arguments because the engine resolves them per
    /// screen:
    /// the display is not carried in params at all, and the width is
    /// std::nullopt when the context's default is "the client decides" (the
    /// engine's ClientDecides kind with no rule pinning a width), in which
    /// case every column keeps the width it has and only display and
    /// heights are reset. The height is std::nullopt on the same terms (the
    /// ClientDecides height kind with no rule pinning one), and then every
    /// tile keeps the height it has. A column the display write turns Tabbed
    /// still resolves to one extent, because the transition names an owner
    /// rather than flattening the tabs that do not own it.
    /// Returns true when any intent changed. Clears the single pre-maximize
    /// slot whenever a default width was supplied, since nothing is maximized
    /// once every column is back at that default — including when no column
    /// needed rewriting. Under a std::nullopt width a maximized column stays
    /// maximized and keeps its restore slot.
    bool resetToDefaults(const std::optional<ColumnWidth>& defaultWidth,
                         const std::optional<WindowHeight>& defaultHeight, ColumnDisplay defaultDisplay,
                         const ScrollLayoutParams& params);
    /// Set the active tile's height intent, verbatim. The height twin of
    /// setActiveColumnWidth: the direct write under the cycle/adjust verbs,
    /// the restore and open paths' setWindowHeightIntent, and the absolute
    /// set-window-height verb. Callers own validation. The height is the
    /// tile's extent ACROSS the strip.
    bool setActiveWindowHeight(const WindowHeight& height);
    /// Cycle the active tile through the preset height list. @p delta -1/+1.
    /// Entry follows the same niri rule the width cycle uses (nearest entry
    /// TALLER going forward, nearest shorter going back, wrapping at each
    /// end), measured off a fresh relayout so an AUTO tile enters the cycle
    /// at what it currently renders rather than always at the first entry.
    /// A TABBED column cycles too, in two steps: the press first CLAIMS
    /// extent ownership for the shown tab, and the OWNER's intent then sizes
    /// the whole column (tabbedColumnCrossPx), so the press moves the
    /// column's cross extent. The measurement then reads the column rather than the tile,
    /// since the indicator's reservation sits between the two.
    bool cycleActiveWindowPresetHeight(int delta, const ScrollLayoutParams& params);
    /// Adjust the active tile's height by @p deltaPercent of the work area's
    /// CROSS extent. The current height is read off a fresh relayout, since an
    /// Auto tile only gets a pixel value from the whole column distribution,
    /// and the result is floored the way adjustActiveColumnWidth floors its
    /// width (MinWindowHeightFraction, raised to the client minimum while
    /// @c respectMinimumSize is on). A TABBED column adjusts too, in the same
    /// column-extent space the cycle measures in.
    bool adjustActiveWindowHeight(qreal deltaPercent, const ScrollLayoutParams& params);
    /// Toggle the active tile between filling its column's cross budget and
    /// the even auto-split. The height twin of toggleMaximizeActiveColumn,
    /// with ONE deliberate difference: there is no pre-maximize slot, so the
    /// un-maximize arm writes Auto rather than a remembered height. A slot
    /// would have to be re-indexed by every structural op the way
    /// m_preMaximizeColumnIdx is, and the height family already treats Auto
    /// as "the column decides", which is what un-maximizing a window inside a
    /// STACK means. For a tile that is alone in its column Auto and the full
    /// budget render identically, so the un-maximize press changes the stored
    /// intent without moving anything — the toggle still answers true, because
    /// the intent decides how the tile shares the column the moment a sibling
    /// arrives.
    ///
    /// The maximized test reads the stored intent as well as the rendered
    /// extent: siblings held up by their client minimums can stop the tile
    /// reaching the budget on screen, and a pixel-only test would then
    /// maximize forever and never come back. Both non-Auto spellings count —
    /// a Preset whose fraction RESOLVES to the budget is maximized just as a
    /// Fixed at the budget is, or the top preset would be silently rewritten
    /// to a Fixed of the same extent and lose its anchor. A TABBED column
    /// toggles in the column's own space, the cycle and adjust verbs' rule.
    ///
    /// KNOWN GAP, recorded because the fix is bigger than the verb: the
    /// Fixed(budget) written here is the only memory that the tile is
    /// maximized, and reconcileWindowSize overwrites it with whatever the
    /// client acks. A client with size increments (a terminal) acks a
    /// quantised height short of the applied one, so the intent stops reading
    /// as maximized and the un-maximize arm is unreachable for the life of
    /// that window; Equalize Window Heights is the way back to Auto. Refusing
    /// the ack in reconcile is NOT the fix — it makes onWindowResized's
    /// refused-ack branch schedule a retile that re-applies the same rect the
    /// client quantises again, which is a self-driving loop. The real fix is a
    /// per-tile latch that travels with the tile the way the width axis uses
    /// m_preMaximizeColumnIdx, which is why that slot exists there.
    bool toggleMaximizeActiveWindowHeight(const ScrollLayoutParams& params);
    /// The active tile at its shortest: the smallest preset height, or
    /// MinWindowHeightFraction of the work area's cross extent when the preset
    /// list is empty. The height twin of minimizeActiveColumnWidth, and the
    /// empty-list arm is reachable only from a test or an embedder for the
    /// same reason. Height has no Proportion spelling, so that fallback is
    /// written as Fixed pixels rather than as a fraction.
    /// Refuses when the tile already renders there.
    bool minimizeActiveWindowHeight(const ScrollLayoutParams& params);
    /// Grow the active tile into the empty cross space left in its column.
    /// The height twin of expandActiveColumnToAvailableWidth. Empty space
    /// inside a column exists only when no tile is Auto: an Auto tile absorbs
    /// the leftover by weight in the relayout, so with one present the
    /// measurement below finds nothing to claim and the verb refuses.
    ///
    /// One case routes through toggleMaximizeActiveWindowHeight rather than
    /// writing Fixed pixels, the expand-column verb's rule: the active tile is
    /// the only visible one in its column (or the column is tabbed, where
    /// every tab is committed at the column's own rect and there is no
    /// leftover WITHIN it), so the result is the whole budget and going
    /// through the toggle leaves a way back out.
    bool expandActiveWindowToAvailableHeight(const ScrollLayoutParams& params);
    /// Back to the even auto-split for EVERY tile in the active column: the
    /// height twin of equalizeVisibleColumnWidths. Auto IS the even share, so
    /// unlike the width verb this one names the split rather than computing
    /// it, and it needs no params.
    bool equalizeActiveColumnHeights();
    /// Record the size a client/user resize actually settled on. The acked
    /// MAIN extent becomes the column's Fixed width intent only when
    /// @p mainChanged (the engine compares against the last applied rect) — a
    /// resize purely ACROSS the strip must not pin a Proportion/Preset column
    /// to pixels. The acked CROSS extent becomes the tile's Fixed height
    /// intent symmetrically, only when @p crossChanged; a lone tile is
    /// included, because relayout honours a solo tile's Fixed height (niri
    /// parity). On a TABBED column that cross intent sizes the COLUMN rather
    /// than the window, so the ack is lifted by @c tabbedCrossReservationPx
    /// on the way in, the same conversion the two height verbs make. Other
    /// columns are untouched.
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
    /// active one). The mode-round-trip restore path re-applies stashed
    /// heights through this, the open path commits a client-decided height
    /// and a migration re-states the height the window carried over, and the
    /// per-window open-height rule resolves through it too. Returns false for
    /// an unknown window or an unchanged intent.
    bool setWindowHeightIntent(const QString& windowId, const WindowHeight& height);

    /// The height intent @p windowId's tile carries, or a default-constructed
    /// (Auto) one for an unknown window. The reader half of
    /// setWindowHeightIntent: every caller that hands a window's height across
    /// a structural boundary — the cross-output move, the context migration —
    /// has to read it before the take destroys the tile, and each had grown
    /// its own column-then-tile walk to do so.
    WindowHeight windowHeightIntent(const QString& windowId) const;

    /// Make @p windowId's tab the one whose height decides its TABBED column's
    /// cross extent. The restore seam for that ownership: the mode-round-trip
    /// stash and the persisted blob both carry it, because the tabs that do
    /// not own it keep their own heights and a fallback scan would hand the
    /// column to a different tab than held it. No-op for an unknown window or
    /// a Normal column. Returns whether the owner moved.
    bool setTabbedHeightOwner(const QString& windowId);

    /// Strip indices of the columns currently intersecting the viewport, in
    /// strip order — a viewport-intersection helper, NOT the zone-number
    /// space. Zone numbers are per-TILE and live one layer up, in
    /// ScrollEngine::visibleTiles; rotateVisibleColumns is this helper's
    /// only consumer.
    QVector<int> visibleColumnIndices(const ScrollLayoutParams& params) const;

    /// Strip indices of the columns lying ENTIRELY inside the viewport, in
    /// strip order — the stricter twin of visibleColumnIndices, and the one
    /// the width-distribution verbs and centerVisibleColumns walk. A column
    /// clipped by either edge is excluded, which is what lets
    /// equalizeVisibleColumnWidths refuse to drag a straddler into the split,
    /// lets centerVisibleColumns leave it out of the span, and lets
    /// expandActiveColumnToAvailableWidth reclaim a straddler's pixels the
    /// way niri does. Zero-extent (fully minimized) columns carry no strip
    /// position and are skipped, matching stripExtentPx.
    QVector<int> fullyVisibleColumnIndices(const ScrollLayoutParams& params) const;

    /// Whether the layout policy pins the ACTIVE column to the middle of the
    /// viewport, so its on-screen position is not the strip's to choose. The
    /// one spelling of the test the anchor math (keepOrRecenterAnchor,
    /// reanchorAfterFocusChange, updateViewForFocus), the removal re-focus,
    /// and the expand verb all ask.
    bool isCenteringActiveColumn(const ScrollLayoutParams& params) const;

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
    ///
    /// Clearing it standalone is the engine-side re-attach the class doc
    /// names (ScrollEngine::windowFocused): it hands the view to the policy
    /// while leaving the anchor for the next updateViewForFocus to derive,
    /// which is the one thing a re-anchor here could not do without a focus
    /// change to derive from.
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
    /// Drag-drop re-anchor (ScrollEngine::commitDragInsertPreview): land the
    /// just-dropped ACTIVE column at the on-screen position the drop
    /// indicator promised — its strip position minus @p oldViewOffset, the
    /// view offset captured BEFORE the commit's insert — then move the view
    /// the MINIMUM that makes the column fully visible (the Never fit),
    /// regardless of the configured centering policy. The policy's OnOverflow
    /// arm centers a column too wide to share the viewport with either
    /// neighbour, which after a drop hides every other window and reads as
    /// the drop having flown away; the minimal fit keeps whatever neighbour
    /// still fits on screen beside it. A maximized-to-edges column keeps its
    /// one correct position (focusAnchorFor's reason). SETS the detach
    /// latch: the drop owns the view the way a pan does, or the applyLayout
    /// the commit runs next would hand it straight back to the centering
    /// policy; the next focus change re-attaches as usual.
    void reanchorForDropCommit(int oldViewOffset, const ScrollLayoutParams& params);
    /// Drag-begin settle (ScrollEngine::beginDragInsertPreview): after the
    /// detach take shortens the strip, pull a view that now hangs past the
    /// TRAILING strip edge back over real columns. removeWindowInternal
    /// deliberately leaves that dead space on an ordinary close; during a
    /// drag hold it is what the user must aim a drop at, so the begin settle
    /// reclaims it. Trailing edge ONLY: a leading overhang is how the
    /// centering mutators deliberately express a centered short strip, and
    /// a view already in range is untouched. The detach latch is left alone
    /// — a pan still owns a clamped view.
    void clampViewIntoStrip(const ScrollLayoutParams& params);
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

    /// How far the view WOULD move, in pixels along the strip, if column
    /// @p columnIndex became the active one right now. A pure query: it runs
    /// the same centering policy reanchorAfterFocusChange runs and compares
    /// the resulting view offset with the current one, mutating nothing.
    ///
    /// This is niri's `focus-follows-mouse max-scroll-amount` predicate:
    /// focus-follows-mouse asks it before activating a window so a pointer
    /// grazing a column that is mostly off screen does not yank the whole
    /// strip. Answers 0 for the active column, for an out-of-range index and
    /// for a degenerate work area, all of which mean "focusing this costs no
    /// scroll" and so can never be refused by a cap.
    int predictedFocusScrollPx(int columnIndex, const ScrollLayoutParams& params) const;

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
    // scrollstrip_sizing.cpp
    /// The active tile's CROSS extent as it currently RENDERS, read off a
    /// fresh relayout. An Auto tile only gets a pixel value from the whole
    /// column distribution (floors, budget rebalance), so the relayout IS the
    /// resolution and no targeted per-tile helper can replace it. Answers -1
    /// when there is no active tile or it resolved to nothing (a minimized
    /// tile is dropped from the relayout entirely). Shortcut-rate path, not
    /// per-frame: the height verbs call it once per press, and
    /// expandActiveWindowToAvailableHeight relayouts a second time to walk the
    /// column's resolved tiles.
    int activeTileCrossPx(const ScrollLayoutParams& params) const;
    /// The cross-axis budget the active column's tiles divide: the work area's
    /// cross extent net of the inner gaps BETWEEN its visible tiles, floored
    /// at one pixel per tile. This is relayout's @c availH for that column, so
    /// a height a verb clamps to it is a height that will actually render —
    /// EXCEPT under @c maximizedToEdges, where relayout resolves the column
    /// against the raw work area with no inner gap and ignores the stored
    /// intents entirely, so a share measured there is not comparable with this
    /// gapped budget. expandActiveWindowToAvailableHeight drops the override
    /// before it measures for exactly that reason;
    /// toggleMaximizeActiveWindowHeight does NOT, and reads a raw-area share
    /// against this budget. That is safe either way: a LONE visible tile
    /// measures the whole raw extent and so reads as already maximized, and in
    /// a stack the raw share falls BELOW this budget, so the press reads as
    /// not maximized and writes Fixed(budget) — the right maximize target
    /// regardless. A
    /// TABBED column stacks nothing and spends no inner gaps, so its budget is
    /// the whole cross extent (tabbedColumnCrossPx caps its owner there).
    /// Answers -1 when there is no active column or the work area is
    /// degenerate.
    int activeColumnCrossBudgetPx(const ScrollLayoutParams& params) const;
    /// The shortest the active tile may be written, in the space the height
    /// verbs WRITE in (the column's, so a tabbed indicator's cross reservation
    /// is already added). MinWindowHeightFraction of the work area's cross
    /// extent, raised to the client minimum while @c respectMinimumSize is on
    /// — and while tabbed, to the tallest minimum in the whole tab set, since
    /// every visible tab is committed at that one rect. Capped at the work
    /// area's cross extent so a client minimum larger than the screen cannot
    /// invert a qBound. Answers -1 when there is no active tile or the work
    /// area is degenerate.
    int activeWindowHeightFloorPx(const ScrollLayoutParams& params) const;
    /// Make @p tileIdx the tab whose height decides @p c's cross extent, when
    /// @p c is tabbed and @p incoming is a height worth owning it for. Writes
    /// only Column::heightOwnerId — no tile's height is touched, so the tabs
    /// that lose the claim keep their intents and untabbing restores the
    /// stack.
    ///
    /// No-op unless @p c is tabbed AND @p incoming is non-Auto. A write of
    /// Auto means "I am not sizing this tab", which is not a bid for the
    /// column: letting it claim would hand the whole work area to a tab that
    /// asked for nothing and drop the extent a sibling had been given. The
    /// DISPLAY transition may still name an Auto owner (applyColumnDisplay
    /// does, for the tab on show) — that is a deliberate choice of owner
    /// rather than a side effect of writing a height.
    ///
    /// Also a no-op when @p tileIdx already owns it. Returns whether the owner
    /// moved, which is a layout change in its own right: the column resolves
    /// through the owner, so moving the pointer moves the column.
    static bool claimTabbedHeightOwnership(Column& c, int tileIdx, const WindowHeight& incoming);
    /// Set @p c's display, maintaining the extent ownership across the
    /// transition: entering Tabbed hands it to the tab on show, leaving Tabbed
    /// drops it. The ONE way display is written for an existing column, so the
    /// invariant cannot be skipped at a call site. Returns whether it changed.
    static bool applyColumnDisplay(Column& c, ColumnDisplay display);

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
    /// Pixel CROSS extent of TABBED column @p c: the height intent of the
    /// tab that OWNS the extent (Column::heightOwnerId), not the tab on show,
    /// resolved exactly the way the stack branch resolves a single
    /// tile's Fixed/Preset height, so an entry of the preset vocabulary lands
    /// on the same pixels whichever display the column is in. Auto means the
    /// whole work area, which is what a tabbed column used to be pinned at.
    /// Raised to the tallest visible tab's own cross minimum plus
    /// @c tabbedCrossReservationPx while minimum sizes are respected: every
    /// tab is committed at this column's content rect, including the hidden
    /// ones, so the floor is the whole set's and not just the shown tab's.
    static int tabbedColumnCrossPx(const Column& c, const ScrollLayoutParams& params);
    /// Pixels the tab indicator takes out of column @p c ACROSS the strip, the
    /// cross-axis twin of the main-axis reservation columnMinExtentPx applies.
    /// 0 for a column that is not tabbed, and whenever the indicator's
    /// thickness eats the MAIN axis instead (which of the two it eats depends
    /// on the strip's axis, see TabIndicatorParams::reservedThickness).
    static int tabbedCrossReservationPx(const Column& c, const ScrollLayoutParams& params);
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
    /// clampedAnchor for an ARBITRARY column rather than the active one, so
    /// the focus-policy maths can be run predictively for a column that is
    /// not focused yet. clampedAnchor is the @p columnIndex == active case.
    int clampedAnchorFor(int columnIndex, int anchor, const ScrollLayoutParams& params) const;
    /// The anchor the centering policy lands on when @p targetIdx becomes
    /// active, coming from @p prevIdx with the view at @p oldViewOffset. The
    /// whole policy (Always / lone-column / OnOverflow / the Never fit) lives
    /// here and nowhere else: reanchorAfterFocusChange applies the answer and
    /// predictedFocusScrollPx merely measures it, so the two can never drift.
    int focusAnchorFor(int targetIdx, int prevIdx, int oldViewOffset, const ScrollLayoutParams& params) const;
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
    Column* activeColumnMutable();
    /// Shared core of the two maximize-toggle entry points. Out-of-range
    /// @p columnIndex (including columnOfWindow's -1 miss) refuses.
    bool toggleMaximizeColumnAt(int columnIndex, const ScrollLayoutParams& params);
    /// Shared core of the two maximize-to-edges entry points; the same
    /// refusal shape as toggleMaximizeColumnAt minus the pinned-by-minimum
    /// arm (the flag is declared state, never measured).
    bool toggleMaximizeToEdgesAt(int columnIndex, const ScrollLayoutParams& params);
    Tile* activeTileMutable();
    void clampActiveIndices();

    QVector<Column> m_columns;
    int m_activeColumnIdx = -1;
    /// Active column's leading edge position within the viewport (see class doc).
    int m_viewAnchor = 0;
    /// Whether an explicit pan owns the view instead of the centering policy
    /// (see the class doc's View detachment section).
    bool m_viewDetached = false;
    /// Pre-maximize width intent for the maximize toggle, and the index of the
    /// column it belongs to.
    ///
    /// ONE SLOT for the whole strip, deliberately, and no longer only a
    /// focused-column toggle: toggleMaximizeColumnForWindow writes it for an
    /// arbitrary column, so a second maximize anywhere discards the first
    /// column's stored width. That column then un-maximizes to the context
    /// default rather than to what it had. The toggle handles it — its
    /// no-usable-slot arm exists for exactly this, and for the two other ways
    /// the slot goes missing (a stash round trip and a restart, neither of
    /// which carries it) — so the degradation is defined rather than a leak.
    ///
    /// Making it per-column would be an improvement and is a design change,
    /// not a bug fix: the index is maintained across every insert, remove and
    /// move in scrollstrip_structure.cpp, and a per-column slot would delete
    /// all of that bookkeeping along with this pair.
    ColumnWidth m_preMaximizeWidth;
    int m_preMaximizeColumnIdx = -1;
};

} // namespace PhosphorScrollEngine
