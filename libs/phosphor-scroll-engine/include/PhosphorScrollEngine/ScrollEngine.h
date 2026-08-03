// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorscrollengine_export.h>

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/PerScreenStates.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/ScreenContextTracker.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QRect>
#include <QVariantMap>

#include <functional>
#include <optional>
#include <QSet>
#include <QString>
#include <QStringList>

namespace PhosphorEngine {
class WindowRegistry;
class IWindowTrackingService;
class IScrollSettings;
}

namespace PhosphorScreens {
class ScreenManager;
}

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
};

/**
 * @brief niri-style scrolling placement engine.
 *
 * Implements IPlacementEngine for screens assigned to Scrolling mode. Each
 * (screen, virtual desktop, activity) context owns one ScrollState wrapping a
 * ScrollStrip — an ordered list of columns on an unbounded horizontal strip
 * viewed through the screen's work area. The defining invariant: OPENING A
 * WINDOW INSERTS A NEW COLUMN AND RESIZES NOTHING; only the viewport scrolls.
 *
 * Geometry leaves the engine as the same windowsTiled JSON contract the
 * autotile engine emits, relayed by the shared tiling adaptor to the KWin
 * effect's tile-request path (staggered apply, supersession epochs, maximize
 * reset). Columns scrolled out of the viewport are parked with real geometry
 * just outside the nearest work-area edge — never at extreme coordinates —
 * so input hit-testing stays sane and enter/leave scroll animations morph
 * from a believable origin. Hidden tiles of a tabbed column park the same
 * way (a hidden tab must not sit under the active tile stealing clicks).
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
    /// delta -1 = left, +1 = right (niri consume-or-expel-window-left/right).
    void consumeOrExpelWindow(int delta, const QString& screenId);
    void centerColumn(const QString& screenId);
    void toggleColumnTabbed(const QString& screenId);
    /// delta -1/+1 through the preset width list.
    void cycleColumnPresetWidth(int delta, const QString& screenId);
    /// deltaPercent of the work-area width (e.g. +10 / -10).
    void adjustColumnWidth(qreal deltaPercent, const QString& screenId);
    void toggleMaximizeColumn(const QString& screenId);
    void expandColumnToAvailableWidth(const QString& screenId);
    void cycleWindowPresetHeight(int delta, const QString& screenId);
    void adjustWindowHeight(qreal deltaPercent, const QString& screenId);
    void resetWindowHeights(const QString& screenId);

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
    /// One visible tile of a strip: the unit of the scroll "zone number"
    /// space. Zone number N is visibleTiles(screenId).at(N - 1) —
    /// sequential in strip order (columns left to right, tiles top to
    /// bottom). That is the ADDRESS space, and it is single-sourced:
    /// previews label it and the Snap-to-Zone digits resolve against it
    /// through moveFocusedToPosition, both from this one walk, so they
    /// always name the same tile.
    ///
    /// The ACTION a digit performs is COARSER than the address it resolves.
    /// A digit naming a stack-mate of the operated window reorders that
    /// window inside its column, but a digit naming a tile in ANOTHER
    /// column moves the whole active COLUMN to that column's strip
    /// position — the deliberate pre-tile-numbering behaviour, kept because
    /// the column is the strip's unit of travel. The consequence is real
    /// and is not a bug: stack-mates travel along, and after a cross-column
    /// move the operated window's own number may differ from the digit
    /// pressed (any stacked column shifts the walk, and Always/OnOverflow
    /// centering re-derives the visible set around the new active column).
    struct VisibleTile
    {
        QString windowId;
        /// Strip index of the owning column. Not the zone number, and not
        /// unique across the walk — a stacked column contributes one entry
        /// per visible tile, all carrying the same index. Read by callers
        /// that need to tell stack-mates apart from separate columns; the
        /// engine's own tests are the only such caller today.
        int columnIndex = -1;
        /// The tile's 1-based zone number, stamped by the walk. THE number
        /// space: every consumer (preview labels, the OSD, the Snap-to-Zone
        /// digits) reads this rather than re-deriving an ordinal from its
        /// own iteration, so no consumer can drift out of step with another.
        int zoneNumber = 0;
        /// Absolute pixel rect, clipped to the work area.
        QRect rect;
    };
    /// The visible tiles of @p screenId's current-context strip in zone-
    /// number order. Not every on-screen window is here: hidden tabs of a
    /// tabbed column, minimized tiles, parked columns, and tiles whose
    /// intersection with the work area is EMPTY (a stack whose min heights
    /// overflow the work area resolves its tail below the bottom edge) all
    /// carry no number and cannot be reached by a digit. Partially-visible
    /// columns are CLIPPED, not dropped, with no minimum-visibility
    /// threshold: an arbitrarily thin sliver still carries its own number,
    /// because the cut-off edge is what tells the viewer the strip
    /// continues off-screen. Empty when the screen has no state, no visible
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
    /// it is off-screen, a hidden tab, or untracked (the navigation OSD
    /// then shows direction-only copy).
    ///
    /// THE resolver for the number space in the window→number direction: it
    /// reads VisibleTile::zoneNumber off the same walk the rect consumers
    /// read, so a caller holding a window id never has to count the walk
    /// itself. Callers that already hold a VisibleTile read its zoneNumber
    /// instead of paying for a second walk here.
    int visibleTileNumberForWindow(const QString& screenId, const QString& windowId) const;
    /// visibleTileRects normalized to the FULL screen geometry (0.0–1.0 per
    /// axis) — the shape zone previews consume. The tiles are clipped to the
    /// gap-inset work area, so the fractions show the panel gap; that is the
    /// same basis the daemon's own OSD card uses (its twin renorm of the
    /// absolute rects in stripzones.h), so the settings thumbnail and the
    /// OSD draw the same shape. Its one production consumer is the D-Bus
    /// strip payload (scrollingadaptor.cpp), which pairs each rect with the
    /// zone number from the matching visibleTiles entry: where a layout
    /// switch shows the layout's zones, a scrolling screen shows what the
    /// strip actually looks like right now. Falls back to the work area as
    /// the basis only when no screen rect is resolvable. Same emptiness
    /// contract.
    ///
    /// The pairing is index-wise and both walks run in the same synchronous
    /// call, so a caller reading this beside visibleTiles gets rects and
    /// numbers for the same tiles in the same order.
    QVector<QRectF> visibleTileRectsRelative(const QString& screenId) const;
    void setInitialWindowOrder(const QString& screenId, const QStringList& windowIds) override;
    int pruneStaleWindows(const QSet<QString>& aliveWindowIds) override;

    // Layout capability (see IPlacementEngine's Layout capability section)
    /// The strip consumes layouts as sizing TEMPLATES: a layout's zone
    /// x-extents become the screen's preset column-width vocabulary (and its
    /// stacked-zone heights the height vocabulary, when it defines any),
    /// never window placement. Explicit override — the capability is load-bearing
    /// for the daemon's layout-selection gates, not an inherited absence.
    LayoutSupport layoutSupport() const override
    {
        return LayoutSupport::Templates;
    }

    /// Effective preset vocabulary for a screen: the per-screen TEMPLATE
    /// override when the daemon pushed a usable one (every entry validated
    /// against the same floor as the settings parser), else the cached
    /// settings list. Wholesale replacement, never a merge — see
    /// ScrollPerScreenKeys. Public for D-Bus/introspection consumers
    /// (ScrollingAdaptor::presetVocabularyJson).
    QList<qreal> effectivePresetColumnWidths(const QString& screenId) const;
    QList<qreal> effectivePresetWindowHeights(const QString& screenId) const;

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
    /// "screenId|desktop|activity". Live wins on a key collision. The
    /// daemon persists this blob through the WTA KConfig layer so a login
    /// restore rebuilds stacked columns (with each column's active tile,
    /// i.e. a tabbed column's shown tab), the strip focus, and the view
    /// anchor instead of one default column per window. Per-tile height
    /// intents ride along; per-window minimum sizes do not — the client
    /// re-reports those. (engine_serialize.cpp)
    QJsonObject serializeStripState() const;
    /// Load a serializeStripState blob into the stash so the EXISTING
    /// arrival-restore path (restoreFromStripStash) rebuilds each strip as
    /// its windows are announced. Additive and conservative: keys that
    /// already have a stash entry or a live populated state are skipped, so
    /// a second load cannot re-stage stale structure over adopted windows.
    void restoreStripState(const QJsonObject& state);
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

    /// Snapping-mode resolver for windowOpened's cross-screen snap-restore
    /// defer gate (the reciprocal of SnapEngine::resolveWindowRestore's
    /// recorded-screen gate; AutotileEngine::windowOpened carries the same
    /// gate). Invoked as (screenId, virtualDesktop, activity) and must
    /// answer whether that context resolves to Snapping mode AND snapping
    /// is globally preferred — the daemon bakes both into the closure so
    /// this library stays free of the zones-layer mode type. Unset → the
    /// gate is off and every open is claimed (headless/test path). Same
    /// clear-before-destroy contract as the other injected closures.
    using SnappingModeResolver = std::function<bool(const QString& screenId, int desktop, const QString& activity)>;
    void setSnappingModeResolver(SnappingModeResolver resolver)
    {
        m_snappingModeResolver = std::move(resolver);
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
    /// Ignored while a ScreenManager is present.
    void setScreenGeometryProviders(std::function<QRect(const QString&)> availableGeometry,
                                    std::function<QRect(const QString&)> screenGeometry)
    {
        m_availableGeometryProvider = std::move(availableGeometry);
        m_screenGeometryProvider = std::move(screenGeometry);
    }

    void setContextGapProvider(ContextGapProvider provider)
    {
        m_contextGapProvider = std::move(provider);
    }

    // Per-screen overrides layered over the config defaults, one map per
    // screen with three producer channels the daemon merges (rules win): the
    // RULE channel (SetScrollDefaultColumnWidth / SetCenterFocusedColumn /
    // SetScrollDefaultColumnDisplay / SetScrollInsertPosition /
    // SetScrollDefaultWindowHeight), the SETTINGS channel (the per-monitor
    // New-columns sizing trio pairs), and the TEMPLATE channel (the
    // presetColumnWidths / presetWindowHeights lists extracted from the
    // context's assigned template layout; wholesale per-list replacement).
    // Key spellings live in ScrollPerScreenKeys (ScrollTypes.h) — the
    // accessor comments there are the authoritative key list.
    void applyPerScreenConfig(const QString& screenId, const QVariantMap& overrides) override;
    void clearPerScreenConfig(const QString& screenId) override;
    QVariantMap perScreenOverrides(const QString& screenId) const override
    {
        return m_perScreenOverrides.value(screenId);
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
    /// screen's indicators. The daemon enriches window ids with titles and
    /// urgency, adds the paint settings, and drives the overlay.
    void tabStripsChanged(const QString& screenId, const QString& stripsJson);

private:
    // engine_core.cpp
    QString canonicalizeForLookup(const QString& rawWindowId) const;
    PhosphorEngine::PlacementStateKey currentKeyForScreen(const QString& screenId) const
    {
        return m_context.currentKeyForScreen(screenId);
    }
    ScrollState* stateForKey(const PhosphorEngine::PlacementStateKey& key, bool createIfMissing);
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
    /// every windowOpened outcome (tiled, consumed, floated, and the
    /// cross-screen snap-restore defer that hands the window to snap), or a
    /// stale seed survives to re-position an unrelated later open.
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
    void stashStripStructure(const PhosphorEngine::PlacementStateKey& key, const ScrollState* state);
    /// insertOpenedWindow's stash restore: place @p windowId per the
    /// stashed structure for @p key (rejoin its stashed column beside an
    /// already-arrived sibling, or recreate the column at its stashed
    /// position with its width/display), re-applying its height intent.
    /// Returns false when the stash has no verdict (no entry, id absent,
    /// or already consumed) — the caller falls through to the seed path.
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
    /// same work area, and a defaulted parameter only two of eighteen call
    /// sites passed left the verbs computing against a gapped rect the apply
    /// path then un-gapped: a lone column off-centre by (outerL+outerR)/2,
    /// leftover width nobody claimed, and a maximize compare that never
    /// matched. Inner gaps need no arm — with one column no inter-column gap
    /// exists.
    ScrollLayoutParams layoutParamsForScreen(const QString& screenId) const;
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
    /// caller must not announce one.
    bool insertOpenedWindow(ScrollState* state, const QString& windowId, const QString& screenId, int minWidth,
                            int minHeight);
    /// Give a window that floats WITHOUT ever having been a strip tile
    /// (floated at open, or arriving already-floating over the handoff) the
    /// FloatRestore entry the clamp lives in while it floats. column stays
    /// -1: there is no remembered slot, so unfloat opens a fresh column.
    /// Refreshes the clamp on an existing entry rather than overwriting a
    /// real remembered slot with a slotless one.
    void seedFloatRestoreForOpen(const QString& windowId, int minWidth, int minHeight);
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

    PhosphorEngine::IWindowTrackingService* m_windowTracker = nullptr;
    PhosphorScreens::ScreenManager* m_screenManager = nullptr;
    /// Embedder/test seam: geometry providers consulted when NO
    /// ScreenManager is wired (headless hosts). First = available work
    /// area, second = full screen rect (parking bounds). With a
    /// ScreenManager present they are ignored.
    std::function<QRect(const QString&)> m_availableGeometryProvider;
    std::function<QRect(const QString&)> m_screenGeometryProvider;
    PhosphorEngine::WindowRegistry* m_windowRegistry = nullptr;
    PhosphorEngine::ICrossSurfaceResolver* m_crossSurfaceResolver = nullptr;

    PhosphorEngine::PerScreenStates<ScrollState> m_states;
    PhosphorEngine::ScreenContextTracker m_context;
    QSet<QString> m_scrollingScreens;
    QString m_activeScreen;
    /// FIFO of window ids this engine asked the compositor to activate
    /// (applyLayout's focusWindowAfter arm) whose windowFocused report has
    /// not come back yet. The effect reports EVERY activation back through
    /// notifyWindowFocused, including ones this engine initiated, and the
    /// round trip is asynchronous: on a rapid focus scroll the strip has
    /// already advanced past the echoed window by the time the report lands,
    /// and treating that stale echo as user focus rewinds the active column —
    /// the next scroll step then advances from the rewound column and skips
    /// one. windowFocused consumes a matching entry and drops the report; a
    /// NON-matching report clears the whole queue, which is sound because the
    /// effect's calls share one ordered D-Bus connection — any echo sent
    /// earlier has already arrived, so a leftover entry means the effect
    /// dropped that activation (show desktop, window gone). The tab-click
    /// path is unaffected: its activation goes out via the adaptor's
    /// focusWindowRequested, never through this engine's emit, so its echo
    /// is never queued and still drives the strip (signals.cpp documents
    /// that contract).
    QStringList m_pendingSelfActivations;
    /// Arrival-burst bracket depth (IPlacementEngine::beginArrivalBurst).
    /// While positive, windowOpened defers its per-arrival applyLayout into
    /// m_burstPendingApplies (screen → whether any deferred arrival took
    /// focus) and the outermost endArrivalBurst applies once per screen —
    /// a daemon-restart re-announce then resolves the restored strip in one
    /// geometry batch instead of N visible partial-strip intermediates.
    int m_arrivalBurstDepth = 0;
    QHash<QString, bool> m_burstPendingApplies;
    /// Armed by the context setters (desktop/activity switch), consumed by
    /// setActiveScreens so the identical-set re-emit only claims
    /// isDesktopSwitch=true for a REAL switch — same contract as
    /// AutotileEngine::m_isDesktopContextSwitch.
    bool m_isDesktopContextSwitch = false;

    /// Cached layout parameters rebuilt by refreshConfigFromSettings().
    QList<qreal> m_presetColumnWidths{1.0 / 3.0, 0.5, 2.0 / 3.0};
    QList<qreal> m_presetWindowHeights{1.0 / 3.0, 0.5, 2.0 / 3.0};
    CenterFocusedColumn m_centerFocusedColumn = CenterFocusedColumn::Never;
    bool m_alwaysCenterSingleColumn = false;
    ColumnWidth m_defaultColumnWidth = ColumnWidth::makeProportion(0.5);
    /// "Client decides" default width: open at the client's initial size.
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
    /// Shared Tiling.Gaps/SmartGaps value (IScrollSettings forward).
    bool m_smartGaps = true;
    /// Default height intent for fresh tiles (Auto = historical even split).
    WindowHeight m_defaultWindowHeight{};
    /// Where a fresh open's column enters the strip (config default; the
    /// openColumnPlacement rule and remembered positions outrank it).
    ScrollInsertPosition m_insertPosition = ScrollInsertPosition::RightOfActive;

    /// The exact rect last APPLIED per window while strip-managed (float-back
    /// poison guard; see PlacementEngineBase::lastManagedRect).
    QHash<QString, QRect> m_lastAppliedRect;
    /// What a floated/minimized window's column held, so unfloat restores
    /// the slot AND the user's width/display intent (a Proportion/Preset
    /// column must not come back as the default width). The min size IS
    /// captured (minWidth/minHeight below): dropping it would strip the
    /// relayout clamps until the compositor re-reports.
    struct FloatRestore
    {
        int column = -1;
        ColumnWidth width;
        ColumnDisplay display = ColumnDisplay::Normal;
        /// The tile slot inside a SHARED column (-1 when the window had its
        /// own column). A stacked tile's float round-trip re-enters its
        /// surviving stack instead of spawning a new column at the index.
        int tileIndex = -1;
        /// A surviving SIBLING of the shared column, used to re-locate the
        /// stack at restore time — the bare column index goes stale when
        /// columns close while the window floats, and a stale index would
        /// splice the window into a stranger's stack.
        QString stackAnchor;
        /// Client-reported minimum size at float time — the tile that held
        /// it dies with takeWindow, and dropping it would strip the
        /// relayout clamps until the compositor happens to re-report.
        /// Kept CURRENT while the window floats: windowMinSizeUpdated has no
        /// tile to write to then, and without the write-through the unfloat
        /// re-applies whatever the client reported at float time.
        int minWidth = 0;
        int minHeight = 0;
        /// The tile's height INTENT at float time. Same reasoning as the
        /// width/display above: without it a float round trip (which the
        /// effect's minimize machinery also drives) silently reset a
        /// user-set window height to Auto, while a mode round trip — which
        /// stashes the intent — preserved it.
        WindowHeight height;
    };
    QHash<QString, FloatRestore> m_floatRestore;
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
    /// Mode-round-trip structure stash (see stashStripStructure). The
    /// stashed lists stay INTACT while they live (positions are counted
    /// against windows already present); consumption is tracked in
    /// m_stripStashConsumed and both entries drop once every stashed id is
    /// consumed. Swept with the context on desktop/activity/output removal,
    /// and by pruneStaleWindows on ALIVENESS — which is the only sweep that
    /// reaches a stash whose context is still live, i.e. a window that closed
    /// while its screen sat in another mode. Entries staged from persistence
    /// are exempt from the aliveness sweep until their first claim; see
    /// StashedTile::stagedFromPersistence.
    struct StashedTile
    {
        QString windowId;
        WindowHeight height;
        /// Carried for serialization fidelity only — the restore paths do
        /// not re-apply it (the effect re-reports live minimize state).
        ///
        /// It reads false for every tile a production daemon ever stashes:
        /// its source is Tile::minimized, and the only writer of that flag
        /// is ScrollStrip::setWindowMinimized, which is a TEST SEAM (the
        /// daemon models minimize as a float, so a minimized window is not
        /// a strip tile at all). The field exists so the strip model's
        /// minimized domain stays round-trippable if the daemon ever drives
        /// it directly; see the seam note on setWindowMinimized.
        bool minimized = false;
        /// True while THIS tile was staged from the persisted blob and has
        /// not been claimed. Per tile, not per entry: a key co-tenanted by a
        /// returning app and a dead one must age the dead tile out while the
        /// returning one keeps claiming, and a claim on one tile must not
        /// expose an unclaimed co-tenant to the aliveness sweep.
        ///
        /// A staged tile names LAST session's window id, which by design
        /// appears in no live alive-set: the cross-session claim in
        /// restoreFromStripStash matches on the appId prefix precisely
        /// because the per-instance half of the id is regenerated every
        /// launch. pruneStaleWindows' sweep must therefore not read "absent
        /// from the alive set" as "closed" while this holds, or the very
        /// first prune after login (the effect fires one at bring-up, right
        /// after the daemon stages the snapshot) would erase it and undo the
        /// structure/focus/anchor restore. Cleared on claim, at which point
        /// the tile is anchored in THIS session's id space and the sweep is
        /// meaningful. A tile whose app never relaunches is aged out by the
        /// unclaimedSessions lease below instead.
        bool stagedFromPersistence = false;
        /// Consecutive logins THIS tile was staged without ever being
        /// claimed. Incremented at serialize while stagedFromPersistence
        /// holds; a claim zeroes it; restoreStripState drops a tile that has
        /// gone kMaxUnclaimedSessions logins unclaimed. The aging exists
        /// because pruneStaleWindows fires exactly ONCE per session (at
        /// bring-up, while the ENTRY is still sweep-exempt), so no sweep can
        /// ever reach a persisted tile whose app never relaunches — without
        /// the lease it would be re-staged forever and eventually hand an
        /// unrelated same-app window a long-dead slot.
        int unclaimedSessions = 0;
    };
    struct StashedColumn
    {
        QVector<StashedTile> tiles;
        ColumnWidth width;
        ColumnDisplay display = ColumnDisplay::Normal;
        /// The column's ACTIVE tile, by window id — for a Tabbed column
        /// that is the shown tab. Carried because every insert makes the
        /// arriving tile its column's active one, so a restore without it
        /// shows whichever sibling happened to announce last.
        QString activeWindowId;
    };
    /// One stashed strip: the structural columns plus the focus/view pair
    /// whose loss made every mode round trip re-anchor on an arbitrary
    /// window (first arrival won the focus).
    struct StashedStrip
    {
        QVector<StashedColumn> columns;
        QString focusedWindowId;
        int viewAnchor = 0;
        /// Monotonic stamp of when this entry was staged (mode exit or
        /// persistence load), from m_stashSequence. serializeStripState
        /// resolves a window listed by two DIFFERENT keys in favour of the
        /// higher stamp: keys do not collide, so write order cannot decide
        /// it, and the reader's alphabetical first-wins would otherwise let
        /// a window's older screen displace its newer one.
        quint64 sequence = 0;

        bool isEmpty() const
        {
            return columns.isEmpty();
        }
        int tileCount() const
        {
            int total = 0;
            for (const StashedColumn& c : columns) {
                total += c.tiles.size();
            }
            return total;
        }
    };
    /// Snapshot @p state's strip as a stash entry (columns + focus + view
    /// anchor). Empty columns list when the state is null or empty.
    StashedStrip buildStashFromState(const ScrollState* state) const;
    QHash<PhosphorEngine::PlacementStateKey, StashedStrip> m_stripStash;
    QHash<PhosphorEngine::PlacementStateKey, QSet<QString>> m_stripStashConsumed;
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

    /// Effective per-screen values: the rule override when present, else the
    /// cached config default.
    CenterFocusedColumn effectiveCenterFocusedColumn(const QString& screenId) const;
    ColumnWidth effectiveDefaultColumnWidth(const QString& screenId) const;
    ColumnDisplay effectiveDefaultColumnDisplay(const QString& screenId) const;
    /// Height needs the work area: the rule channel's bare fraction is
    /// committed as Fixed pixels against the live work area.
    WindowHeight effectiveDefaultWindowHeight(const QString& screenId, const QRect& workArea) const;
    ScrollInsertPosition effectiveInsertPosition(const QString& screenId) const;
    /// Per-property override, so a rule that sets only the position leaves the
    /// other six geometry fields on their configured values.
    TabIndicatorParams effectiveTabIndicator(const QString& screenId) const;

    QHash<QString, QVariantMap> m_perScreenOverrides;
    std::function<void()> m_persistSaveFn;
    std::function<void()> m_persistLoadFn;
    FloatPredicate m_floatPredicate;
    RestorePositionPredicate m_restorePositionPredicate{};
    OpenParamsResolver m_openParamsResolver;
    SnappingModeResolver m_snappingModeResolver;
    ContextGapProvider m_contextGapProvider;
};

} // namespace PhosphorScrollEngine
