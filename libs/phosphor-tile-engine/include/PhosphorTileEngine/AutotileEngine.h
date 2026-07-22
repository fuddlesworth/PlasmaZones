// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortileengine_export.h>
#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorLayoutApi/EdgeGaps.h>
#include <PhosphorEngine/IWindowRegistry.h>
#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/PerScreenStates.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/ScreenContextTracker.h>
#include <PhosphorTileEngine/IAutotileSettings.h>
#include <PhosphorTiles/TilingState.h>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include <PhosphorTileEngine/AutotileEngineTypes.h>
#include <PhosphorTileEngine/OverflowManager.h>

namespace PhosphorZones {
class Layout;
class LayoutRegistry;
}

namespace PhosphorTileEngine {

class AutotileConfig;

class NavigationController;
class PerScreenConfigResolver;
} // namespace PhosphorTileEngine

// ScreenManager lives in libs/phosphor-screens; forward-declared here.
namespace PhosphorScreens {
class ScreenManager;
}

namespace PhosphorTiles {
class ITileAlgorithmRegistry;
class TilingAlgorithm;
class TilingState;
}

namespace PhosphorTileEngine {

/**
 * @brief Core engine for automatic window tiling.
 *
 * Coordinates per-screen PhosphorTiles::TilingState, invokes the active tiling
 * algorithm (a Luau script, via the PhosphorTiles::TilingAlgorithm interface),
 * and applies calculated zone geometries to window positions.
 * Only tiles windows on screens where autotiling is enabled.
 *
 * @see PhosphorTiles::TilingAlgorithm, PhosphorTiles::TilingState, PhosphorTiles::AlgorithmRegistry
 */
class PHOSPHORTILEENGINE_EXPORT AutotileEngine : public PhosphorEngine::PlacementEngineBase
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ isEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString algorithm READ algorithm WRITE setAlgorithm NOTIFY algorithmChanged)

    friend class NavigationController;
    friend class PerScreenConfigResolver;

public:
    explicit AutotileEngine(PhosphorZones::LayoutRegistry* layoutManager,
                            PhosphorEngine::IWindowTrackingService* windowTracker,
                            PhosphorScreens::ScreenManager* screenManager,
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
     * the PhosphorTiles::AlgorithmRegistry so PhosphorTiles::LuauTileAlgorithm's lifecycle hooks see the
     * current appId on each tiled window. Future algorithm registrations
     * (hot-reloaded Luau algorithms) pick up the resolver from the
     * algorithmRegistered signal bound inside this method.
     *
     * Must be set before start. Not owned.
     */
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
     * pruneStatesForDesktop(). The m_states map itself stays private; per-screen
     * lookup is available through tilingStateForScreen(screenId). (Design notes
     * on why the older screenStates() accessor was removed live above the
     * definition in AutotileEngine.cpp.)
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
     * Reads the reverse window→key map which is authoritative.
     */
    bool isWindowTracked(const QString& windowId) const override
    {
        return m_states.hasWindow(windowId);
    }

    /**
     * @brief Check if a window is currently tiled (tracked AND not floating).
     *
     * Used by the drag protocol's Reorder mode to decide whether a dragged
     * window should enter the drag-insert preview (tiled windows reorder;
     * floating / untracked windows drag free and float on drop as before).
     */
    bool isWindowTiled(const QString& rawWindowId) const override;

    /**
     * @brief Authoritative per-window autotile float state.
     *
     * Returns true iff the window is tracked by autotile AND its owning
     * TilingState marks it floating. This is the autotile engine's half of the
     * per-engine float contract: the daemon's WTS float resolver consults this
     * for windows whose current screen mode is Autotile (the snap half is
     * SnapState::isFloating). Distinct from isModeSpecificFloated(), which
     * reports the mode-transition MARKER (autotile-originated float), not the
     * live TilingState float.
     */
    bool isWindowFloatingInAutotile(const QString& windowId) const;

    /**
     * @brief All windows currently floating in autotile across every tracked state.
     *
     * Used by the daemon's WTS floating-windows aggregator so the engine-agnostic
     * floatingWindows() enumeration (effect float-cache seed, getAllWindowStates)
     * still sees autotile floats now that they live in TilingState rather than the
     * old shared WTS set.
     */
    QStringList allFloatingWindows() const;

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
     * Identical-set re-emit (discussion #219): when called for a
     * desktop/activity switch whose autotile set matches the previous
     * context's, the set comparison early-returns but the engine still
     * RE-EMITS autotileScreensChanged with the unchanged set and
     * isDesktopSwitch=true — the compositor effect's catch-scan depends on
     * that wakeup to re-add windows moved to this desktop while the user was
     * away. An empty identical set skips the re-emit (nothing to catch).
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
     * @brief Set a single screen's current virtual desktop (Plasma 6.7 per-output
     *        virtual desktops, #648)
     *
     * Pure context swap: records the screen's desktop in m_context's per-output map so
     * currentKeyForScreen() resolves that screen's per-(screen, desktop) state. Does
     * NOT migrate windows between states — the other desktop's state stays put so it
     * reappears when the screen returns. Like setCurrentDesktop(), call BEFORE
     * updateAutotileScreens() so the new key resolves.
     */
    void setCurrentDesktopForScreen(const QString& screenId, int desktop) override;

    /// Drop a screen's per-output desktop, reverting it to m_context's global desktop.
    void clearCurrentDesktopForScreen(const QString& screenId) override;

    /// Inject the cross-surface resolver (neighbouring output / desktop lookup)
    /// used by directional navigation when it reaches a layout boundary.
    void setCrossSurfaceResolver(PhosphorEngine::ICrossSurfaceResolver* resolver) override
    {
        m_crossSurfaceResolver = resolver;
    }

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
     * with state migrated to m_context's global desktop if necessary.
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
        return m_context.currentDesktop();
    }

    /**
     * @brief Get the current activity tracked by the engine
     */
    const QString& currentActivity() const noexcept
    {
        return m_context.currentActivity();
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

    PhosphorEngine::IPlacementState* stateForScreen(const QString& screenId) override;
    const PhosphorEngine::IPlacementState* stateForScreen(const QString& screenId) const override;

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
     * Wired by the daemon to WTA's saveState(). Per-window autotile restore state
     * is persisted by the common WindowPlacementStore (each window's tiled position
     * is captured into the store); the placementChanged → WTA::scheduleSaveState()
     * connection is what triggers that save.
     */
    void saveState() override;

    /**
     * @brief Load tiling state via persistence delegate (IPlacementEngine contract)
     *
     * Delegates to the load function set by setPersistenceDelegate().
     * Wired by the daemon to WTA's loadState(). Autotiled windows reopen at their
     * saved position from the unified WindowPlacementStore (insertWindow consumes
     * the record), not from a separate window-order key.
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
     * @brief Predicate consulted on reopen to decide whether a FLOATED (untiled)
     *        window should have its previous global position re-applied.
     *
     * Mirrors SnapEngine::RestorePositionPredicate: the daemon resolves the global
     * `autotileRestoreFloatedWindowsOnLogin` setting plus the per-window
     * RestorePosition rule and injects the result here, keyed by the live windowId.
     * The float STATE is always restored (the window comes back floating); only the
     * geometry MOVE is gated. When UNSET (default), the engine preserves its
     * historical behaviour of always re-applying the recorded position — the path
     * unit tests rely on.
     */
    using RestorePositionPredicate = std::function<bool(const QString& windowId)>;

    /// Inject the floated-position-restore gate. Clear with `{}` before destroying
    /// any state captured by the closure (the daemon honours this on re-wire).
    void setRestorePositionPredicate(RestorePositionPredicate predicate)
    {
        m_restorePositionPredicate = std::move(predicate);
    }

    /**
     * Predicate deciding whether an opening window should start FLOATING because a
     * "Float this app" rule matched it. Daemon-injected, keyed by the live
     * windowId. The window is still inserted (so it stays managed and Meta+F can
     * re-tile it); it is just marked floating, identical to a manual float. When
     * UNSET (default) no window is rule-floated. Clear with `{}` before destroying
     * any state the closure captured.
     */
    using FloatPredicate = std::function<bool(const QString& windowId)>;

    void setFloatPredicate(FloatPredicate predicate)
    {
        m_floatPredicate = std::move(predicate);
    }

    // Cross-engine handoff (see PhosphorEngine/IPlacementEngine.h for contract)
    QString engineId() const override
    {
        return QStringLiteral("autotile");
    }
    void handoffReceive(const HandoffContext& ctx) override;
    void handoffRelease(const QString& windowId) override;
    QString screenForTrackedWindow(const QString& windowId) const override
    {
        return m_states.keyForWindow(canonicalizeForLookup(windowId)).screenId;
    }

    /**
     * @brief Reflow neighbors after a window was interactively resized.
     *
     * Entry point for the "drag an edge, neighbors fill the gap" behaviour
     * (GitHub #652). Called by the daemon's WindowTracking adaptor when the
     * compositor reports an interactive resize of a tiled window has finished.
     *
     * For tree/memory algorithms the moved edge(s) are mapped to the owning
     * @ref PhosphorTiles::SplitTree split(s), whose ratio is adjusted so the
     * neighbour subtree absorbs the change, then the screen is retiled
     * gap-free. Floating windows, single-window screens, cross-output drags,
     * and algorithms without a reflow model are no-ops.
     *
     * @param rawWindowId The interactively-resized window (raw instance id;
     *                    canonicalized internally to match the state/tree keys)
     * @param oldFrame The window's frame geometry before the resize (drag baseline)
     * @param newFrame The window's frame geometry after the resize
     * @param screenId Screen the daemon resolved the window to (authoritative)
     */
    void onWindowResized(const QString& rawWindowId, const QRect& oldFrame, const QRect& newFrame,
                         const QString& screenId) override;
    // ═══════════════════════════════════════════════════════════════════════════
    // Settings synchronization
    // ═══════════════════════════════════════════════════════════════════════════

    PhosphorEngine::IAutotileSettings* autotileSettings() const;
    void writeBackTuning();
    void refreshConfigFromSettings() override;

    // Per-screen config — forwarded to PerScreenConfigResolver (IPlacementEngine overrides)
    void applyPerScreenConfig(const QString& screenId, const QVariantMap& overrides) override;
    void clearPerScreenConfig(const QString& screenId) override;
    QVariantMap perScreenOverrides(const QString& screenId) const override;
    bool hasPerScreenOverride(const QString& screenId, const QString& key) const;
    void updatePerScreenOverride(const QString& screenId, const QString& key, const QVariant& value);

    // Inject the per-context (window-rule) gap-override provider — forwarded to
    // PerScreenConfigResolver. The daemon supplies the screen's current-context
    // gap overrides so tiled windows honour context gap rules like snapping does.
    void setContextGapProvider(std::function<QVariantMap(const QString& screenId)> provider);

    // Mark the active (screen, desktop, activity) state's split ratio / master
    // count as user-tuned so propagateGlobalSplitRatio/MasterCount leaves it
    // alone — the adjustment stays local to that desktop instead of bleeding into
    // the global config. Called by NavigationController after a shortcut/resize
    // adjustment in the no-per-screen-override case.
    void noteSplitRatioUserTuned(const QString& screenId);
    void noteMasterCountUserTuned(const QString& screenId);

    // Effective per-screen values — forwarded to PerScreenConfigResolver
    int effectiveInnerGap(const QString& screenId) const;
    ::PhosphorLayout::EdgeGaps effectiveOuterGaps(const QString& screenId) const;
    bool effectiveSmartGaps(const QString& screenId) const;
    bool effectiveRespectMinimumSize(const QString& screenId) const;
    int effectiveMaxWindows(const QString& screenId) const;
    PhosphorTiles::AutotileInsertPosition effectiveInsertPosition(const QString& screenId) const;
    qreal effectiveSplitRatioStep(const QString& screenId) const override;
    int runtimeMaxWindows() const override;
    QString effectiveAlgorithmId(const QString& screenId) const;
    PhosphorTiles::TilingAlgorithm* effectiveAlgorithm(const QString& screenId) const;

    /**
     * @brief Request that a window is activated after the given screen's next applyTiling
     *
     * Stored in m_pendingFocusByScreen and emitted as activateWindowRequested
     * AFTER windowsTiled, so the KWin effect's post-tile raise loop runs first
     * and the activation lands on top of it. Keyed by screen so a request that
     * outlives a no-op retile (early return before applyTiling) can only fire
     * on ITS screen's next retile, never on another screen's. Used by
     * operations that change which window should be frontmost (e.g. rotating
     * an overlap layout).
     */
    void requestPostRetileFocus(const QString& screenId, const QString& windowId);

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
     * @brief Set master ratio globally (config + every state, every desktop)
     *
     * Used by the D-Bus adaptor to set the ratio as an absolute value. This is
     * the ABSOLUTE setter, so it means what it says: it writes the global config
     * and every existing state on every desktop and activity, and it discards
     * every per-desktop tuning recorded by increaseMasterRatio. A screen carrying
     * an explicit per-screen SplitRatio override is the one exception and keeps
     * it, mirroring propagateGlobalSplitRatio.
     *
     * Contrast increaseMasterRatio, which is the RELATIVE nudge and stays local
     * to one screen+desktop+activity.
     *
     * @param ratio New split ratio (clamped to valid range)
     */
    void setGlobalSplitRatio(qreal ratio);

    /**
     * @brief Set master count globally (config + every state, every desktop)
     *
     * The master-count twin of setGlobalSplitRatio, with the same scope: global
     * config plus every state on every desktop and activity, every per-desktop
     * tuning dropped, and screens with an explicit per-screen MasterCount
     * override left alone.
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

    void focusInDirection(const QString& direction, const PhosphorEngine::NavigationContext& ctx) override;
    void moveFocusedInDirection(const QString& direction, const PhosphorEngine::NavigationContext& ctx) override;
    void swapFocusedInDirection(const QString& direction, const PhosphorEngine::NavigationContext& ctx) override;
    void moveFocusedToPosition(int position, const PhosphorEngine::NavigationContext& ctx) override;
    void rotateWindows(bool clockwise, const PhosphorEngine::NavigationContext& ctx) override;

    /// Cross-mode swap support (queried by the daemon when THIS engine is the
    /// target): the tiled window at @p screenId's entry edge facing the source
    /// for a crossing arriving in @p direction — the swap partner. Empty when
    /// the screen has no tiled windows.
    QString entryWindowForCrossing(const QString& screenId, const QString& direction) const;
    /// The RAW window-order index of @p windowId on @p screenId (current desktop;
    /// counts floats, matching TilingState::addWindow), or -1 when not present —
    /// lets the daemon land a swap counterpart in the same slot the departing
    /// window held when re-inserted via HandoffContext.insertIndex.
    int windowOrderIndexForWindow(const QString& screenId, const QString& windowId) const;
    void reapplyLayout(const PhosphorEngine::NavigationContext& ctx) override;
    void reapplyManagedWindowAppearance() override;
    std::optional<PhosphorEngine::WindowPlacement> capturePlacement(const QString& windowId) const override;
    void snapAllWindows(const PhosphorEngine::NavigationContext& ctx) override;
    void toggleFocusedFloat(const PhosphorEngine::NavigationContext& ctx) override;
    void cycleFocus(bool forward, const PhosphorEngine::NavigationContext& ctx) override;
    void pushToEmptyZone(const PhosphorEngine::NavigationContext& ctx) override;
    void restoreFocusedWindow(const PhosphorEngine::NavigationContext& ctx) override;

    // Autotile-specific navigation. Callable directly on the concrete
    // AutotileEngine pointer from internal callers.
    void swapInDirection(const QString& direction, const QString& action);
    void rotateWindows(bool clockwise, const QString& screenId);

    /**
     * @brief Set the floating state of a specific window
     *
     * When shouldFloat is true, marks the window as floating (excluded from
     * automatic tiling) and retiles the remaining windows. When false,
     * removes the floating flag and re-inserts the window into the tiling layout.
     *
     * @param windowId Window identifier from KWin
     * @param shouldFloat True to float, false to unfloat
     * @param screenId Authoritative current screen (unused: autotile re-tiles
     *        fresh on the window's live screen); present to match the interface.
     */
    Q_INVOKABLE void setWindowFloat(const QString& windowId, bool shouldFloat,
                                    const QString& screenId = QString()) override;

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
    bool beginDragInsertPreview(const QString& rawWindowId, const QString& screenId) override;

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
     * first zone that contains the cursor. When the cursor is beyond all
     * zones: holds at the active preview's lastInsertIndex if a preview is
     * live (the normal call context — keeps the slot stable while the cursor
     * crosses gaps), otherwise falls back to the last tiled index. Returns 0
     * when the state exists but has no zones yet, and -1 if the screen has no
     * tiling state. updateDragInsertPreview() clamps to
     * [0, tiledWindowCount()-1], so returning the last slot is equivalent to
     * "append".
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
     *
     * Also RE-EMITTED with an unchanged set on a desktop/activity switch
     * between two contexts whose autotile sets are identical (with
     * isDesktopSwitch=true, discussion #219) — a wire-contract wakeup for
     * the compositor effect's catch-scan, not a set change. Receivers must
     * be idempotent for the same-set case.
     *
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

    void markAutotileFloated(const QString& rawWindowId);
    void clearAutotileFloated(const QString& rawWindowId);
    bool isAutotileFloated(const QString& rawWindowId) const;

    int pruneStaleWindows(const QSet<QString>& aliveWindowIds) override;

    /// The tile rect this engine last emitted for @p rawWindowId via applyTiling,
    /// remembered PAST the window's transition out of the tiled state (see the
    /// base doc: on a float toggle the tiled bit clears before the compositor
    /// repositions the window, and the capture orchestrator needs this rect to
    /// recognise the still-tiled live frame). Invalid when the window was never
    /// tiled here (or has closed).
    QRect lastManagedRect(const QString& rawWindowId) const override;

private Q_SLOTS:
    void onWindowZoneChanged(const QString& windowId, const QString& zoneId);
    void onWindowAdded(const QString& windowId);
    void onWindowRemoved(const QString& windowId);
    void onWindowFocused(const QString& windowId);
    void onScreenGeometryChanged(const QString& screenId);
    void onLayoutChanged(PhosphorZones::Layout* layout);

private:
    void connectSignals();
    bool insertWindow(const QString& windowId, const QString& screenId);
    // Passive float-state sync after insertWindow() places a window: notify the
    // daemon it opened floating (matched Float rule / restored saved float), or
    // clear a stale WTS float when it was placed tiled. Shared by onWindowAdded
    // and backfillWindows so the two cannot diverge.
    void emitInsertFloatStateSync(const QString& windowId, const QString& screenId);
    /// Add @p windowId to @p state at the position dictated by the
    /// insertion-order setting (End / AfterFocused / AsMaster). Shared by
    /// insertWindow's new-window path and handoffReceive's cross-engine adopt.
    void insertWindowByConfigOrder(PhosphorTiles::TilingState* state, const QString& windowId, const QString& screenId);
    void removeWindow(const QString& windowId);

    /// Algorithm lifecycle REMOVE hook + state removal for a tracked window,
    /// WITHOUT the per-window immediate retile that onWindowRemoved performs.
    /// Returns the affected screen id (empty if the window was untracked) so
    /// batch callers (pruneStaleWindows) can retile each affected screen once
    /// instead of N times. onWindowRemoved is this + an immediate retile.
    QString removeTrackedWindowNoRetile(const QString& windowId);

    /// If @p windowId is the active drag-insert preview's dragged window or
    /// evicted neighbour, drop it from the preview so a later commit/cancel
    /// never operates on a now-dead id. Shared by windowClosed and the
    /// pruneStaleWindows dead-window sweep.
    void dropClosedWindowFromDragPreview(const QString& windowId);
    bool storeWindowMinSize(const QString& windowId, int minWidth, int minHeight);
    bool recalculateLayout(const QString& screenId);
    void applyTiling(const QString& screenId);

    /**
     * @brief Tier-A interactive-resize reflow for tree/memory algorithms.
     *
     * Maps the moved edge(s) of @p windowId to the owning SplitTree split(s)
     * and adjusts their ratios so neighbours absorb the resize. Returns true if
     * at least one split ratio actually changed (caller should retile). The
     * split's extent is read from the currently rendered zones so the math
     * stays in the same coordinate space as @p newFrame.
     */
    bool applyTreeResizeReflow(PhosphorTiles::TilingState* state, const QString& windowId, const QRect& oldFrame,
                               const QRect& newFrame, const QString& screenId);
    bool shouldTileWindow(const QString& windowId) const;
    QString screenForWindow(const QString& windowId) const;
    QRect screenGeometry(const QString& screenId) const;

    /// Check if a screen ID refers to a known (connected) screen.
    /// Virtual screen IDs are validated via PhosphorScreens::ScreenManager geometry;
    /// physical IDs via QScreen lookup.
    bool isKnownScreen(const QString& screenId) const;

    /**
     * @brief Shared per-state teardown body for screen removal.
     *
     * Captures every window's placement into the unified record, drops the
     * overflow set (AFTER capture — the discriminator needs it), appends the
     * released windows, clears the pending-order bookkeeping, and
     * deleteLater()s the state. Callers own the divergent parts: removing
     * the state from m_states (they iterate it) and the per-path
     * override policy — toggle-off drops only the resolver's in-memory
     * overrides, the orphaned-VS teardown purges persisted settings too.
     *
     * @param drainOverflow Pass false when tearing down SEVERAL states that
     *        share one screenId (the orphaned-VS loop spans every
     *        desktop/activity context); the overflow bucket is keyed per
     *        screenId only, so the caller must drain once per screen AFTER
     *        all of that screen's states are captured.
     */
    void releaseScreenStateForTeardown(const QString& screenId, PhosphorTiles::TilingState* state,
                                       QStringList& releasedWindows, bool drainOverflow = true);

    /**
     * @brief Shared key-migration body for focus-driven window moves.
     *
     * Removes @p windowId from @p oldKey's TilingState (running the
     * algorithm lifecycle hook), migrates overflow tracking, retiles the
     * source screen, and re-adds via onWindowAdded() when @p newScreenId is
     * an autotile screen. The caller updates/removes the
     * m_states entry FIRST. No-op when the old state doesn't
     * contain the window.
     */
    void migrateWindowBetweenKeys(const QString& windowId, const PhosphorEngine::TilingStateKey& oldKey,
                                  const QString& newScreenId);

    /**
     * @brief Deferred re-check for context-only (desktop/activity) key
     *        deltas detected by windowFocused().
     *
     * Queued one event-loop pass after the focus event so an in-flight
     * setCurrentDesktop/setCurrentActivity push can land first; migrates
     * only if the key mismatch persists (the window genuinely moved
     * context). Prevents the focus-outran-the-push race from yanking a
     * correctly-tiled window into the wrong desktop's state.
     */
    void revalidateWindowContext(const QString& windowId, const QString& screenId);

    /**
     * @brief Construct a TilingStateKey for the current desktop/activity
     *
     * Respects per-screen desktop overrides for screens where all autotiled
     * windows are sticky (on all desktops). This prevents desktop switches
     * from creating orphan TilingStates when the KWin script
     * "virtualdesktopsonlyonprimary" pins secondary-screen windows.
     */
    PhosphorEngine::TilingStateKey currentKeyForScreen(const QString& screenId) const
    {
        // Precedence and the sticky-pin / per-output subtleties live in the
        // shared ScreenContextTracker (sticky-pin override > per-output desktop >
        // global desktop; activity = current activity).
        return m_context.currentKeyForScreen(screenId);
    }

    /**
     * @brief Which states a global propagate writes: only the current
     *        desktop/activity's (CurrentContext) or every key (AllContexts).
     *
     * The clear-scope/write-scope pairing rationale lives above
     * propagateGlobalSplitRatio in AutotileEngine.cpp.
     */
    enum class PropagateScope {
        CurrentContext,
        AllContexts,
    };

    /**
     * @brief Propagate global split ratio to screens without per-screen overrides
     */
    void propagateGlobalSplitRatio(PropagateScope scope = PropagateScope::CurrentContext);

    /**
     * @brief Propagate global master count to screens without per-screen overrides
     */
    void propagateGlobalMasterCount(PropagateScope scope = PropagateScope::CurrentContext);

    /**
     * @brief Backfill windows on screens where tiledWindowCount < maxWindows
     *
     * When maxWindows increases, windows previously rejected by onWindowAdded()'s
     * gate check remain untiled. This method iterates autotile screens and inserts
     * tracked-but-untiled windows up to the per-screen effective limit.
     *
     * Note: Iteration order over m_states (QHash) is non-deterministic.
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
     * @brief Validate that a windowId is not empty, logging a warning if it is
     * @param windowId Window ID to validate
     * @param operation Operation name for the warning message
     * @return true if valid (non-empty), false if empty (logs warning)
     */
    bool warnIfEmptyWindowId(const QString& windowId, const char* operation) const;

    /**
     * @brief Normalize a window id received from D-Bus to the canonical key
     *        used by internal storage (m_states, PhosphorTiles::TilingState::m_windowOrder, …).
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
     * @brief Shared toggle-float implementation for toggleFocusedWindowFloat/toggleWindowFloat
     *
     * Toggles the floating state, retiles, and emits windowFloatingChanged.
     */
    void performToggleFloat(PhosphorTiles::TilingState* state, const QString& windowId, const QString& screenId);

    /**
     * @brief Get PhosphorTiles::TilingState for a window by looking up its screen
     *
     * Consolidates the common pattern of m_states lookup + state resolution.
     *
     * @param windowId Window ID to look up
     * @param outScreenId If non-null, receives the screen ID
     * @return PhosphorTiles::TilingState pointer or nullptr if window not tracked/screen invalid
     */
    PhosphorTiles::TilingState* stateForWindow(const QString& windowId, QString* outScreenId = nullptr);

    QSet<QString> m_autotileFloatedWindows;

    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    PhosphorEngine::IWindowTrackingService* m_windowTracker = nullptr;
    PhosphorScreens::ScreenManager* m_screenManager = nullptr;
    /// Borrowed cross-surface resolver (neighbouring output / desktop lookup);
    /// null when not injected, in which case directional navigation stops at the
    /// layout boundary instead of crossing surfaces.
    PhosphorEngine::ICrossSurfaceResolver* m_crossSurfaceResolver = nullptr;
    PhosphorEngine::IWindowRegistry* m_windowRegistry = nullptr;
    PhosphorTiles::ITileAlgorithmRegistry* m_algorithmRegistry = nullptr; ///< Borrowed; outlives engine
    std::unique_ptr<AutotileConfig> m_config;
    std::unique_ptr<PerScreenConfigResolver> m_configResolver;
    std::unique_ptr<NavigationController> m_navigation;
    QTimer m_writeBackGuardTimer;
    QTimer m_settingsRetileTimer;

    // Persistence delegates (KConfig stays in WTA layer)
    std::function<void()> m_persistSaveFn;
    std::function<void()> m_persistLoadFn;

    // Floated-position-restore gate. Empty until the daemon wires it; while empty
    // the engine always re-applies a floated window's recorded position (historical
    // behaviour). See RestorePositionPredicate doc above.
    RestorePositionPredicate m_restorePositionPredicate{};

    // Rule-driven open-floating gate. Empty until the daemon wires it; while empty
    // no window is rule-floated. See FloatPredicate doc above.
    FloatPredicate m_floatPredicate{};

    // MigrationArrival moved to AutotileEngineTypes.h; alias keeps the
    // AutotileEngine::MigrationArrival spelling valid for existing call sites.
    using MigrationArrival = ::PhosphorTileEngine::MigrationArrival;
    std::optional<MigrationArrival> m_migrationArrival;

    /// The float state @p windowId must be inserted with: the live state it
    /// carried across a migration, else the open-time "Float this app" rule.
    bool insertShouldFloat(const QString& windowId) const;

    QSet<QString> m_autotileScreens;
    QString m_algorithmId;
    bool m_algorithmEverSet = false; ///< True after first successful setAlgorithm() call
    QString m_activeScreen; // Last-focused screen (updated by onWindowFocused)

    // Per-screen tiling states + the windowId→owning-key reverse map. States are
    // owned via Qt parent (this); PerScreenStates holds only the two maps and
    // their lockstep bookkeeping — engine-specific lifecycle stays in this class.
    PhosphorEngine::PerScreenStates<PhosphorTiles::TilingState> m_states;

    // Screen+desktop states whose split ratio / master count the user has
    // explicitly tuned (keyboard shortcut or interactive resize). propagateGlobal*
    // skips these so a per-desktop tweak survives a settings refresh and is never
    // written into the global config — keeping the adjustment local to that
    // (screen, desktop, activity). Cleared on an algorithm switch and when the
    // user changes the corresponding global value in settings. This is
    // within-session state only: it is not persisted, so the per-desktop tweak
    // does not survive a daemon restart (neither does the value it guards —
    // autotile persistence is per-window, not per-desktop ratio/count).
    QSet<PhosphorEngine::TilingStateKey> m_userTunedSplitRatio;
    QSet<PhosphorEngine::TilingStateKey> m_userTunedMasterCount;

    // Script-state bags rescued from TilingStates that a teardown destroys, so a
    // re-created state for the same key can pick its bag back up. See
    // StashedScriptState in AutotileEngineTypes.h for the full rationale (harvest
    // rules, the algorithm tag, split-tree carry). The alias keeps the
    // AutotileEngine::StashedScriptState spelling valid for existing call sites.
    using StashedScriptState = ::PhosphorTileEngine::StashedScriptState;
    std::unordered_map<PhosphorEngine::TilingStateKey, StashedScriptState> m_scriptStateStash;

    /// Rescue @p state's script-state bag and split tree into m_scriptStateStash
    /// under @p key, tagged with the screen's CURRENT effective algorithm. Takes
    /// a mutable state because the tree is MOVED out of it — the state is being
    /// destroyed, so nothing else will read it. Call before the state is
    /// destroyed and before any override drop that would change what "effective"
    /// resolves to. An EMPTY bag erases the key's entry instead of
    /// inserting: the state is the truth for its key, so emptiness must not be
    /// shadowed by something stashed earlier.
    void stashScriptState(const PhosphorEngine::TilingStateKey& key, PhosphorTiles::TilingState* state);

    /// Hand a stashed bag to @p state for @p key, if one is held and its
    /// algorithm tag matches what the screen currently resolves to. Purely
    /// read-only: the entry is left in the stash either way. Callers pass either
    /// a freshly created state (the state factory in tilingStateForScreen) or, on
    /// the autotile re-enable path, an existing state whose bag applyPerScreenConfig has just
    /// wiped — in that case the stashed bag overwrites what the state holds,
    /// which is the point. A mismatch is NOT proof the bag is dead, only that
    /// the resolver may not be authoritative yet, so it must not erase.
    void restoreStashedScriptState(const PhosphorEngine::TilingStateKey& key, PhosphorTiles::TilingState* state);

    /// Hand @p state back its stashed split tree, if one is held and it still
    /// describes @p state's tiled windows exactly.
    ///
    /// Deliberately NOT called from the state factory like the bag is. A bag is
    /// window-agnostic, so it can be handed to an empty state; a tree's leaves ARE
    /// window ids, and syncTreeInsert is not idempotent, so installing one before
    /// the windows are re-added would duplicate every leaf as they arrive. This
    /// runs at retile time instead, once the window set is whole again.
    ///
    /// The leaf-set check is the guard for everything that happened while the
    /// state was gone: a window closed, or a new one opened, leaves the tree
    /// describing a layout that no longer exists, and it is dropped rather than
    /// reconciled. Rebuilding it from window order would restore the topology
    /// while silently resetting the ratios the tree exists to carry.
    void restoreStashedSplitTree(const PhosphorEngine::TilingStateKey& key, PhosphorTiles::TilingState* state,
                                 const PhosphorTiles::TilingAlgorithm* algo);

    /// Drop @p screenId's stashed bags that were written under an algorithm other
    /// than @p newAlgorithmId, because the screen has genuinely moved to it.
    /// Tag-aware and therefore safe to call eagerly, unlike a blanket per-screen
    /// drop: entries belonging to the incoming algorithm survive. Call ONLY where
    /// the change is real — the resolver's remembered id is what establishes
    /// that, and calling this on a teardown-window reading destroyed exactly the
    /// bags this stash exists to keep.
    void dropStashedScriptStatesForAlgorithmChange(const QString& screenId, const QString& newAlgorithmId);

    QHash<QString, QSize> m_windowMinSizes; // windowId -> minimum size from KWin

    // Canonical windowId → tile rect last emitted for it by applyTiling.
    // Backs lastManagedRect(): deliberately NOT cleared when the window
    // leaves the tiled state (that survival is the point — see the base
    // doc). Cleared on exactly two events: a genuine cross-screen move off
    // an autotile screen (the reposition has already moved the frame off
    // the old tile rect) and stale-window pruning. Everything else KEEPS
    // the entry — windowClosed (the effect notifies autotile before
    // WindowTracking, so the orchestrator's close capture and its tile-rect
    // guard run after this engine's teardown), handoffRelease (the adopting
    // engine's capture runs right after the release), and
    // releaseScreenStateForTeardown (windows still open) — because in each
    // the live frame can still be the tile rect, exactly when the
    // orchestrator's guard needs this memory. Closed windows' entries are
    // reclaimed by pruneStaleWindows, whose sweep is independent of
    // tracking. Used solely for an exact frame comparison, so a stale rect
    // is harmless.
    QHash<QString, QRect> m_lastAppliedTileRect;

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

    // Current desktop/activity context — the global current desktop, per-output
    // desktop overrides (#648), the sticky-desktop pin, the current activity, and
    // the "ever set" arming flags. Used by tilingStateForScreen() to construct
    // the owning key via currentKeyForScreen(). Fed by setCurrentDesktop()/
    // setCurrentActivity()/setCurrentDesktopForScreen() BEFORE updateAutotileScreens()
    // runs on a desktop/activity switch.
    PhosphorEngine::ScreenContextTracker m_context;

    // Armed by a genuine desktop/activity switch (see the setCurrent* mutators);
    // consumed by setAutotileScreens()/the desktop-switch pass. Engine-specific,
    // so it stays here rather than in the shared ScreenContextTracker.
    bool m_isDesktopContextSwitch = false;

    // Pre-seeded window order for snapping → autotile transitions.
    // Keyed by stable EDID-based screen ID (PhosphorScreens::ScreenIdentity::identifierFor).
    // Consumed by insertWindow() as windows arrive; also cleaned up by
    // removeWindow() if a pre-seeded window closes before arriving.
    QHash<QString, QStringList> m_pendingInitialOrders;
    QHash<QString, uint64_t> m_pendingOrderGeneration;
    // Screens whose pendingInitialOrders entry is "strict" — saved order
    // wins even when arrival order differs. Set by setInitialWindowOrder
    // (mode transition: the daemon intentionally pre-computed an order from
    // the previous mode's zones, and that order MUST be preserved).
    // Cleared after the order is fully consumed. Entries seeded by
    // setInitialWindowOrder (mode transition) are the strict ones; advisory
    // entries reconstructed per-window from the placement store are NOT in this
    // set — for those the saved position is honored only when it appends at the
    // current tail, otherwise insertPosition takes over. This is the behaviour
    // users expect from their "After existing" / "After focused" / "As main
    // window" preference for new windows.
    QSet<QString> m_strictInitialOrderScreens;

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

    // Deferred focus, keyed by screen: set by onWindowAdded and
    // requestPostRetileFocus, emitted after that screen's applyTiling so the
    // focus request arrives at KWin AFTER windowsTiled (whose onComplete raises
    // windows in tiling order). Without this, the raise loop buries the new
    // window. Per-screen so an entry stranded by a no-op retile cannot be
    // consumed by another screen's batch and activate the wrong window.
    QHash<QString, QString> m_pendingFocusByScreen;

    // Focus-before-track reseed: a windowFocused() notification can arrive before
    // the window it names is tracked in a TilingState — most visibly on daemon
    // restart, where the effect re-notifies the active window during bring-up but
    // the window re-announce (windowsOpenedBatch) lands afterwards. onWindowFocused
    // would otherwise drop that focus, leaving a focus-driven layout (Theater) with
    // no focused window until the user clicks around. Stash the dropped id here and
    // replay it in onWindowAdded once the window is tracked.
    QString m_pendingFocusReseedWindowId;

    // DragInsertPreview moved to AutotileEngineTypes.h; alias keeps the
    // AutotileEngine::DragInsertPreview spelling valid for existing call sites.
    using DragInsertPreview = ::PhosphorTileEngine::DragInsertPreview;
    std::optional<DragInsertPreview> m_dragInsertPreview;

    /**
     * @brief Process all pending retiles (fires via QueuedConnection)
     *
     * Retiles all screens that had windows added or were newly activated
     * since the last event loop pass.
     */
    void processPendingRetiles();
};

} // namespace PhosphorTileEngine
