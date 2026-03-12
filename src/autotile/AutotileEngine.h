// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "core/constants.h"
#include "core/iwindowengine.h"
#include <QHash>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <memory>

#include "OverflowManager.h"

namespace PlasmaZones {

class AutotileConfig;
class Layout;
class LayoutManager;
class NavigationController;
class PerScreenConfigResolver;
class ScreenManager;
class Settings;
class SettingsBridge;
class TilingAlgorithm;
class TilingState;
class WindowTrackingService;

/**
 * @brief Core engine for automatic window tiling.
 *
 * Coordinates per-screen TilingState, invokes tiling algorithms (Master-Stack,
 * Columns, BSP), and applies calculated zone geometries to window positions.
 * Only tiles windows on screens where autotiling is enabled.
 *
 * @see TilingAlgorithm, TilingState, AlgorithmRegistry
 */
class PLASMAZONES_EXPORT AutotileEngine : public QObject, public IWindowEngine
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ isEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString algorithm READ algorithm WRITE setAlgorithm NOTIFY algorithmChanged)

    friend class NavigationController;
    friend class PerScreenConfigResolver;
    friend class SettingsBridge;

public:
    explicit AutotileEngine(LayoutManager* layoutManager, WindowTrackingService* windowTracker,
                            ScreenManager* screenManager, QObject* parent = nullptr);
    ~AutotileEngine() override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-screen autotile state (derived from layout assignments)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if any screen has autotile enabled
     * @return true if at least one screen uses autotile
     */
    bool isEnabled() const noexcept;

    /**
     * @brief Check if a specific screen uses autotile
     * @param screenName Screen to check
     * @return true if the screen has an autotile assignment
     */
    bool isAutotileScreen(const QString& screenName) const;

    // IWindowEngine
    bool isActiveOnScreen(const QString& screenName) const override;

    /**
     * @brief Get the set of screens currently using autotile
     * @return Set of screen names with autotile assignments
     */
    const QSet<QString>& autotileScreens() const
    {
        return m_autotileScreens;
    }

    /**
     * @brief Set which screens use autotile (derived from layout assignments)
     *
     * Computes added/removed screens, retiles newly-added ones, and emits
     * enabledChanged if the overall state flips.
     *
     * @param screens Set of screen names that should use autotile
     */
    void setAutotileScreens(const QSet<QString>& screens);

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
    void setAlgorithm(const QString& algorithmId);

    /**
     * @brief Get the current algorithm instance
     * @return Pointer to algorithm, or nullptr if none set
     */
    TilingAlgorithm* currentAlgorithm() const;

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
    TilingState* stateForScreen(const QString& screenName);

    /**
     * @brief Get the autotile configuration
     * @return Pointer to configuration
     */
    AutotileConfig* config() const noexcept;

    // ═══════════════════════════════════════════════════════════════════════════
    // Session Persistence
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Save tiling state to KConfig for session persistence
     *
     * Serializes per-screen TilingState (window order by stableId,
     * masterCount, splitRatio, algorithm) to the [AutoTileState] config group.
     * Called by Daemon::stop() before shutdown.
     */
    void saveState() override;

    /**
     * @brief Load tiling state from KConfig
     *
     * Deserializes per-screen state from the [AutoTileState] config group.
     * Actual retiling is deferred until windows are announced by the KWin effect.
     * Called by Daemon::start() after initialization.
     */
    void loadState() override;

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
    void syncFromSettings(Settings* settings);

    // Per-screen config — forwarded to PerScreenConfigResolver
    void applyPerScreenConfig(const QString& screenName, const QVariantMap& overrides);
    void clearPerScreenConfig(const QString& screenName);
    QVariantMap perScreenOverrides(const QString& screenName) const;
    bool hasPerScreenOverride(const QString& screenName, const QString& key) const;

    // Effective per-screen values — forwarded to PerScreenConfigResolver
    int effectiveInnerGap(const QString& screenName) const;
    int effectiveOuterGap(const QString& screenName) const;
    EdgeGaps effectiveOuterGaps(const QString& screenName) const;
    bool effectiveSmartGaps(const QString& screenName) const;
    bool effectiveRespectMinimumSize(const QString& screenName) const;
    int effectiveMaxWindows(const QString& screenName) const;
    QString effectiveAlgorithmId(const QString& screenName) const;
    TilingAlgorithm* effectiveAlgorithm(const QString& screenName) const;

    /**
     * @brief Connect to Settings change signals for live updates
     *
     * Connects to all autotile-related Settings signals and updates the
     * internal config when they change. Uses debouncing to coalesce rapid
     * changes (e.g., slider adjustments) into a single retile operation.
     *
     * @param settings Settings object to connect to (not owned, must outlive engine)
     */
    void connectToSettings(Settings* settings);

    // ═══════════════════════════════════════════════════════════════════════════
    // Manual tiling operations
    // ═══════════════════════════════════════════════════════════════════════════

    void setInnerGap(int gap);
    void setOuterGap(int gap);
    void setSmartGaps(bool enabled);
    void setFocusNewWindows(bool enabled);
    /**
     * @brief Force retiling of windows
     *
     * Recalculates and applies tiling for the specified screen,
     * or all screens if screenName is empty.
     *
     * @param screenName Screen to retile, or empty for all screens
     */
    Q_INVOKABLE void retile(const QString& screenName = QString());

    /**
     * @brief Swap positions of two tiled windows
     *
     * @param windowId1 First window ID
     * @param windowId2 Second window ID
     */
    Q_INVOKABLE void swapWindows(const QString& windowId1, const QString& windowId2);

    /**
     * @brief Promote a window to the master area
     *
     * For algorithms with a master concept, moves the window to the
     * master position. For other algorithms, moves to the first position.
     *
     * @param windowId Window to promote
     */
    Q_INVOKABLE void promoteToMaster(const QString& windowId);

    /**
     * @brief Demote a window from the master area
     *
     * Moves the window from master to the stack area.
     *
     * @param windowId Window to demote
     */
    Q_INVOKABLE void demoteFromMaster(const QString& windowId);

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
    void setFocusedWindow(const QString& windowId);

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

    /**
     * @brief Set master ratio globally (config + all per-screen states)
     *
     * Used by D-Bus adaptor to set the ratio as an absolute value,
     * updating both the global config and all existing per-screen states.
     *
     * @param ratio New split ratio (clamped to valid range)
     */
    void setGlobalSplitRatio(qreal ratio);

    /**
     * @brief Set master count globally (config + all per-screen states)
     *
     * Used by D-Bus adaptor to set the count as an absolute value,
     * updating both the global config and all existing per-screen states.
     *
     * @param count New master count (clamped to valid range)
     */
    void setGlobalMasterCount(int count);

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
     *
     * @note Prefer toggleWindowFloat() which takes explicit IDs and avoids
     *       focus-tracking desync issues.
     */
    Q_INVOKABLE void toggleFocusedWindowFloat();

    /**
     * @brief Toggle a specific window between tiled and floating states
     *
     * Bypasses internal focus tracking by using the caller-supplied windowId
     * and screenName directly. This avoids silent no-ops caused by
     * m_activeScreen or focusedWindow() desyncing from KWin's actual state
     * after rapid repeated toggles.
     *
     * @param windowId Window identifier from KWin
     * @param screenName Screen where the window is located
     */
    Q_INVOKABLE void toggleWindowFloat(const QString& windowId, const QString& screenName) override;

    /**
     * @brief Swap the focused window with the adjacent window in tiling order
     *
     * Maps directional keyboard shortcuts (move/swap window left/right/up/down)
     * to autotile's linear tiling order. Forward swaps with the next window,
     * backward swaps with the previous.
     *
     * @param direction Direction string ("left", "right", "up", "down")
     * @param action OSD action label — "move" or "swap" (defaults to "move")
     */
    Q_INVOKABLE void swapFocusedInDirection(const QString& direction, const QString& action = QStringLiteral("move"));

    /**
     * @brief Focus the adjacent window in tiling order with OSD feedback
     *
     * Maps directional focus/cycle shortcuts to autotile's linear order.
     *
     * @param direction Direction string ("left", "right", "up", "down")
     * @param action OSD action label — "focus" or "cycle" (defaults to "focus")
     */
    Q_INVOKABLE void focusInDirection(const QString& direction,
                                      const QString& action = QStringLiteral("focus")) override;

    /**
     * @brief Move the focused window to a specific position in the tiling order
     *
     * Maps "snap to zone N" shortcuts to autotile positions.
     *
     * @param position Target position (1-based, clamped to valid range)
     */
    Q_INVOKABLE void moveFocusedToPosition(int position);

    // IWindowEngine wrappers (delegate to existing methods)
    void swapInDirection(const QString& direction, const QString& action) override;
    void rotateWindows(bool clockwise, const QString& screenName) override;
    void moveToPosition(const QString& windowId, int position, const QString& screenName) override;

    /**
     * @brief Set the floating state of a specific window
     *
     * When shouldFloat is true, marks the window as floating (excluded from
     * automatic tiling) and retiles the remaining windows. When false,
     * removes the floating flag and re-inserts the window into the tiling layout.
     *
     * @param windowId Window identifier from KWin
     * @param shouldFloat True to float, false to unfloat
     */
    Q_INVOKABLE void setWindowFloat(const QString& windowId, bool shouldFloat) override;

    /**
     * @brief Float a specific window by its ID (convenience forwarder)
     */
    Q_INVOKABLE void floatWindow(const QString& windowId);

    /**
     * @brief Unfloat a specific window by its ID (convenience forwarder)
     */
    Q_INVOKABLE void unfloatWindow(const QString& windowId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Zone-ordered window transitions (snapping ↔ autotile)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Pre-seed initial window order for deterministic snapping → autotile transitions
     *
     * When toggling from manual snapping to autotile, windows arrive in KWin stacking
     * order (focus-based). This method stores a zone-ordered list so that insertWindow()
     * can place windows in the correct autotile positions (zone 1 → master, etc.).
     *
     * Only takes effect when the screen's TilingState is empty (no prior windows from
     * session restore). The pending order is consumed as windows are inserted.
     *
     * @param screenName Screen to set initial order for
     * @param windowIds Window IDs in desired order (zone-number ascending)
     */
    void setInitialWindowOrder(const QString& screenName, const QStringList& windowIds);

    /**
     * @brief Clear saved floating state for windows that are actively zone-snapped.
     *
     * Called during snapping → autotile transitions so that windows the user
     * re-snapped in manual mode aren't incorrectly restored as floating.
     *
     * @param windowIds Windows to remove from the saved floating set
     */
    void clearSavedFloatingForWindows(const QStringList& windowIds);

    /**
     * @brief Clear ALL saved floating state (used when autotile is disabled globally)
     *
     * Prevents stale entries from incorrectly floating windows on next activation.
     */
    void clearAllSavedFloating();

    /**
     * @brief Get the current tiled window order for a screen
     *
     * Returns the autotile engine's tiled window list for deterministic
     * autotile → snapping transitions. Call BEFORE switching layouts so
     * the TilingState still exists.
     *
     * @param screenName Screen to query
     * @return Ordered list of tiled window IDs (master first), or empty if no state
     */
    QStringList tiledWindowOrder(const QString& screenName) const;

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
     * @param minWidth Window minimum width in pixels (0 if unconstrained)
     * @param minHeight Window minimum height in pixels (0 if unconstrained)
     */
    using IWindowEngine::windowOpened; // Expose 2-arg convenience overload
    void windowOpened(const QString& windowId, const QString& screenName, int minWidth, int minHeight) override;

    /**
     * @brief Update a window's minimum size at runtime
     *
     * Called when a window's minimum size changes after initial windowOpened.
     * Triggers retiling if the new minimum differs from the stored value.
     *
     * @param windowId Window identifier from KWin
     * @param minWidth New minimum width in pixels (0 if unconstrained)
     * @param minHeight New minimum height in pixels (0 if unconstrained)
     */
    void windowMinSizeUpdated(const QString& windowId, int minWidth, int minHeight);

    /**
     * @brief Notify the engine that a window was closed
     *
     * Called by Daemon when KWin reports a window closed. Triggers retiling
     * to fill the gap left by the closed window.
     *
     * @param windowId Window identifier from KWin
     */
    void windowClosed(const QString& windowId) override;

    /**
     * @brief Remove a window from tracking without triggering a retile
     *
     * Used by windowsClosedBatch() to remove multiple windows in one pass,
     * deferring the retile until all removals are complete.
     *
     * @param windowId Window identifier from KWin
     */
    void removeWindowOnly(const QString& windowId);

    /**
     * @brief Get the screen name for a tracked window
     * @param windowId Window identifier
     * @return Screen name, or empty if not tracked
     */
    QString screenForWindow(const QString& windowId) const;

    /**
     * @brief Notify the engine that a window was focused
     *
     * Called by Daemon when KWin reports window activation. Updates focus
     * tracking for tiling operations.
     *
     * @param windowId Window identifier from KWin
     * @param screenName Screen where the window is located
     */
    void windowFocused(const QString& windowId, const QString& screenName) override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Retile helpers (public — used by extracted classes)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Schedule a deferred retile for a screen
     *
     * Adds the screen to the pending set and posts a QueuedConnection call
     * to processPendingRetiles(). Multiple calls in the same event loop pass
     * are coalesced — only one retile fires per screen.
     */
    void scheduleRetileForScreen(const QString& screenName);

    /**
     * @brief Helper to retile a screen after a window operation
     *
     * Recalculates layout and applies tiling if enabled, then emits tilingChanged.
     * Only emits signal if operationSucceeded is true.
     *
     * @param screenName Screen to retile
     * @param operationSucceeded Whether the triggering operation actually changed something
     */
    void retileAfterOperation(const QString& screenName, bool operationSucceeded);

Q_SIGNALS:
    /**
     * @brief Emitted when the enabled state changes
     * @param enabled New enabled state
     */
    void enabledChanged(bool enabled);

    /**
     * @brief Emitted when the set of autotile screens changes
     * @param screenNames List of screen names using autotile
     */
    void autotileScreensChanged(const QStringList& screenNames);

    /**
     * @brief Emitted when the algorithm changes
     * @param algorithmId New algorithm ID
     */
    void algorithmChanged(const QString& algorithmId);

    /**
     * @brief Emitted when tiling layout changes for a screen
     * @param screenName Screen that was retiled
     */
    void tilingChanged(const QString& screenName);

    /**
     * @brief Emitted when a window's floating state changes
     * @param windowId Window whose floating state changed
     * @param floating True if the window is now floating, false if tiled
     * @param screenName Screen where the window is (for OSD placement)
     */
    void windowFloatingChanged(const QString& windowId, bool floating, const QString& screenName);

    /**
     * @brief Emitted when windows are tiled to new geometries (batch)
     *
     * JSON array of objects with windowId, x, y, width, height.
     * The adaptor forwards to KWin effect.
     *
     * @param tileRequestsJson JSON array of {windowId,x,y,width,height}
     */
    void windowsTiled(const QString& tileRequestsJson);

    /**
     * @brief Emitted when a window should be focused
     *
     * The D-Bus adaptor forwards this signal to KWin effect for activation.
     *
     * @param windowId Window ID to focus
     */
    void focusWindowRequested(const QString& windowId);

    /**
     * @brief Emitted when an autotile navigation operation completes (for OSD)
     *
     * Same signature as WindowTrackingAdaptor::navigationFeedback so the daemon
     * can use a single handler for both manual (KWin effect) and autotile
     * (engine) feedback. Ensures consistent OSD display: shortcut → operation → OSD.
     *
     * @param success Whether the operation succeeded
     * @param action Operation type: "rotate", "focus_master", "swap_master", etc.
     * @param reason Failure reason or success detail (e.g. "clockwise:3")
     * @param sourceZoneId Source zone (empty for rotate)
     * @param targetZoneId Target zone (empty for rotate)
     * @param screenName Screen where operation occurred
     */
    void navigationFeedbackRequested(bool success, const QString& action, const QString& reason,
                                     const QString& sourceZoneId, const QString& targetZoneId,
                                     const QString& screenName);

    /**
     * @brief Emitted when windows are released from autotile management
     *
     * Fired when screens are removed from autotile.
     *
     * @param windowIds Window IDs no longer under autotile control
     */
    void windowsReleasedFromTiling(const QStringList& windowIds);

private Q_SLOTS:
    void onWindowAdded(const QString& windowId);
    void onWindowRemoved(const QString& windowId);
    void onWindowFocused(const QString& windowId);
    void onScreenGeometryChanged(const QString& screenName);
    void onLayoutChanged(Layout* layout);

private:
    void connectSignals();
    bool insertWindow(const QString& windowId, const QString& screenName);
    void removeWindow(const QString& windowId);
    void recalculateLayout(const QString& screenName);
    void applyTiling(const QString& screenName);
    bool shouldTileWindow(const QString& windowId) const;
    QRect screenGeometry(const QString& screenName) const;

    /**
     * @brief Reset maxWindows when switching algorithms (DRY helper)
     *
     * If the current maxWindows matches the old algorithm's default, reset
     * it to the new algorithm's default. Shared by setAlgorithm() and
     * syncFromSettings().
     */
    void resetMaxWindowsForAlgorithmSwitch(TilingAlgorithm* oldAlgo, TilingAlgorithm* newAlgo);

    /**
     * @brief Propagate global split ratio to screens without per-screen overrides
     */
    void propagateGlobalSplitRatio();

    /**
     * @brief Propagate global master count to screens without per-screen overrides
     */
    void propagateGlobalMasterCount();

    /**
     * @brief Backfill windows on screens where tiledWindowCount < maxWindows
     *
     * When maxWindows increases, windows previously rejected by onWindowAdded()'s
     * gate check remain untiled. This method iterates autotile screens and inserts
     * tracked-but-untiled windows up to the per-screen effective limit.
     *
     * Note: Iteration order over m_windowToScreen (QHash) is non-deterministic.
     * Backfill order may differ between runs; this is acceptable since all
     * candidates are equally valid.
     */
    void backfillWindows();

    /**
     * @brief Recover overflow windows and retile a single screen
     *
     * Encapsulates the four-step retile sequence: overflow recovery,
     * recalculate layout, apply tiling, emit tilingChanged.
     *
     * @param screenName Screen to retile
     */
    void retileScreen(const QString& screenName);

    // ═══════════════════════════════════════════════════════════════════════════════
    // Helper Methods
    // ═══════════════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if all pending initial-order windows are resolved for a screen
     *
     * Returns true if every window in the pending order for the given screen
     * is already present in the screen's TilingState. If all resolved,
     * removes the pending order entry. Used by insertWindow() and removeWindow().
     *
     * @param screenName Screen whose pending order to check
     * @return true if the pending order was fully resolved and removed
     */
    bool cleanupPendingOrderIfResolved(const QString& screenName);

    /**
     * @brief Validate that a windowId is not empty, logging a warning if it is
     * @param windowId Window ID to validate
     * @param operation Operation name for the warning message
     * @return true if valid (non-empty), false if empty (logs warning)
     */
    bool warnIfEmptyWindowId(const QString& windowId, const char* operation) const;

    /**
     * @brief Shared toggle-float implementation for toggleFocusedWindowFloat/toggleWindowFloat
     *
     * Toggles the floating state, retiles, and emits windowFloatingChanged.
     */
    void performToggleFloat(TilingState* state, const QString& windowId, const QString& screenName);

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

    LayoutManager* m_layoutManager = nullptr;
    WindowTrackingService* m_windowTracker = nullptr;
    ScreenManager* m_screenManager = nullptr;
    std::unique_ptr<AutotileConfig> m_config;
    std::unique_ptr<PerScreenConfigResolver> m_configResolver;
    std::unique_ptr<NavigationController> m_navigation;
    std::unique_ptr<SettingsBridge> m_settingsBridge;

    QSet<QString> m_autotileScreens;
    QString m_algorithmId;
    QString m_activeScreen; // Last-focused screen (updated by onWindowFocused)
    QHash<QString, TilingState*> m_screenStates; // Owned via Qt parent (this)
    QHash<QString, QString> m_windowToScreen; // windowId -> screenName
    QHash<QString, QSize> m_windowMinSizes; // windowId -> minimum size from KWin

    // Floating window IDs preserved across mode switches.
    // When autotile is deactivated, floated windows are saved here so that
    // re-enabling autotile restores them as floating regardless of screen.
    QSet<QString> m_savedFloatingWindows;

    // Pre-seeded window order for snapping → autotile transitions.
    // Keyed by screen connector name (screen->name()), NOT stable screen ID.
    // Consumed by insertWindow() as windows arrive; also cleaned up by
    // removeWindow() if a pre-seeded window closes before arriving.
    QHash<QString, QStringList> m_pendingInitialOrders;

    // Per-screen overflow tracking with O(1) reverse-index lookups.
    OverflowManager m_overflow;

    bool m_retiling = false;

    // Queued-connection retile coalescing: windowOpened D-Bus calls arriving in
    // the same event loop pass are coalesced into a single retile per screen.
    // Uses QMetaObject::invokeMethod(Qt::QueuedConnection) which fires after
    // all currently-pending events are processed — no fixed delay needed.
    QSet<QString> m_pendingRetileScreens;
    bool m_retilePending = false;

    // Deferred focus: set by onWindowAdded, emitted after applyTiling so the
    // focus request arrives at KWin AFTER windowsTiled (whose onComplete raises
    // windows in tiling order). Without this, the raise loop buries the new window.
    QString m_pendingFocusWindowId;

    /**
     * @brief Process all pending retiles (fires via QueuedConnection)
     *
     * Retiles all screens that had windows added or were newly activated
     * since the last event loop pass.
     */
    void processPendingRetiles();
};

} // namespace PlasmaZones
