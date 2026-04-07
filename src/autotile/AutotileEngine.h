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
#include <functional>
#include <memory>

#include "OverflowManager.h"

#include <QHashFunctions>

namespace PlasmaZones {

/**
 * @brief Composite key for per-desktop/activity TilingState lookup
 *
 * desktop=1 (matching m_currentDesktop default) and empty activity represent
 * the initial desktop/activity context. Always uses explicit desktop numbers.
 */
struct TilingStateKey
{
    QString screenId;
    int desktop = 1;
    QString activity;

    bool operator==(const TilingStateKey& other) const
    {
        return screenId == other.screenId && desktop == other.desktop && activity == other.activity;
    }
};

inline size_t qHash(const TilingStateKey& key, size_t seed = 0)
{
    return qHashMulti(seed, key.screenId, key.desktop, key.activity);
}

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

    /// @brief Read-only access to per-screen tiling states (used by daemon for state persistence)
    const QHash<TilingStateKey, TilingState*>& screenStates() const
    {
        return m_screenStates;
    }

    /**
     * @brief Check if a specific screen uses autotile
     * @param screenId Screen to check
     * @return true if the screen has an autotile assignment
     */
    bool isAutotileScreen(const QString& screenId) const;

    // IWindowEngine
    bool isActiveOnScreen(const QString& screenId) const override;

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
     * enabledChanged if the overall state flips.  Only processes states for
     * the current desktop/activity — states for other desktops are preserved.
     *
     * @param screens Set of screen names that should use autotile
     */
    void setAutotileScreens(const QSet<QString>& screens);

    /**
     * @brief Set the current virtual desktop for per-desktop tiling state
     *
     * Swaps the active TilingState set without releasing windows. Must be
     * called BEFORE updateAutotileScreens() on desktop switch so the engine
     * resolves states for the correct desktop.
     *
     * @param desktop Virtual desktop number (1-based from KWin)
     */
    void setCurrentDesktop(int desktop);

    /**
     * @brief Set the current activity for per-activity tiling state
     *
     * Swaps the active TilingState set without releasing windows. Must be
     * called BEFORE updateAutotileScreens() on activity switch so the engine
     * resolves states for the correct activity.
     *
     * @param activity Activity ID (empty string for no activity)
     */
    void setCurrentActivity(const QString& activity);

    /**
     * @brief Pin screens where all autotiled windows are sticky (on all desktops)
     *
     * For screens where every tiled/floating window is sticky, pins the
     * TilingStateKey desktop to the current effective desktop so that
     * currentKeyForScreen() continues to resolve the existing state after
     * a desktop switch. Screens where not all windows are sticky are unpinned,
     * with state migrated to m_currentDesktop if necessary.
     *
     * Must be called BEFORE setCurrentDesktop()/setCurrentActivity() so the
     * pins are evaluated against the pre-switch context.
     *
     * @param isWindowSticky Callback returning true if the window is on all desktops
     */
    void updateStickyScreenPins(const std::function<bool(const QString&)>& isWindowSticky);

    /**
     * @brief Prune TilingState and saved floating entries for a removed desktop
     *
     * Removes all states where key.desktop == removedDesktop. Called when a
     * virtual desktop is deleted so stale entries don't accumulate.
     */
    void pruneStatesForDesktop(int removedDesktop);

    /**
     * @brief Prune TilingState entries for activities not in the given set
     *
     * Removes states whose activity is non-empty and not in validActivities.
     * Called when activities change so stale entries don't accumulate.
     */
    void pruneStatesForActivities(const QStringList& validActivities);

    /**
     * @brief Get the current virtual desktop tracked by the engine
     */
    int currentDesktop() const noexcept
    {
        return m_currentDesktop;
    }

    /**
     * @brief Get the current activity tracked by the engine
     */
    const QString& currentActivity() const noexcept
    {
        return m_currentActivity;
    }

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
     * @param screenId Screen identifier
     * @return Pointer to TilingState (owned by engine)
     */
    TilingState* stateForScreen(const QString& screenId);

    /**
     * @brief Get the autotile configuration
     * @return Pointer to configuration
     */
    AutotileConfig* config() const noexcept;

    // ═══════════════════════════════════════════════════════════════════════════
    // Session Persistence
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Save tiling state via persistence delegate
     *
     * Delegates to the save function set by setPersistenceDelegate().
     * Typically wired to WTA's saveState() by the daemon.
     */
    void saveState() override;

    /**
     * @brief Load tiling state via persistence delegate
     *
     * Delegates to the load function set by setPersistenceDelegate().
     * Typically wired to WTA's loadState() by the daemon.
     */
    void loadState() override;

    /**
     * @brief Set persistence callbacks for save/load
     *
     * KConfig persistence is owned by WindowTrackingAdaptor (engine is KConfig-free).
     * These callbacks allow AutotileEngine to fulfill the IWindowEngine persistence
     * contract without introducing KConfig as a dependency.
     *
     * @param saveFn Called by saveState() to persist tiling state
     * @param loadFn Called by loadState() to restore tiling state
     */
    void setPersistenceDelegate(std::function<void()> saveFn, std::function<void()> loadFn)
    {
        m_persistSaveFn = std::move(saveFn);
        m_persistLoadFn = std::move(loadFn);
    }

    /**
     * @brief Access the settings bridge for serialization delegates
     */
    SettingsBridge* settingsBridge() const
    {
        return m_settingsBridge.get();
    }

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
    void applyPerScreenConfig(const QString& screenId, const QVariantMap& overrides);
    void clearPerScreenConfig(const QString& screenId);
    QVariantMap perScreenOverrides(const QString& screenId) const;
    bool hasPerScreenOverride(const QString& screenId, const QString& key) const;
    void updatePerScreenOverride(const QString& screenId, const QString& key, const QVariant& value);

    // Effective per-screen values — forwarded to PerScreenConfigResolver
    int effectiveInnerGap(const QString& screenId) const;
    int effectiveOuterGap(const QString& screenId) const;
    EdgeGaps effectiveOuterGaps(const QString& screenId) const;
    bool effectiveSmartGaps(const QString& screenId) const;
    bool effectiveRespectMinimumSize(const QString& screenId) const;
    int effectiveMaxWindows(const QString& screenId) const;
    qreal effectiveSplitRatioStep(const QString& screenId) const;
    QString effectiveAlgorithmId(const QString& screenId) const;
    TilingAlgorithm* effectiveAlgorithm(const QString& screenId) const;

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
     * or all screens if screenId is empty.
     *
     * @param screenId Screen to retile, or empty for all screens
     */
    Q_INVOKABLE void retile(const QString& screenId = QString());

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
     * and screenId directly. This avoids silent no-ops caused by
     * m_activeScreen or focusedWindow() desyncing from KWin's actual state
     * after rapid repeated toggles.
     *
     * @param windowId Window identifier from KWin
     * @param screenId Screen where the window is located
     */
    Q_INVOKABLE void toggleWindowFloat(const QString& windowId, const QString& screenId) override;

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
    void rotateWindows(bool clockwise, const QString& screenId) override;
    void moveToPosition(const QString& windowId, int position, const QString& screenId) override;

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
     * @param screenId Screen to set initial order for
     * @param windowIds Window IDs in desired order (zone-number ascending)
     */
    void setInitialWindowOrder(const QString& screenId, const QStringList& windowIds);

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
     * @param screenId Screen to query
     * @return Ordered list of tiled window IDs (master first), or empty if no state
     */
    QStringList tiledWindowOrder(const QString& screenId) const;

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
     * @param screenId Screen where the window appeared
     * @param minWidth Window minimum width in pixels (0 if unconstrained)
     * @param minHeight Window minimum height in pixels (0 if unconstrained)
     */
    using IWindowEngine::windowOpened; // Expose 2-arg convenience overload
    void windowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight) override;

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
     * @brief Notify the engine that a window was focused
     *
     * Called by Daemon when KWin reports window activation. Updates focus
     * tracking for tiling operations.
     *
     * @param windowId Window identifier from KWin
     * @param screenId Screen where the window is located
     */
    void windowFocused(const QString& windowId, const QString& screenId) override;

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
    void scheduleRetileForScreen(const QString& screenId);

    /**
     * @brief Helper to retile a screen after a window operation
     *
     * Recalculates layout and applies tiling if enabled, then emits tilingChanged.
     * Only emits signal if operationSucceeded is true.
     *
     * @param screenId Screen to retile
     * @param operationSucceeded Whether the triggering operation actually changed something
     */
    void retileAfterOperation(const QString& screenId, bool operationSucceeded);

Q_SIGNALS:
    /**
     * @brief Emitted when the enabled state changes
     * @param enabled New enabled state
     */
    void enabledChanged(bool enabled);

    /**
     * @brief Emitted when the set of autotile screens changes
     * @param screenIds List of screen IDs using autotile
     * @param isDesktopSwitch True if caused by desktop/activity switch (not user toggle)
     */
    void autotileScreensChanged(const QStringList& screenIds, bool isDesktopSwitch);

    /**
     * @brief Emitted when the algorithm changes
     * @param algorithmId New algorithm ID
     */
    void algorithmChanged(const QString& algorithmId);

    /**
     * @brief Emitted when tiling layout changes for a screen
     * @param screenId Screen that was retiled
     */
    void tilingChanged(const QString& screenId);

    /**
     * @brief Emitted when a window's floating state changes
     * @param windowId Window whose floating state changed
     * @param floating True if the window is now floating, false if tiled
     * @param screenId Screen where the window is (for OSD placement)
     */
    void windowFloatingChanged(const QString& windowId, bool floating, const QString& screenId);

    /**
     * @brief Emitted when overflow windows are batch-floated during applyTiling
     *
     * Replaces per-window windowFloatingChanged for overflow. The daemon handler
     * updates WTS state directly (no D-Bus signals) since the effect processes
     * float entries from the windowsTileRequested batch.
     *
     * @param windowIds Overflow window IDs that were just floated
     * @param screenId Screen where tiling occurred
     */
    void windowsBatchFloated(const QStringList& windowIds, const QString& screenId);

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
     * @param screenId Screen where operation occurred
     */
    void navigationFeedbackRequested(bool success, const QString& action, const QString& reason,
                                     const QString& sourceZoneId, const QString& targetZoneId, const QString& screenId);

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
    void onScreenGeometryChanged(const QString& screenId);
    void onLayoutChanged(Layout* layout);

private:
    void connectSignals();
    bool insertWindow(const QString& windowId, const QString& screenId);
    void removeWindow(const QString& windowId);
    bool storeWindowMinSize(const QString& windowId, int minWidth, int minHeight);
    void recalculateLayout(const QString& screenId);
    void applyTiling(const QString& screenId);
    bool shouldTileWindow(const QString& windowId) const;
    QString screenForWindow(const QString& windowId) const;
    QRect screenGeometry(const QString& screenId) const;

    /**
     * @brief Construct a TilingStateKey for the current desktop/activity
     *
     * Respects per-screen desktop overrides for screens where all autotiled
     * windows are sticky (on all desktops). This prevents desktop switches
     * from creating orphan TilingStates when the KWin script
     * "virtualdesktopsonlyonprimary" pins secondary-screen windows.
     */
    TilingStateKey currentKeyForScreen(const QString& screenId) const
    {
        auto it = m_screenDesktopOverride.constFind(screenId);
        int desktop = (it != m_screenDesktopOverride.constEnd()) ? it.value() : m_currentDesktop;
        return TilingStateKey{screenId, desktop, m_currentActivity};
    }

    /**
     * @brief Get TilingState for an explicit key (bypasses current desktop/activity)
     *
     * Creates the state if it doesn't exist. Used by loadState() to restore states
     * for arbitrary desktop/activity combinations without temporarily mutating
     * m_currentDesktop/m_currentActivity.
     */
    TilingState* stateForKey(const TilingStateKey& key);

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
     * Note: Iteration order over m_windowToStateKey (QHash) is non-deterministic.
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
     * @param screenId Screen to retile
     */
    void retileScreen(const QString& screenId);

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
     * @param screenId Screen whose pending order to check
     * @return true if the pending order was fully resolved and removed
     */
    bool cleanupPendingOrderIfResolved(const QString& screenId);

    /**
     * @brief Validate that a windowId is not empty, logging a warning if it is
     * @param windowId Window ID to validate
     * @param operation Operation name for the warning message
     * @return true if valid (non-empty), false if empty (logs warning)
     */
    bool warnIfEmptyWindowId(const QString& windowId, const char* operation) const;

    /**
     * @brief Sync shortcut-adjusted ratio/count to config and settings
     *
     * Called by NavigationController after increase/decreaseMasterRatio/Count.
     * Updates per-algorithm saved settings and writes to Settings (signal-blocked)
     * so that subsequent propagateGlobalSplitRatio() calls and settings syncs
     * don't overwrite the shortcut-adjusted values.
     */
    void syncShortcutAdjustmentToSettings();

    /**
     * @brief Shared toggle-float implementation for toggleFocusedWindowFloat/toggleWindowFloat
     *
     * Toggles the floating state, retiles, and emits windowFloatingChanged.
     */
    void performToggleFloat(TilingState* state, const QString& windowId, const QString& screenId);

    /**
     * @brief Get TilingState for a window by looking up its screen
     *
     * Consolidates the common pattern of m_windowToStateKey lookup + state resolution.
     *
     * @param windowId Window ID to look up
     * @param outScreenId If non-null, receives the screen ID
     * @return TilingState pointer or nullptr if window not tracked/screen invalid
     */
    TilingState* stateForWindow(const QString& windowId, QString* outScreenId = nullptr);

    LayoutManager* m_layoutManager = nullptr;
    WindowTrackingService* m_windowTracker = nullptr;
    ScreenManager* m_screenManager = nullptr;
    std::unique_ptr<AutotileConfig> m_config;
    std::unique_ptr<PerScreenConfigResolver> m_configResolver;
    std::unique_ptr<NavigationController> m_navigation;
    std::unique_ptr<SettingsBridge> m_settingsBridge;

    // Persistence delegates (KConfig stays in WTA layer)
    std::function<void()> m_persistSaveFn;
    std::function<void()> m_persistLoadFn;

    QSet<QString> m_autotileScreens;
    QString m_algorithmId;
    bool m_algorithmEverSet = false; ///< True after first successful setAlgorithm() call
    QString m_activeScreen; // Last-focused screen (updated by onWindowFocused)
    QHash<TilingStateKey, TilingState*> m_screenStates; // Owned via Qt parent (this)

    QHash<QString, TilingStateKey> m_windowToStateKey; // windowId -> owning state key
    QHash<QString, QSize> m_windowMinSizes; // windowId -> minimum size from KWin

    // Current desktop/activity context — used by stateForScreen() to construct
    // TilingStateKey. Updated by setCurrentDesktop()/setCurrentActivity() BEFORE
    // updateAutotileScreens() runs on desktop/activity switch.
    int m_currentDesktop = 1;
    QString m_currentActivity;
    bool m_isDesktopContextSwitch = false;

    // Per-screen desktop override for sticky screens. When the KWin script
    // "virtualdesktopsonlyonprimary" pins all secondary-screen windows to all
    // desktops, the TilingStateKey desktop dimension becomes meaningless for
    // those screens. This map pins such screens to their original desktop so
    // currentKeyForScreen() returns the key of the existing TilingState rather
    // than a new (empty) key after a desktop switch.
    QHash<QString, int> m_screenDesktopOverride;

    // Floating window IDs preserved across mode switches, per desktop/activity.
    // When autotile is deactivated, floated windows are saved here so that
    // re-enabling autotile restores them as floating regardless of screen.
    QHash<TilingStateKey, QSet<QString>> m_savedFloatingWindows;

    // Pre-seeded window order for snapping → autotile transitions.
    // Keyed by stable EDID-based screen ID (Utils::screenIdentifier).
    // Consumed by insertWindow() as windows arrive; also cleaned up by
    // removeWindow() if a pre-seeded window closes before arriving.
    QHash<QString, QStringList> m_pendingInitialOrders;
    QHash<QString, uint64_t> m_pendingOrderGeneration;

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
