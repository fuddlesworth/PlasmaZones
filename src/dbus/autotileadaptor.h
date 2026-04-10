// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <dbus_types.h>
#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QRect>
#include <QSet>
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
    explicit AutotileAdaptor(AutotileEngine* engine, QObject* parent = nullptr);
    ~AutotileAdaptor() override = default;

    // ═══════════════════════════════════════════════════════════════════════════
    // Property Accessors
    // ═══════════════════════════════════════════════════════════════════════════

    bool enabled() const;
    QStringList autotileScreens() const;

    QString algorithm() const;
    void setAlgorithm(const QString& algorithmId);

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
     * @param screenId Screen to retile, or empty for all screens
     */
    void retile(const QString& screenId);

    /**
     * @brief Force retiling of all autotile screens
     *
     * Convenience slot called by KWin effect (e.g. after border width change).
     * Equivalent to retile("").
     */
    void retileAllScreens();

    /**
     * @brief Swap positions of two tiled windows
     * @param windowId1 First window ID
     * @param windowId2 Second window ID
     */
    void swapWindows(const QString& windowId1, const QString& windowId2);

    /**
     * @brief Promote a window to the master area
     * @param windowId Window ID to promote
     */
    void promoteToMaster(const QString& windowId);

    /**
     * @brief Demote a window from master to stack area
     * @param windowId Window ID to demote
     */
    void demoteFromMaster(const QString& windowId);

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
     * @param screenId Screen where the window appeared
     * @param minWidth Window minimum width in pixels (0 if unconstrained)
     * @param minHeight Window minimum height in pixels (0 if unconstrained)
     */
    void windowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight);

    /**
     * @brief Batch window-opened notifications
     *
     * Processes multiple windowOpened in one D-Bus call. Used on daemon
     * startup/restart and autotile toggle-on to avoid per-window D-Bus
     * round-trips.
     *
     * @param entries Array of (windowId, screenId, minWidth, minHeight) structs
     */
    void windowsOpenedBatch(const PlasmaZones::WindowOpenedList& entries);

    /**
     * @brief Update a window's minimum size at runtime
     *
     * Called by KWin effect when a window's minimum size changes after
     * initial windowOpened. Triggers retiling if the value differs.
     *
     * @param windowId Window identifier from KWin
     * @param minWidth New minimum width in pixels (0 if unconstrained)
     * @param minHeight New minimum height in pixels (0 if unconstrained)
     */
    void windowMinSizeUpdated(const QString& windowId, int minWidth, int minHeight);

    /**
     * @brief Notify the engine that a window was closed
     *
     * Called by KWin effect when a window is closed. Removes the window
     * from the autotile engine's tracking and triggers retiling.
     *
     * @param windowId Window identifier from KWin
     */
    void windowClosed(const QString& windowId);

    /**
     * @brief Notify the daemon that a window has been focused
     *
     * Called by KWin effect when a window gains focus. This allows the
     * autotile engine to track the focused window for focus cycling and
     * updates the window-to-screen mapping for correct per-screen tiling.
     *
     * @param windowId Window ID that gained focus
     * @param screenId Screen where the window is located
     */
    void notifyWindowFocused(const QString& windowId, const QString& screenId);

    // floatWindow, unfloatWindow, toggleFocusedWindowFloat, toggleWindowFloat removed:
    // all float operations are now routed through the unified WTA methods
    // (toggleFloatForWindow for toggle, setWindowFloatingForScreen for directional).

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
     * @return AlgorithmInfoEntry struct with algorithm metadata
     */
    AlgorithmInfoEntry algorithmInfo(const QString& algorithmId);

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
     * @param screenIds List of screen IDs currently using autotile
     * @param isDesktopSwitch True if the change is due to desktop/activity switch
     */
    void autotileScreensChanged(const QStringList& screenIds, bool isDesktopSwitch);

    /**
     * @brief Emitted when the tiling algorithm changes
     * @param algorithmId New algorithm ID
     */
    void algorithmChanged(const QString& algorithmId);

    /**
     * @brief Emitted when tiling layout changes for a screen
     * @param screenId Screen that was retiled
     */
    void tilingChanged(const QString& screenId);

    /**
     * @brief Emitted when windows should be moved to new geometries (batch)
     *
     * The KWin effect listens to this signal and applies geometries to all
     * windows in a single slot invocation, avoiding race conditions when
     * many windows are retiled (e.g. rotate).
     *
     * @param tileRequestsJson JSON array of {windowId,x,y,width,height}
     */
    void windowsTileRequested(const PlasmaZones::TileRequestList& tileRequests);

    /**
     * @brief Emitted when a window should be focused
     *
     * The KWin effect listens to this signal and activates the specified window.
     *
     * @param windowId Window ID to focus
     */
    void focusWindowRequested(const QString& windowId);

    /**
     * @brief Emitted when windows are released from autotile management
     *
     * Fired when screens are removed from autotile (e.g., switching from
     * autotile to manual mode). The KWin effect should restore these windows
     * to their pre-autotile geometry or leave them at their current position.
     *
     * @param windowIds Window IDs no longer under autotile control
     * @param releasedScreenIds Screen IDs that triggered the release
     */
    void windowsReleasedFromTiling(const QStringList& windowIds, const QSet<QString>& releasedScreenIds);

    /**
     * @brief Emitted when a window's floating state changes in autotile
     *
     * The KWin effect listens to this signal to restore pre-autotile geometry
     * when floating, and to update its floating cache.
     *
     * @param windowId Window ID whose floating state changed
     * @param isFloating New floating state
     * @param screenId Screen where the window is located
     */
    void windowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId);

    /**
     * @brief Emitted when any configuration property changes
     */
    void configChanged();

private Q_SLOTS:
    void onWindowsTiled(const QString& tileRequestsJson);

private:
    /**
     * @brief Check if engine is available, logging warning if not
     * @param methodName Name of the calling method for logging
     * @return true if engine is available
     */
    bool ensureEngine(const char* methodName) const;

    /**
     * @brief Check if engine and config are available, logging warning if not
     * @param methodName Name of the calling method for logging
     * @return true if both engine and config are available
     */
    bool ensureEngineAndConfig(const char* methodName) const;

    AutotileEngine* m_engine = nullptr;

public:
    /**
     * @brief Clear the engine pointer during shutdown
     *
     * Called by Daemon::stop() before the AutotileEngine unique_ptr is reset,
     * so that any late D-Bus calls (arriving between engine destruction and
     * adaptor destruction) hit ensureEngine()'s null check instead of a
     * dangling pointer.
     */
    void clearEngine();
};

} // namespace PlasmaZones