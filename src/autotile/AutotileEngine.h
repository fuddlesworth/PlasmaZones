// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "core/constants.h"
#include "core/types.h"
#include <PhosphorEngineApi/IWindowTrackingService.h>
#include <PhosphorEngineApi/PlacementEngineBase.h>
#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <memory>
#include <optional>

#include "OverflowManager.h"
#include "core/utils.h"

#include <PhosphorScreens/ScreenIdentity.h>

namespace PhosphorZones {
class Layout;
class LayoutRegistry;
}

namespace PlasmaZones {

using NavigationContext = PhosphorEngineApi::NavigationContext;

// TilingStateKey is defined in core/types.h (shared between engine and daemon).

/**
 * @brief Saved position for a window removed from autotile, keyed by appId.
 *
 * When a window closes while autotiled, its position is captured so that
 * reopening the same app restores it to the same tiling position. Analogous
 * to snapping's PendingRestoreQueues but for autotile's order-based model.
 */
struct PendingAutotileRestore
{
    PendingAutotileRestore() = default;
    PendingAutotileRestore(int pos, TilingStateKey ctx, bool floating)
        : position(pos)
        , context(std::move(ctx))
        , wasFloating(floating)
    {
    }

    int position = -1; ///< Index in window order at time of removal
    TilingStateKey context; ///< Screen/desktop/activity where the window was tiled
    bool wasFloating = false; ///< Whether the window was floating when removed
};

/// Maximum pending restore entries per appId (prevents unbounded growth).
constexpr int MaxPendingRestoresPerApp = 16;

class AutotileConfig;

class NavigationController;
class PerScreenConfigResolver;
// Phosphor::Screens::ScreenManager moved to libs/phosphor-screens (Phosphor::Screens::ScreenManager).
} // namespace PlasmaZones
namespace Phosphor::Screens {
class ScreenManager;
}
namespace PlasmaZones {
class ISettings;
class Settings;
class SettingsBridge;
class WindowRegistry;
} // namespace PlasmaZones

namespace PhosphorTiles {
class ITileAlgorithmRegistry;
class TilingAlgorithm;
class TilingState;
}

namespace PlasmaZones {

/**
 * @brief Core engine for automatic window tiling.
 *
 * Coordinates per-screen PhosphorTiles::TilingState, invokes tiling algorithms (Master-Stack,
 * Columns, BSP), and applies calculated zone geometries to window positions.
 * Only tiles windows on screens where autotiling is enabled.
 *
 * @see PhosphorTiles::TilingAlgorithm, PhosphorTiles::TilingState, PhosphorTiles::AlgorithmRegistry
 */
class PLASMAZONES_EXPORT AutotileEngine : public PhosphorEngineApi::PlacementEngineBase
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ isEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString algorithm READ algorithm WRITE setAlgorithm NOTIFY algorithmChanged)

    friend class NavigationController;
    friend class PerScreenConfigResolver;
    friend class SettingsBridge;

public:
    explicit AutotileEngine(PhosphorZones::LayoutRegistry* layoutManager,
                            PhosphorEngineApi::IWindowTrackingService* windowTracker,
                            Phosphor::Screens::ScreenManager* screenManager,
                            PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry, QObject* parent = nullptr);
    ~AutotileEngine() override;

    /// The injected tile-algorithm registry. Borrowed — owner outlives the engine.
    PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry() const
    {
        return m_algorithmRegistry;
    }

    /**
     * @brief Wire up the shared WindowRegistry.
     *
     * Optional — tests construct the engine without a registry and fall back
     * to string parsing (PhosphorIdentity::WindowId::extractAppId). Production daemons set this to
     * the daemon-root registry so class lookups return the latest value after
     * an Electron/CEF app renames itself mid-session.
     *
     * Side effect: installs a live-class resolver on every PhosphorTiles::TilingAlgorithm in
     * the PhosphorTiles::AlgorithmRegistry so PhosphorTiles::ScriptedAlgorithm's lifecycle hooks see the
     * current appId on each tiled window. Future algorithm registrations
     * (hot-reloaded JS algorithms) pick up the resolver from the
     * algorithmRegistered signal bound inside this method.
     *
     * Must be set before start. Not owned.
     */
    void setWindowRegistry(WindowRegistry* registry);
    void setWindowRegistry(QObject* registry) override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-screen autotile state (derived from layout assignments)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if any screen has autotile enabled
     * @return true if at least one screen uses autotile
     */
    bool isEnabled() const noexcept override;

    /**
     * @brief Return the set of virtual-desktop numbers that currently have
     *        any tiling state.
     *
     * Used by the daemon's desktop-count-changed handler to find stale
     * desktops (states on desktop > newCount) so they can be pruned via
     * pruneStatesForDesktop(). Replaces an older screenStates() accessor
     * that returned a const-ref to a QHash<TilingStateKey, PhosphorTiles::TilingState*>
     * — that accessor leaked mutable PhosphorTiles::TilingState pointers via the
     * const-reference loophole (const on the hash doesn't propagate to the
     * pointed-to values), for a single caller that only needed desktop
     * numbers.
     *
     * Callers that need the raw state map should add a purpose-built
     * query method rather than iterating private state. The intent here
     * is that external consumers can't iterate or mutate PhosphorTiles::TilingState
     * objects through any public accessor — that's why screenStates()
     * is private.
     */
    QSet<int> desktopsWithActiveState() const override;

    /**
     * @brief Check if a specific screen uses autotile
     * @param screenId Screen to check
     * @return true if the screen has an autotile assignment
     */
    bool isAutotileScreen(const QString& screenId) const;

    /**
     * @brief Check if a window is currently tracked in any autotile state.
     *
     * Used by the drag protocol (beginDrag) to decide whether to apply an
     * immediate free-floating-size restore when a tiled window is picked up.
     * Reads m_windowToStateKey which is authoritative.
     */
    bool isWindowTracked(const QString& windowId) const override
    {
        return m_windowToStateKey.contains(windowId);
    }

    /**
     * @brief Check if a window is currently tiled (tracked AND not floating).
     *
     * Used by the drag protocol's Reorder mode to decide whether a dragged
     * window should enter the drag-insert preview (tiled windows reorder;
     * floating / untracked windows drag free and float on drop as before).
     */
    bool isWindowTiled(const QString& windowId) const override;

    // IPlacementEngine
    bool isActiveOnScreen(const QString& screenId) const override;

    // ═══════════════════════════════════════════════════════════════════════════
    // IPlacementEngine — generic screen/window management overrides
    //
    // Each override delegates to the concrete autotile method below it.
    // AutotileAdaptor continues to call the concrete methods directly.
    // ═══════════════════════════════════════════════════════════════════════════

    QSet<QString> activeScreens() const override
    {
        return autotileScreens();
    }
    void setActiveScreens(const QSet<QString>& screens) override
    {
        setAutotileScreens(screens);
    }
    QStringList managedWindowOrder(const QString& screenId) const override
    {
        return tiledWindowOrder(screenId);
    }
    bool isModeSpecificFloated(const QString& windowId) const override
    {
        return isAutotileFloated(windowId);
    }
    void clearModeSpecificFloatMarker(const QString& windowId) override
    {
        clearAutotileFloated(windowId);
    }
    bool isWindowManaged(const QString& windowId) const override
    {
        return isWindowTiled(windowId);
    }
    QString algorithmId() const override
    {
        return algorithm();
    }
    void markModeSpecificFloated(const QString& windowId) override
    {
        markAutotileFloated(windowId);
    }

    /**
     * @brief Get the set of screens currently using autotile
     * @return Set of screen names with autotile assignments
     */
    const QSet<QString>& autotileScreens() const
    {
        return m_autotileScreens;
    }

    /**
     * @brief Get the last-focused screen (updated by onWindowFocused)
     * @return Screen ID of the most recently focused screen, or empty string
     */
    QString activeScreen() const override
    {
        return m_activeScreen;
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
     * Swaps the active PhosphorTiles::TilingState set without releasing windows. Must be
     * called BEFORE updateAutotileScreens() on desktop switch so the engine
     * resolves states for the correct desktop.
     *
     * @param desktop Virtual desktop number (1-based from KWin)
     */
    void setCurrentDesktop(int desktop) override;

    /**
     * @brief Set the current activity for per-activity tiling state
     *
     * Swaps the active PhosphorTiles::TilingState set without releasing windows. Must be
     * called BEFORE updateAutotileScreens() on activity switch so the engine
     * resolves states for the correct activity.
     *
     * @param activity Activity ID (empty string for no activity)
     */
    void setCurrentActivity(const QString& activity) override;

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
    void updateStickyScreenPins(const std::function<bool(const QString&)>& isWindowSticky) override;

    /**
     * @brief Prune PhosphorTiles::TilingState and saved floating entries for a removed desktop
     *
     * Removes all states where key.desktop == removedDesktop. Called when a
     * virtual desktop is deleted so stale entries don't accumulate.
     */
    void pruneStatesForDesktop(int removedDesktop) override;

    /**
     * @brief Prune PhosphorTiles::TilingState entries for activities not in the given set
     *
     * Removes states whose activity is non-empty and not in validActivities.
     * Called when activities change so stale entries don't accumulate.
     */
    void pruneStatesForActivities(const QStringList& validActivities) override;

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
     * @param algorithmId Algorithm identifier from PhosphorTiles::AlgorithmRegistry
     */
    void setAlgorithm(const QString& algorithmId) override;

    /**
     * @brief Get the current algorithm instance
     * @return Pointer to algorithm, or nullptr if none set
     */
    PhosphorTiles::TilingAlgorithm* currentAlgorithm() const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Tiling state access
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get the tiling state for a specific screen
     *
     * Creates the state if it doesn't exist.
     *
     * @param screenId Screen identifier
     * @return Pointer to PhosphorTiles::TilingState (owned by engine)
     */
    PhosphorTiles::TilingState* tilingStateForScreen(const QString& screenId);

    PhosphorEngineApi::IPlacementState* stateForScreen(const QString& screenId) override;
    const PhosphorEngineApi::IPlacementState* stateForScreen(const QString& screenId) const override;

    /**
     * @brief Get the autotile configuration
     * @return Pointer to configuration
     */
    AutotileConfig* config() const noexcept;

    // ═══════════════════════════════════════════════════════════════════════════
    // Session Persistence
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Save tiling state via persistence delegate (IPlacementEngine contract)
     *
     * Delegates to the save function set by setPersistenceDelegate().
     * Wired by the daemon to WTA's saveState(), which triggers a full WTA save
     * including both snap and autotile state. Autotile window orders are embedded
     * in WTA's save cycle via setTilingStateDelegates — this method exists to
     * satisfy the IPlacementEngine interface. For autotile-only persistence,
     * the placementChanged signal → WTA::scheduleSaveState() connection is the
     * primary path.
     */
    void saveState() override;

    /**
     * @brief Load tiling state via persistence delegate (IPlacementEngine contract)
     *
     * Delegates to the load function set by setPersistenceDelegate().
     * Wired by the daemon to WTA's loadState(), which triggers a full WTA load
     * including autotile window order restoration via setTilingStateDelegates.
     */
    void loadState() override;

    /**
     * @brief Set persistence callbacks for save/load
     *
     * KConfig persistence is owned by WindowTrackingAdaptor (engine is KConfig-free).
     * These callbacks allow AutotileEngine to fulfill the IPlacementEngine persistence
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
     * @brief Set callback to query daemon-side window floating state
     *
     * Used by toggleWindowFloat to adopt untracked floating windows into autotile.
     */
    void setIsWindowFloatingFn(std::function<bool(const QString&)> fn) override
    {
        m_isWindowFloatingFn = std::move(fn);
    }

    /**
     * @brief Adopt an untracked window as floating on an autotile screen
     *
     * Used when a window is dragged from a snap screen to an autotile screen.
     * Adds the window to the PhosphorTiles::TilingState as floating so subsequent
     * toggleWindowFloat/setWindowFloat calls can find and manage it.
     * No-op if the window is already tracked or the screen isn't autotile.
     */
    void adoptWindowAsFloating(const QString& windowId, const QString& screenId) override;

    /**
     * @brief Serialize per-context autotile window orders to JSON
     *
     * Forwarded to SettingsBridge. Called by WTA's save cycle via persistence delegate.
     * masterCount/splitRatio are NOT included — Settings owns those.
     */
    QJsonArray serializeWindowOrders() const override;

    /**
     * @brief Deserialize per-context autotile window orders from JSON
     *
     * Forwarded to SettingsBridge. Restores window order and floating state.
     *
     * @param orders JSON array produced by serializeWindowOrders()
     */
    void deserializeWindowOrders(const QJsonArray& orders) override;

    /**
     * @brief Serialize pending autotile restore queues to JSON
     *
     * Forwarded to SettingsBridge. Returns appId-keyed pending restore entries.
     */
    QJsonObject serializePendingRestores() const override;

    /**
     * @brief Deserialize pending autotile restore queues from JSON
     *
     * Forwarded to SettingsBridge. Restores close/reopen queue.
     */
    void deserializePendingRestores(const QJsonObject& obj) override;

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
    void syncFromSettings(QObject* settings) override;

    // Per-screen config — forwarded to PerScreenConfigResolver (IPlacementEngine overrides)
    void applyPerScreenConfig(const QString& screenId, const QVariantMap& overrides) override;
    void clearPerScreenConfig(const QString& screenId) override;
    QVariantMap perScreenOverrides(const QString& screenId) const override;
    bool hasPerScreenOverride(const QString& screenId, const QString& key) const;
    void updatePerScreenOverride(const QString& screenId, const QString& key, const QVariant& value);

    // Effective per-screen values — forwarded to PerScreenConfigResolver
    int effectiveInnerGap(const QString& screenId) const;
    int effectiveOuterGap(const QString& screenId) const;
    ::PhosphorLayout::EdgeGaps effectiveOuterGaps(const QString& screenId) const;
    bool effectiveSmartGaps(const QString& screenId) const;
    bool effectiveRespectMinimumSize(const QString& screenId) const;
    int effectiveMaxWindows(const QString& screenId) const;
    qreal effectiveSplitRatioStep(const QString& screenId) const override;
    int runtimeMaxWindows() const override;
    QString effectiveAlgorithmId(const QString& screenId) const;
    PhosphorTiles::TilingAlgorithm* effectiveAlgorithm(const QString& screenId) const;

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
    void connectToSettings(QObject* settings) override;

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
    Q_INVOKABLE void retile(const QString& screenId = QString()) override;

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
    Q_INVOKABLE void swapFocusedWithMaster() override;

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
    Q_INVOKABLE void focusMaster() override;

    /**
     * @brief Notify the engine that a window has been focused
     *
     * Called by D-Bus adaptor when KWin reports a window focus change.
     * Updates the focused window in the appropriate PhosphorTiles::TilingState.
     *
     * @param windowId Window ID that gained focus
     */
    void setFocusedWindow(const QString& windowId);

    /**
     * @brief Set the active screen hint for keyboard shortcut handlers.
     *
     * Called before parameterless engine methods (focusMaster, increaseMasterCount, etc.)
     * to ensure the engine operates on the correct virtual screen when no focused window
     * has been tracked yet on that screen.
     *
     * @param screenId Resolved screen ID (virtual or physical)
     */
    void setActiveScreenHint(const QString& screenId) override;

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
    Q_INVOKABLE void increaseMasterRatio(qreal delta = 0.05) override;

    /**
     * @brief Decrease the master area ratio
     *
     * Makes the master area smaller by the specified delta.
     *
     * @param delta Amount to decrease (default 0.05 = 5%)
     */
    Q_INVOKABLE void decreaseMasterRatio(qreal delta = 0.05) override;

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
    Q_INVOKABLE void increaseMasterCount() override;

    /**
     * @brief Decrease the number of master windows
     */
    Q_INVOKABLE void decreaseMasterCount() override;

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
    Q_INVOKABLE void focusInDirection(const QString& direction, const QString& action = QStringLiteral("focus"));

    /**
     * @brief Move the focused window to a specific position in the tiling order
     *
     * Maps "snap to zone N" shortcuts to autotile positions.
     *
     * @param position Target position (1-based, clamped to valid range)
     */
    Q_INVOKABLE void moveFocusedToPosition(int position);

    // ═══════════════════════════════════════════════════════════════════════════
    // IPlacementEngine — navigation overrides
    //
    // Each override absorbs what AutotileNavigationAdapter did: translate
    // the user-intent-shaped IPlacementEngine call into the existing
    // concrete AutotileEngine method with the right parameters.
    // ═══════════════════════════════════════════════════════════════════════════

    void focusInDirection(const QString& direction, const NavigationContext& ctx) override;
    void moveFocusedInDirection(const QString& direction, const NavigationContext& ctx) override;
    void swapFocusedInDirection(const QString& direction, const NavigationContext& ctx) override;
    void moveFocusedToPosition(int position, const NavigationContext& ctx) override;
    void rotateWindows(bool clockwise, const NavigationContext& ctx) override;
    void reapplyLayout(const NavigationContext& ctx) override;
    void snapAllWindows(const NavigationContext& ctx) override;
    void toggleFocusedFloat(const NavigationContext& ctx) override;
    void cycleFocus(bool forward, const NavigationContext& ctx) override;
    void pushToEmptyZone(const NavigationContext& ctx) override;
    void restoreFocusedWindow(const NavigationContext& ctx) override;

    // Autotile-specific navigation. Callable directly on the concrete
    // AutotileEngine pointer from internal callers.
    void swapInDirection(const QString& direction, const QString& action);
    void rotateWindows(bool clockwise, const QString& screenId);
    void moveToPosition(const QString& windowId, int position, const QString& screenId);

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
    // PhosphorZones::Zone-ordered window transitions (snapping ↔ autotile)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Pre-seed initial window order for deterministic snapping → autotile transitions
     *
     * When toggling from manual snapping to autotile, windows arrive in KWin stacking
     * order (focus-based). This method stores a zone-ordered list so that insertWindow()
     * can place windows in the correct autotile positions (zone 1 → master, etc.).
     *
     * Only takes effect when the screen's PhosphorTiles::TilingState is empty (no prior windows from
     * session restore). The pending order is consumed as windows are inserted.
     *
     * @param screenId Screen to set initial order for
     * @param windowIds Window IDs in desired order (zone-number ascending)
     */
    void setInitialWindowOrder(const QString& screenId, const QStringList& windowIds) override;

    /**
     * @brief Clear saved floating state for windows that are actively zone-snapped.
     *
     * Called during snapping → autotile transitions so that windows the user
     * re-snapped in manual mode aren't incorrectly restored as floating.
     *
     * @param windowIds Windows to remove from the saved floating set
     */
    void clearSavedFloatingForWindows(const QStringList& windowIds) override;

    /**
     * @brief Clear ALL saved floating state (used when autotile is disabled globally)
     *
     * Prevents stale entries from incorrectly floating windows on next activation.
     */
    void clearAllSavedFloating() override;

    /**
     * @brief Get the current tiled window order for a screen
     *
     * Returns the autotile engine's tiled window list for deterministic
     * autotile → snapping transitions. Call BEFORE switching layouts so
     * the PhosphorTiles::TilingState still exists.
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
    using IPlacementEngine::windowOpened;
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
    void scheduleRetileForScreen(const QString& screenId) override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Drag-insert preview (trigger-held window drag reorders autotile stack)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Begin a drag-insert preview for a window already tiled on a screen.
     *
     * Captures the window's current position so it can be restored on cancel.
     * While a preview is active, applyTiling() skips emitting geometry for the
     * dragged window — KWin's interactive move remains in control — but still
     * animates the other windows shifting to leave a gap at the preview index.
     *
     * @return true if the window is tiled on the screen and preview was started.
     */
    bool beginDragInsertPreview(const QString& windowId, const QString& screenId) override;

    /**
     * @brief Update the target insert index for the active drag preview.
     *
     * Moves the dragged window within PhosphorTiles::TilingState to the new index (clamped
     * to [0, tiledWindowCount()-1]) and retiles. No-op if the index hasn't
     * changed from the last update.
     */
    void updateDragInsertPreview(int insertIndex) override;

    /**
     * @brief Commit the active drag-insert preview.
     *
     * Clears preview state and retiles normally so the dragged window's
     * geometry is applied (KWin is finishing the interactive move and will
     * accept the geometry set).
     */
    void commitDragInsertPreview() override;

    /**
     * @brief Cancel the active drag-insert preview, restoring the original order.
     */
    void cancelDragInsertPreview() override;

    /**
     * @brief Compute the insert index for a cursor position on an autotile screen.
     *
     * Walks the screen's current calculated zones and returns the index of the
     * first zone that contains the cursor. Returns the last tiled index as a
     * fallback when the cursor is beyond all zones, or -1 if the screen has no
     * tiling state. updateDragInsertPreview() clamps to [0, tiledWindowCount()-1],
     * so returning the last slot is equivalent to "append".
     *
     * The dragged window's zone is intentionally NOT excluded from the hit
     * test: cursor-over-own-zone returns its current index (stable identity),
     * preventing an oscillating shuffle where moving to a neighbour slot
     * immediately re-matches under the cursor every dragMoved tick.
     */
    int computeDragInsertIndexAtPoint(const QString& screenId, const QPoint& cursorPos) const override;

    /**
     * @brief Query whether a drag-insert preview is currently active.
     */
    bool hasDragInsertPreview() const override
    {
        return m_dragInsertPreview.has_value();
    }

    /**
     * @brief Get the window ID of the active drag-insert preview, or empty.
     */
    QString dragInsertPreviewWindowId() const
    {
        return m_dragInsertPreview ? m_dragInsertPreview->windowId : QString();
    }

    /**
     * @brief Get the target screen ID of the active drag-insert preview, or empty.
     */
    QString dragInsertPreviewScreenId() const override
    {
        return m_dragInsertPreview ? m_dragInsertPreview->targetScreenId : QString();
    }

    /**
     * @brief Helper to retile a screen after a window operation
     *
     * Recalculates layout and applies tiling if enabled, then emits placementChanged.
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

    // algorithmChanged(const QString&) — inherited from PlacementEngineBase.
    // placementChanged(const QString&) — inherited from PlacementEngineBase.
    //   Replaces the former tilingChanged signal; all internal emitters now
    //   emit placementChanged, and callers connect to the base-class signal.
    // windowsReleased(const QStringList&, const QSet<QString>&) — inherited
    //   from PlacementEngineBase. Replaces windowsReleasedFromTiling.

    /**
     * @brief Emitted when a window's floating state changes due to a user action
     *
     * User-intent semantics: the downstream handler restores pre-tile geometry,
     * shows the navigation OSD, etc. Emitted from performToggleFloat and
     * setWindowFloat (explicit user/caller toggles).
     *
     * windowFloatingChanged, activateWindowRequested, and navigationFeedback
     * are inherited from PlacementEngineBase.
     */

    // windowFloatingStateSynced and windowsBatchFloated are inherited from
    // PlacementEngineBase. Autotile-specific documentation: windowFloatingStateSynced
    // is emitted when the engine's TilingState::isFloating diverges from WTS's view
    // (e.g. a newly-inserted window carries stale snap-mode float state). The
    // downstream handler updates WTS bookkeeping without geometry restore.
    // windowsBatchFloated is emitted when overflow windows are batch-floated
    // during applyTiling; the daemon handler updates WTS state directly.

    /**
     * @brief Emitted when windows are tiled to new geometries (batch)
     *
     * JSON array of objects with windowId, x, y, width, height.
     * The adaptor forwards to KWin effect.
     *
     * @param tileRequestsJson JSON array of {windowId,x,y,width,height}
     */
    void windowsTiled(const QString& tileRequestsJson);

public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Autotile-float origin tracking (ephemeral, not persisted)
    // ═══════════════════════════════════════════════════════════════════════════

    void markAutotileFloated(const QString& windowId);
    void clearAutotileFloated(const QString& windowId);
    bool isAutotileFloated(const QString& windowId) const;

    int pruneStaleWindows(const QSet<QString>& aliveWindowIds) override;

private Q_SLOTS:
    void onWindowZoneChanged(const QString& windowId, const QString& zoneId);
    void onWindowAdded(const QString& windowId);
    void onWindowRemoved(const QString& windowId);
    void onWindowFocused(const QString& windowId);
    void onScreenGeometryChanged(const QString& screenId);
    void onLayoutChanged(PhosphorZones::Layout* layout);

protected:
    void onWindowClaimed(const QString& windowId) override;
    void onWindowReleased(const QString& windowId) override;
    void onWindowFloated(const QString& windowId) override;
    void onWindowUnfloated(const QString& windowId) override;

private:
    void connectSignals();
    bool insertWindow(const QString& windowId, const QString& screenId);
    void removeWindow(const QString& windowId);
    void removeSavedFloatingEntry(const QString& windowId);
    void pruneStaleRestores(const QString& appId);
    bool storeWindowMinSize(const QString& windowId, int minWidth, int minHeight);
    bool recalculateLayout(const QString& screenId);
    void applyTiling(const QString& screenId);
    bool shouldTileWindow(const QString& windowId) const;
    QString screenForWindow(const QString& windowId) const;
    QRect screenGeometry(const QString& screenId) const;

    /// Check if a screen ID refers to a known (connected) screen.
    /// Virtual screen IDs are validated via Phosphor::Screens::ScreenManager geometry;
    /// physical IDs via QScreen lookup.
    bool isKnownScreen(const QString& screenId) const;

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
     * @brief Get PhosphorTiles::TilingState for an explicit key (bypasses current desktop/activity)
     *
     * Creates the state if it doesn't exist. Used by loadState() to restore states
     * for arbitrary desktop/activity combinations without temporarily mutating
     * m_currentDesktop/m_currentActivity.
     */
    PhosphorTiles::TilingState* stateForKey(const TilingStateKey& key);

    /**
     * @brief Reset maxWindows when switching algorithms (DRY helper)
     *
     * If the current maxWindows matches the old algorithm's default, reset
     * it to the new algorithm's default. Shared by setAlgorithm() and
     * syncFromSettings().
     */
    void resetMaxWindowsForAlgorithmSwitch(PhosphorTiles::TilingAlgorithm* oldAlgo,
                                           PhosphorTiles::TilingAlgorithm* newAlgo);

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
     * Encapsulates the four-step retile sequence: pre-validate screen geometry,
     * overflow recovery, recalculate layout, apply tiling, emit placementChanged.
     *
     * If screen geometry is transiently unavailable (e.g. during a virtual
     * desktop switch on Wayland), schedules a bounded retry instead of
     * silently dropping the retile.
     *
     * @param screenId Screen to retile
     */
    void retileScreen(const QString& screenId);

    /**
     * @brief Schedule a bounded retry for a screen whose geometry was transiently invalid
     *
     * Retries up to MaxRetileRetries times per screen, with RetileRetryIntervalMs
     * between attempts. The retry counter is cleared on successful retile or when
     * the screen is removed from autotile.
     *
     * @param screenId Screen to retry
     */
    void scheduleRetileRetry(const QString& screenId);

    /**
     * @brief Process all pending retile retries (fires via m_retileRetryTimer)
     */
    void processRetileRetries();

    // ═══════════════════════════════════════════════════════════════════════════════
    // Helper Methods
    // ═══════════════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if all pending initial-order windows are resolved for a screen
     *
     * Returns true if every window in the pending order for the given screen
     * is already present in the screen's PhosphorTiles::TilingState. If all resolved,
     * removes the pending order entry. Used by insertWindow() and removeWindow().
     *
     * @param screenId Screen whose pending order to check
     * @return true if the pending order was fully resolved and removed
     */
    bool cleanupPendingOrderIfResolved(const QString& screenId);

    /**
     * @brief Schedule promotion of saved window orders for the current context
     *
     * Coalesced via zero-delay timer so that simultaneous desktop+activity switches
     * (both calling this method) result in a single promotion after both
     * m_currentDesktop and m_currentActivity are set to their final values.
     */
    void schedulePromoteSavedWindowOrders();

    /**
     * @brief Promote saved window orders for the current context into pending orders
     *
     * Moves orders from m_savedWindowOrders (populated by deserializeWindowOrders
     * for all contexts) into m_pendingInitialOrders so windows arriving on the
     * new desktop get their saved ordering.
     */
    void promoteSavedWindowOrders();

    /**
     * @brief Validate that a windowId is not empty, logging a warning if it is
     * @param windowId Window ID to validate
     * @param operation Operation name for the warning message
     * @return true if valid (non-empty), false if empty (logs warning)
     */
    bool warnIfEmptyWindowId(const QString& windowId, const char* operation) const;

    /**
     * @brief Normalize a window id received from D-Bus to the canonical key
     *        used by internal storage (m_windowToStateKey, PhosphorTiles::TilingState::m_windowOrder, …).
     *
     * Why this exists: KWin apps like Emby (CEF/Electron) mutate their
     * resourceClass / desktopFileName after the surface is already mapped, so
     * successive calls arrive with different "appId|uuid" composites for the
     * same underlying window. The uuid portion is stable — the canonical form
     * is the FIRST composite ever seen for a given uuid, and every subsequent
     * mention of the same window is translated back to that canonical string
     * so map lookups don't miss.
     *
     * Populates m_canonicalByInstance on first observation. Stale entries are
     * cleared by cleanupCanonical() when the window closes.
     */
    QString canonicalizeWindowId(const QString& rawWindowId);

    /**
     * @brief Release the canonical translation for a window that's going away.
     *
     * Called from removeWindow / windowClosed paths. Safe to call with an id
     * that's already canonical or unknown.
     */
    void cleanupCanonical(const QString& anyWindowId);

    /**
     * @brief Const-safe translation for read-only methods.
     *
     * Same as canonicalizeWindowId(), but does not mutate m_canonicalByInstance:
     * unknown windows return their raw input. Use in shouldTileWindow(),
     * screenForWindow(), and other const methods.
     */
    QString canonicalizeForLookup(const QString& rawWindowId) const;

    /**
     * @brief Return the CURRENT app class for a window, not the first-seen one.
     *
     * Prefers m_windowRegistry (populated live by the kwin-effect bridge) so
     * that apps which mutate their appId mid-session (Electron/CEF — Emby)
     * hit the most recent value. Falls back to parsing the composite form
     * when no registry is attached (unit tests, legacy callers).
     *
     * Use this anywhere you'd otherwise call PhosphorIdentity::WindowId::extractAppId(windowId)
     * on the canonical form — the canonical string is frozen at first
     * observation, so parsing it returns a stale class after mutation.
     */
    QString currentAppIdFor(const QString& anyWindowId) const;

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
    void performToggleFloat(PhosphorTiles::TilingState* state, const QString& windowId, const QString& screenId);

    /**
     * @brief Get PhosphorTiles::TilingState for a window by looking up its screen
     *
     * Consolidates the common pattern of m_windowToStateKey lookup + state resolution.
     *
     * @param windowId Window ID to look up
     * @param outScreenId If non-null, receives the screen ID
     * @return PhosphorTiles::TilingState pointer or nullptr if window not tracked/screen invalid
     */
    PhosphorTiles::TilingState* stateForWindow(const QString& windowId, QString* outScreenId = nullptr);

    QSet<QString> m_autotileFloatedWindows;

    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    PhosphorEngineApi::IWindowTrackingService* m_windowTracker = nullptr;
    Phosphor::Screens::ScreenManager* m_screenManager = nullptr;
    WindowRegistry* m_windowRegistry = nullptr; ///< Shared registry for class lookups; not owned
    PhosphorTiles::ITileAlgorithmRegistry* m_algorithmRegistry = nullptr; ///< Borrowed; outlives engine
    std::unique_ptr<AutotileConfig> m_config;
    std::unique_ptr<PerScreenConfigResolver> m_configResolver;
    std::unique_ptr<NavigationController> m_navigation;
    std::unique_ptr<SettingsBridge> m_settingsBridge;

    // Persistence delegates (KConfig stays in WTA layer)
    std::function<void()> m_persistSaveFn;
    std::function<void()> m_persistLoadFn;
    std::function<bool(const QString&)> m_isWindowFloatingFn;

    QSet<QString> m_autotileScreens;
    QString m_algorithmId;
    bool m_algorithmEverSet = false; ///< True after first successful setAlgorithm() call
    QString m_activeScreen; // Last-focused screen (updated by onWindowFocused)
    QHash<TilingStateKey, PhosphorTiles::TilingState*> m_screenStates; // Owned via Qt parent (this)

    QHash<QString, TilingStateKey> m_windowToStateKey; // windowId -> owning state key
    QHash<QString, QSize> m_windowMinSizes; // windowId -> minimum size from KWin

    // Instance id → first-seen canonical windowId.
    //
    // Fallback for when no shared WindowRegistry is attached (unit tests).
    // With a registry, canonicalization delegates to it instead. Production
    // daemons always take the registry path, so this map stays empty.
    //
    // The canonical form is the FIRST windowId string we saw for a given
    // instance id. Subsequent arrivals with a mutated appId (Electron/CEF
    // apps that swap WM_CLASS mid-session) resolve back to that canonical
    // form so every map/PhosphorTiles::TilingState key in the engine stays consistent.
    QHash<QString, QString> m_canonicalByInstance;

    // Current desktop/activity context — used by tilingStateForScreen() to construct
    // TilingStateKey. Updated by setCurrentDesktop()/setCurrentActivity() BEFORE
    // updateAutotileScreens() runs on desktop/activity switch.
    int m_currentDesktop = 1;
    QString m_currentActivity;
    bool m_isDesktopContextSwitch = false;

    // Per-screen desktop override for sticky screens. When the KWin script
    // "virtualdesktopsonlyonprimary" pins all secondary-screen windows to all
    // desktops, the TilingStateKey desktop dimension becomes meaningless for
    // those screens. This map pins such screens to their original desktop so
    // currentKeyForScreen() returns the key of the existing PhosphorTiles::TilingState rather
    // than a new (empty) key after a desktop switch.
    QHash<QString, int> m_screenDesktopOverride;

    // Floating window IDs preserved across mode switches, per desktop/activity.
    // When autotile is deactivated, floated windows are saved here so that
    // re-enabling autotile restores them as floating regardless of screen.
    QHash<TilingStateKey, QSet<QString>> m_savedFloatingWindows;

    // Pre-seeded window order for snapping → autotile transitions.
    // Keyed by stable EDID-based screen ID (Phosphor::Screens::ScreenIdentity::identifierFor).
    // Consumed by insertWindow() as windows arrive; also cleaned up by
    // removeWindow() if a pre-seeded window closes before arriving.
    QHash<QString, QStringList> m_pendingInitialOrders;
    QHash<QString, uint64_t> m_pendingOrderGeneration;

    // Saved window orders from session persistence, keyed by full context.
    // On desktop/activity switch, orders for the new context are promoted into
    // m_pendingInitialOrders so windows arriving on the new desktop get their
    // saved ordering. Consumed once per context (removed after promotion).
    QHash<TilingStateKey, QStringList> m_savedWindowOrders;

    // Pending restore queue for windows removed from autotile (close/reopen).
    // Keyed by appId (stable across KWin restarts). Multiple entries per appId
    // support multi-instance apps; consumed FIFO by insertWindow().
    // Analogous to snapping's PendingRestoreQueues.
    QHash<QString, QList<PendingAutotileRestore>> m_pendingAutotileRestores;

    // Zero-delay timer to coalesce promoteSavedWindowOrders() calls during
    // simultaneous desktop+activity switches. Without coalescing, the first
    // call (after setCurrentDesktop) would promote using the stale activity,
    // potentially consuming the wrong specific-activity entry.
    QTimer m_promoteOrdersTimer;

    // Per-screen overflow tracking with O(1) reverse-index lookups.
    OverflowManager m_overflow;

    bool m_retiling = false;

    // Queued-connection retile coalescing: windowOpened D-Bus calls arriving in
    // the same event loop pass are coalesced into a single retile per screen.
    // Uses QMetaObject::invokeMethod(Qt::QueuedConnection) which fires after
    // all currently-pending events are processed — no fixed delay needed.
    QSet<QString> m_pendingRetileScreens;
    bool m_retilePending = false;

    // Bounded retry for transient screen geometry failures.
    // When QScreen is temporarily unavailable (e.g. during Wayland desktop switch),
    // recalculateLayout cannot compute zone geometry. Rather than silently dropping
    // the retile (leaving stale zones), we retry after a short interval.
    // Per-screen retry counts prevent infinite loops; cleared on success or screen removal.
    static constexpr int MaxRetileRetries = 3;
    static constexpr int RetileRetryIntervalMs = 150;
    QTimer m_retileRetryTimer;
    QSet<QString> m_retileRetryScreens;
    QHash<QString, int> m_retileRetryCount;

    // Deferred focus: set by onWindowAdded, emitted after applyTiling so the
    // focus request arrives at KWin AFTER windowsTiled (whose onComplete raises
    // windows in tiling order). Without this, the raise loop buries the new window.
    QString m_pendingFocusWindowId;

    // Drag-insert preview state. When set, applyTiling() filters the dragged
    // window out of the windowsTiled batch so the KWin interactive move isn't
    // fought, while the remaining windows animate into their new positions.
    // Supports three entry modes (captured at begin() time for cancel restoration):
    //   - Same-screen reorder: window was already tiled/floating on targetScreenId
    //   - Cross-screen adoption: window was tracked on a different autotile screen
    //   - Fresh adoption: window was not tracked by the engine at all
    struct DragInsertPreview
    {
        QString windowId;
        QString targetScreenId;
        int lastInsertIndex = -1;

        // Prior-state restoration info (used on cancel)
        bool hadPriorState = false; // True if m_windowToStateKey contained windowId at begin
        TilingStateKey priorKey; // Key of the prior PhosphorTiles::TilingState (meaningful iff hadPriorState)
        int priorRawIndex = -1; // Raw index in priorState->windowOrder() at begin
        bool priorFloating = false; // Prior floating flag in priorState
        bool priorSameScreen = false; // priorKey == currentKeyForScreen(targetScreenId)

        // Eviction info (used when the target stack is already at maxWindows
        // and the dragged window is a new member). The last tiled neighbour
        // is floated to make room; on cancel the eviction is undone, on
        // commit the evicted window is sent through the batch-float path so
        // its pre-tile geometry is restored.
        QString evictedWindowId;
    };
    std::optional<DragInsertPreview> m_dragInsertPreview;

    /**
     * @brief Process all pending retiles (fires via QueuedConnection)
     *
     * Retiles all screens that had windows added or were newly activated
     * since the last event loop pass.
     */
    void processPendingRetiles();
};

} // namespace PlasmaZones
