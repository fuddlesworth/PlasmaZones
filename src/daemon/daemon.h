// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QThreadPool>
#include <chrono>
#include <memory>

#include "shortcutmanager.h"
#include <PhosphorLayoutApi/LayoutSourceBundle.h>
#include "../core/types.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/Swapper.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorTiles/AlgorithmPreviewParams.h>

namespace Phosphor::Screens {
class PlasmaPanelSource;
class DBusScreenAdaptor;
}

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/QtQuickClockManager.h>
#include <PhosphorConfig/IBackend.h>

namespace PhosphorAnimation {
class CurveLoader;
class ProfileLoader;
}

namespace PhosphorAnimationShaders {
class AnimationShaderRegistry;
}

namespace PhosphorEngine {
class IScrollEngine;
class WindowRegistry;
}

namespace PhosphorScrollEngine {
class ScrollEngine;
}

namespace PhosphorWorkspaces {
class ActivityManager;
class VirtualDesktopManager;
}

namespace PhosphorZones {
class Layout;
class LayoutComputeService;
class LayoutRegistry;
class ZoneDetector;
}

namespace PlasmaZones {

enum class DisabledReason;
class Settings;
class OverlayService;

class ShortcutManager;
class LayoutAdaptor;
class SettingsAdaptor;
class ShaderAdaptor;
class ControlAdaptor;
class CompositorBridgeAdaptor;
class OverlayAdaptor;
class ZoneDetectionAdaptor;
class WindowTrackingAdaptor;
class WindowDragAdaptor;
class ModeTracker;
class ZoneSelectorController;
class UnifiedLayoutController;
class AutotileAdaptor;
class ScrollAdaptor;
class ScreenModeRouter;
class SettingsConfigStore;
class SnapAdaptor;
class ShaderRegistry;
} // namespace PlasmaZones

namespace PhosphorTiles {
class AlgorithmRegistry;
class ScriptedAlgorithmLoader;
}

namespace PlasmaZones {

/**
 * @brief Main daemon for PlasmaZones
 *
 * Runs in the background managing layouts, zone overlays, KWin D-Bus
 * communication, keyboard shortcuts, and multi-monitor support.
 */
class Daemon : public QObject
{
    Q_OBJECT

public:
    explicit Daemon(QObject* parent = nullptr);
    ~Daemon() override;

    // No singleton - use dependency injection instead

    // Initialization
    bool init();
    void start();
    void stop();

    // Component access
    PhosphorZones::LayoutRegistry* layoutManager() const
    {
        return m_layoutManager.get();
    }
    PhosphorZones::ZoneDetector* zoneDetector() const
    {
        return m_zoneDetector.get();
    }
    Settings* settings() const
    {
        return m_settings.get();
    }

    /**
     * @brief Unified layout-preview source (manual zones + autotile algorithms).
     *
     * Returns a composite that aggregates PhosphorZones::ZonesLayoutSource
     * (over m_layoutManager) and PhosphorTiles::AutotileLayoutSource (over
     * the daemon-owned PhosphorTiles::AlgorithmRegistry instance at
     * m_algorithmRegistry).  Daemon-internal consumers — overlay layout
     * picker, snap-assist preview thumbnails, the layout adaptor's D-Bus
     * surface — see one ILayoutSource* and branch on
     * `LayoutPreview::isAutotile` rather than on which concrete provider
     * produced an entry.
     */
    PhosphorLayout::ILayoutSource* layoutSource() const
    {
        return m_layoutSources.composite();
    }
    OverlayService* overlayService() const
    {
        return m_overlayService.get();
    }
    Phosphor::Screens::ScreenManager* screenManager() const
    {
        return m_screenManager.get();
    }
    PhosphorWorkspaces::VirtualDesktopManager* virtualDesktopManager() const
    {
        return m_virtualDesktopManager.get();
    }
    PhosphorWorkspaces::ActivityManager* activityManager() const
    {
        return m_activityManager.get();
    }
    ShortcutManager* shortcutManager() const
    {
        return m_shortcutManager.get();
    }
    PhosphorEngine::WindowRegistry* windowRegistry() const
    {
        return m_windowRegistry.get();
    }

    // Overlay control (delegates to OverlayService)
    Q_INVOKABLE void showOverlay();
    Q_INVOKABLE void hideOverlay();
    Q_INVOKABLE bool isOverlayVisible() const;

    // OSD notifications
    void showLayoutOsd(PhosphorZones::Layout* layout, const QString& screenId = QString());
    void showLockedOsd(const QString& screenId);
    void showLockedPreviewOsd(const QString& screenId);
    void showContextDisabledOsd(const QString& screenId, int desktop, const QString& activity, DisabledReason reason);

private:
    /**
     * @brief Show layout OSD for an autotile algorithm (visual zone preview)
     *
     * Renders the OSD unconditionally — gating on user OSD toggles
     * (showOsdOnLayoutSwitch / showOsdOnDesktopSwitch) is the caller's
     * responsibility. The osdStyle setting controls visual style.
     */
    void showLayoutOsdForAlgorithm(const QString& algorithmId, const QString& displayName, const QString& screenId);
    void clearHighlight();

    /**
     * @brief Bridge Settings::animationProfile into `PhosphorProfileRegistry`
     *        so QML `PhosphorMotionAnimation { profile: "<path>" }` resolves
     *        to the user's active animation settings and live-updates on edit.
     *
     * Phase 4 sub-commit 7. Scans the XDG `plasmazones/curves` and
     * `plasmazones/profiles` directories for user-authored definitions
     * (per decision U's consumer-namespace pattern) and installs
     * live-reload watchers. Registers the daemon's active animation
     * Profile under every well-known `ProfilePaths` shell path that
     * maps to PlasmaZones's single-Profile settings surface so QML
     * consumers can reference specific paths (`widget.zoneHighlight`,
     * `osd.show`, etc.) without the daemon carrying per-event
     * sub-profiles — future sub-commits can diverge paths when
     * per-event customisation is actually exposed to users.
     *
     * Reconnects to `Settings::animationProfileChanged` for live
     * updates; each emit re-registers the active Profile against the
     * same path set, firing `PhosphorProfileRegistry::profileChanged`
     * on each — bound `PhosphorMotionAnimation` consumers re-resolve
     * transparently.
     */
    void setupAnimationProfiles();
    void setupAnimationShaderEffects();
    /// Push the current `Settings::animationProfile()` into the registry
    /// under the shell's well-known paths. Called from
    /// `setupAnimationProfiles()` at startup and from the coalescing
    /// trampoline `requestAnimationProfilePublish` on every
    /// `animationProfileChanged` / `profilesChanged` /
    /// `curvesChanged` signal.
    void publishActiveAnimationProfile();
    /// Schedule a coalesced publish on the next event-loop tick. The
    /// settings-slider drag fires `animationProfileChanged` at ~30 Hz,
    /// and a curve-pack edit can fire `curvesChanged` and
    /// `profilesChanged` back-to-back in the same tick. Funnelling
    /// through a single-shot 0-ms timer collapses every signal in the
    /// current event-loop iteration into one publish call. The
    /// registry's value-equality guard would already make duplicate
    /// publishes free, but the publish itself does a Settings parse
    /// + curve resolve which is not free during a slider drag.
    void requestAnimationProfilePublish();

    // ═══════════════════════════════════════════════════════════════════════════
    // Navigation handlers — single code path per operation (DRY/SOLID)
    // Resolve screen → check mode (autotile vs zones) → delegate → OSD from backend
    // ═══════════════════════════════════════════════════════════════════════════

    /** @brief Return the active IPlacementEngine for a screen (autotile or snap) */
    PhosphorEngine::IPlacementEngine* engineForScreen(const QString& screenId) const;

    /**
     * @brief Convenience mode check: routed through m_screenModeRouter.
     *
     * All daemon navigation/signal paths that need to branch on "is this
     * screen in autotile mode?" use this method instead of checking the
     * engine pointer directly. Centralising the lookup behind one call
     * is how the single-source-of-truth invariant is enforced inside the
     * daemon.
     */
    bool isAutotileScreen(const QString& screenId) const;

    void handleRotate(bool clockwise);
    void handleFloat();
    void handleMove(NavigationDirection direction);
    void handleFocus(NavigationDirection direction);
    void handlePush();
    void handleRestore();
    void handleSwap(NavigationDirection direction);
    void handleSnap(int zoneNumber);
    void handleCycle(bool forward);
    void handleConsume();
    void handleExpel();
    void handleCycleColumnWidth();
    void handleCycleWindowHeight();
    void handleToggleColumnFullWidth();
    void handleGrowColumnWidth();
    void handleShrinkColumnWidth();
    void handleToggleCenterFocusedColumn();
    void handleResnap();
    void handleSnapAll();
    void handleFocusMaster();
    void handleSwapWithMaster();
    void handleIncreaseMasterRatio();
    void handleDecreaseMasterRatio();
    void handleIncreaseMasterCount();
    void handleDecreaseMasterCount();
    void handleRetile();
    void handleSwapVirtualScreen(NavigationDirection direction);
    void handleRotateVirtualScreens(bool clockwise);

    /** @brief Check if screen is locked for layout change in its current mode */
    bool isScreenLockedForLayoutChange(const QString& screenId);

    /** @brief Handle cycle-layout shortcut (previous or next) */
    void handleCycleLayout(const QString& screenId, bool forward);

    // Start-up sub-methods (defined in start.cpp)
    void connectScreenSignals();
    void connectDesktopActivity();
    void connectShortcutSignals();
    void initializeAutotile();
    void initializeUnifiedController();
    void connectLayoutSignals();
    void connectOverlaySignals();
    void finalizeStartup();
    /** @brief Migrate window screen assignments from physical to virtual IDs after startup */
    void migrateStartupScreenAssignments();

    /**
     * @brief Pre-seed autotile engine with zone-ordered windows for one screen
     *
     * Builds the zone-ordered window list from WTS and passes it to the autotile
     * engine's setInitialWindowOrder(). Used by both per-screen toggle and global
     * snapping→autotile transition.
     *
     * @param screenId Screen identifier
     */
    void seedAutotileOrderForScreen(const QString& screenId);

    /**
     * @brief Handle autotile feature being disabled (clear assignments, restore manual mode)
     */
    void handleAutotileDisabled();

    /**
     * @brief Handle snapping toggle activating autotile mode on all screens
     */
    void handleSnappingToAutotile();

    /**
     * @brief Pre-save snap-mode floating state before entering autotile
     *
     * Saves non-autotile-floated floating windows to WTS's savedSnapFloating set.
     * When screenId is provided, only saves windows on that screen. When empty,
     * saves all floating windows (used for global autotile enable).
     * Idempotent (QSet::insert).
     */
    void presaveSnapFloats(const QString& screenId = QString());

    /**
     * @brief Capture autotile window order for all autotile screens
     *
     * Must be called BEFORE any mode switch that destroys PhosphorTiles::TilingState
     * (e.g. applyLayoutById, handleAutotileDisabled, updateAutotileScreens).
     *
     * @return Map of (screen, desktop, activity) -> ordered window IDs (master first)
     */
    QHash<TilingStateKey, QStringList> captureAutotileOrders() const;

    /**
     * @brief Restore pre-tile geometry for autotile-only windows
     *
     * Iterates m_lastAutotileOrders and calls applyGeometryForFloat for each
     * window that has no zone assignment (never manually snapped). PhosphorZones::Zone-snapped
     * windows are already handled by resnapCurrentAssignments.
     */
    void restoreAutotileOnlyGeometries(const QSet<QString>& excludeWindows = {}, int desktop = -1,
                                       const QString& activity = QString());
    QVector<ZoneAssignmentEntry> buildAutotileRestoreEntries(const QSet<QString>& excludeWindows, int desktop,
                                                             const QString& activity);

    /** @brief Show layout OSD deferred (avoids blocking on first-time QML compilation) */
    void showLayoutOsdDeferred(const QUuid& layoutId, const QString& screenId);
    /** @brief Show algorithm OSD deferred (avoids blocking on first-time QML compilation) */
    void showAlgorithmOsdDeferred(const QString& algorithmId, const QString& algorithmName, const QString& screenId);

    /**
     * @brief Show OSD for the current desktop's layout/algorithm on desktop or activity switch
     *
     * Resolves the focused screen, reads the per-desktop assignment, and shows
     * the appropriate OSD (layout or algorithm). DRY helper for both
     * currentDesktopChanged and currentActivityChanged handlers.
     *
     * @param desktop Current virtual desktop number
     * @param activity Current activity ID
     */
    void showDesktopSwitchOsd(int desktop, const QString& activity);

    /**
     * @brief Show per-screen OSD for all effective screens
     *
     * Iterates effectiveScreenIds, resolves assignment (autotile vs snapping),
     * and calls showLayoutOsdForAlgorithm or showLayoutOsd per screen inside
     * a single deferred event-loop pass so all surfaces show simultaneously.
     * DRY helper shared by showDesktopSwitchOsd and settingsChanged handler.
     */
    void showOsdForAllScreens(int desktop, const QString& activity);

    /**
     * @brief Recompute which screens use autotile from layout assignments
     *
     * Reads all screen assignments via assignmentIdForScreen(), computes
     * which screens have autotile IDs, calls setActiveScreens() on engine.
     */
    void updateAutotileScreens();

    /**
     * @brief Resolve which screens are in scroll mode and update ScrollEngine.
     *
     * Mirrors updateAutotileScreens() for the scroll placement engine: reads
     * each screen's assignment, collects the ones carrying a "scroll:" id, and
     * pushes the set (plus the current desktop/activity context) to the scroll
     * engine. Invoked from updateAutotileScreens() so both engines resolve on
     * the same triggers.
     */
    void updateScrollScreens();

    /**
     * @brief Resolve scroll-mode geometry for a screen and push it to the effect.
     *
     * Connected to ScrollEngine::placementChanged. ScrollEngine is
     * geometry-agnostic, so the daemon resolves the strip here via
     * resolveScrollLayout() against the screen's working area and emits the
     * result through WindowTrackingAdaptor::applyGeometriesBatch.
     */
    void onScrollPlacementChanged(const QString& screenId);

    /**
     * @brief Push scroll-mode settings to ScrollEngine and re-resolve every
     *        active scroll screen.
     *
     * Pushes the preset width/height lists, the default-column-width and the
     * viewport-centering mode from Settings into ScrollEngine, then re-resolves
     * each active scroll strip so gap / preset / centering changes take effect
     * immediately. Invoked once at startup and on any scroll settings change.
     *
     * At the startup invocation no scroll screen is active yet, so only the
     * engine-global config is seeded; the per-screen-override push and the
     * strip re-resolve are picked up by the first updateScrollScreens().
     */
    void refreshScrollConfigFromSettings();

    /// Schedule a coalesced refreshScrollConfigFromSettings on the next
    /// event-loop tick. Wired to every scroll-setting change signal so a
    /// burst of edits (slider drag at ~30 Hz) collapses into one re-resolve
    /// pass rather than N. See @ref m_scrollRefreshTimer.
    void requestScrollConfigRefresh();

    /**
     * @brief Push each active scroll screen's per-screen override map into
     *        ScrollEngine.
     *
     * Mirrors updateAutotileScreens()' per-screen autotile push: reads
     * Settings::getPerScreenScrollSettings() for every active scroll screen
     * and hands it to ScrollEngine::applyPerScreenConfig (or clears it). The
     * engine's effective*() accessors then resolve override → global default.
     */
    void applyPerScreenScrollOverrides();

    /**
     * @brief Persist / restore the scroll-mode strip state across a restart.
     *
     * ScrollEngine is geometry-agnostic and daemon-orchestrated, so the daemon
     * owns its disk persistence: saveScrollState() writes serializeEngineState()
     * to scroll-session.json on shutdown; loadScrollState() feeds it back
     * through deserializeEngineState() at startup, before the effect re-reports
     * windows (a still-existing window's windowOpened then no-ops, keeping its
     * restored column). A restored window that did not survive the restart is
     * pruned by ScrollEngine::reconcileRestoredWindows() when the effect's first
     * windowsOpenedBatch arrives, so no phantom column lingers.
     */
    void saveScrollState();
    void loadScrollState();

    /**
     * @brief The scroll placement engine narrowed to its IScrollEngine surface.
     *
     * m_scrollEngine is held as the base PlacementEngineBase pointer, but the
     * scroll-specific config / persistence / reconciliation API lives on the
     * IScrollEngine pure interface (resolved via cross-cast at engine-creation
     * time). Centralises the cross-cast the scroll handlers all need; returns
     * nullptr when no scroll engine is active.
     */
    PhosphorEngine::IScrollEngine* scrollEngine() const;

    /**
     * @brief Respond to a Phosphor::Screens::ScreenManager VS cache change for a physical screen
     *
     * Wired to Phosphor::Screens::ScreenManager::virtualScreensChanged. Performs the post-change
     * fan-out: clears stale resnap buffer, migrates window assignments to the
     * new VS IDs (when subdivisions exist), prunes stale autotile orders,
     * refreshes the autotile screen set, recalculates affected zone
     * geometries inline, resnaps windows on this physical screen and its
     * virtual children, and schedules the debounced geometry update for
     * downstream consumers.
     */
    void onVirtualScreensReconfigured(const QString& physicalScreenId);

    /**
     * @brief Lightweight handler for regions-only VS config changes.
     *
     * Fires on swap/rotate/boundary-resize where the VS ID set is unchanged.
     * Skips migrate/prune/updateAutotileScreens (all no-ops for regions-only)
     * and only recalculates zone geometries and triggers a snap-mode resnap
     * tagged with the vs_reconfigure action so the kwin-effect does not fire
     * snap-assist.
     *
     * The autotile retile is driven by the engine's own handler on
     * virtualScreenRegionsChanged — the Daemon's path does NOT force-retile
     * so there is exactly one retile per change (eliminates the "move then
     * retile" double-pass users observed on VS swap/rotate).
     */
    void onVirtualScreenRegionsChanged(const QString& physicalScreenId);

    /** @brief Resnap windows to current layout zones (only in manual/snap mode) */
    void resnapIfManualMode();

    /**
     * @brief Update layout filter on overlay service and unified layout controller
     *
     * Shows both manual and autotile layouts when the feature gate is enabled.
     */
    void updateLayoutFilter();
    /** @brief Update layout filter for a specific screen's mode (for cycle/popup) */
    void updateLayoutFilterForScreen(const QString& focusedScreenId);

    /**
     * @brief Sync ModeTracker and UnifiedLayoutController from per-desktop assignments
     *
     * Derives the tiling mode (Manual vs Autotile) and current layout from the
     * actual per-desktop assignment for the focused screen. Must be called on
     * every desktop/activity switch so global state reflects the new context.
     */
    void syncModeFromAssignments();

    std::unique_ptr<PhosphorConfig::IBackend> m_configBackend;
    std::unique_ptr<PhosphorZones::LayoutRegistry> m_layoutManager;
    // Daemon-owned tile-algorithm registry. Replaces the old
    // AlgorithmRegistry::instance() singleton — per-process ownership is
    // the only shape that works once PlasmaZones becomes a plugin-based
    // compositor/WM/shell (plugins can't share process-global state
    // safely).
    //
    // ─── DECLARATION ORDER INVARIANT ─────────────────────────────────
    // Every FactoryContext service the bundle borrows (m_layoutManager
    // → IZoneLayoutRegistry, m_algorithmRegistry → ITileAlgorithmRegistry)
    // MUST be declared before m_layoutSources so reverse-order member
    // destruction tears the bundle (and its ZonesLayoutSource /
    // AutotileLayoutSource children) down BEFORE the registries those
    // children borrow. The LayoutSourceBundle contract
    // (libs/phosphor-layout-api/.../LayoutSourceBundle.h) is explicit
    // about this — violating the order produces dangling pointers in
    // every source's destructor, hidden today only by Qt's signal
    // auto-disconnect. Do not reorder these three lines without revisiting
    // every source's destructor.
    //
    // Also declared before ScriptedAlgorithmLoader + AutotileEngine
    // because both take a borrowed pointer to it in their constructor.
    std::unique_ptr<PhosphorTiles::AlgorithmRegistry> m_algorithmRegistry;
    // Manual layouts + autotile algorithms composed behind layoutSource().
    // The bundle owns all three objects so destruction is deterministic
    // (composite first, then the child sources it borrows from). See
    // libs/phosphor-layout-api/.../LayoutSourceBundle.h for the
    // construction contract.
    PhosphorLayout::LayoutSourceBundle m_layoutSources;
    /// Cached pointer to the bundle's autotile source — populated once
    /// after buildFromRegistered in the ctor, then handed to every
    /// consumer that wants the long-lived preview-cache fast path
    /// (overlay service, layout adaptor, unified controller). Avoids
    /// repeating m_layoutSources.source(autotileLayoutSourceName())
    /// at every wiring site (DRY) and removes the temptation to typo
    /// the literal. Borrowed; lifetime tied to m_layoutSources, so it
    /// MUST stay declared immediately adjacent (and below) the bundle
    /// so reverse-order member destruction nulls borrowed-pointer
    /// consumers before the source itself is gone — see
    /// "DECLARATION ORDER INVARIANT" comment above.
    PhosphorLayout::ILayoutSource* m_autotileLayoutSource = nullptr;
    // ─── End of layout-source declaration block ─────────────────────────
    std::unique_ptr<PhosphorZones::LayoutComputeService> m_layoutComputeService;
    /// Per-daemon curve registry. Replaces the prior per-process
    /// `CurveRegistry::instance()` singleton — composition roots own
    /// their own.
    ///
    /// DECLARATION ORDER INVARIANT: must be declared BEFORE `m_settings`
    /// (which takes a borrowed pointer to it in its constructor) and
    /// BEFORE `m_curveLoader` / `m_profileLoader` (which also borrow).
    /// Reverse-order destruction then tears every consumer down before
    /// the registry itself, guaranteeing no UAF on the Settings /
    /// loader teardown paths. Also reset from `PhosphorCurve::s_registry`
    /// in `~Daemon` before teardown so the QML static helper doesn't
    /// dangle into freed memory on process shutdown or successive
    /// Daemon constructions in tests.
    PhosphorAnimation::CurveRegistry m_curveRegistry;
    /// Per-daemon profile registry. Replaces the prior process-global
    /// `PhosphorProfileRegistry::instance()` singleton — composition
    /// roots own their own and publish via `setDefaultRegistry` so QML
    /// callsites resolve through the same instance the daemon
    /// populates from Settings + ProfileLoader.
    ///
    /// DECLARATION ORDER INVARIANT: must be declared BEFORE
    /// `m_overlayService` (which takes a reference into its
    /// SurfaceAnimator) and BEFORE `m_profileLoader` (which holds a
    /// reference). Reverse-order destruction then tears the consumers
    /// down before the registry itself, guaranteeing no UAF on the
    /// service / loader teardown paths. The `setDefaultRegistry(nullptr)`
    /// call in `stop()` clears the QML static handle before teardown.
    PhosphorAnimation::PhosphorProfileRegistry m_profileRegistry;
    /// Per-daemon QtQuickClock manager — replaces the prior process-
    /// global `QtQuickClockManager::instance()` singleton. Published via
    /// `setDefaultManager` so any `PhosphorAnimatedValueBase`-derived
    /// QML type in the overlay shell resolves the same per-window
    /// clocks as the C++ side. Owned over the daemon lifetime; the
    /// destroy-time `setDefaultManager(nullptr)` call in `stop()` drops
    /// the published handle before the unique_ptr destructs.
    std::unique_ptr<PhosphorAnimation::QtQuickClockManager> m_clockManager;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<PhosphorZones::ZoneDetector> m_zoneDetector;
    // Single source of truth for live-window instance identity + metadata.
    // Populated by the kwin-effect bridge. Consumers query appIdFor() etc.
    // instead of parsing composite windowId strings.
    std::unique_ptr<PhosphorEngine::WindowRegistry> m_windowRegistry;
    /// Plasma D-Bus panel-offset source. Declared before m_screenManager
    /// because the manager holds a non-owning IPanelSource* into it.
    std::unique_ptr<Phosphor::Screens::PlasmaPanelSource> m_panelSource;
    /// Settings-backed IConfigStore for VS topology. Shared by
    /// m_screenManager (Config::configStore) and m_virtualScreenSwapper
    /// (constructor arg). Declared before both so destruction order
    /// runs swapper → screen-manager → store.
    std::unique_ptr<SettingsConfigStore> m_virtualScreenStore;
    std::unique_ptr<Phosphor::Screens::ScreenManager> m_screenManager;
    /// Per-daemon shader registry. Replaces the previous
    /// ShaderRegistry::instance() singleton — per-process ownership is the
    /// plugin-architecture-friendly shape (matches m_algorithmRegistry).
    /// Declared BEFORE m_overlayService so the OverlayService can hold a
    /// borrowed pointer to it; reverse-order destruction tears the service
    /// down before the registry, guaranteeing no UAF on shadersChanged
    /// disconnect during shutdown. Also declared before the D-Bus adaptors
    /// (ShaderAdaptor, SettingsAdaptor) that borrow it.
    std::unique_ptr<ShaderRegistry> m_shaderRegistry;
    /// OverlayService takes ScreenManager* via constructor injection — must
    /// be declared AFTER m_screenManager so the initializer-list construction
    /// order matches.
    std::unique_ptr<OverlayService> m_overlayService;
    std::unique_ptr<PhosphorWorkspaces::VirtualDesktopManager> m_virtualDesktopManager;
    std::unique_ptr<PhosphorWorkspaces::ActivityManager> m_activityManager;
    std::unique_ptr<ShortcutManager> m_shortcutManager;

    // Domain-specific D-Bus adaptors
    // D-Bus adaptors need a parent (the adapted object); Qt requires it.
    // So we use raw pointers; Qt parent-child system manages their lifetime
    LayoutAdaptor* m_layoutAdaptor = nullptr;
    SettingsAdaptor* m_settingsAdaptor = nullptr;
    OverlayAdaptor* m_overlayAdaptor = nullptr; // Overlay visibility only
    ZoneDetectionAdaptor* m_zoneDetectionAdaptor = nullptr; // PhosphorZones::Zone detection queries
    WindowTrackingAdaptor* m_windowTrackingAdaptor = nullptr; // Window-zone tracking
    Phosphor::Screens::DBusScreenAdaptor* m_screenAdaptor = nullptr;
    WindowDragAdaptor* m_windowDragAdaptor = nullptr; // Window drag handling
    // Held so stop() can invoke detach() before the unique_ptr members
    // those adaptors borrow from are destroyed. ~QObject runs AFTER all
    // unique_ptr member destructors, so without an explicit detach the
    // adaptors would see dangling pointers for a destruction-ordering
    // window (and any queued D-Bus call landing in that window would UAF).
    ShaderAdaptor* m_shaderAdaptor = nullptr;
    ControlAdaptor* m_controlAdaptor = nullptr;
    // Compositor bridge adaptor (KWin effect ↔ daemon protocol endpoint).
    // Parented to `this`; holds only plain state, so it needs no detach().
    CompositorBridgeAdaptor* m_compositorBridge = nullptr;

    // Mode tracking
    std::unique_ptr<ModeTracker> m_modeTracker;

    // Unified layout management
    std::unique_ptr<UnifiedLayoutController> m_unifiedLayoutController;

    // Scripted algorithm loader (file watcher for user-defined JS algorithms).
    // m_algorithmRegistry is declared up at the top of the member block with
    // m_layoutManager — see the DECLARATION ORDER INVARIANT comment there.
    std::unique_ptr<PhosphorTiles::ScriptedAlgorithmLoader> m_scriptedAlgorithmLoader;

    // Window engines (held as base class; concrete types known only in daemon.cpp/enginefactory.cpp)
    std::unique_ptr<PhosphorEngine::PlacementEngineBase> m_autotileEngine;
    std::unique_ptr<PhosphorEngine::PlacementEngineBase> m_snapEngine;
    std::unique_ptr<PhosphorEngine::PlacementEngineBase> m_scrollEngine;
    /// Cached cross-cast of m_scrollEngine to its IScrollEngine surface. Set
    /// once when the engine is constructed (daemon::start.cpp's
    /// createBuiltInEngines path) and nulled in stop() before
    /// m_scrollEngine.reset(). The scroll-only API (effective*(),
    /// serialize/deserializeEngineState, hasPersistableState,
    /// reconcileRestoredWindows, applyPerScreenConfig, pruneStatesForScreen,
    /// stateForScreen) is reached through this pointer instead of running
    /// dynamic_cast on every onScrollPlacementChanged — that path fires per
    /// scroll setting change and per scroll-mode resolve, so caching matters
    /// under slider-drag rates. Lifetime tied 1:1 with m_scrollEngine's
    /// unique_ptr.
    PhosphorEngine::IScrollEngine* m_scrollEngineCached = nullptr;
    /// Single source of truth for "which engine owns screen X". Used by
    /// WindowTrackingAdaptor and (via @ref engineForScreen) daemon-internal
    /// dispatch paths. Owns no state of its own — just delegates to the
    /// layout manager and engine pointers it was constructed with.
    std::unique_ptr<ScreenModeRouter> m_screenModeRouter;
    /// Stateless facade over m_virtualScreenStore for VS swap/rotate.
    /// Held as a member rather than reconstructed per-call so navigation
    /// handlers don't need to know about its dependencies.
    std::unique_ptr<Phosphor::Screens::VirtualScreenSwapper> m_virtualScreenSwapper;
    SnapAdaptor* m_snapAdaptor = nullptr;
    AutotileAdaptor* m_autotileAdaptor = nullptr;
    ScrollAdaptor* m_scrollAdaptor = nullptr;

    /// Phase 6: animation shader effect discovery. Scans
    /// `plasmazones/animations` from XDG data dirs and monitors for
    /// user-dropped packs via QFileSystemWatcher. Declared AFTER
    /// m_overlayService — lifetime is managed explicitly in stop():
    /// the overlay service's borrowed registry pointer is nulled
    /// before this registry is reset, preventing dangling-pointer
    /// access during shutdown.
    std::unique_ptr<PhosphorAnimationShaders::AnimationShaderRegistry> m_animationShaderRegistry;

    /// Phase 4 sub-commit 7: user-authored curve / profile scanners.
    /// Scan `plasmazones/curves` and `plasmazones/profiles` from XDG
    /// data dirs and register discovered entries with `CurveRegistry`
    /// / `PhosphorProfileRegistry` with live-reload enabled. Owned by
    /// the daemon for process lifetime; QFileSystemWatcher survives
    /// as long as the loader.
    std::unique_ptr<PhosphorAnimation::CurveLoader> m_curveLoader;
    std::unique_ptr<PhosphorAnimation::ProfileLoader> m_profileLoader;

    /// Coalescing trampoline for the publish path — see
    /// `requestAnimationProfilePublish`. Single-shot, parented to the
    /// daemon so destruction is automatic; only its `pending` flag is
    /// used (the timeout slot fires at 0 ms regardless of when the
    /// trampoline was first armed during the current event-loop tick).
    QTimer m_animationPublishTimer;
    bool m_animationPublishPending = false;

    /// Coalescing trampoline for refreshScrollConfigFromSettings. The six
    /// scroll-config Q_PROPERTY change signals fire ~30 Hz during a slider
    /// drag, and each emit re-resolves every active scroll strip and pushes
    /// applyGeometriesBatch to the effect. Routing through a 0-ms single-shot
    /// timer collapses the burst into one re-resolve per event-loop tick —
    /// the on-disk values it reads are already settled, so no payload
    /// difference, just dropped redundant work. Same pattern as the
    /// animation-profile publish path.
    QTimer m_scrollRefreshTimer;

    /// Per-screen cache of the last scroll-mode geometry batch we pushed via
    /// applyGeometriesBatch. onScrollPlacementChanged compares the freshly
    /// resolved set against this cache and skips the emit when identical —
    /// a slider drag at the settings page can produce multiple
    /// placementChanged + refresh fires per event-loop tick that all resolve
    /// to the same pixel geometry. Cleared on shutdown via stop(). Keyed by
    /// screenId so a per-screen-only change doesn't clobber other screens'
    /// cache entries.
    QHash<QString, QHash<QString, QRect>> m_lastScrollGeometryByScreen;

    // Desktop/activity resolution helpers (DRY — used by multiple handlers)
    int currentDesktop() const;
    QString currentActivity() const;
    bool isCurrentContextLocked(const QString& screenId) const;
    bool isCurrentContextLockedForMode(const QString& screenId, int mode) const;

    /**
     * @brief Sync daemon-side float state when autotile floats/unfloats a window
     *
     * Propagates floating state to PhosphorPlacement::WindowTrackingService and KWin effect,
     * manages autotile-originated vs snap-mode float bookkeeping, restores
     * pre-tile geometry on float, and shows navigation OSD.
     */
    void syncAutotileFloatState(const QString& windowId, bool floating, const QString& screenId);

    /**
     * @brief Passively sync daemon-side float state without restoring geometry
     *
     * Handler for AutotileEngine::windowFloatingStateSynced. Mirrors the WTS
     * bookkeeping of syncAutotileFloatState (setWindowFloating, autotileFloated
     * marker, pre-float zone housekeeping) but skips applyGeometryForFloat and
     * the navigation OSD — this path is invoked when the engine's internal
     * state diverges from WTS (e.g. a newly-inserted window carrying stale
     * snap-mode float state), not by a user float toggle. The window already
     * has a valid position and must not be teleported.
     */
    void syncAutotileFloatStatePassive(const QString& windowId, bool floating, const QString& screenId);

    /**
     * @brief Batch-update daemon-side float state for overflow-floated windows
     *
     * Updates WTS state directly without emitting per-window D-Bus signals
     * (the effect already processed the float from the windowsTileRequested batch).
     */
    void syncAutotileBatchFloatState(const QStringList& windowIds, const QString& screenId);

    /** @brief Prune m_lastAutotileOrders for stale desktops */
    void pruneContextMapsForDesktop(int maxDesktop);
    /** @brief Prune context maps for removed activities */
    void pruneContextMapsForActivities(const QSet<QString>& validActivities);
    /** @brief Prune m_lastAutotileOrders for old virtual screen IDs that no longer exist */
    void pruneAutotileOrdersForRemovedScreens(const QString& physicalScreenId);
    /**
     * @brief Drop a closed window from every saved autotile order.
     *
     * Without this, a window that closes while the screen is in manual mode
     * stays in m_lastAutotileOrders. On the next manual→autotile toggle,
     * seedAutotileOrderForScreen feeds the stale id back through
     * setInitialWindowOrder; setActiveScreens replays it into the TilingState
     * and recalculateLayout tiles a phantom window. Match by instance id —
     * saved entries are canonical "appId|instanceId" composites.
     */
    void pruneAutotileOrdersForWindow(const QString& instanceId);

    bool m_running = false;
    int m_suppressResnapOsd = 0;

    /// Shutdown flag — set by `aboutToQuit`, `stop()`. Gates `shouldSuppressOsd()`.
    bool m_shuttingDown = false;
    bool m_aboutToQuitConnected = false;

    /// Deadline bumped by `screenRemoved` (start.cpp). ~1 s cooldown prevents
    /// OSD shows during output teardown cascades and monitor hot-unplug.
    std::chrono::steady_clock::time_point m_screensSettlingUntil;

    /// Mirrors `plasma-workspace.target` ActiveState on the user-bus systemd.
    /// `true` = real session, `false` = phantom/inactive. Defaults to `true`
    /// (fail-open for non-systemd / headless). See `queryPlasmaWorkspaceState()`
    /// for the full rationale.
    bool m_plasmaWorkspaceActive = true;

    /// D-Bus object path for `plasma-workspace.target`, resolved by GetUnit.
    QString m_plasmaWorkspaceTargetPath;

    bool shouldSuppressOsd() const;

    /// Async query of systemd's user bus for `plasma-workspace.target` state.
    /// Fail-open on all D-Bus errors. Called once from `start()`.
    void queryPlasmaWorkspaceState();

    /// Continuation of `queryPlasmaWorkspaceState()` — fetches ActiveState
    /// and subscribes to PropertiesChanged after GetUnit resolves.
    void fetchPlasmaWorkspaceActiveState();

private Q_SLOTS:
    void onPlasmaWorkspaceTargetPropertiesChanged(const QString& interfaceName, const QVariantMap& changedProperties,
                                                  const QStringList& invalidatedProperties);

private:
    // Debounce timers for shortcuts that generate expensive work (Vulkan surface
    // creation, geometry batches, OSD churn) when triggered faster than ~100ms
    // by keyboard auto-repeat. Checked at the top of each handler.
    static constexpr int kShortcutDebounceMs = 100;
    QElapsedTimer m_rotateDebounce;
    QElapsedTimer m_floatDebounce;
    QElapsedTimer m_cycleLayoutDebounce;
    // Shared debounce for VS swap/rotate. Each fire commits a config change
    // through Settings and kicks a refresh → resnap cascade — cheap per call
    // but pile-up-prone under keyboard auto-repeat, same rationale as
    // m_rotateDebounce above. One timer for both ops: rapid alternation
    // between swap and rotate is not a user pattern.
    QElapsedTimer m_virtualScreenDebounce;

    // Last autotile window order per (screen, desktop, activity), captured when
    // leaving autotile. Used to re-seed the autotile engine with the same order
    // on re-entry, producing deterministic arrangements across mode toggles.
    // Keyed by TilingStateKey (not plain screen name) so cross-desktop toggles
    // don't overwrite each other's ordering.
    QHash<TilingStateKey, QStringList> m_lastAutotileOrders;

    // Snap-float restore entries collected during windowsReleasedFromTiling.
    // Consumed by the toggle handler to batch geometry restores into the resnap signal.
    QVector<ZoneAssignmentEntry> m_pendingSnapFloatRestores;

    // State tracking for settingsChanged delta detection (replaces individual signal handlers)
    // Initialized from m_settings in init() before settingsChanged is connected.
    // Header defaults are safe no-ops: both false means "no prior state" so the
    // first settingsChanged won't detect a spurious toggle.
    bool m_prevSnappingEnabled = false;
    bool m_prevAutotileEnabled = false;

    QTimer m_previewNotifyTimer;
    PhosphorTiles::AlgorithmPreviewParams m_preRetilePreviewParams;

    // Single-threaded pool for shader baking — QShaderBaker/glslang is not
    // thread-safe for concurrent compilation (SIGSEGV in QSpirvCompiler).
    QThreadPool m_shaderBakePool;

    // Geometry update debouncing to prevent cascade of redundant recalculations
    QTimer m_geometryUpdateTimer;
    bool m_geometryUpdatePending = false;
    void processPendingGeometryUpdates();

    // After geometry updates settle, request KWin effect to re-apply window positions (panel editor fix)
    QTimer m_reapplyGeometriesTimer;

    // Watchdog: if the KWin effect has not registered as a compositor bridge
    // within a grace period after startup, window control is dead (drags and
    // shortcuts do nothing). On timeout the daemon logs a diagnostic warning
    // and raises a desktop notification. Stopped early once the bridge
    // registers. Single-shot.
    QTimer m_bridgeWatchdogTimer;
    void warnCompositorBridgeMissing();

    // Log a "compositor bridge missing" warning and raise a desktop
    // notification. `diagnosis` carries a specific root cause (e.g. an effect
    // plugin built against a different KWin version) when one was identified;
    // an empty string falls back to the generic enable-the-effect guidance.
    void emitBridgeMissingWarning(const QString& diagnosis);
};

} // namespace PlasmaZones
