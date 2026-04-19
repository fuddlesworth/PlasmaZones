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
#include <memory>

#include "shortcutmanager.h"
#include <PhosphorLayoutApi/LayoutSourceBundle.h>
#include <PhosphorTiles/AutotileLayoutSourceFactory.h>
#include <PhosphorZones/ZonesLayoutSourceFactory.h>
#include "../core/types.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/Swapper.h>
#include "../autotile/AutotileEngine.h"

namespace Phosphor::Screens {
class PlasmaPanelSource;
}

#include <PhosphorConfig/IBackend.h>

namespace PhosphorZones {
class Layout;
class ZoneDetector;
}

namespace PlasmaZones {

enum class DisabledReason;
class LayoutManager;
class LayoutComputeService;
class Settings;
class OverlayService;
class VirtualDesktopManager;
class ActivityManager;
class ShortcutManager;
class LayoutAdaptor;
class SettingsAdaptor;
class OverlayAdaptor;
class ZoneDetectionAdaptor;
class WindowTrackingAdaptor;
class ScreenAdaptor;
class WindowDragAdaptor;
class ModeTracker;
class ZoneSelectorController;
class UnifiedLayoutController;
class AutotileAdaptor;
class AutotileEngine;
class IEngineLifecycle;
class AutotileNavigationAdapter;
class ScreenModeRouter;
class SettingsConfigStore;
class SnapNavigationAdapter;
class SnapAdaptor;
class SnapEngine;
class WindowRegistry;
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
    LayoutManager* layoutManager() const
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
     * the in-process PhosphorTiles::AlgorithmRegistry singleton).  Daemon-internal
     * consumers — overlay layout picker, snap-assist preview thumbnails,
     * the layout adaptor's D-Bus surface — see one ILayoutSource* and
     * branch on `LayoutPreview::isAutotile` rather than on which
     * concrete provider produced an entry.
     */
    PhosphorLayout::ILayoutSource* layoutSource() const
    {
        return m_layoutSources.composite.get();
    }
    OverlayService* overlayService() const
    {
        return m_overlayService.get();
    }
    Phosphor::Screens::ScreenManager* screenManager() const
    {
        return m_screenManager.get();
    }
    VirtualDesktopManager* virtualDesktopManager() const
    {
        return m_virtualDesktopManager.get();
    }
    ActivityManager* activityManager() const
    {
        return m_activityManager.get();
    }
    ShortcutManager* shortcutManager() const
    {
        return m_shortcutManager.get();
    }
    WindowRegistry* windowRegistry() const
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
     * Uses showOsdOnLayoutSwitch and osdStyle settings, same as manual layout switch.
     */
    void showLayoutOsdForAlgorithm(const QString& algorithmId, const QString& displayName, const QString& screenId);
    void clearHighlight();

    // ═══════════════════════════════════════════════════════════════════════════
    // Navigation handlers — single code path per operation (DRY/SOLID)
    // Resolve screen → check mode (autotile vs zones) → delegate → OSD from backend
    // ═══════════════════════════════════════════════════════════════════════════

    /** @brief Return the active IEngineLifecycle for a screen (autotile or snap) */
    IEngineLifecycle* engineForScreen(const QString& screenId) const;

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
     * and calls showAlgorithmOsdDeferred or showLayoutOsdDeferred per screen.
     * DRY helper shared by showDesktopSwitchOsd and settingsChanged handler.
     */
    void showOsdForAllScreens(int desktop, const QString& activity);

    /**
     * @brief Recompute which screens use autotile from layout assignments
     *
     * Reads all screen assignments via assignmentIdForScreen(), computes
     * which screens have autotile IDs, calls setAutotileScreens() on engine.
     */
    void updateAutotileScreens();

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
    std::unique_ptr<LayoutManager> m_layoutManager;
    // Manual layouts + autotile algorithms composed behind layoutSource().
    // The bundle owns all three objects so destruction is deterministic
    // (composite first, then the child sources it borrows from). See
    // layoutsourcefactory.h for the construction contract.
    PhosphorLayout::LayoutSourceBundle m_layoutSources;
    std::unique_ptr<LayoutComputeService> m_layoutComputeService;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<PhosphorZones::ZoneDetector> m_zoneDetector;
    // Single source of truth for live-window instance identity + metadata.
    // Populated by the kwin-effect bridge. Consumers query appIdFor() etc.
    // instead of parsing composite windowId strings.
    std::unique_ptr<WindowRegistry> m_windowRegistry;
    /// Plasma D-Bus panel-offset source. Declared before m_screenManager
    /// because the manager holds a non-owning IPanelSource* into it.
    std::unique_ptr<Phosphor::Screens::PlasmaPanelSource> m_panelSource;
    /// Settings-backed IConfigStore for VS topology. Shared by
    /// m_screenManager (Config::configStore) and m_virtualScreenSwapper
    /// (constructor arg). Declared before both so destruction order
    /// runs swapper → screen-manager → store.
    std::unique_ptr<SettingsConfigStore> m_virtualScreenStore;
    std::unique_ptr<Phosphor::Screens::ScreenManager> m_screenManager;
    /// OverlayService takes ScreenManager* via constructor injection — must
    /// be declared AFTER m_screenManager so the initializer-list construction
    /// order matches.
    std::unique_ptr<OverlayService> m_overlayService;
    std::unique_ptr<VirtualDesktopManager> m_virtualDesktopManager;
    std::unique_ptr<ActivityManager> m_activityManager;
    std::unique_ptr<ShortcutManager> m_shortcutManager;

    // Domain-specific D-Bus adaptors
    // D-Bus adaptors need a parent (the adapted object); Qt requires it.
    // So we use raw pointers; Qt parent-child system manages their lifetime
    LayoutAdaptor* m_layoutAdaptor = nullptr;
    SettingsAdaptor* m_settingsAdaptor = nullptr;
    OverlayAdaptor* m_overlayAdaptor = nullptr; // Overlay visibility only
    ZoneDetectionAdaptor* m_zoneDetectionAdaptor = nullptr; // PhosphorZones::Zone detection queries
    WindowTrackingAdaptor* m_windowTrackingAdaptor = nullptr; // Window-zone tracking
    ScreenAdaptor* m_screenAdaptor = nullptr;
    WindowDragAdaptor* m_windowDragAdaptor = nullptr; // Window drag handling

    // Mode tracking
    std::unique_ptr<ModeTracker> m_modeTracker;

    // Unified layout management
    std::unique_ptr<UnifiedLayoutController> m_unifiedLayoutController;

    // Daemon-owned tile-algorithm registry. Replaces the old
    // AlgorithmRegistry::instance() singleton — per-process ownership is
    // the only shape that works once PlasmaZones becomes a plugin-based
    // compositor/WM/shell (plugins can't share process-global state
    // safely). Declared before ScriptedAlgorithmLoader + AutotileEngine
    // because both take a borrowed pointer to it in their constructor.
    std::unique_ptr<PhosphorTiles::AlgorithmRegistry> m_algorithmRegistry;

    // Scripted algorithm loader (file watcher for user-defined JS algorithms)
    std::unique_ptr<PhosphorTiles::ScriptedAlgorithmLoader> m_scriptedAlgorithmLoader;

    // Window engines
    std::unique_ptr<AutotileEngine> m_autotileEngine;
    std::unique_ptr<SnapEngine> m_snapEngine;
    /// Single source of truth for "which engine owns screen X". Used by
    /// WindowTrackingAdaptor and (via @ref engineForScreen) daemon-internal
    /// dispatch paths. Owns no state of its own — just delegates to the
    /// layout manager and engine pointers it was constructed with.
    std::unique_ptr<ScreenModeRouter> m_screenModeRouter;
    /// Thin INavigationActions adapters presented via
    /// ScreenModeRouter::navigatorFor() so daemon/navigation.cpp shortcut
    /// handlers dispatch user intents through a common interface instead
    /// of branching on mode and calling engine methods ad hoc. Both
    /// adapters forward to their respective engine — SnapEngine owns the
    /// snap-mode navigation methods (see src/snap/snapengine/navigation_actions.cpp)
    /// and AutotileEngine owns the autotile ones.
    std::unique_ptr<AutotileNavigationAdapter> m_autotileNavigationAdapter;
    std::unique_ptr<SnapNavigationAdapter> m_snapNavigationAdapter;
    /// Stateless façade over m_virtualScreenStore for VS swap/rotate.
    /// Held as a member rather than reconstructed per-call so navigation
    /// handlers don't need to know about its dependencies.
    std::unique_ptr<Phosphor::Screens::VirtualScreenSwapper> m_virtualScreenSwapper;
    SnapAdaptor* m_snapAdaptor = nullptr;
    AutotileAdaptor* m_autotileAdaptor = nullptr;

    // Desktop/activity resolution helpers (DRY — used by multiple handlers)
    int currentDesktop() const;
    QString currentActivity() const;
    bool isCurrentContextLocked(const QString& screenId) const;
    bool isCurrentContextLockedForMode(const QString& screenId, int mode) const;

    /**
     * @brief Sync daemon-side float state when autotile floats/unfloats a window
     *
     * Propagates floating state to WindowTrackingService and KWin effect,
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

    bool m_running = false;
    int m_suppressResnapOsd = 0;

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

    // Single-threaded pool for shader baking — QShaderBaker/glslang is not
    // thread-safe for concurrent compilation (SIGSEGV in QSpirvCompiler).
    QThreadPool m_shaderBakePool;

    // Geometry update debouncing to prevent cascade of redundant recalculations
    QTimer m_geometryUpdateTimer;
    bool m_geometryUpdatePending = false;
    void processPendingGeometryUpdates();

    // After geometry updates settle, request KWin effect to re-apply window positions (panel editor fix)
    QTimer m_reapplyGeometriesTimer;
};

} // namespace PlasmaZones
