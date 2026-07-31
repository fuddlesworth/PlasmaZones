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
    QString activeScreen() const override
    {
        return m_activeScreen;
    }
    void setActiveScreenHint(const QString& screenId) override;

    // ═══════════════════════════════════════════════════════════════════════
    // Window lifecycle
    // ═══════════════════════════════════════════════════════════════════════

    using IPlacementEngine::windowOpened;
    void windowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight) override;
    void windowClosed(const QString& windowId) override;
    void windowFocused(const QString& windowId, const QString& screenId) override;
    void windowMinSizeUpdated(const QString& windowId, int minWidth, int minHeight) override;
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
    void rotateWindows(bool clockwise, const PhosphorEngine::NavigationContext& ctx) override;
    void reapplyLayout(const PhosphorEngine::NavigationContext& ctx) override;
    void snapAllWindows(const PhosphorEngine::NavigationContext& ctx) override;
    void cycleFocus(bool forward, const PhosphorEngine::NavigationContext& ctx) override;
    void pushToEmptyZone(const PhosphorEngine::NavigationContext& ctx) override;
    void restoreFocusedWindow(const PhosphorEngine::NavigationContext& ctx) override;
    void toggleFocusedFloat(const PhosphorEngine::NavigationContext& ctx) override;

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
    bool isWindowTracked(const QString& windowId) const override;
    bool isWindowTiled(const QString& windowId) const override;
    bool isWindowManaged(const QString& windowId) const override;
    QString screenForTrackedWindow(const QString& windowId) const override;
    QRect lastManagedRect(const QString& rawWindowId) const override;
    QStringList managedWindowOrder(const QString& screenId) const override;
    /// Visible-tile rects of @p screenId's current-context strip in strip
    /// order, clipped to the work area (hidden tabs and parked columns
    /// excluded). The daemon's OSD preview seam: where a layout switch
    /// shows the layout's zones, a scrolling screen shows what the strip
    /// actually looks like right now. Empty when the screen has no state
    /// or no visible tile.
    /// @p columnNumbers, when given, receives one entry per returned rect:
    /// the tile's 1-based VISIBLE column slot (leftmost on-screen column is
    /// 1) — the scroll "zone number" the Snap-to-Zone digits target — so
    /// previews label exactly what is on screen. Off-screen columns carry
    /// no number by design.
    QVector<QRect> visibleTileRects(const QString& screenId, QVector<int>* columnNumbers = nullptr) const;
    /// @p windowId's 1-based visible column slot on @p screenId's current
    /// strip, or -1 when its column is off-screen or untracked (the
    /// navigation OSD then shows direction-only copy).
    int visibleColumnNumberForWindow(const QString& screenId, const QString& windowId) const;
    /// visibleTileRects normalized to the work area (0.0–1.0 per axis) —
    /// the shape zone previews consume. Same emptiness contract.
    QVector<QRectF> visibleTileRectsRelative(const QString& screenId, QVector<int>* columnNumbers = nullptr) const;
    void setInitialWindowOrder(const QString& screenId, const QStringList& windowIds) override;
    int pruneStaleWindows(const QSet<QString>& aliveWindowIds) override;

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
    /// enters the viewport's left edge. Empty when the strip is empty.
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
    /// restore rebuilds tabbed/stacked columns, focus, and the view anchor
    /// instead of one default column per window. (engine_serialize.cpp)
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

    // Per-context rule overrides (SetScrollDefaultColumnWidth /
    // SetCenterFocusedColumn / SetScrollDefaultColumnDisplay), layered over
    // the config defaults per screen. Map keys: "CenterFocusedColumn" (int),
    // "DefaultColumnWidth" (double fraction), "DefaultColumnDisplay" (int).
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
    /// Tab-strip indicator model for @p screenId, emitted after every
    /// relayout: a JSON array with one entry per VISIBLE tabbed column —
    /// {x, y, width (absolute px), activeIndex, tabs: [windowId, ...]}.
    /// An empty array clears the screen's indicators. The daemon enriches
    /// window ids with titles and drives the overlay.
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
    bool restoreFromStripStash(ScrollState* state, const PhosphorEngine::PlacementStateKey& key,
                               const QString& windowId, const QString& screenId, int minWidth, int minHeight);
    /// Drop stash entries whose key @p stale answers true for — called by
    /// the same prunes that reap context states.
    void sweepStripStash(const std::function<bool(const PhosphorEngine::PlacementStateKey&)>& stale);
    // engine_apply.cpp
    /// @p suppressOuterGaps is the smart-gaps arm: the two user-visible
    /// geometry producers (applyLayout, visibleTileRects) pass true when the
    /// strip holds exactly one column, zeroing the OUTER gaps only. Inner
    /// gaps need no arm — with one column no inter-column gap exists, so
    /// the pure-math callers that never suppress stay consistent by
    /// construction.
    ScrollLayoutParams layoutParamsForScreen(const QString& screenId, bool suppressOuterGaps = false) const;
    /// Relayout the strip and emit the geometry batch for @p screenId's
    /// current-context state. @p focusWindowAfter activates the strip's
    /// active window after the batch (engine-driven navigation only).
    void applyLayout(const QString& screenId, bool focusWindowAfter = false);
    // engine_lifecycle.cpp
    void insertOpenedWindow(ScrollState* state, const QString& windowId, const QString& screenId, int minWidth,
                            int minHeight);
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
    /// Returns true when a crossing was initiated.
    bool moveActiveWindowAcrossBoundary(ScrollState* state, const QString& screenId, const QString& direction,
                                        bool swap);

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
        int minWidth = 0;
        int minHeight = 0;
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
    /// StashedStrip::stagedFromPersistence.
    struct StashedTile
    {
        QString windowId;
        WindowHeight height;
        /// Carried for serialization fidelity only — the restore paths do
        /// not re-apply it (the effect re-reports live minimize state).
        bool minimized = false;
        /// True while THIS tile was staged from the persisted blob and has
        /// not been claimed. Per tile, not per entry: a key co-tenanted by a
        /// returning app and a dead one must age the dead tile out while the
        /// returning one keeps claiming.
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
    };
    /// One stashed strip: the structural columns plus the focus/view pair
    /// whose loss made every mode round trip re-anchor on an arbitrary
    /// window (first arrival won the focus).
    struct StashedStrip
    {
        QVector<StashedColumn> columns;
        QString focusedWindowId;
        int viewAnchor = 0;
        /// True while this entry was staged by restoreStripState from the
        /// PERSISTED blob and has not yet had a single tile claimed.
        ///
        /// Such an entry names LAST session's window ids, which by design do
        /// not appear in any live alive-set: the cross-session claim in
        /// restoreFromStripStash matches on the appId prefix precisely because
        /// the per-instance half of the id is regenerated every launch. The
        /// aliveness sweep in pruneStaleWindows must therefore not read
        /// "absent from the alive set" as "closed" here, or the very first
        /// prune after login (the effect fires one at bringup, right after the
        /// daemon stages this) would erase the whole snapshot and undo the
        /// structure/focus/anchor restore.
        ///
        /// Cleared on the first successful consume, at which point the entry
        /// is anchored in THIS session's id space and the sweep is meaningful.
        ///
        /// This exemption is not the whole story: pruneStaleWindows fires
        /// exactly once per session, at bring-up, while the entry is still
        /// exempt — so no sweep ever reaches a persisted tile whose app
        /// never relaunches. That is handled by the PER-TILE
        /// StashedTile::unclaimedSessions lease, which ages each unclaimed
        /// tile out individually so a returning co-tenant cannot keep a dead
        /// sibling's tile alive forever.
        bool stagedFromPersistence = false;

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

    QHash<QString, QVariantMap> m_perScreenOverrides;
    std::function<void()> m_persistSaveFn;
    std::function<void()> m_persistLoadFn;
    FloatPredicate m_floatPredicate;
    OpenParamsResolver m_openParamsResolver;
    SnappingModeResolver m_snappingModeResolver;
    ContextGapProvider m_contextGapProvider;
};

} // namespace PhosphorScrollEngine
