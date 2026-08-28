// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// FILE-SIZE EXCEPTION (sanctioned): one engine, one installed public header.
// This is the scroll engine's complete public API surface; the value-type
// hoists (ScrollEngineTypes.h, ScrollStashTypes.h) already carried out the
// split-by-concern work, and splitting the remaining single class would
// scatter one interface across headers for line count alone. Same rationale
// as its peers (SnapEngine.h, AutotileEngine.h, LayoutRegistry.h).

#pragma once

#include <phosphorscrollengine_export.h>

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/PerScreenStates.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/ScreenContextTracker.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorScrollEngine/IScrollSettings.h>
#include <PhosphorScrollEngine/ScrollEngineTypes.h>
#include <PhosphorScrollEngine/ScrollStashTypes.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollTypes.h>
#include <PhosphorScrollEngine/StripAxis.h>

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <functional>
#include <optional>

namespace PhosphorEngine {
class WindowRegistry;
class IWindowTrackingService;
}

namespace PhosphorScreens {
class ScreenManager;
}

namespace PhosphorScrollEngine {

/**
 * @brief niri-style scrolling placement engine.
 *
 * Implements IPlacementEngine for screens assigned to Scrolling mode. Each
 * (screen, virtual desktop, activity) context owns one ScrollState wrapping a
 * ScrollStrip — an ordered list of columns on an unbounded strip, running
 * whichever way that screen's axis resolves, viewed through the screen's work
 * area. The defining invariant: OPENING A
 * WINDOW INSERTS A NEW COLUMN AND RESIZES NOTHING; only the viewport scrolls.
 *
 * Geometry leaves the engine as the same windowsTiled JSON contract the
 * autotile engine emits, relayed by the shared tiling adaptor to the KWin
 * effect's tile-request path (staggered apply, supersession epochs, maximize
 * reset). Columns scrolled out of the viewport are parked with real geometry
 * just below the union of all outputs — the one place no monitor topology
 * can occupy, and never at extreme coordinates — so input hit-testing stays
 * sane; the enter/leave animation origin comes from the tile request's
 * scrollEdge field, not from where the park happens to sit. A column
 * STRADDLING a screen edge is committed CLAMPED at that edge (both MAIN
 * edges, plus the cross-axis overflow edge; a cross-axis UNDERFLOW cannot
 * occur, since a column starts at the work area's cross origin by
 * construction) unless the crop-straddlers setting keeps the true rect for the
 * effect to crop; a remainder below the peek floor parks instead. Hidden
 * tiles of a tabbed column park the same way (a hidden tab must not sit
 * under the active tile stealing clicks).
 *
 * Floating reuses the shared PlasmaZones float model: a floated window
 * leaves the strip (its column closes up) and the engine remembers the
 * column index so unfloat / unminimize restores the slot. The effect's
 * minimize machinery reports minimize as a float toggle, so slot memory
 * covers minimize for free.
 *
 * @see PhosphorEngine::IPlacementEngine, ScrollStrip, ScrollState
 */
class PHOSPHORSCROLLENGINE_EXPORT ScrollEngine : public PhosphorEngine::PlacementEngineBase
{
    Q_OBJECT

public:
    explicit ScrollEngine(PhosphorEngine::IWindowTrackingService* windowTracker,
                          PhosphorScreens::ScreenManager* screenManager, QObject* parent = nullptr);
    ~ScrollEngine() override;

    void setWindowRegistry(QObject* registry) override;

    // ═══════════════════════════════════════════════════════════════════════
    // Screen ownership (derived from layout assignments, daemon-driven)
    // ═══════════════════════════════════════════════════════════════════════

    bool isActiveOnScreen(const QString& screenId) const override;
    bool isEnabled() const noexcept override;
    QSet<QString> activeScreens() const override
    {
        return m_scrollingScreens;
    }
    /// Set which screens use scrolling. Retiles newly-added screens; tears
    /// down (and releases the windows of) removed ones. Mirrors
    /// AutotileEngine::setAutotileScreens, including the identical-set
    /// re-emit contract on desktop switches (the context-switch flag is
    /// consumed on both branches and PROPAGATED on both: the identical-set
    /// re-emit fires only on a real switch, and the changed-set emit
    /// carries the flag through). One deliberate divergence: the
    /// identical-set branch RETILES every screen even without a switch —
    /// the daemon's per-pass override push depends on it (scrolling.cpp,
    /// LOAD-BEARING).
    void setActiveScreens(const QSet<QString>& screens) override;
    /// NOTE: activeScreen() is deliberately NOT overridden. m_activeScreen
    /// is this engine's private "which output does a hint-less shortcut act
    /// on" memory, consumed only by resolveOperationScreen; nothing outside
    /// asks for it, and exposing an unguarded reader invited a caller to
    /// treat a screen this engine may have stopped managing as current.
    void setActiveScreenHint(const QString& screenId) override;

    // ═══════════════════════════════════════════════════════════════════════
    // Window lifecycle
    // ═══════════════════════════════════════════════════════════════════════

    using IPlacementEngine::windowOpened;
    void windowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight) override;
    /// Cross-screen session reclaim (see IPlacementEngine for the base
    /// contract). This implementation: first-observation gate by ScrollState
    /// MEMBERSHIP (not the raw reverse-map key); decides via the store's
    /// live-instance-excluding peekForReclaim over the registry-aware appId;
    /// requires the recorded home in the LIVE scrolling set AND the record's
    /// (desktop, activity) to match the home screen's current key (sticky and
    /// unknown-context sentinel records stay eligible — see
    /// recordContextMatchesLive); and
    /// returns the REAL adoption outcome verified by membership after the
    /// windowOpened re-entry. Peek-not-take: consumption stays with the open
    /// path's own restore machinery (strip stash claim, takeForReopen).
    bool claimCrossScreenReopen(const QString& windowId, const QString& openingScreenId, int minWidth,
                                int minHeight) override;
    QString heldScreenForWindow(const QString& windowId) const override;
    void beginArrivalBurst() override;
    void endArrivalBurst() override;
    void windowClosed(const QString& windowId) override;
    void windowFocused(const QString& windowId, const QString& screenId) override;
    void windowMinSizeUpdated(const QString& windowId, int minWidth, int minHeight) override;
    /// The window's client-reported minimum, from its strip tile, falling
    /// back to the FloatRestore entry while it is floated (or minimized —
    /// the effect reports minimize as a float). The fallback matters at the
    /// cross-engine handoff, which queries this whatever state the window
    /// is in: answering 0x0 hands the receiving engine an unclamped window.
    QSize windowMinimumSize(const QString& windowId) const override;
    void onWindowResized(const QString& rawWindowId, const QRect& oldFrame, const QRect& newFrame,
                         const QString& screenId) override;

    // ═══════════════════════════════════════════════════════════════════════
    // Float management
    // ═══════════════════════════════════════════════════════════════════════

    void toggleWindowFloat(const QString& windowId, const QString& screenId) override;
    void setWindowFloat(const QString& windowId, bool shouldFloat, const QString& screenId = QString()) override;
    /// Authoritative per-window scroll float state (the scroll half of the
    /// per-engine float contract; daemon WTS resolver consults this for
    /// Scrolling-mode windows).
    bool isWindowFloatingInScroll(const QString& windowId) const;
    QStringList allFloatingWindows() const;
    bool isModeSpecificFloated(const QString& windowId) const override;
    void markModeSpecificFloated(const QString& windowId) override;
    void clearModeSpecificFloatMarker(const QString& windowId) override;

    // ═══════════════════════════════════════════════════════════════════════
    // Navigation (IPlacementEngine user intents)
    // ═══════════════════════════════════════════════════════════════════════

    void focusInDirection(const QString& direction, const PhosphorEngine::NavigationContext& ctx) override;
    void moveFocusedInDirection(const QString& direction, const PhosphorEngine::NavigationContext& ctx) override;
    void swapFocusedInDirection(const QString& direction, const PhosphorEngine::NavigationContext& ctx) override;
    void moveFocusedToPosition(int position, const PhosphorEngine::NavigationContext& ctx) override;
    /// Zone spanning has no strip analogue; reports a "not_supported" span
    /// OSD like AutotileEngine so the shortcut is not a silent press.
    void spanFocusedInDirection(const QString& direction, const PhosphorEngine::NavigationContext& ctx) override;
    void rotateWindows(bool clockwise, const PhosphorEngine::NavigationContext& ctx) override;
    void reapplyLayout(const PhosphorEngine::NavigationContext& ctx) override;
    void snapAllWindows(const PhosphorEngine::NavigationContext& ctx) override;
    void cycleFocus(bool forward, const PhosphorEngine::NavigationContext& ctx) override;
    void pushToEmptyZone(const PhosphorEngine::NavigationContext& ctx) override;
    void restoreFocusedWindow(const PhosphorEngine::NavigationContext& ctx) override;
    void toggleFocusedFloat(const PhosphorEngine::NavigationContext& ctx) override;
    /// niri switch-focus-between-floating-and-tiling (IPlacementEngine
    /// override — the daemon reaches it by virtual dispatch, not the
    /// scroll-specific mode-check-and-cast route below).
    void switchFocusBetweenFloatingAndTiling(const QString& screenId) override;

private:
    /// Shared body of toggleFocusedFloat and restoreFocusedWindow: resolve
    /// the focused window and toggle its float state, reporting the
    /// no-window failure under @p failureAction so each verb's OSD carries
    /// its own token ("float" vs "restore").
    void toggleFocusedFloatAs(const PhosphorEngine::NavigationContext& ctx, const QString& failureAction);

public:
    // ═══════════════════════════════════════════════════════════════════════
    // Scroll-specific vocabulary (concrete type; daemon reaches these via a
    // mode check + cast, per the IPlacementEngine extension policy)
    // ═══════════════════════════════════════════════════════════════════════

    void focusColumnFirst(const QString& screenId);
    void focusColumnLast(const QString& screenId);
    void moveColumnToFirst(const QString& screenId);
    void moveColumnToLast(const QString& screenId);
    void consumeWindowIntoColumn(const QString& screenId);
    void expelWindowFromColumn(const QString& screenId);
    /// delta -1 = towards the strip's start, +1 = towards its end (niri
    /// consume-or-expel-window-left/right).
    void consumeOrExpelWindow(int delta, const QString& screenId);
    void centerColumn(const QString& screenId);
    void toggleColumnTabbed(const QString& screenId);
    /// Windowed fullscreen (niri toggle-windowed-fullscreen) on the active
    /// window: layout-neutral per-tile flag, see Tile::windowedFullscreen.
    void toggleWindowedFullscreen(const QString& screenId);
    /// Compositor-driven reconciliation: the client left fullscreen on its
    /// own, so drop the flag and re-apply that window's screen.
    void clearWindowedFullscreen(const QString& windowId);
    /// Compositor-driven repair: the compositor moved this window behind
    /// the engine's back (KWin's fullscreen-exit restore), so evict its
    /// emit-gate memory and relayout its screen to re-emit the true rect.
    void reapplyWindowGeometry(const QString& windowId);
    /// delta -1/+1 through the preset width list.
    void cycleColumnPresetWidth(int delta, const QString& screenId);
    /// deltaPercent of the work area's MAIN extent (e.g. +10 / -10).
    void adjustColumnWidth(qreal deltaPercent, const QString& screenId);
    /// Toggle the maximized state of a column on @p screenId. An empty
    /// @p windowId targets the ACTIVE column (the keyboard shortcut's
    /// meaning); a named window targets the column owning it and refuses when
    /// the strip does not hold it, which is what the compositor's maximize
    /// interception needs — that request names one window and the active
    /// column is frequently a different one.
    void toggleMaximizeColumn(const QString& screenId, const QString& windowId = QString());
    void expandColumnToAvailableWidth(const QString& screenId);
    /// Equal shares of the viewport for every fully visible column
    /// (Karousel equalize). Refuses with fewer than two.
    void equalizeVisibleColumnWidths(const QString& screenId);
    /// The focused column at the smallest preset (Karousel minimize-width).
    /// The strip falls back to the engine floor when the vocabulary is
    /// empty, an arm only a test or an embedder handing the strip bare
    /// params can reach: every in-tree producer of the preset list
    /// substitutes a non-empty fallback for an empty one.
    void minimizeColumnWidth(const QString& screenId);
    /// Every column on the screen back to the context's default width and
    /// display, every window back to the even split: the scrolling half of
    /// the mode-neutral Retile shortcut. Widths are left as they are when
    /// the context's default is "the client decides" and no rule pins one
    /// (see resetDefaultColumnWidthFor); display and heights still reset.
    /// Re-applies the layout the way the autotile and snapping halves
    /// re-apply theirs.
    void resetStripToDefaults(const QString& screenId);
    void cycleWindowPresetHeight(int delta, const QString& screenId);
    void adjustWindowHeight(qreal deltaPercent, const QString& screenId);
    void resetWindowHeights(const QString& screenId);
    /// Center the span of fully visible columns (niri center-visible-columns).
    void centerVisibleColumns(const QString& screenId);
    /// Scroll the view along the strip by @p percent of the work area's MAIN
    /// extent WITHOUT changing focus (Karousel scroll-left/right and its page
    /// variants; niri has no equivalent). Positive is forward along the strip,
    /// negative is back, clamped at both ends, and the view stays where it
    /// lands: the strip's centering policy hands the view over to the pan
    /// until the next focus change or centering verb takes it back (see
    /// ScrollStrip's View detachment section). A percent that rounds to fewer
    /// than one pixel reports no_movement; a pan already pinned at the end it
    /// is asked to move toward reports no_target. The OSD names the two
    /// refusals differently. Takes a percent rather than pixels because
    /// the work area is resolved here and nowhere the shortcut layer can see.
    void scrollViewByPercent(qreal percent, const QString& screenId);
    /// First/last non-minimized tile of the active column (niri
    /// focus-window-top/bottom).
    void focusWindowTop(const QString& screenId);
    void focusWindowBottom(const QString& screenId);
    /// Plain adjacent-column focus that stops at the strip edge (niri
    /// focus-column-left/right). delta -1/+1. The generic focusInDirection
    /// crosses monitors instead; this is the opt-in edge-stop variant.
    void focusColumnPlain(int delta, const QString& screenId);
    /// Adjacent-column focus that wraps to the far end at the strip edge
    /// (niri focus-column-left-or-last / right-or-first). delta -1/+1.
    void focusColumnWrap(int delta, const QString& screenId);
    /// Explicit float / re-tile of the focused window (niri
    /// move-window-to-floating / move-window-to-tiling); already-there
    /// presses answer with no_target feedback.
    void moveFocusedToFloating(const QString& screenId);
    void moveFocusedToTiling(const QString& screenId);
    /// Absolute width/height intents (niri set-column-width/set-window-height
    /// with an absolute value). D-Bus surface; no shortcut carries a value.
    void setColumnWidth(const ColumnWidth& width, const QString& screenId);
    void setWindowHeight(const WindowHeight& height, const QString& screenId);

    /// Which way @p screenId's strip runs, resolved live.
    ///
    /// THE single source of the resolved axis for everything outside the
    /// layout path — the daemon publishes it to the effect, and the strip
    /// selector draws its miniature with it. Neither may derive an aspect
    /// ratio of its own: two independent derivations can disagree on a
    /// near-square monitor, and that disagreement is invisible in tests and
    /// intermittent in the field.
    ///
    /// Resolves the work area LIVE. The engine subscribes to no ScreenManager
    /// signal, so answering from a snapshot taken at the last relayout would
    /// hand out the PRE-rotation axis at exactly the moment a rotation makes
    /// someone ask.
    StripAxis stripAxisForScreen(const QString& screenId) const;

    /// Focus the previous/next column ALONG THE STRIP, for callers holding a
    /// strip-relative intent rather than a physical direction. The wheel is
    /// the one that matters: the effect collapses both physical wheel axes
    /// onto a single +/-1 before it ever reaches D-Bus.
    ///
    /// The physical token is synthesized HERE from the screen's own axis,
    /// which is the whole point — a caller that spelled "left"/"right" itself
    /// would walk the STACK on a vertical strip. Deliberately not routed
    /// through focusColumnPlain: that verb stops at the strip edge, and the
    /// wheel's documented contract is that a notch at the edge crosses onto
    /// the adjacent output.
    void focusColumnByDelta(int delta, const QString& screenId);

    // ═══════════════════════════════════════════════════════════════════════
    // State access / ordering / tracking
    // ═══════════════════════════════════════════════════════════════════════

    PhosphorEngine::IPlacementState* stateForScreen(const QString& screenId) override;
    const PhosphorEngine::IPlacementState* stateForScreen(const QString& screenId) const override;
    /// The tab-indicator geometry @p screenId resolves to: the configured
    /// values with any per-screen rule override layered on per property.
    ///
    /// Public because the resolution has seven independent fall-back paths and
    /// is otherwise only observable through a relayout's resolved rects, which
    /// conflates it with the layout maths. Const and side-effect free.
    TabIndicatorParams tabIndicatorParamsForScreen(const QString& screenId) const;
    bool isWindowTracked(const QString& windowId) const override;
    bool isWindowTiled(const QString& windowId) const override;
    bool isWindowManaged(const QString& windowId) const override;
    QString screenForTrackedWindow(const QString& windowId) const override;
    QRect lastManagedRect(const QString& rawWindowId) const override;
    QStringList managedWindowOrder(const QString& screenId) const override;
    /// The zone-number unit. Declared in ScrollEngineTypes.h (the header's
    /// file-size split) and aliased here, so ScrollEngine::VisibleTile keeps
    /// naming it for every consumer — the daemon's strip zones alias it by
    /// that spelling. Its full contract, including why a digit's ACTION is
    /// coarser than the address it resolves, lives on ScrollVisibleTile.
    using VisibleTile = ScrollVisibleTile;
    /// The visible tiles of @p screenId's current-context strip in zone-
    /// number order. Not every on-screen window is here: hidden tabs of a
    /// tabbed column, minimized tiles, parked columns, and tiles whose
    /// intersection with the work area is EMPTY (a stack whose min heights
    /// overflow the work area resolves its tail beyond the work area's far
    /// cross edge) all
    /// carry no number and cannot be reached by a digit. Partially-visible
    /// columns are CLIPPED, not dropped: an arbitrarily thin sliver still
    /// carries its own number, because the cut-off edge is what tells the
    /// viewer the strip continues off-screen. Note the walk numbers by the
    /// STRIP rect clipped to the work area, while the APPLIED geometry may
    /// differ: the apply path clamps straddlers to the SCREEN edge and
    /// parks a remainder below its peek floor, so a barely-visible column
    /// can carry a number here while sitting parked — a digit press still
    /// works, because focusing re-anchors the view and scrolls it back in.
    /// Empty when the screen has no state, no visible
    /// tile, or no valid work area (unknown/removed screen, or outer gaps
    /// that swallowed it).
    ///
    /// PRECONDITION, unlike the navigation verbs: this and its siblings do
    /// NO resolveOperationScreen fallback. They answer for the screen
    /// NAMED, so a caller passing a screen the engine does not manage gets
    /// an empty result rather than the active screen's strip — pass the
    /// same screen the verb will act on or the two halves of the
    /// address/action pair can describe different outputs.
    ///
    /// Transient staleness: the walk relayouts WITHOUT updateViewForFocus,
    /// so between a work-area change (resolution, panels, outer gaps) and
    /// the next applyLayout it resolves against the pre-change view anchor.
    /// The window is the daemon's ~400ms geometry debounce plus the queued
    /// hop; it is self-healing (the next apply re-anchors) and reaches the
    /// settings poll, the OSD preview and a digit press landing inside it.
    QVector<VisibleTile> visibleTiles(const QString& screenId) const;
    /// The rects of visibleTiles — the plain absolute-pixel projection, in
    /// the same order. Convenience only, and with no production caller today:
    /// every consumer wants the zone number alongside the rect, so they walk
    /// visibleTiles and read VisibleTile::zoneNumber rather than re-deriving
    /// an ordinal from this list's index. Kept for callers that want bare
    /// screen coordinates; the normalized twin is below.
    QVector<QRect> visibleTileRects(const QString& screenId) const;
    /// @p windowId's zone number on @p screenId's current strip, or -1 when
    /// it is off-screen, a hidden tab, or untracked.
    ///
    /// Convenience/test seam with no production caller today (like
    /// visibleTileRects above): every production number consumer walks
    /// visibleTiles and reads VisibleTile::zoneNumber directly, so this
    /// exists for callers that hold only a window id.
    int visibleTileNumberForWindow(const QString& screenId, const QString& windowId) const;
    /// visibleTileRects normalized to the FULL screen geometry (0.0–1.0 per
    /// axis) — the shape zone previews consume. The tiles are clipped to the
    /// gap-inset work area, so the fractions show the panel gap; that is the
    /// same basis the daemon's own OSD card uses (its twin renorm of the
    /// absolute rects in stripzones.h) WHENEVER the screen rect resolves, so
    /// the settings thumbnail and the OSD draw the same shape. Its one
    /// production consumer is the D-Bus strip payload (scrollingadaptor.cpp),
    /// which pairs each rect with the zone number from the matching
    /// visibleTiles entry: where a layout switch shows the layout's zones, a
    /// scrolling screen shows what the strip actually looks like right now.
    /// Falls back to the work area as the basis only when no screen rect is
    /// resolvable — a KNOWN divergence from the OSD twin in that window (it
    /// falls back to QScreen::geometry() via the daemon's shared resolver,
    /// which this LGPL library cannot link), so during early startup the two
    /// surfaces can briefly disagree by the panel's share of the output.
    /// Self-heals on the next poll once the screen resolves. Same emptiness
    /// contract. Production reads the PAIRED form (visibleTilesWithRects,
    /// via the D-Bus strip payload in scrollingadaptor.cpp); this projection
    /// serves the test suites.
    ///
    /// The pairing is index-wise and both walks run in the same synchronous
    /// call, so a caller reading this beside visibleTiles gets rects and
    /// numbers for the same tiles in the same order.
    QVector<QRectF> visibleTileRectsRelative(const QString& screenId) const;

    /// A tile paired with its screen-normalized rect, from ONE strip walk.
    /// Aliased out of ScrollEngineTypes.h like VisibleTile above.
    using VisibleTileWithRect = ScrollVisibleTileWithRect;

    /// Column-aware strip snapshot for the daemon's strip-mode drag popup.
    /// One relayout pass, same cost discipline as visibleTilesWithRects.
    /// Resolves against the drag-insert preview's CAPTURED context key when
    /// a preview is live for @p screenId (same rule as
    /// computeDragInsertTargetAtPoint), else the current context — so the
    /// detached drag window is absent either way. @p excludeWindowId filters
    /// a drag window that has NOT been detached yet; it is consulted ONLY on
    /// the no-preview path (callers must pass only the window a live preview
    /// detached — any other id is silently ignored while a preview owns the
    /// screen). See the snapshot type's index contract in
    /// ScrollEngineTypes.h. Implemented in engine_snapshot.cpp.
    ScrollStripSnapshot stripSnapshot(const QString& screenId, const QString& excludeWindowId = QString()) const;

    /// visibleTiles and visibleTileRectsRelative in a single resolve.
    ///
    /// Reading the two separately costs TWO layoutParamsForScreen calls and
    /// two relayouts for one answer, and layoutParamsForScreen is not cheap —
    /// it resolves the per-screen override map and parses both preset
    /// vocabularies. The D-Bus strip payload is polled on a live timer while
    /// the Monitors state view is open, so that doubling is a recurring cost
    /// rather than a one-off. It also removes the count-mismatch guard the
    /// paired reads needed: one walk cannot disagree with itself.
    QVector<VisibleTileWithRect> visibleTilesWithRects(const QString& screenId) const;

    void setInitialWindowOrder(const QString& screenId, const QStringList& windowIds) override;
    QString managedFocusedWindow(const QString& screenId) const override;
    /// The desktop this engine has pinned this screen to, or 0. See the
    /// interface doc for why the gate compares the PIN and not the resolved key.
    int stickyPinnedDesktopForScreen(const QString& screenId) const override
    {
        return m_context.stickyPinnedDesktop(screenId);
    }
    void setInitialFocusedWindow(const QString& screenId, const QString& windowId) override;
    int pruneStaleWindows(const QSet<QString>& aliveWindowIds) override;

    // Layout capability (see IPlacementEngine's Layout capability section)
    /// The strip consumes layouts as sizing TEMPLATES, never as window
    /// placement: the screen's assigned ScrollingTemplate is the sizing
    /// vocabulary. Its STORED preset lists (presetColumnWidths /
    /// presetWindowHeights) become the screen's preset column-width and
    /// window-height vocabularies, pushed straight through by the daemon, and
    /// its seed blueprint prescribes the columns the strip opens with. No
    /// geometry is derived from a layout's zones. Explicit override — the
    /// capability is load-bearing for the daemon's layout-selection gates, not
    /// an inherited absence.
    LayoutSupport layoutSupport() const override
    {
        return LayoutSupport::Templates;
    }

    /// The strip renders in the daemon's drag popup as column cards speaking
    /// the drag-insert vocabulary (new column in a gap, join on a card, tab
    /// dock on a tabbed card). Explicit override — load-bearing for the
    /// drag adaptor's selector gate, not an inherited absence.
    bool providesDragInsertSelector() const override
    {
        return true;
    }

    /// Effective preset vocabulary for a screen: the per-screen TEMPLATE
    /// override when the daemon pushed a usable one (every entry validated
    /// against the same floor as the settings parser), else the cached
    /// settings list. Wholesale replacement, never a merge — see
    /// ScrollPerScreenKeys. Public for D-Bus/introspection consumers
    /// (ScrollingAdaptor::presetVocabularyJson).
    QList<qreal> effectivePresetColumnWidths(const QString& screenId) const;
    QList<qreal> effectivePresetWindowHeights(const QString& screenId) const;

    /// How far this screen's CURRENT context has worked through its template
    /// blueprint. Public for the same reason as the preset readers above: it
    /// is a read-only introspection surface, here for
    /// ScrollingAdaptor::blueprintProgressJson and the Monitors page that
    /// renders it.
    ///
    /// Reports the current (desktop, activity) context, matching
    /// visibleStripJson — a sibling desktop's strip has its own progress and
    /// is not what a screen readout means. A screen with no blueprint, no
    /// state yet, or one this engine does not own reports {0, 0}.
    ScrollBlueprintProgress blueprintProgressForScreen(const QString& screenId) const;

    // ═══════════════════════════════════════════════════════════════════════
    // Cross-engine handoff
    // ═══════════════════════════════════════════════════════════════════════

    QString engineId() const override
    {
        return PhosphorEngine::WindowPlacement::scrollingEngineId();
    }
    void handoffReceive(const HandoffContext& ctx) override;
    void handoffRelease(const QString& windowId) override;
    /// Cross-mode swap support (queried by the daemon when THIS engine is
    /// the swap target): the strip window at the entry edge facing the
    /// source for a crossing arriving in @p direction — a "right" crossing
    /// enters the viewport's left edge. Ranked over visibleTiles, so the
    /// answer is always a tile that carries a zone number. Empty when the
    /// strip is empty, and equally when nothing on it is currently visible
    /// (every column parked, or no valid work area) — the caller degrades a
    /// swap to a plain move on both.
    QString entryWindowForCrossing(const QString& screenId, const QString& direction) const;
    /// The column index @p windowId's column holds on @p screenId (current
    /// context), or -1 — the landing-slot reference a swap counterpart uses
    /// via HandoffContext.insertIndex.
    int columnIndexForWindow(const QString& screenId, const QString& windowId) const;
    /// The windows on @p screenId that focus-follows-mouse must REFUSE to
    /// focus under a cap of @p maxScrollPercent percent of the viewport's
    /// extent along the strip — niri's `focus-follows-mouse
    /// max-scroll-amount`. A window is named when activating it would move
    /// the view further than the cap allows.
    ///
    /// The daemon publishes the answer to the KWin effect, which owns
    /// focus-follows-mouse and cannot ask this question per pointer event.
    ///
    /// 100 or more is the "no cap" SENTINEL and answers empty without
    /// walking the strip, which is the shipped default and so the case worth
    /// short-circuiting. Read it as the sentinel it is rather than as a
    /// geometric claim: under CenterFocusedColumn::Always a step from a
    /// viewport-wide column to a narrow one costs more than one viewport
    /// (the recentring pays half of each column plus the gap), so a cap of
    /// exactly 100 is NOT equivalent to measuring against the viewport.
    /// Nothing depends on the two agreeing, but a future reader should not
    /// infer that they do.
    ///
    /// Callers refuse on the answer, so every question this cannot answer —
    /// an unknown screen, an empty strip, a degenerate work area — comes
    /// back empty. Fully minimized columns are skipped rather than measured:
    /// they hold no strip extent, so the centering arms would answer about
    /// nothing, and their tiles are not focus targets under a pointer.
    QStringList windowsBeyondFocusScrollLimit(const QString& screenId, int maxScrollPercent) const;

    // ═══════════════════════════════════════════════════════════════════════
    // Drag-insert (trigger-held window drag re-inserts into the strip)
    //
    // DETACH-ONCE architecture, deliberately unlike autotile's live-
    // restructure preview: begin detaches the window from the strip (one
    // settle, neighbours close up), update only remembers the hit-tested
    // drop target against the now-stable strip, and commit applies the
    // structure once at drop. A strip cannot restructure per tick the way
    // a fixed zone grid can — it slides the layout under the cursor (see
    // drag_preview.cpp's header for the full rationale). Restoration state
    // is captured in FloatRestore vocabulary (column + tile + stack anchor
    // + width/display/height intents) — the strip has no raw-order index
    // for cancel to restore by. All in drag_preview.cpp.
    // ═══════════════════════════════════════════════════════════════════════

    bool hasDragInsertPreview() const override
    {
        return m_dragInsertPreview.has_value();
    }
    QString dragInsertPreviewScreenId() const override
    {
        return m_dragInsertPreview ? m_dragInsertPreview->targetScreenId : QString();
    }
    /// See the base declaration. Empty without a preview, and empty when begin
    /// took the window from untracked, which for this engine also covers a
    /// window that was floating outside any strip.
    QString dragInsertPreviewPriorScreenId() const override
    {
        return m_dragInsertPreview && m_dragInsertPreview->hadPriorState ? m_dragInsertPreview->priorKey.screenId
                                                                         : QString();
    }
    /// The window id of the active drag-insert preview, or empty (test seam,
    /// mirroring AutotileEngine's accessor).
    QString dragInsertPreviewWindowId() const
    {
        return m_dragInsertPreview ? m_dragInsertPreview->windowId : QString();
    }
    bool beginDragInsertPreview(const QString& rawWindowId, const QString& screenId) override;
    void commitDragInsertPreview() override;
    void cancelDragInsertPreview() override;
    /// `primary` = column index; `newSlot` true opens a NEW column at
    /// `primary`; otherwise the window joins column `primary` as tile
    /// `secondary` (a MODEL-column tile index — minimized tiles count).
    /// Zone map, symmetric by construction: a visible column's two END bands
    /// along the strip open a new column at that column's own spot (it steps
    /// aside and the indicator covers it), its middle joins it, and each
    /// inter-column boundary belongs to exactly one band — the following
    /// neighbour's LEADING band. Only the view's two extremes differ: the
    /// first visible column's LEADING band (plus everything before it along
    /// the strip) aims the leading slot as a past-the-edge hint
    /// (`leadingEdge`), and the last visible column's TRAILING band (plus
    /// everything after it) appends after the
    /// strip. The dragged window is DETACHED while a preview is live, so
    /// the strip hit-tested here is stable across ticks and no own-slot
    /// special case exists (nothing the cursor hovers can be the dragged
    /// window). While a preview is live for @p screenId the hit-test
    /// resolves against the preview's captured context key, not the
    /// screen's current one.
    DragInsertTarget computeDragInsertTargetAtPoint(const QString& screenId, const QPoint& cursorPos) const override;
    void updateDragInsertPreview(const DragInsertTarget& target) override;
    /// Edge auto-scroll (drag_autoscroll.cpp). Moves the VIEW only, which
    /// is compatible with DETACH-ONCE: the invariant is that structure and
    /// the hit-tested answer hold still under a stationary cursor, not that
    /// pixels do. It keeps that answer still by owning the target while it
    /// scrolls — see dragAutoScrollActive.
    bool dragAutoScrollTick(const QString& screenId, const QPoint& cursorPos, qreal dtSeconds) override;
    bool dragAutoScrollActive() const override;
    /// The ONE disarm: hands the drop target back to the ordinary hit-test
    /// and restarts the start delay. Every caller must either re-aim
    /// (repairDragAutoScrollTarget), clear lastTarget itself, or accept the
    /// stale target deliberately the way the tick's no-geometry exits do
    /// (foreign screen, dead state, dead work area — where a hit-test is
    /// impossible and the daemon's own screen-mismatch teardown follows).
    /// A thoughtless bare cancel leaves the auto-scroll's last edge slot
    /// standing for a release to commit.
    void cancelDragAutoScroll() override;
    /// The rect the dragged window would occupy if it were dropped at the
    /// currently remembered target — the drop indicator the daemon paints.
    /// Absolute screen pixels, the same basis as visibleTiles.
    ///
    /// Detach-once means the strip NEVER opens a gap to show where the drop
    /// lands (autotile's live restructure slid this layout out from under a
    /// stationary cursor), so this rect is the only feedback there is. Null
    /// when no preview is live for @p screenId or before the first
    /// hit-test resolves a target. Not clamped into the viewport: a target
    /// on a parked column reports where it truly lands, and the overlay
    /// clips.
    QRect dragInsertIndicatorRect(const QString& screenId) const override;
    /// While set, applyLayout never emits this window's rect and
    /// onWindowResized never reconciles its acks: during a drag the effect
    /// floats the window VISUALLY ONLY. The engine keeps its strip tile for a
    /// drag with no drag-insert preview armed; under DETACH-ONCE the tile is
    /// gone for the duration of a preview, and the mark still has to hold
    /// because the acks keep arriving either way. So without it every mid-drag
    /// ack re-emitted the slot rect
    /// (yanking the window from the cursor) and pinned the column's
    /// width/height intents to transient drag frames. Clearing does NOT
    /// retile — every drop path finalizes on its own (see the definition).
    void setInteractiveDragWindow(const QString& windowId) override;

    // ═══════════════════════════════════════════════════════════════════════
    // Desktop / activity context
    // ═══════════════════════════════════════════════════════════════════════

    void setCrossSurfaceResolver(PhosphorEngine::ICrossSurfaceResolver* resolver) override
    {
        m_crossSurfaceResolver = resolver;
    }
    void setCurrentDesktop(int desktop) override;
    void setCurrentDesktopForScreen(const QString& screenId, int desktop) override;
    void clearCurrentDesktopForScreen(const QString& screenId) override;
    void setCurrentActivity(const QString& activity) override;
    /// Pin screens whose managed windows are ALL sticky to their current
    /// desktop before a desktop switch, and unpin (migrating the state to
    /// the new desktop key) once a non-sticky window appears — the same
    /// "virtualdesktopsonlyonprimary" contract as AutotileEngine: without
    /// the pin a desktop switch resolves a fresh (screen, desktop) key and
    /// the strip comes up empty while the sticky windows are still visible.
    void updateStickyScreenPins(const std::function<bool(const QString&)>& isWindowSticky) override;
    QSet<int> desktopsWithActiveState() const override;
    void pruneStatesForDesktop(int removedDesktop) override;
    void pruneStatesForActivities(const QStringList& validActivities) override;
    void pruneStatesForRemovedScreen(const QString& physicalScreenId) override;

    // ═══════════════════════════════════════════════════════════════════════
    // Persistence + settings
    // ═══════════════════════════════════════════════════════════════════════

    void saveState() override;
    void loadState() override;
    void setPersistenceDelegate(std::function<void()> saveFn, std::function<void()> loadFn)
    {
        m_persistSaveFn = std::move(saveFn);
        m_persistLoadFn = std::move(loadFn);
    }
    /// Durable strip-structure snapshot: every LIVE strip (current states)
    /// plus the un-consumed mode-round-trip stash entries, keyed
    /// "screenId|desktop|activity". On a key collision the live strip's
    /// focus, view and axis win and the stash's not-yet-returned columns
    /// are appended after the live ones (a merge, not a replacement). The
    /// daemon persists this blob through the WTA KConfig layer so a login
    /// restore rebuilds stacked columns (with each column's active tile,
    /// i.e. a tabbed column's shown tab), the strip focus, and the view
    /// anchor together with whether that anchor was an explicit pan (the
    /// detach latch) instead of one default column per window. Per-tile height
    /// intents ride along; per-window minimum sizes do not — the client
    /// re-reports those. (engine_serialize.cpp)
    QJsonObject serializeStripState() const;
    /// Load a serializeStripState blob into the stash so the EXISTING
    /// arrival-restore path (restoreFromStripStash) rebuilds each strip as
    /// its windows are announced. Additive and conservative: keys that
    /// already have a stash entry or a live populated state are skipped, so
    /// a second load cannot re-stage stale structure over adopted windows.
    void restoreStripState(const QJsonObject& state);
    /// TEST SEAM: shorten (or lengthen) the cross-session fuzzy-claim grace
    /// window — the period after a stash is staged, or its screen re-enters
    /// scrolling, during which an appId-only claim may take a dead sibling's
    /// slot. See StashedStrip::fuzzyClaimWindow.
    ///
    /// It exists because the grace is measured by a QElapsedTimer against a
    /// 60 s constant, which leaves the REFUSAL half of the contract — the
    /// half that stops a stash hijacking every same-app window the user opens
    /// for the rest of the session — reachable in a test only by sleeping a
    /// minute. Passing 0 expires the window immediately, so a test can assert
    /// a late arrival opens fresh instead of adopting a stashed slot.
    ///
    /// Production never calls this; the constructor's seed is the shipped
    /// value.
    void setFuzzyClaimGraceMsForTesting(qint64 graceMs)
    {
        m_fuzzyClaimGraceMs = graceMs;
    }
    std::optional<PhosphorEngine::WindowPlacement> capturePlacement(const QString& windowId) const override;
    void refreshConfigFromSettings() override;
    void retile(const QString& screenId = QString()) override;
    void scheduleRetileForScreen(const QString& screenId) override;

    /// Predicate deciding whether an opening window should start FLOATING
    /// because a "Float this app" rule matched it. Daemon-injected, keyed by
    /// the live windowId. Clear with {} before destroying captured state.
    ///
    /// Takes the OPENING SCREEN as well, for the same reason
    /// OpenParamsResolver does: the resolver needs it to stamp ScreenId and to
    /// derive Mode, without which a rule pairing either with Float is silently
    /// inert even though the rules editor offers that pairing.
    using FloatPredicate = std::function<bool(const QString& windowId, const QString& screenId)>;
    void setFloatPredicate(FloatPredicate predicate)
    {
        m_floatPredicate = std::move(predicate);
    }

    /// Predicate gating the float-POSITION restore on the floating-reopen
    /// branch: the window is marked floating unconditionally, only the
    /// geometry move onto the recorded free spot is gated. Mirrors
    /// AutotileEngine::RestorePositionPredicate (daemon-wired
    /// scrollingRestoreFloatedWindowsOnLogin setting + the per-window
    /// RestorePosition rule); unset (tests / no daemon) means the move
    /// always fires, preserving historical behaviour.
    using RestorePositionPredicate = std::function<bool(const QString& windowId)>;
    void setRestorePositionPredicate(RestorePositionPredicate predicate)
    {
        m_restorePositionPredicate = std::move(predicate);
    }

    /// Resolver for the per-window open-behaviour rule overrides. Same
    /// injection contract as the float predicate. Takes the OPENING screen
    /// as well as the window: the open-behaviour rules a user authors are
    /// commonly pinned to a screen or to Scrolling mode, and the daemon-side
    /// window metadata carries neither, so this path is the one that can
    /// supply them.
    using OpenParamsResolver = std::function<ScrollOpenParams(const QString& windowId, const QString& screenId)>;
    void setOpenParamsResolver(OpenParamsResolver resolver)
    {
        m_openParamsResolver = std::move(resolver);
    }

    /// Context-mode resolver shape shared by the three injected closures
    /// below: invoked as (screenId, virtualDesktop, activity) → bool. The
    /// daemon bakes the mode lookup (and, where relevant, engine liveness
    /// and global toggles) into each closure so this library stays free of
    /// the zones-layer mode type. Same clear-before-destroy contract as the
    /// other injected closures.
    using ModeResolver = std::function<bool(const QString& screenId, int desktop, const QString& activity)>;

    /// Snapping-mode resolver for windowOpened's cross-screen restore defer
    /// gate (one term of the N-way reciprocity with the other engines'
    /// gates and claims). Must answer whether the RECORDED context resolves
    /// to Snapping mode AND snapping is globally preferred — a disabled snap
    /// engine never claims, so deferring to it would strand the window.
    /// Unset → this gate TERM is off (the autotile term below self-gates
    /// independently); with neither resolver set every open is claimed
    /// (headless/test path).
    void setSnappingModeResolver(ModeResolver resolver)
    {
        m_snappingModeResolver = std::move(resolver);
    }

    /// Scrolling-mode resolver for claimCrossScreenReopen — must answer
    /// whether the RECORDED context resolves to Scrolling mode, so a session
    /// window KWin dropped on the wrong output is pulled back to its recorded
    /// scrolling screen. Unset → this engine never claims cross-screen
    /// (headless/test path).
    void setScrollingModeResolver(ModeResolver resolver)
    {
        m_scrollingModeResolver = std::move(resolver);
    }

    /// Autotile-mode resolver for windowOpened's cross-screen tile-restore
    /// defer gate: a window arriving here that carries a TILED autotile slot
    /// recorded on an autotile-mode screen belongs to autotile's cross-screen
    /// reclaim, and this engine must not splice it into the strip. Must
    /// answer mode AND autotile liveness on that screen (the daemon owns
    /// both engines and bakes the liveness term in — deferring to an engine
    /// whose live set disagrees with the assignment would strand the
    /// window). Unset → this gate TERM is off.
    void setAutotileModeResolver(ModeResolver resolver)
    {
        m_autotileModeResolver = std::move(resolver);
    }

    /// Per-context (window-rule) gap overrides, resolved daemon-side so this
    /// library stays settings-agnostic. Returns a PerScreenKeys-shaped map
    /// (InnerGap / OuterGap* / UsePerSideOuterGap); values present in the
    /// map win over the IScrollSettings gaps. Same lifetime contract as the
    /// other injected closures.
    using ContextGapProvider = std::function<QVariantMap(const QString& screenId)>;
    /// Embedder/test seam: inject screen geometry when NO ScreenManager is
    /// wired (headless hosts). @p availableGeometry supplies the work area,
    /// @p screenGeometry the full rect used for off-canvas parking bounds.
    /// Ignored while a ScreenManager is present. Without a ScreenManager the
    /// parking union degrades to the single screen's own bottom unless
    /// setAllScreenGeometriesProvider is also wired — a multi-output embedder
    /// must supply one or the other, or a park below one monitor may land on
    /// the one beneath it.
    void setScreenGeometryProviders(std::function<QRect(const QString&)> availableGeometry,
                                    std::function<QRect(const QString&)> screenGeometry)
    {
        m_availableGeometryProvider = std::move(availableGeometry);
        m_screenGeometryProvider = std::move(screenGeometry);
    }

    /// The union half of the provider seam: every output's full rect, used
    /// only for the parking union's bottom edge. Ignored while a
    /// ScreenManager is present (it answers authoritatively).
    void setAllScreenGeometriesProvider(std::function<QList<QRect>()> allScreenGeometries)
    {
        m_allScreenGeometriesProvider = std::move(allScreenGeometries);
    }

    void setContextGapProvider(ContextGapProvider provider)
    {
        m_contextGapProvider = std::move(provider);
    }

    // Per-CONTEXT overrides layered over the config defaults, one map per
    // (screen, desktop, activity) — see the member for why the key is the
    // context rather than the screen. Three producer channels the daemon
    // merges (rules win): the
    // RULE channel (SetScrollDefaultColumnWidth / SetCenterFocusedColumn /
    // SetScrollDefaultColumnDisplay / SetScrollInsertPosition /
    // SetScrollDefaultWindowHeight / SetScrollStripAxis), the SETTINGS
    // channel (the per-monitor New-columns sizing trio pairs and the
    // per-monitor StripAxis — the axis SHARES its key across both channels,
    // so the daemon's rule-after-seed insert IS the precedence collapse),
    // and the TEMPLATE channel (from the
    // context's assigned ScrollingTemplate: the presetColumnWidths /
    // presetWindowHeights lists, replaced wholesale per list; the
    // TemplateColumns seed blueprint that engine_lifecycle consumes at column
    // creation; and the beyond-blueprint default width trio and display).
    // Key spellings live in ScrollPerScreenKeys (ScrollTypes.h) — the
    // accessor comments there are the authoritative key list.
    void applyPerScreenConfig(const QString& screenId, const QVariantMap& overrides) override;
    void clearPerScreenConfig(const QString& screenId) override;
    QVariantMap perScreenOverrides(const QString& screenId) const override
    {
        return overridesForScreen(screenId);
    }

Q_SIGNALS:
    /// Batch of absolute pixel rects for the KWin effect, same JSON contract
    /// as AutotileEngine::windowsTiled ({windowId, screenId, x, y, width,
    /// height}). Float transitions are signalled separately via
    /// windowFloatingChanged — this batch never carries release entries.
    void windowsTiled(const QString& tileRequestsJson);
    /// Scrolling twin of autotileScreensChanged, with the same
    /// identical-set re-emit contract on desktop/activity switches.
    void scrollingScreensChanged(const QStringList& screenIds, bool isDesktopSwitch);
    void enabledChanged(bool enabled);
    /// Tab-indicator model for @p screenId, emitted when the resolved model
    /// changes (a relayout that produces an identical payload stays silent): a
    /// JSON array with one entry per VISIBLE tabbed column that actually
    /// resolves an indicator —
    /// {x, y, width, height (the INDICATOR's absolute px rect, not the
    /// column's), position (TabIndicatorPosition), activeIndex,
    /// tabs: [windowId, ...]}.
    ///
    /// Columns whose indicator is suppressed (the master switch off, or a
    /// single-tab column under hideWhenSingleTab) are simply absent, so a
    /// consumer never re-tests those conditions and cannot disagree with the
    /// relayout that sized the column around them. An empty array clears the
    /// screen's indicators. The daemon only relays this payload; the
    /// compositor enriches the window ids with titles and urgency and paints
    /// the pills itself.
    void tabStripsChanged(const QString& screenId, const QString& stripsJson);

private:
    // engine_core.cpp
    QString canonicalizeForLookup(const QString& rawWindowId) const;
    /// App identity from the registry record, never parsed out of the
    /// first-contact-frozen id. Mirrors AutotileEngine's twin; see engine_core.cpp.
    QString currentAppIdFor(const QString& anyWindowId) const;
    PhosphorEngine::PlacementStateKey currentKeyForScreen(const QString& screenId) const
    {
        return m_context.currentKeyForScreen(screenId);
    }
    /// The override map entry for @p screenId's CURRENT context.
    ///
    /// Every screenId-taking effective* reader goes through here, so the
    /// map's per-context keying stays an implementation detail of this class
    /// rather than something each reader has to know. A screen with no entry
    /// for its current context answers an empty map, which is what every
    /// reader already treats as "no override, use the configured value".
    QVariantMap overridesForScreen(const QString& screenId) const
    {
        return m_perScreenOverrides.value(currentKeyForScreen(screenId));
    }
    ScrollState* stateForKey(const PhosphorEngine::PlacementStateKey& key, bool createIfMissing);
    /// Point the live preview's drop target at the view's leading (@p
    /// direction < 0) or trailing new-column slot, the two shapes the band
    /// hit-test already produces at the view's extremes. Called on every
    /// auto-scroll tick INSTEAD of the hit-test, so the target cannot churn
    /// as columns slide under a stationary cursor (drag_autoscroll.cpp).
    /// Returns true when the stored target actually changed. @p cursorPos
    /// feeds the defensive empty-viewport arm's re-aim only.
    ///
    /// PRECONDITION: m_dragInsertPreview must be live (asserted, with a
    /// release-build refusal, since the signature reads preview-independent).
    bool writeDragAutoScrollTarget(const ScrollState& state, const ScrollLayoutParams& params, int direction,
                                   const QPoint& cursorPos);
    /// Re-aim the live preview's drop target at @p cursorPos with the
    /// ordinary hit-test, undoing an edge slot the auto-scroll wrote. Called
    /// wherever ownership ends with a usable cursor on the preview's own
    /// screen. Returns true when the stored target actually changed.
    ///
    /// PRECONDITION: m_dragInsertPreview must be live.
    bool repairDragAutoScrollTarget(const QPoint& cursorPos);
    ScrollState* stateForWindow(const QString& canonicalId, PhosphorEngine::PlacementStateKey* outKey = nullptr) const;
    /// The screen the engine should operate on for a screen-hinted verb:
    /// @p screenId when it is a scrolling screen, else the active screen.
    QString resolveOperationScreen(const QString& screenId) const;
    /// Tear down one context state: appends its windows to
    /// @p releasedWindows, drops the per-window unfloat-slot memory and the
    /// per-screen bookkeeping (pending seed, tab-strip latch), and
    /// deleteLater()s the state. The latch and its payload are cleared
    /// synchronously, but the "[]" clear BROADCAST is QUEUED: this runs from
    /// inside the state map's own iteration, so a consumer touching engine
    /// state would invalidate the live iterator. A caller must not assume the
    /// clear has landed by the time this returns.
    ///
    /// It deliberately does NOT drop the mode-specific float markers or the
    /// last-applied rects. Both are INPUTS to the daemon's windowsReleased
    /// handler, which runs after this returns: it reads
    /// isModeSpecificFloated() to decide whether a window still needs its
    /// snap float cleared and its snap slot restored (and clears the marker
    /// itself, per window), and the adaptor reads lastManagedRect() as the
    /// float-back tile-rect poison guard. Clearing either here reports every
    /// scroll-floated window as not-floated and the window stays floated at
    /// its scroll-float geometry. The rects are reclaimed by
    /// pruneStaleWindows instead. AutotileEngine documents the same contract
    /// on releaseScreenStateForTeardown.
    void releaseScreenState(ScrollState* state, QStringList& releasedWindows);
    /// Latch-guarded tab-strip clear: emits the "[]" payload once for a
    /// screen that had a strip showing, no-op otherwise.
    void clearTabStripsForScreen(const QString& screenId);
    // engine_context.cpp
    /// Shared per-window side-map sweep for the SILENT prune paths (desktop
    /// and activity teardown), which emit no windowsReleased and so have no
    /// downstream consumer of the float marker or the last-applied rect.
    ///
    /// The removed-output prune is NOT one of them: it releases live windows
    /// and emits, so it goes through releaseScreenState and sweeps the side
    /// maps only AFTER the emit. The mode-transition release path likewise
    /// uses releaseScreenState; see the contract there.
    void dropWindowBookkeeping(const ScrollState* state);
    /// Consume @p windowId from a screen's mode-transition seed (marking it
    /// in m_consumedInitialOrder; the list itself keeps its positions) and
    /// drop both entries once every listed id is consumed — MUST run on
    /// every windowOpened outcome (tiled, consumed, floated, the cross-screen
    /// restore defer that hands the window to another engine, and — for the
    /// ARRIVAL screen — a successful claimCrossScreenReopen, whose dispatch
    /// short-circuits the arrival-screen open), or a stale seed survives to
    /// re-position an unrelated later open.
    void consumePendingInitialOrder(const QString& screenId, const QString& windowId);
    /// Drop per-screen bookkeeping (seed, tab-strip latch) for each screen
    /// in @p screenIds that no longer has ANY context state. Overrides
    /// survive by design; see the definition.
    void sweepStatelessScreenBookkeeping(const QSet<QString>& screenIds);
    // engine_core.cpp
    /// Capture @p state's strip STRUCTURE (column groupings, widths,
    /// display, per-tile height intents) before a mode reassignment tears
    /// it down, so cycling back to Scrolling rebuilds the strip the user
    /// left instead of one default-width column per window — the scroll
    /// twin of AutotileEngine's script-state stash. Keyed per context;
    /// overwritten on every teardown of the same key.
    ///
    /// @p preResolvedFallbackAxis is a PRECONDITION, not an optimization, for
    /// one class of caller: anything running inside a walk over m_states MUST
    /// pass it. The nullopt path resolves the axis live through
    /// stripAxisForScreen, which invokes the daemon-injected geometry and gap
    /// providers — running those mid-iteration is what the pre-resolve at
    /// setActiveScreens exists to avoid. Callers outside such a walk may omit
    /// it.
    void stashStripStructure(const PhosphorEngine::PlacementStateKey& key, const ScrollState* state,
                             std::optional<PhosphorProtocol::ScrollAxis> preResolvedFallbackAxis = std::nullopt);
    /// insertOpenedWindow's stash restore: place @p windowId per the
    /// stashed structure for @p key (rejoin its stashed column beside an
    /// already-arrived sibling, or recreate the column at its stashed
    /// position with its width/display), re-applying its height intent.
    /// Returns false when the stash has no verdict (no entry, id absent,
    /// or already consumed) — the caller falls through to the seed path.
    /// NOT side-effect free on that false: an entry that exists for @p key
    /// hands its blueprint cursor and identity to @p state before any tile
    /// is matched, because the claim can fail for an arrival the entry does
    /// not name while the spent-ness is owed to the state regardless.
    /// @p params is the caller's already-resolved layout params (the only
    /// caller holds them; re-deriving would pay a second ScreenManager
    /// query plus context-gap-provider call).
    bool restoreFromStripStash(ScrollState* state, const PhosphorEngine::PlacementStateKey& key,
                               const QString& windowId, const ScrollLayoutParams& params, int minWidth, int minHeight);
    /// Drop stash entries whose key @p stale answers true for — called by
    /// the same prunes that reap context states.
    void sweepStripStash(const std::function<bool(const PhosphorEngine::PlacementStateKey&)>& stale);
    // engine_apply.cpp
    /// The smart-gaps arm is resolved INSIDE, not passed in: a single-column
    /// strip on the screen's current context zeroes the OUTER gaps for every
    /// caller alike. It has to be every caller — the geometry producers
    /// (applyLayout, the visibleTiles walks) and the pure-math verbs
    /// (navigation, anchor math, the maximize compare) resolve against the
    /// same work area; a defaulted parameter only a handful of call sites
    /// passed left the verbs computing against a gapped rect the apply path
    /// then un-gapped. Inner gaps need no arm — with one column no
    /// inter-column gap exists.
    /// @param columnCountOverride When >= 0, the smart-gaps arm judges the
    /// single-column case against THIS count instead of the live strip's.
    /// Two callers pass it: the drop indicator (a preview holds the dragged
    /// window detached, so the post-drop column count differs from the live
    /// one) and windowOpened's height-rule arm, which re-resolves the work
    /// area against the POST-insert column count.
    ScrollLayoutParams layoutParamsForScreen(const QString& screenId, int columnCountOverride = -1) const;

    /// Auto-resolve the strip axis from a FINAL work area. Private because
    /// callers must not pass a rect that has not been through the outer-gap
    /// adjust; stripAxisForScreen is the public door.
    StripAxis resolveStripAxis(const QRect& workArea) const;

    /// The tri-state INTENT (per-screen key, else the global setting)
    /// collapsed to a resolved axis, with Auto derived from @p workArea.
    StripAxis effectiveStripAxis(const QVariantMap& overrides, const QRect& workArea) const;

    /// visibleTiles' real body, taking params the caller already resolved.
    /// The public overload is the thin wrapper; callers that hold params
    /// (the digit path, the normalized-rect walk) use this instead of paying
    /// a second ScreenManager query plus context-gap-provider call. The
    /// state itself is re-resolved here (a hash lookup, unlike the params);
    /// what the overload saves is the params resolution.
    QVector<VisibleTile> visibleTiles(const QString& screenId, const ScrollLayoutParams& params) const;
    /// Relayout the strip and emit the geometry batch for @p screenId's
    /// current-context state. @p focusWindowAfter activates the strip's
    /// active window after the batch (engine-driven navigation only).
    void applyLayout(const QString& screenId, bool focusWindowAfter = false);
    // engine_lifecycle.cpp
    /// Place an arriving window into @p state: floated (oversized, rule, or
    /// a floating placement record) or tiled through the stash / seed /
    /// recorded-slot / plain-insert ladder. Returns false only when every
    /// insert was refused — today that means the strip already holds the
    /// window — in which case nothing about the placement changed and the
    /// caller must not announce one. @p outOpenParams, when given, receives
    /// the per-window open-rule verdict resolved inside (default-constructed
    /// on the early FLOAT exits, which return before the resolver runs) so
    /// the caller's focus arm can read the openFocused override without a
    /// second rule resolve.
    bool insertOpenedWindow(ScrollState* state, const QString& windowId, const QString& screenId, int minWidthIn,
                            int minHeightIn, ScrollOpenParams* outOpenParams = nullptr);
    /// Give a window that floats WITHOUT ever having been a strip tile
    /// (floated at open, or arriving already-floating over the handoff) the
    /// FloatRestore entry the clamp lives in while it floats. column stays
    /// -1: there is no remembered slot, so unfloat opens a fresh column.
    /// Refreshes the clamp on an existing entry rather than overwriting a
    /// real remembered slot with a slotless one.
    void seedFloatRestoreForOpen(const QString& windowId, int minWidth, int minHeight);
    /// Consume the window's FLOATING placement record on an engine-decided
    /// float at open (oversized / rule / sticky) and apply the gated
    /// float-back position restore — the same record consumption and
    /// geometry emit the record-float branch of insertOpenedWindow performs.
    /// Without it an engine-decided float leaves the record stale in the
    /// FIFO and forgets the remembered position autotile restores.
    void restoreFloatRecordForOpen(const QString& windowId, const QString& screenId);
    bool floatWindowInternal(ScrollState* state, const PhosphorEngine::PlacementStateKey& key, const QString& windowId,
                             const QString& screenId);
    bool unfloatWindowInternal(ScrollState* state, const QString& windowId, const QString& screenId,
                               bool applyAfter = true);
    // engine_navigation.cpp
    /// Move the active window off the strip's boundary onto the adjacent
    /// output in @p direction. Scroll→scroll crossings migrate internally;
    /// a different-mode target defers to the daemon's cross-mode handoff.
    /// @p swap requests the two-way exchange: a scroll→scroll crossing
    /// trades slots with the target's entry window, a different-mode target
    /// goes through the daemon's crossModeSwapRequested orchestration.
    /// Returns true when a crossing was initiated, and writes the LANDING
    /// screen to @p landingScreen — the crossing's feedback is announced
    /// there, matching the snap engine's destination convention (announcing
    /// on the source paints the OSD on the output the window just left).
    bool moveActiveWindowAcrossBoundary(ScrollState* state, const QString& screenId, const QString& direction,
                                        bool swap, QString* landingScreen = nullptr);
    /// The insert-refusal float adoption both of moveActiveWindowAcrossBoundary's
    /// legs share; the body documents the repair set (engine_navigation.cpp).
    void adoptAsFloatAfterRefusal(ScrollState* owner, const QString& windowId, const QSize& minSize,
                                  const QString& announceScreen);

    /// The FOCUS twin of the boundary move: cross focus onto the adjacent
    /// output. A scroll-mode neighbour is handled in-engine (entry-edge
    /// window focused + activated); a different-mode neighbour defers to
    /// the daemon via crossModeFocusRequested. True when a crossing was
    /// initiated (feedback already emitted, on the destination).
    bool focusAcrossBoundary(const QString& screenId, const QString& direction, const QString& focusedBefore);
    /// focusInDirection's body behind the resolve: the public entry runs
    /// P_SCROLL_RESOLVE and delegates here, and focusColumnByDelta — which
    /// already resolved the same screen to derive its direction token — calls
    /// this directly, so the wheel path pays ONE layoutParamsForScreen per
    /// notch instead of two (each is a ScreenManager query plus a context-gap
    /// rule cascade plus two preset-vocabulary parses). @p params is consumed
    /// only when @p state is non-null, matching the macro's own contract.
    void focusInDirectionResolved(const QString& direction, const PhosphorEngine::NavigationContext& ctx,
                                  const QString& screen, ScrollState* state, const ScrollLayoutParams& params);

    /// After a SUCCESSFUL focus crossing (either arm), the source state's
    /// floatingHasFocus must drop — focus demonstrably left that output.
    void clearSourceFloatFocusAfterCrossing(const QString& sourceScreenId);

    PhosphorEngine::IWindowTrackingService* m_windowTracker = nullptr;
    PhosphorScreens::ScreenManager* m_screenManager = nullptr;
    /// Embedder/test seam: geometry providers consulted when NO
    /// ScreenManager is wired (headless hosts). First = available work
    /// area, second = full screen rect (parking bounds). With a
    /// ScreenManager present they are ignored.
    std::function<QRect(const QString&)> m_availableGeometryProvider;
    std::function<QRect(const QString&)> m_screenGeometryProvider;
    std::function<QList<QRect>()> m_allScreenGeometriesProvider;
    PhosphorEngine::WindowRegistry* m_windowRegistry = nullptr;
    PhosphorEngine::ICrossSurfaceResolver* m_crossSurfaceResolver = nullptr;

    PhosphorEngine::PerScreenStates<ScrollState> m_states;
    PhosphorEngine::ScreenContextTracker m_context;
    QSet<QString> m_scrollingScreens;
    QString m_activeScreen;
    /// FIFO of window ids this engine asked the compositor to activate
    /// (applyLayout's focusWindowAfter arm) whose windowFocused report has
    /// not come back yet — the self-activation echo filter. The full
    /// consume/clear contract is documented on windowFocused's drain
    /// (engine_lifecycle.cpp).
    QStringList m_pendingSelfActivations;
    /// Append to m_pendingSelfActivations and trim to the cap — the single
    /// producer path (applyLayout's focus arm and the verb TU's switch).
    void queueSelfActivation(const QString& windowId);
    /// Cap for m_pendingSelfActivations (enforced in queueSelfActivation).
    static constexpr int kMaxPendingSelfActivations = 16;
    /// The one arrival whose focus an `openFocused = false` rule declined, held
    /// until its compositor focus report arrives and is consumed exactly once.
    ///
    /// Why this exists: declining focus rewinds the STRIP's active column, but
    /// the compositor has already focused the arriving window on its own and
    /// reports that focus independently. Without this mark the report reaches
    /// the strip adopt below and re-takes the column the rewind just left, so
    /// the rule reads as a no-op. Confirmed live in a nested session before it
    /// was added.
    ///
    /// Consumed ONCE, deliberately: the rewind also asks the compositor to
    /// re-activate the prior window, so swallowing this single report is what
    /// keeps the strip and the compositor agreeing rather than hiding a
    /// disagreement. A LATER report for the same window is a real user click
    /// and must adopt normally, which is why this is a one-shot rather than a
    /// standing veto.
    ///
    /// A SET rather than a single slot: two non-burst opens can both decline
    /// focus before either compositor report lands, and a slot's overwrite
    /// did not merely degrade the OLDER arrival — its report then fell
    /// through to the reclaim below, clearing BOTH queued prior-window
    /// echoes and adopting the arrival, the exact rewind-undo the mark
    /// exists to prevent. Growth is bounded the same way
    /// m_pendingSelfActivations' is: entries are consumed on match and swept
    /// on windowClosed, handoffRelease and releaseScreenState.
    QSet<QString> m_declinedOpenFocus;
    /// Arrival-burst bracket depth (IPlacementEngine::beginArrivalBurst).
    /// While positive, windowOpened defers its per-arrival applyLayout into
    /// m_burstPendingApplies (context key → whether any deferred arrival took
    /// focus) and the outermost endArrivalBurst applies once per screen —
    /// a daemon-restart re-announce then resolves the restored strip in one
    /// geometry batch instead of N visible partial-strip intermediates.
    int m_arrivalBurstDepth = 0;
    QHash<PhosphorEngine::PlacementStateKey, bool> m_burstPendingApplies;
    /// Armed by the context setters (desktop/activity switch), consumed by
    /// setActiveScreens so the identical-set re-emit only claims
    /// isDesktopSwitch=true for a REAL switch — same contract as
    /// AutotileEngine::m_isDesktopContextSwitch.
    bool m_isDesktopContextSwitch = false;

    /// Cached layout parameters rebuilt by refreshConfigFromSettings().
    QList<qreal> m_presetColumnWidths{1.0 / 3.0, 0.5, 2.0 / 3.0};
    QList<qreal> m_presetWindowHeights{1.0 / 3.0, 0.5, 2.0 / 3.0};
    CenterFocusedColumn m_centerFocusedColumn = CenterFocusedColumn::Never;
    /// Cached tri-state intent from the global config. NEVER the resolved
    /// axis: under Auto two screens with no per-screen key resolve
    /// differently, so a cached verdict would hand one monitor the other's.
    int m_stripAxis = 0;
    bool m_alwaysCenterSingleColumn = false;
    /// Crop mode: keep TRUE rects for partial edge columns and rely on the
    /// effect forcing GL composition + per-output culling to crop the
    /// overhang. When false (default) the emit loop clamps the rect at the
    /// screen edge instead, which no present path can bypass.
    bool m_cropStraddlers = false;
    /// Edge auto-scroll cache (refreshConfigFromSettings). Seeded from the
    /// IScrollSettings defaults rather than from repeated literals: those
    /// bodies are already pinned to ConfigDefaults by static_asserts in
    /// src/config/settings/scrolling.cpp, so taking them here makes one
    /// source of truth instead of a third uncoordinated copy. They govern
    /// behaviour until the first refresh, and for any settings object that
    /// is not an IScrollSettings they govern for good.
    bool m_dragScrollEnabled = PhosphorEngine::IScrollSettings::kDragScrollEnabledDefault;
    int m_dragScrollTriggerWidth = PhosphorEngine::IScrollSettings::kDragScrollTriggerWidthDefault;
    int m_dragScrollDelayMs = PhosphorEngine::IScrollSettings::kDragScrollDelayMsDefault;
    int m_dragScrollMaxSpeed = PhosphorEngine::IScrollSettings::kDragScrollMaxSpeedDefault;
    ColumnWidth m_defaultColumnWidth = ColumnWidth::makeProportion(0.5);
    /// "Client decides" default width: open at the client's initial size.
    /// This is the GLOBAL verdict only — a per-screen kind override answers
    /// for its own screen through effectiveWidthClientDecides, which every
    /// open-path consumer must use instead of reading this directly.
    bool m_defaultWidthClientDecides = false;
    ColumnDisplay m_defaultColumnDisplay = ColumnDisplay::Normal;
    /// Tab-indicator GEOMETRY, the half of Scrolling.TabIndicator that changes
    /// resolved rects (IScrollSettings documents the split).
    TabIndicatorParams m_tabIndicator{};
    /// Scrolling.Behavior tunables (refreshConfigFromSettings). Sticky
    /// handling gates INSERTION only — the desktop-pin logic in
    /// updateStickyScreenPins stays unconditional, matching autotile.
    PhosphorEngine::StickyWindowHandling m_stickyWindowHandling = PhosphorEngine::StickyWindowHandling::TreatAsNormal;
    bool m_respectMinimumSize = true;
    /// Scrolling's OWN Scrolling.Behavior/SmartGaps value, not a forward of the
    /// tiling one. Seeded to match ConfigDefaults::scrollingSmartGaps(), which
    /// is false: tiling defaults this on because a sole window fills the screen
    /// and gaps around it frame nothing, but a sole COLUMN sits at its own
    /// width, so dropping the gaps only pins it to one edge with dead space
    /// beside it. The seed matters for any engine that never receives an
    /// IScrollSettings — refreshConfigFromSettings early-returns for those, so
    /// the initializer governs for good.
    bool m_smartGaps = false;
    /// Default height intent for fresh tiles (Auto = historical even split).
    WindowHeight m_defaultWindowHeight{};
    /// Where a fresh open's column enters the strip (config default; the
    /// openColumnPlacement rule and remembered positions outrank it).
    ScrollInsertPosition m_insertPosition = ScrollInsertPosition::RightOfActive;

    /// The exact rect last APPLIED per window while strip-managed (float-back
    /// poison guard; see PlacementEngineBase::lastManagedRect).
    QHash<QString, QRect> m_lastAppliedRect;
    /// Windows whose last EMITTED batch entry carried windowedFullscreen —
    /// the flag's own leg of applyLayout's emit-on-change gate (a toggle
    /// never moves a rect). The emitted value IS the model flag: parks and
    /// hidden tabs no longer suppress it (suppressing cycled the client's
    /// fullscreen presentation on every scroll past a flagged column).
    /// A QSet because only "told true" is representable.
    /// Dropped alongside m_lastAppliedRect on the context-teardown paths
    /// and swept by aliveness in pruneStaleWindows. Elsewhere a stale entry
    /// is self-correcting rather than co-dropped: any path that drops the
    /// rect memory forces an emit, and that batch carries the current
    /// model flag.
    QSet<QString> m_lastAppliedWindowedFs;
    /// Which screen edge each currently-parked window went out by — one of
    /// "left", "right", "top" or "bottom". Which PAIR is in play is decided by
    /// the screen's strip axis: a horizontal strip goes out left/right, a
    /// vertical one top/bottom. So that when the window scrolls back INTO the
    /// viewport the batch can tell the effect which side to animate it in
    /// from. Remembered rather
    /// than derived: the park position is direction-agnostic, so the parked
    /// rect cannot answer. The write/consume/eviction contract (including
    /// the two deliberate rect-drop exceptions) is documented at the park
    /// sites in engine_apply.cpp.
    QHash<QString, QString> m_parkedScrollEdge;
    /// What a floated/minimized window's column held, so unfloat restores
    /// the slot AND the user's width/display intent (a Proportion/Preset
    /// column must not come back as the default width). Value type hoisted
    /// to ScrollStashTypes.h (the stash types' file-size-ceiling precedent).
    QHash<QString, FloatRestore> m_floatRestore;
    /// Live drag-insert preview state (drag_preview.cpp). The structural
    /// edits a preview makes while it is LIVE are signal-silent, mirroring
    /// autotile's contract, so the daemon's float bookkeeping never sees the
    /// transient begin/update round trip.
    ///
    /// Both ENDINGS announce, not just commit. Commit emits
    /// windowFloatingStateSynced for every entry mode except a plain
    /// same-screen tiled reorder, and cancel emits it too on the arms that
    /// re-home a window whose prior context died — those paths genuinely
    /// changed which strip holds the window, so leaving the daemon's
    /// bookkeeping stale would be the bug.
    std::optional<DragInsertPreview> m_dragInsertPreview;
    /// Canonical id of the window under a compositor interactive move (see
    /// setInteractiveDragWindow). Independent of the preview: the mark
    /// covers the WHOLE drag, trigger held or not.
    QString m_interactiveDragWindow;
    // drag_preview.cpp
    /// Capture @p windowId's current slot in FloatRestore vocabulary — the
    /// twin of floatWindowInternal's capture block.
    static FloatRestore captureDragSlot(const ScrollStrip& strip, const QString& windowId);
    /// Re-insert @p windowId into @p strip from a FloatRestore-shaped slot
    /// (anchor arm → column arm → fresh-column fallback), silently. Shared
    /// by begin (same-screen floating entry) and cancel.
    bool dragPreviewRestoreSlot(ScrollState* state, const QString& windowId, const FloatRestore& slot,
                                const ScrollLayoutParams& params, const QString& screenId);
    /// Preview hygiene for a closing window: drops the preview without
    /// restoration when the DRAGGED window closes, and discards the stale
    /// hit-tested target when a NEIGHBOUR in the target strip closes (the
    /// remembered indexes were aimed at a structure that is changing).
    void dropClosedWindowFromDragPreview(const QString& windowId);
    /// Windows floated BY scroll mode (mode-transition marker, ephemeral).
    QSet<QString> m_scrollFloatedWindows;
    /// Restore-order seed for deterministic mode transitions. The captured
    /// list stays INTACT while it lives (later arrivals count their
    /// earlier-arrived neighbours by original position); consumption is
    /// tracked in m_consumedInitialOrder, and both entries drop once every
    /// listed id is consumed.
    QHash<QString, QStringList> m_pendingInitialOrder;
    /// Ids already consumed from a screen's seed (subset of the seed list).
    /// Kept beside, not inside, the list so consuming an id cannot shift the
    /// recorded positions of the ids still pending.
    QHash<QString, QSet<QString>> m_consumedInitialOrder;
    /// Focus seed for a mode transition, the companion to
    /// m_pendingInitialOrder. Consumed at the END of the arrival burst rather
    /// than per arrival: the seeded window is usually not the last to
    /// re-announce, and every positional insert leaves the strip pointed at
    /// whichever column it adopted first, so an eager apply is overwritten by
    /// the arrivals that follow. Dropped on consumption, and on a seed
    /// replacement, so it can never re-anchor a strip the user has since moved.
    QHash<QString, QString> m_pendingInitialFocus;
    /// Snapshot @p state's strip as a stash entry: columns, focus, view
    /// anchor, captured axis, and the blueprint cursor with the blueprint
    /// identity it counts against. Empty columns list when the state is null
    /// or its strip is empty — but a strip that is merely EMPTY still reports
    /// its cursor, which is the whole content of the entry
    /// stashStripStructure stores for an all-floated screen.
    ///
    /// @p preResolvedFallbackAxis carries the same PRECONDITION documented on
    /// stashStripStructure above: mandatory for a caller inside an m_states
    /// walk, because the nullopt path resolves the axis live and invokes the
    /// injected providers.
    StashedStrip
    buildStashFromState(const ScrollState* state,
                        std::optional<PhosphorProtocol::ScrollAxis> preResolvedFallbackAxis = std::nullopt) const;
    /// Mode-round-trip structure stash (see stashStripStructure). The
    /// stashed lists stay INTACT while they live (positions are counted
    /// against windows already present); consumption is tracked in
    /// m_stripStashConsumed and both entries drop once every stashed id is
    /// consumed. Swept with the context on desktop/activity/output removal,
    /// and by pruneStaleWindows on ALIVENESS — which is the only sweep that
    /// reaches a stash whose context is still live, i.e. a window that closed
    /// while its screen sat in another mode. Entries staged from persistence
    /// are exempt from the aliveness sweep until their first claim; see
    /// StashedTile::stagedFromPersistence. The stash value types live in
    /// ScrollStashTypes.h (hoisted for the file-size ceiling).
    QHash<PhosphorEngine::PlacementStateKey, StashedStrip> m_stripStash;
    QHash<PhosphorEngine::PlacementStateKey, QSet<QString>> m_stripStashConsumed;
    /// How long StashedStrip::fuzzyClaimWindow stays open, in ms. Seeded from
    /// kFuzzyClaimGraceMs in the constructor (the constant lives in the
    /// engine-internal enginelimits.h, which this exported header must not
    /// pull in) and only ever changed by the test seam below.
    qint64 m_fuzzyClaimGraceMs;
    /// Ever-increasing stamp source for StashedStrip::sequence.
    quint64 m_stashSequence = 0;
    /// Screens with a retile queued this event-loop pass (coalescing).
    QSet<QString> m_pendingRetiles;
    /// Whether the last tabStripsChanged emission for a screen was
    /// non-empty, so an empty state is announced exactly once.
    QSet<QString> m_screensWithTabStrips;
    /// Last non-empty tab-strip payload broadcast per screen — the
    /// emit-on-change gate for tabStripsChanged (the empty case latches via
    /// m_screensWithTabStrips instead). Swept with the screen's state.
    QHash<QString, QString> m_lastTabStripPayload;

    // engine_overrides.cpp
    /// Effective per-screen values: the rule override when present, else the
    /// cached config default. Each accessor is a thin screenId wrapper over a
    /// map-taking overload, so a caller resolving several values for one
    /// screen (layoutParamsForScreen resolves eleven per relayout, and the open
    /// path four more) fetches the override map ONCE and threads it through
    /// instead of re-looking it up per accessor.
    CenterFocusedColumn effectiveCenterFocusedColumn(const QString& screenId) const;
    CenterFocusedColumn effectiveCenterFocusedColumn(const QVariantMap& overrides) const;
    /// The six scrolling BEHAVIOUR toggles, rule-only per-screen keys layered
    /// over the config-seeded members. Four of them (always-center-single-
    /// column, respect-minimum-size, smart gaps and the straddler clamp) are
    /// consumed inside layoutParamsForScreen and exist ONLY in map-taking
    /// form, since that is the one call site and it has already fetched the
    /// map. The other two (the open-path focus arm and the sticky gate) are
    /// consumed outside it and carry a screenId wrapper; the sticky gate also
    /// keeps a map-taking form, because the open path resolves several values
    /// for one screen off a single fetch.
    bool effectiveAlwaysCenterSingleColumn(const QVariantMap& overrides) const;
    bool effectiveRespectMinimumSize(const QVariantMap& overrides) const;
    bool effectiveSmartGaps(const QVariantMap& overrides) const;
    /// Hoisted out of the emit loop by its one caller: it is a per-SCREEN
    /// verdict, so re-resolving it per tile would rebuild the override map
    /// once per window on the relayout path.
    bool effectiveCropStraddlers(const QVariantMap& overrides) const;
    /// Falls back to the LIVE IScrollSettings read rather than a cached
    /// member: focus-new-windows is the one behaviour the engine never
    /// cached, and reading it live keeps a settings change effective without
    /// waiting for a settings-reload pass.
    bool effectiveFocusNewWindows(const QString& screenId) const;
    PhosphorEngine::StickyWindowHandling effectiveStickyWindowHandling(const QString& screenId) const;
    PhosphorEngine::StickyWindowHandling effectiveStickyWindowHandling(const QVariantMap& overrides) const;
    /// Shared bool-override reader for the five toggles above: takes the
    /// override only when it is a real bool, so a hand-edited string cannot
    /// coerce to false and silently disable a behaviour.
    static bool effectiveBoolOverride(const QVariantMap& overrides, const QString& key, bool fallback);
    ColumnWidth effectiveDefaultColumnWidth(const QString& screenId) const;
    ColumnWidth effectiveDefaultColumnWidth(const QVariantMap& overrides) const;
    /// Vocabulary-taking overload, the same "resolve it ONCE" shape as the
    /// override map above: a Preset kind resolves its spin against the
    /// screen's effective width list, which layoutParamsForScreen has already
    /// parsed for the params it hands the strip. The one-argument map-taking
    /// form (above) is a wrapper that parses it again, for the call sites that
    /// need only this one value.
    ColumnWidth effectiveDefaultColumnWidth(const QVariantMap& overrides, const QList<qreal>& presetWidths) const;
    /// The RULE channel's bare default-width fraction, when the map carries
    /// one that clears the range effectiveDefaultColumnWidth accepts. The
    /// open path's ClientDecides gate asks THIS rather than testing the key's
    /// presence: an out-of-range rule value contributes no width (the
    /// resolver falls through to the configured default), so a presence test
    /// suppressed the client-sized open on the strength of a value that
    /// changed nothing.
    static std::optional<qreal> ruleColumnWidthFraction(const QVariantMap& overrides);
    /// Whether "the client decides" is the EFFECTIVE default-width verdict
    /// for @p screenId: a per-screen kind override answers for itself (true
    /// only when it IS ClientDecides), and an absent — or unrecognised —
    /// override defers to the cached global flag, on the same terms
    /// effectiveDefaultColumnWidth falls through. The open path must use this
    /// rather than the raw global, or a monitor scoped TO ClientDecides reads
    /// as "pinned to a width" and gets the opposite of what the user chose.
    bool effectiveWidthClientDecides(const QString& screenId) const;
    bool effectiveWidthClientDecides(const QVariantMap& overrides) const;
    /// The width resetStripToDefaults hands the strip for the screen whose
    /// resolved @p overrides these are: params.defaultColumnWidth (which
    /// already folds a rule's fraction in, rule > screen > global), or
    /// std::nullopt when the effective verdict is "the client decides" and no
    /// rule pins a width. The same two-term test the open path applies (minus
    /// its tracker term, which only gates READING a client size; a reset
    /// reads none). Map-taking only, like effectiveWidthClientDecides' inner
    /// form, so the verb resolves the map once for both defaults it needs.
    std::optional<ColumnWidth> resetDefaultColumnWidthFor(const QVariantMap& overrides,
                                                          const ScrollLayoutParams& params) const;
    ColumnDisplay effectiveDefaultColumnDisplay(const QString& screenId) const;
    ColumnDisplay effectiveDefaultColumnDisplay(const QVariantMap& overrides) const;
    /// Height needs the work area AND the axis: the rule channel's bare
    /// fraction is committed as Fixed pixels against the live work area's
    /// CROSS extent, which is what a window height divides. The axis is a
    /// parameter rather than re-resolved inside because the caller on the
    /// layout path has already resolved it for this very work area, and two
    /// independent resolves of the same question are two things to keep in
    /// step.
    WindowHeight effectiveDefaultWindowHeight(const QString& screenId, const QRect& workArea, StripAxis axis) const;
    WindowHeight effectiveDefaultWindowHeight(const QVariantMap& overrides, const QRect& workArea,
                                              StripAxis axis) const;
    /// Vocabulary-taking overload — the height twin of the width one above.
    WindowHeight effectiveDefaultWindowHeight(const QVariantMap& overrides, const QRect& workArea, StripAxis axis,
                                              const QList<qreal>& presetHeights) const;
    ScrollInsertPosition effectiveInsertPosition(const QString& screenId) const;
    ScrollInsertPosition effectiveInsertPosition(const QVariantMap& overrides) const;
    /// Per-property override, so a rule that sets only the position leaves the
    /// other six geometry fields on their configured values.
    TabIndicatorParams effectiveTabIndicator(const QString& screenId) const;
    TabIndicatorParams effectiveTabIndicator(const QVariantMap& overrides) const;
    /// Map-taking overloads of the two PUBLIC preset accessors (declared in
    /// the public section above), private because the override map is.
    QList<qreal> effectivePresetColumnWidths(const QVariantMap& overrides) const;
    QList<qreal> effectivePresetWindowHeights(const QVariantMap& overrides) const;

    /// Keyed per CONTEXT, not per screen, because that is how the producer
    /// resolves them: the daemon asks scrollingTemplateForContext(screen,
    /// desktop, activity) and pushes the answer for the context that is
    /// current at the time. Keying by screen alone meant two desktops on one
    /// monitor could not hold different templates at all — the last resolve
    /// won for both, and switching between them reset each other's blueprint
    /// cursor. Readers go through overridesForScreen(), which resolves the
    /// screen's CURRENT context, so the public screenId-taking accessors are
    /// unchanged.
    ///
    /// ONE EXCEPTION to "the context the producer resolved for": a screen
    /// under a sticky-desktop pin. currentKeyForScreen answers with the PINNED
    /// desktop (the pin outranks the per-output desktop), while the daemon
    /// resolves its template against the LIVE one and knows nothing about the
    /// pin — it is engine-internal state with no accessor. So a pinned
    /// screen's entry is filed under the pinned desktop while describing the
    /// live desktop's template. Reads stay self-consistent, because they
    /// resolve the same pinned key, and the pin exists precisely because the
    /// desktop dimension is meaningless for an all-sticky screen. Documented
    /// rather than re-keyed: re-keying would have to teach the daemon about
    /// the pin, which is a wider change than the defect justifies.
    QHash<PhosphorEngine::PlacementStateKey, QVariantMap> m_perScreenOverrides;
    std::function<void()> m_persistSaveFn;
    std::function<void()> m_persistLoadFn;
    FloatPredicate m_floatPredicate;
    RestorePositionPredicate m_restorePositionPredicate{};
    OpenParamsResolver m_openParamsResolver;
    ModeResolver m_snappingModeResolver;
    ModeResolver m_scrollingModeResolver;
    ModeResolver m_autotileModeResolver;
    ContextGapProvider m_contextGapProvider;
};

} // namespace PhosphorScrollEngine
