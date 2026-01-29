// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QHash>
#include <QObject>
#include <QRect>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

namespace PlasmaZones {

class AutotileConfig;
class Layout;
class LayoutManager;
class ScreenManager;
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
     * @brief Helper to get tiled windows for focus operations
     *
     * Gets the focused window, validates screen, retrieves state and windows.
     * Returns empty list if any step fails.
     *
     * @param[out] outScreenName Screen name of the focused window
     * @return List of tiled windows, or empty list if unavailable
     */
    QStringList tiledWindowsForFocusedScreen(QString &outScreenName) const;

    /**
     * @brief Helper to apply an operation to all screen states
     *
     * Iterates all screen states and applies the given operation, then retiles if enabled.
     *
     * @param operation Function to apply to each TilingState
     */
    void applyToAllStates(const std::function<void(TilingState *)> &operation);

    LayoutManager *m_layoutManager = nullptr;
    WindowTrackingService *m_windowTracker = nullptr;
    ScreenManager *m_screenManager = nullptr;
    std::unique_ptr<AutotileConfig> m_config;

    bool m_enabled = false;
    QString m_algorithmId;
    QHash<QString, TilingState *> m_screenStates; // Owned via Qt parent (this)
    QHash<QString, QString> m_windowToScreen;     // windowId -> screenName
};

} // namespace PlasmaZones
