// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorscrollengine_export.h>

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/PerScreenStates.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/ScreenContextTracker.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include <QHash>
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
    /// re-emit contract on desktop switches.
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
    void setInitialWindowOrder(const QString& screenId, const QStringList& windowIds) override;
    int pruneStaleWindows(const QSet<QString>& aliveWindowIds) override;

    // ═══════════════════════════════════════════════════════════════════════
    // Cross-engine handoff
    // ═══════════════════════════════════════════════════════════════════════

    QString engineId() const override
    {
        return QStringLiteral("scrolling");
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
    std::optional<PhosphorEngine::WindowPlacement> capturePlacement(const QString& windowId) const override;
    void refreshConfigFromSettings() override;
    void retile(const QString& screenId = QString()) override;
    void scheduleRetileForScreen(const QString& screenId) override;

    /// Predicate deciding whether an opening window should start FLOATING
    /// because a "Float this app" rule matched it. Daemon-injected, keyed by
    /// the live windowId. Clear with {} before destroying captured state.
    using FloatPredicate = std::function<bool(const QString& windowId)>;
    void setFloatPredicate(FloatPredicate predicate)
    {
        m_floatPredicate = std::move(predicate);
    }

    /// Resolver for the per-window open-behaviour rule overrides. Same
    /// injection contract as the float predicate.
    using OpenParamsResolver = std::function<ScrollOpenParams(const QString& windowId)>;
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
    /// @p releasedWindows, drops all per-window and per-screen bookkeeping
    /// (float markers, pending seed, tab-strip latch — with the "[]" clear
    /// broadcast), and deleteLater()s the state.
    void releaseScreenState(ScrollState* state, QStringList& releasedWindows);
    /// Latch-guarded tab-strip clear: emits the "[]" payload once for a
    /// screen that had a strip showing, no-op otherwise.
    void clearTabStripsForScreen(const QString& screenId);
    /// Shared per-window side-map sweep for every state-destruction path.
    void dropWindowBookkeeping(const ScrollState* state);
    /// Drop per-screen bookkeeping (seed, tab-strip latch) for each screen
    /// in @p screenIds that no longer has ANY context state. Overrides
    /// survive by design; see the definition.
    void sweepStatelessScreenBookkeeping(const QSet<QString>& screenIds);
    // engine_apply.cpp
    ScrollLayoutParams layoutParamsForScreen(const QString& screenId) const;
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

    /// Cached layout parameters rebuilt by refreshConfigFromSettings().
    QList<qreal> m_presetColumnWidths{1.0 / 3.0, 0.5, 2.0 / 3.0};
    QList<qreal> m_presetWindowHeights{1.0 / 3.0, 0.5, 2.0 / 3.0};
    CenterFocusedColumn m_centerFocusedColumn = CenterFocusedColumn::Never;
    bool m_alwaysCenterSingleColumn = false;
    ColumnWidth m_defaultColumnWidth = ColumnWidth::makeProportion(0.5);
    /// "Client decides" default width: open at the client's initial size.
    bool m_defaultWidthClientDecides = false;
    ColumnDisplay m_defaultColumnDisplay = ColumnDisplay::Normal;

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
    /// Restore-order seed for deterministic mode transitions (consumed as
    /// windows arrive, mirroring autotile's pending-order model).
    QHash<QString, QStringList> m_pendingInitialOrder;
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
