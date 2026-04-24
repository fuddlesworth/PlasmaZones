// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "core/types.h"
#include <PhosphorProtocol/WireTypes.h>
#include <QDBusAbstractAdaptor>
#include <QObject>
#include <QRect>
#include <QStringList>
#include <QVector>

namespace PlasmaZones {

using PhosphorProtocol::SnapAllResultList;
using PhosphorProtocol::SnapConfirmationList;
using PhosphorProtocol::UnfloatRestoreResult;
using PhosphorProtocol::WindowGeometryList;

class SnapEngine;
class ScreenModeRouter;
class WindowTrackingAdaptor;
class ISettings;

/**
 * @brief D-Bus adaptor for snap-mode window placement
 *
 * Provides D-Bus interface: org.plasmazones.Snap
 *
 * Owns the snap-specific D-Bus surface: commit/uncommit, snap-restore
 * (appRule / persisted / emptyZone / lastZone / resolveWindowRestore),
 * resnap, calculateSnapAll, windowsSnappedBatch, snap-mode navigation
 * (move/focus/swap/push/snap-by-number/rotate/cycle/restore),
 * snap-mode convenience (moveWindowToZone, swapWindowsById), and
 * snap-mode float (toggleFloat, setWindowFloat, calculateUnfloatRestore,
 * windowUnsnappedForFloat).
 *
 * Signal relay from SnapEngine to WindowTrackingAdaptor is also wired
 * here (navigationFeedback, windowFloatingChanged, applyGeometryRequested,
 * resnapToNewLayoutRequested, snapAllWindowsRequested).
 *
 * @see SnapEngine, WindowTrackingAdaptor
 */
class PLASMAZONES_EXPORT SnapAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Snap")

public:
    /**
     * @brief Construct a SnapAdaptor
     *
     * Connects all SnapEngine signals to the corresponding
     * WindowTrackingAdaptor signals for D-Bus relay.
     *
     * @param engine SnapEngine to relay signals from (not owned)
     * @param adaptor WindowTrackingAdaptor to relay signals to (not owned)
     * @param settings ISettings for restore-on-login gate (not owned)
     * @param parent Parent QObject (must be the D-Bus-registered daemon)
     */
    explicit SnapAdaptor(SnapEngine* engine, WindowTrackingAdaptor* adaptor, ISettings* settings,
                         QObject* parent = nullptr);
    ~SnapAdaptor() override = default;

    /**
     * @brief Clear the engine pointer during shutdown
     *
     * Disconnects all signals. Mirrors AutotileAdaptor::clearEngine().
     * Called by Daemon::stop() before the SnapEngine unique_ptr is reset.
     */
    void clearEngine();

    /**
     * @brief Set the ScreenModeRouter for resnap screen filtering
     *
     * Required for resnapCurrentAssignments and resnapForVirtualScreenReconfigure
     * to correctly partition screens by mode.
     *
     * @param router ScreenModeRouter instance (not owned, must outlive adaptor)
     */
    void setScreenModeRouter(ScreenModeRouter* router);

    /**
     * @brief Access the underlying SnapEngine (for daemon-side callers)
     */
    SnapEngine* engine() const;

public Q_SLOTS:
    // ═══════════════════════════════════════════════════════════════════════════
    // Snap-commit D-Bus slots
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Confirm a single-zone snap from the KWin effect
     */
    void windowSnapped(const QString& windowId, const QString& zoneId, const QString& screenId);

    /**
     * @brief Confirm a multi-zone snap from the KWin effect
     */
    void windowSnappedMultiZone(const QString& windowId, const QStringList& zoneIds, const QString& screenId);

    /**
     * @brief Confirm an unsnap from the KWin effect
     */
    void windowUnsnapped(const QString& windowId);

    /**
     * @brief Batch snap confirmations from the KWin effect
     */
    void windowsSnappedBatch(const PlasmaZones::SnapConfirmationList& entries);

    /**
     * @brief Record that a window class was USER-snapped (not auto-snapped)
     */
    void recordSnapIntent(const QString& windowId, bool wasUserInitiated);

    // ═══════════════════════════════════════════════════════════════════════════
    // Snap-restore D-Bus slots
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Snap a new window to the last used zone
     */
    void snapToLastZone(const QString& windowId, const QString& windowScreenId, bool sticky, int& snapX, int& snapY,
                        int& snapWidth, int& snapHeight, bool& shouldSnap);

    /**
     * @brief Snap a window to its app-rule-defined zone
     */
    void snapToAppRule(const QString& windowId, const QString& windowScreenName, bool sticky, int& snapX, int& snapY,
                       int& snapWidth, int& snapHeight, bool& shouldSnap);

    /**
     * @brief Snap a window to the first empty zone
     */
    void snapToEmptyZone(const QString& windowId, const QString& windowScreenId, bool sticky, int& snapX, int& snapY,
                         int& snapWidth, int& snapHeight, bool& shouldSnap);

    /**
     * @brief Restore a window to its persisted zone from the previous session
     */
    void restoreToPersistedZone(const QString& windowId, const QString& screenId, bool sticky, int& snapX, int& snapY,
                                int& snapWidth, int& snapHeight, bool& shouldRestore);

    /**
     * @brief Run the full 4-level snap-restore fallback chain in one call
     */
    void resolveWindowRestore(const QString& windowId, const QString& screenId, bool sticky, int& snapX, int& snapY,
                              int& snapWidth, int& snapHeight, bool& shouldSnap);

    // ═══════════════════════════════════════════════════════════════════════════
    // Resnap / snap-all D-Bus slots
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Resnap all windows from the previous layout to the current layout
     */
    void resnapToNewLayout();

    /**
     * @brief Resnap windows to their current zone assignments
     * @param screenFilter When non-empty, only resnap windows on this screen
     */
    void resnapCurrentAssignments(const QString& screenFilter = QString());

    /**
     * @brief Resnap windows from autotile to manual zones using explicit window order
     */
    void resnapFromAutotileOrder(const QStringList& autotileWindowOrder, const QString& screenId);

    /**
     * @brief Calculate snap assignments for all provided windows
     */
    PlasmaZones::SnapAllResultList calculateSnapAllWindows(const QStringList& windowIds, const QString& screenId);

    /**
     * @brief Trigger snap-all-windows from daemon shortcut
     */
    void snapAllWindows(const QString& screenId);

    /**
     * @brief Process a batch of resnap entries
     */
    void handleBatchedResnap(const QString& resnapData);

    // ═══════════════════════════════════════════════════════════════════════════
    // Snap-mode navigation D-Bus slots
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Move the focused window to an adjacent zone (daemon-driven)
     * @param direction Direction to move ("left", "right", "up", "down")
     */
    void moveWindowToAdjacentZone(const QString& direction);

    /**
     * @brief Focus a window in an adjacent zone (daemon-driven)
     * @param direction Direction to look for windows ("left", "right", "up", "down")
     */
    void focusAdjacentZone(const QString& direction);

    /**
     * @brief Push the focused window to the first empty zone (daemon-driven)
     * @param screenId Screen to find layout/geometry for (empty = active layout)
     */
    void pushToEmptyZone(const QString& screenId = QString());

    /**
     * @brief Restore the focused window to its original size (daemon-driven)
     */
    void restoreWindowSize();

    /**
     * @brief Swap the focused window with the window in an adjacent zone (daemon-driven)
     * @param direction Direction to swap ("left", "right", "up", "down")
     */
    void swapWindowWithAdjacentZone(const QString& direction);

    /**
     * @brief Snap the focused window to a zone by its number (daemon-driven)
     * @param zoneNumber Zone number (1-9)
     * @param screenId Screen to resolve layout for (empty = active layout)
     */
    void snapToZoneByNumber(int zoneNumber, const QString& screenId = QString());

    /**
     * @brief Rotate windows in the layout for a specific screen (daemon-driven)
     * @param clockwise true for clockwise rotation, false for counterclockwise
     * @param screenId Screen to rotate on (empty = all screens)
     */
    void rotateWindowsInLayout(bool clockwise, const QString& screenId = QString());

    /**
     * @brief Cycle focus between windows stacked in the same zone (daemon-driven)
     * @param forward true to cycle to next window, false to cycle to previous
     */
    void cycleWindowsInZone(bool forward);

    // ═══════════════════════════════════════════════════════════════════════════
    // Snap-mode convenience D-Bus slots
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Convenience: snap a window to a specific zone by ID
     * @param windowId Window to snap
     * @param zoneId Target zone UUID
     */
    void moveWindowToZone(const QString& windowId, const QString& zoneId);

    /**
     * @brief Convenience: swap two specific windows by ID
     * @param windowId1 First window
     * @param windowId2 Second window
     */
    void swapWindowsById(const QString& windowId1, const QString& windowId2);

    // ═══════════════════════════════════════════════════════════════════════════
    // Snap-mode float D-Bus slots
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Daemon-driven float toggle for snap-mode screens
     * @param windowId Window identifier
     * @param screenId Screen where window is located
     */
    void toggleFloatForWindow(const QString& windowId, const QString& screenId);

    /**
     * @brief Set a window's floating state (snap-mode direct)
     * @param windowId Window identifier
     * @param floating true to float, false to unfloat
     */
    void setWindowFloat(const QString& windowId, bool floating);

    /**
     * @brief Calculate unfloat restore geometry and zone IDs in a single call
     * @param windowId Window identifier
     * @param screenId Screen for geometry calculation
     * @return UnfloatRestoreResult with found, zoneIds, screenName, x, y, width, height
     */
    PlasmaZones::UnfloatRestoreResult calculateUnfloatRestore(const QString& windowId, const QString& screenId);

    /**
     * @brief Unsnap a window for floating: save its zone to restore on unfloat
     * @param windowId Window identifier
     */
    void windowUnsnappedForFloat(const QString& windowId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Internal (not D-Bus, but callable from daemon C++ code)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Resnap for virtual screen reconfigure (silent, no snap-assist)
     *
     * Not a D-Bus method. Called from Daemon when VS config changes.
     */
    void resnapForVirtualScreenReconfigure(const QString& physicalScreenId);

    /// Resolve a resnap filter into the concrete list of snap-mode screens.
    QStringList resolveSnapModeScreensForResnap(const QString& screenFilter) const;

private:
    // ═══════════════════════════════════════════════════════════════════════════
    // Private helpers
    // ═══════════════════════════════════════════════════════════════════════════

    bool validateWindowId(const QString& windowId, const QString& operation) const;
    QString resolveScreenForSnap(const QString& callerScreen, const QString& zoneId) const;

    /**
     * @brief Apply a successful SnapResult: assign outputs, mark auto-snapped,
     *        clear floating state, and track the zone assignment.
     */
    void applySnapResult(const SnapResult& result, const QString& windowId, int& snapX, int& snapY, int& snapWidth,
                         int& snapHeight, bool& shouldSnap);

    SnapEngine* m_engine = nullptr;
    WindowTrackingAdaptor* m_adaptor = nullptr;
    ISettings* m_settings = nullptr;
    ScreenModeRouter* m_screenModeRouter = nullptr;

    // Stored handles for the signal relays wired in the constructor so
    // clearEngine() can disconnect exactly the connections this class
    // made. A broad disconnect(m_engine, nullptr, m_adaptor, nullptr)
    // would also remove any connection another class happens to make
    // between the same sender/receiver pair — a latent footgun the
    // targeted approach avoids.
    QVector<QMetaObject::Connection> m_connections;
};

} // namespace PlasmaZones
