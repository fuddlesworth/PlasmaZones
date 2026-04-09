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
#include "../core/types.h"
#include "../autotile/AutotileEngine.h"

namespace PlasmaZones {

enum class DisabledReason;
class Layout;
class LayoutManager;
class ZoneDetector;
class Settings;
class OverlayService;
class ScreenManager;
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
class IWindowEngine;
class IConfigBackend;
class ScriptedAlgorithmLoader;
class SnapAdaptor;
class SnapEngine;

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
    ZoneDetector* zoneDetector() const
    {
        return m_zoneDetector.get();
    }
    Settings* settings() const
    {
        return m_settings.get();
    }
    OverlayService* overlayService() const
    {
        return m_overlayService.get();
    }
    ScreenManager* screenManager() const
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

    // Overlay control (delegates to OverlayService)
    Q_INVOKABLE void showOverlay();
    Q_INVOKABLE void hideOverlay();
    Q_INVOKABLE bool isOverlayVisible() const;

    // OSD notifications
    void showLayoutOsd(Layout* layout, const QString& screenId = QString());
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

    /** @brief Return the active IWindowEngine for a screen (autotile or snap) */
    IWindowEngine* engineForScreen(const QString& screenId) const;

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
     * Iterates all floating windows and saves non-autotile-floated ones
     * to WTS's savedSnapFloating set. Idempotent (QSet::insert).
     */
    void presaveSnapFloats();

    /**
     * @brief Capture autotile window order for all autotile screens
     *
     * Must be called BEFORE any mode switch that destroys TilingState
     * (e.g. applyLayoutById, handleAutotileDisabled, updateAutotileScreens).
     *
     * @return Map of (screen, desktop, activity) -> ordered window IDs (master first)
     */
    QHash<TilingStateKey, QStringList> captureAutotileOrders() const;

    /**
     * @brief Restore pre-tile geometry for autotile-only windows
     *
     * Iterates m_lastAutotileOrders and calls applyGeometryForFloat for each
     * window that has no zone assignment (never manually snapped). Zone-snapped
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

    std::unique_ptr<IConfigBackend> m_configBackend;
    std::unique_ptr<LayoutManager> m_layoutManager;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<ZoneDetector> m_zoneDetector;
    std::unique_ptr<OverlayService> m_overlayService;
    std::unique_ptr<ScreenManager> m_screenManager;
    std::unique_ptr<VirtualDesktopManager> m_virtualDesktopManager;
    std::unique_ptr<ActivityManager> m_activityManager;
    std::unique_ptr<ShortcutManager> m_shortcutManager;

    // Domain-specific D-Bus adaptors
    // D-Bus adaptors need a parent (the adapted object); Qt requires it.
    // So we use raw pointers; Qt parent-child system manages their lifetime
    LayoutAdaptor* m_layoutAdaptor = nullptr;
    SettingsAdaptor* m_settingsAdaptor = nullptr;
    OverlayAdaptor* m_overlayAdaptor = nullptr; // Overlay visibility only
    ZoneDetectionAdaptor* m_zoneDetectionAdaptor = nullptr; // Zone detection queries
    WindowTrackingAdaptor* m_windowTrackingAdaptor = nullptr; // Window-zone tracking
    ScreenAdaptor* m_screenAdaptor = nullptr;
    WindowDragAdaptor* m_windowDragAdaptor = nullptr; // Window drag handling

    // Mode tracking
    std::unique_ptr<ModeTracker> m_modeTracker;

    // Unified layout management
    std::unique_ptr<UnifiedLayoutController> m_unifiedLayoutController;

    // Scripted algorithm loader (file watcher for user-defined JS algorithms)
    std::unique_ptr<ScriptedAlgorithmLoader> m_scriptedAlgorithmLoader;

    // Window engines
    std::unique_ptr<AutotileEngine> m_autotileEngine;
    std::unique_ptr<SnapEngine> m_snapEngine;
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
