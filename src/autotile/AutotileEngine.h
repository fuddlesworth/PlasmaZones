// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <memory>

namespace PlasmaZones {

class AutotileConfig;
class Layout;
class LayoutManager;
class ScreenManager;
class Settings;
class TilingAlgorithm;
class TilingState;
class WindowTrackingService;

/**
 * @brief Core engine for automatic window tiling
 *
 * AutotileEngine coordinates automatic window tiling by:
 * - Tracking window open/close/focus events
 * - Managing per-screen TilingState
 * - Invoking tiling algorithms to calculate zone geometries
 * - Applying calculated zones to window positions
 *
 * The engine supports multiple tiling algorithms (Master-Stack, Columns, BSP)
 * and allows per-screen configuration of tiling parameters.
 *
 * Usage:
 * @code
 * auto *engine = new AutotileEngine(layoutManager, windowTracker, screenManager, this);
 * engine->setEnabled(true);
 * engine->setAlgorithm("master-stack");
 * @endcode
 *
 * @note The engine only tiles windows on screens where autotiling is enabled.
 *       Use config() to access per-screen configuration.
 *
 * @see TilingAlgorithm for the algorithm interface
 * @see TilingState for per-screen tiling state
 * @see AlgorithmRegistry for algorithm discovery
 */
class PLASMAZONES_EXPORT AutotileEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString algorithm READ algorithm WRITE setAlgorithm NOTIFY algorithmChanged)

public:
    /**
     * @brief Construct an AutotileEngine
     *
     * @param layoutManager Layout manager for zone access
     * @param windowTracker Window tracking service for window events
     * @param screenManager Screen manager for screen geometry
     * @param parent Parent QObject
     */
    explicit AutotileEngine(LayoutManager *layoutManager,
                            WindowTrackingService *windowTracker,
                            ScreenManager *screenManager,
                            QObject *parent = nullptr);
    ~AutotileEngine() override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Global enable/disable
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if autotiling is globally enabled
     * @return true if autotiling is active
     */
    bool isEnabled() const noexcept;

    /**
     * @brief Enable or disable autotiling globally
     *
     * When disabled, windows are not automatically tiled but existing
     * tile positions are preserved.
     *
     * @param enabled New enabled state
     */
    void setEnabled(bool enabled);

    // ═══════════════════════════════════════════════════════════════════════════
    // Algorithm selection
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get current algorithm ID
     * @return Algorithm identifier (e.g., "master-stack")
     */
    QString algorithm() const noexcept;

    /**
     * @brief Set the tiling algorithm
     *
     * If the algorithm ID is invalid, falls back to the default algorithm.
     *
     * @param algorithmId Algorithm identifier from AlgorithmRegistry
     */
    void setAlgorithm(const QString &algorithmId);

    /**
     * @brief Get the current algorithm instance
     * @return Pointer to algorithm, or nullptr if none set
     */
    TilingAlgorithm *currentAlgorithm() const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Tiling state access
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get the tiling state for a specific screen
     *
     * Creates the state if it doesn't exist.
     *
     * @param screenName Screen identifier
     * @return Pointer to TilingState (owned by engine)
     */
    TilingState *stateForScreen(const QString &screenName);

    /**
     * @brief Get the autotile configuration
     * @return Pointer to configuration
     */
    AutotileConfig *config() const noexcept;

    // ═══════════════════════════════════════════════════════════════════════════
    // Settings synchronization
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Apply all settings from Settings to internal config
     *
     * Copies all autotile-related settings from the Settings object to the
     * internal AutotileConfig. Also sets the algorithm and enabled state.
     * Call this once during initialization.
     *
     * @param settings Settings object to read from (not owned)
     */
    void syncFromSettings(Settings *settings);

    /**
     * @brief Connect to Settings change signals for live updates
     *
     * Connects to all autotile-related Settings signals and updates the
     * internal config when they change. Uses debouncing to coalesce rapid
     * changes (e.g., slider adjustments) into a single retile operation.
     *
     * @param settings Settings object to connect to (not owned, must outlive engine)
     */
    void connectToSettings(Settings *settings);

    // ═══════════════════════════════════════════════════════════════════════════
    // Manual tiling operations
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Force retiling of windows
     *
     * Recalculates and applies tiling for the specified screen,
     * or all screens if screenName is empty.
     *
     * @param screenName Screen to retile, or empty for all screens
     */
    Q_INVOKABLE void retile(const QString &screenName = QString());

    /**
     * @brief Swap positions of two tiled windows
     *
     * @param windowId1 First window ID
     * @param windowId2 Second window ID
     */
    Q_INVOKABLE void swapWindows(const QString &windowId1, const QString &windowId2);

    /**
     * @brief Promote a window to the master area
     *
     * For algorithms with a master concept, moves the window to the
     * master position. For other algorithms, moves to the first position.
     *
     * @param windowId Window to promote
     */
    Q_INVOKABLE void promoteToMaster(const QString &windowId);

    /**
     * @brief Demote a window from the master area
     *
     * Moves the window from master to the stack area.
     *
     * @param windowId Window to demote
     */
    Q_INVOKABLE void demoteFromMaster(const QString &windowId);

    /**
     * @brief Swap the currently focused window with the master window
     *
     * Convenience method that promotes the focused window to master position.
     * If the focused window is already master, this is a no-op.
     */
    Q_INVOKABLE void swapFocusedWithMaster();

    // ═══════════════════════════════════════════════════════════════════════════
    // Focus/window cycling
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Focus the next tiled window
     *
     * Cycles forward through the tiled window list.
     */
    Q_INVOKABLE void focusNext();

    /**
     * @brief Focus the previous tiled window
     *
     * Cycles backward through the tiled window list.
     */
    Q_INVOKABLE void focusPrevious();

    /**
     * @brief Focus the master window
     *
     * For algorithms with a master concept, focuses the master window.
     */
    Q_INVOKABLE void focusMaster();

    /**
     * @brief Notify the engine that a window has been focused
     *
     * Called by D-Bus adaptor when KWin reports a window focus change.
     * Updates the focused window in the appropriate TilingState.
     *
     * @param windowId Window ID that gained focus
     */
    void setFocusedWindow(const QString &windowId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Split ratio adjustment
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Increase the master area ratio
     *
     * Makes the master area larger by the specified delta.
     *
     * @param delta Amount to increase (default 0.05 = 5%)
     */
    Q_INVOKABLE void increaseMasterRatio(qreal delta = 0.05);

    /**
     * @brief Decrease the master area ratio
     *
     * Makes the master area smaller by the specified delta.
     *
     * @param delta Amount to decrease (default 0.05 = 5%)
     */
    Q_INVOKABLE void decreaseMasterRatio(qreal delta = 0.05);

    // ═══════════════════════════════════════════════════════════════════════════
    // Master count adjustment
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Increase the number of master windows
     */
    Q_INVOKABLE void increaseMasterCount();

    /**
     * @brief Decrease the number of master windows
     */
    Q_INVOKABLE void decreaseMasterCount();

    // ═══════════════════════════════════════════════════════════════════════════
    // Window rotation and floating (context-aware shortcuts support)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Rotate all tiled windows by one position
     *
     * Shifts all windows in the tiling order. Clockwise moves each window
     * to the next position (first becomes last), counterclockwise moves
     * each to the previous position (last becomes first).
     *
     * @param clockwise Direction of rotation
     */
    Q_INVOKABLE void rotateWindowOrder(bool clockwise = true);

    /**
     * @brief Toggle the focused window between tiled and floating states
     *
     * When the window is tiled, it becomes floating and is excluded from
     * automatic tiling. When floating, it re-enters the tiling layout.
     */
    Q_INVOKABLE void toggleFocusedWindowFloat();

    // ═══════════════════════════════════════════════════════════════════════════
    // Window event handlers (public API for external notification)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Notify the engine that a new window was added
     *
     * Called by Daemon when KWin reports a new window. Triggers retiling
     * if autotile is enabled and window is tileable.
     *
     * @param windowId Window identifier from KWin
     * @param screenName Screen where the window appeared
     */
    void windowOpened(const QString &windowId, const QString &screenName);

    /**
     * @brief Notify the engine that a window was closed
     *
     * Called by Daemon when KWin reports a window closed. Triggers retiling
     * to fill the gap left by the closed window.
     *
     * @param windowId Window identifier from KWin
     */
    void windowClosed(const QString &windowId);

    /**
     * @brief Notify the engine that a window was focused
     *
     * Called by Daemon when KWin reports window activation. Updates focus
     * tracking for tiling operations.
     *
     * @param windowId Window identifier from KWin
     * @param screenName Screen where the window is located
     */
    void windowFocused(const QString &windowId, const QString &screenName);

Q_SIGNALS:
    /**
     * @brief Emitted when the enabled state changes
     * @param enabled New enabled state
     */
    void enabledChanged(bool enabled);

    /**
     * @brief Emitted when the algorithm changes
     * @param algorithmId New algorithm ID
     */
    void algorithmChanged(const QString &algorithmId);

    /**
     * @brief Emitted when tiling layout changes for a screen
     * @param screenName Screen that was retiled
     */
    void tilingChanged(const QString &screenName);

    /**
     * @brief Emitted when a window is tiled to a new geometry
     * @param windowId Window that was tiled
     * @param geometry New window geometry
     */
    void windowTiled(const QString &windowId, const QRect &geometry);

    /**
     * @brief Emitted when a window should be focused
     *
     * The D-Bus adaptor forwards this signal to KWin effect for activation.
     *
     * @param windowId Window ID to focus
     */
    void focusWindowRequested(const QString &windowId);

private Q_SLOTS:
    void onWindowAdded(const QString &windowId);
    void onWindowRemoved(const QString &windowId);
    void onWindowFocused(const QString &windowId);
    void onScreenGeometryChanged(const QString &screenName);
    void onLayoutChanged(Layout *layout);

private:
    void connectSignals();
    bool insertWindow(const QString &windowId, const QString &screenName);
    void removeWindow(const QString &windowId);
    void recalculateLayout(const QString &screenName);
    void applyTiling(const QString &screenName);
    bool shouldTileWindow(const QString &windowId) const;
    QString screenForWindow(const QString &windowId) const;
    QRect screenGeometry(const QString &screenName) const;

    /**
     * @brief Helper to retile a screen after a window operation
     *
     * Recalculates layout and applies tiling if enabled, then emits tilingChanged.
     * Only emits signal if operationSucceeded is true.
     *
     * @param screenName Screen to retile
     * @param operationSucceeded Whether the triggering operation actually changed something
     */
    void retileAfterOperation(const QString &screenName, bool operationSucceeded);

    /**
     * @brief Helper to get tiled windows and state for focus operations
     *
     * Gets the focused window, validates screen, retrieves state and windows.
     * Returns empty list if any step fails.
     *
     * @param[out] outScreenName Screen name of the focused window
     * @param[out] outState Pointer to the TilingState (may be nullptr)
     * @return List of tiled windows, or empty list if unavailable
     */
    QStringList tiledWindowsForFocusedScreen(QString &outScreenName, TilingState *&outState) const;

    /**
     * @brief Helper to emit focus request for a window at calculated index
     *
     * Consolidates common focus operation logic: get windows, calculate index, emit signal.
     *
     * @param indexOffset Offset from current focus (-1 for previous, +1 for next, 0 for current)
     * @param useFirst If true, always focus first window (for focusMaster)
     */
    void emitFocusRequestAtIndex(int indexOffset, bool useFirst = false);

    /**
     * @brief Helper to apply an operation to all screen states
     *
     * Iterates all screen states and applies the given operation, then retiles if enabled.
     *
     * @param operation Function to apply to each TilingState
     */
    void applyToAllStates(const std::function<void(TilingState *)> &operation);

    // ═══════════════════════════════════════════════════════════════════════════════
    // Helper Methods
    // ═══════════════════════════════════════════════════════════════════════════════

    /**
     * @brief Validate that a windowId is not empty, logging a warning if it is
     * @param windowId Window ID to validate
     * @param operation Operation name for the warning message
     * @return true if valid (non-empty), false if empty (logs warning)
     */
    bool warnIfEmptyWindowId(const QString& windowId, const char* operation) const;

    /**
     * @brief Get TilingState for a window by looking up its screen
     *
     * Consolidates the common pattern of m_windowToScreen lookup + stateForScreen.
     *
     * @param windowId Window ID to look up
     * @param outScreenName If non-null, receives the screen name
     * @return TilingState pointer or nullptr if window not tracked/screen invalid
     */
    TilingState* stateForWindow(const QString& windowId, QString* outScreenName = nullptr);

    LayoutManager *m_layoutManager = nullptr;
    WindowTrackingService *m_windowTracker = nullptr;
    ScreenManager *m_screenManager = nullptr;
    std::unique_ptr<AutotileConfig> m_config;

    bool m_enabled = false;
    QString m_algorithmId;
    QHash<QString, TilingState *> m_screenStates; // Owned via Qt parent (this)
    QHash<QString, QString> m_windowToScreen;     // windowId -> screenName

    // Settings synchronization
    QPointer<Settings> m_settings;  // QPointer for safe access if Settings destroyed
    QTimer m_settingsRetileTimer;   // Debounce timer for settings changes
    bool m_pendingSettingsRetile = false;

    /**
     * @brief Schedule a debounced retile after settings change
     *
     * Sets pending flag and starts/restarts the debounce timer.
     * When timer fires, retile() is called if enabled.
     */
    void scheduleSettingsRetile();

    /**
     * @brief Process pending settings retile after debounce timeout
     */
    void processSettingsRetile();
};

} // namespace PlasmaZones
