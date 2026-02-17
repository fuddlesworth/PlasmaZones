// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QRect>
#include <QString>
#include <QStringList>

namespace PlasmaZones {

class AutotileEngine;

/**
 * @brief D-Bus adaptor for autotiling control
 *
 * Provides D-Bus interface: org.plasmazones.Autotile
 * Exposes AutotileEngine functionality for external control and KWin effect communication.
 *
 * This adaptor enables:
 * - Enabling/disabling autotiling
 * - Switching tiling algorithms
 * - Manual tiling operations (swap, promote, demote)
 * - Focus cycling (next, previous, master)
 * - Master ratio/count adjustment
 * - Querying available algorithms
 *
 * The adaptor emits signals that the KWin effect listens to for applying
 * window geometries and focus changes.
 */
class PLASMAZONES_EXPORT AutotileAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Autotile")

    // ═══════════════════════════════════════════════════════════════════════════
    // D-Bus Properties
    // ═══════════════════════════════════════════════════════════════════════════

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(QStringList autotileScreens READ autotileScreens NOTIFY autotileScreensChanged)
    Q_PROPERTY(QString algorithm READ algorithm WRITE setAlgorithm NOTIFY algorithmChanged)
    Q_PROPERTY(double masterRatio READ masterRatio WRITE setMasterRatio NOTIFY configChanged)
    Q_PROPERTY(int masterCount READ masterCount WRITE setMasterCount NOTIFY configChanged)
    Q_PROPERTY(int innerGap READ innerGap WRITE setInnerGap NOTIFY configChanged)
    Q_PROPERTY(int outerGap READ outerGap WRITE setOuterGap NOTIFY configChanged)
    Q_PROPERTY(bool smartGaps READ smartGaps WRITE setSmartGaps NOTIFY configChanged)
    Q_PROPERTY(bool focusNewWindows READ focusNewWindows WRITE setFocusNewWindows NOTIFY configChanged)

public:
    /**
     * @brief Construct an AutotileAdaptor
     *
     * @param engine The AutotileEngine to expose via D-Bus
     * @param parent Parent QObject (typically the daemon)
     */
    explicit AutotileAdaptor(AutotileEngine *engine, QObject *parent = nullptr);
    ~AutotileAdaptor() override = default;

    // ═══════════════════════════════════════════════════════════════════════════
    // Property Accessors
    // ═══════════════════════════════════════════════════════════════════════════

    bool enabled() const;
    QStringList autotileScreens() const;

    QString algorithm() const;
    void setAlgorithm(const QString &algorithmId);

    double masterRatio() const;
    void setMasterRatio(double ratio);

    int masterCount() const;
    void setMasterCount(int count);

    int innerGap() const;
    void setInnerGap(int gap);

    int outerGap() const;
    void setOuterGap(int gap);

    bool smartGaps() const;
    void setSmartGaps(bool enabled);

    bool focusNewWindows() const;
    void setFocusNewWindows(bool enabled);

public Q_SLOTS:
    // ═══════════════════════════════════════════════════════════════════════════
    // Tiling Operations
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Force retiling of windows
     * @param screenName Screen to retile, or empty for all screens
     */
    void retile(const QString &screenName);

    /**
     * @brief Swap positions of two tiled windows
     * @param windowId1 First window ID
     * @param windowId2 Second window ID
     */
    void swapWindows(const QString &windowId1, const QString &windowId2);

    /**
     * @brief Promote a window to the master area
     * @param windowId Window ID to promote
     */
    void promoteToMaster(const QString &windowId);

    /**
     * @brief Demote a window from master to stack area
     * @param windowId Window ID to demote
     */
    void demoteFromMaster(const QString &windowId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Focus Operations
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Focus the master window on the current screen
     */
    void focusMaster();

    /**
     * @brief Cycle focus to the next tiled window
     */
    void focusNext();

    /**
     * @brief Cycle focus to the previous tiled window
     */
    void focusPrevious();

    /**
     * @brief Notify the engine that a window was opened
     *
     * Called by KWin effect when a new window is added. Adds the window
     * to the autotile engine's tracking and triggers retiling.
     *
     * @param windowId Window identifier from KWin
     * @param screenName Screen where the window appeared
     */
    void windowOpened(const QString &windowId, const QString &screenName);

    /**
     * @brief Notify the engine that a window was closed
     *
     * Called by KWin effect when a window is closed. Removes the window
     * from the autotile engine's tracking and triggers retiling.
     *
     * @param windowId Window identifier from KWin
     */
    void windowClosed(const QString &windowId);

    /**
     * @brief Notify the daemon that a window has been focused
     *
     * Called by KWin effect when a window gains focus. This allows the
     * autotile engine to track the focused window for focus cycling.
     *
     * @param windowId Window ID that gained focus
     */
    void notifyWindowFocused(const QString &windowId);

    /**
     * @brief Float a specific window from autotile management
     *
     * Called by KWin effect when a window is dragged during autotile mode.
     * Marks the window as floating and retiles the remaining windows.
     *
     * @param windowId Window identifier from KWin
     */
    void floatWindow(const QString &windowId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Ratio/Count Adjustment
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Increase the master area ratio
     * @param delta Amount to increase (e.g., 0.05 for 5%)
     */
    void increaseMasterRatio(double delta);

    /**
     * @brief Decrease the master area ratio
     * @param delta Amount to decrease (e.g., 0.05 for 5%)
     */
    void decreaseMasterRatio(double delta);

    /**
     * @brief Increase the number of master windows
     */
    void increaseMasterCount();

    /**
     * @brief Decrease the number of master windows
     */
    void decreaseMasterCount();

    // ═══════════════════════════════════════════════════════════════════════════
    // Algorithm Query
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get list of available tiling algorithm IDs
     * @return List of algorithm IDs
     */
    QStringList availableAlgorithms();

    /**
     * @brief Get information about a specific algorithm
     * @param algorithmId Algorithm ID to query
     * @return JSON object with id, name, description, icon
     */
    QString algorithmInfo(const QString &algorithmId);

Q_SIGNALS:
    // ═══════════════════════════════════════════════════════════════════════════
    // D-Bus Signals
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Emitted when autotiling enabled state changes
     * @param enabled New enabled state
     */
    void enabledChanged(bool enabled);

    /**
     * @brief Emitted when the set of autotile screens changes
     * @param screenNames List of screen names currently using autotile
     */
    void autotileScreensChanged(const QStringList &screenNames);

    /**
     * @brief Emitted when the tiling algorithm changes
     * @param algorithmId New algorithm ID
     */
    void algorithmChanged(const QString &algorithmId);

    /**
     * @brief Emitted when tiling layout changes for a screen
     * @param screenName Screen that was retiled
     */
    void tilingChanged(const QString &screenName);

    /**
     * @brief Emitted when a window should be moved to a new geometry
     *
     * The KWin effect listens to this signal and applies the geometry
     * to the specified window.
     *
     * @param windowId Window ID to tile
     * @param x Target X position
     * @param y Target Y position
     * @param width Target width
     * @param height Target height
     */
    void windowTileRequested(const QString &windowId, int x, int y, int width, int height);

    /**
     * @brief Emitted when a window should be focused
     *
     * The KWin effect listens to this signal and activates the specified window.
     *
     * @param windowId Window ID to focus
     */
    void focusWindowRequested(const QString &windowId);

    /**
     * @brief Emitted when monocle mode visibility changes are needed
     *
     * The KWin effect listens to this signal to minimize/unminimize
     * windows in monocle mode.
     *
     * @param focusedWindowId Window to unminimize/show
     * @param windowsToHide List of window IDs to minimize
     */
    void monocleVisibilityChanged(const QString &focusedWindowId, const QStringList &windowsToHide);

    /**
     * @brief Emitted when any configuration property changes
     */
    void configChanged();

private Q_SLOTS:
    void onWindowTiled(const QString &windowId, const QRect &geometry);

private:
    /**
     * @brief Check if engine is available, logging warning if not
     * @param methodName Name of the calling method for logging
     * @return true if engine is available
     */
    bool ensureEngine(const char *methodName) const;

    /**
     * @brief Check if engine and config are available, logging warning if not
     * @param methodName Name of the calling method for logging
     * @return true if both engine and config are available
     */
    bool ensureEngineAndConfig(const char *methodName) const;

    AutotileEngine *m_engine = nullptr;

public:
    /**
     * @brief Clear the engine pointer during shutdown
     *
     * Called by Daemon::stop() before the AutotileEngine unique_ptr is reset,
     * so that any late D-Bus calls (arriving between engine destruction and
     * adaptor destruction) hit ensureEngine()'s null check instead of a
     * dangling pointer.
     */
    void clearEngine() { m_engine = nullptr; }
};

} // namespace PlasmaZones