// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "core/iwindowengine.h"
#include "core/types.h"
#include <dbus_types.h>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QStringList>
#include <functional>

namespace PlasmaZones {

class AutotileEngine;
class ISettings;
class IZoneDetector;
class LayoutManager;
class VirtualDesktopManager;
class WindowTrackingService;
class ZoneDetectionAdaptor;

/**
 * @brief Engine for manual zone-based window snapping
 *
 * Implements IEngineLifecycle for screens using manual zone layouts (non-autotile).
 * Handles auto-snap on window open, zone-based navigation, floating state,
 * rotation, and resnap operations.
 *
 * Uses WindowTrackingService as a shared state store for zone assignments,
 * pre-tile geometries, and floating state. The snap engine adds behavior
 * on top: auto-snap fallback chains, directional navigation via zone
 * adjacency, and layout-change resnapping.
 *
 * @see IEngineLifecycle, AutotileEngine, WindowTrackingService
 */
class PLASMAZONES_EXPORT SnapEngine : public QObject, public IEngineLifecycle
{
    Q_OBJECT

public:
    explicit SnapEngine(LayoutManager* layoutManager, WindowTrackingService* windowTracker, IZoneDetector* zoneDetector,
                        ISettings* settings, VirtualDesktopManager* vdm, QObject* parent = nullptr);
    ~SnapEngine() override;

    // ═══════════════════════════════════════════════════════════════════════════
    // IEngineLifecycle implementation
    // ═══════════════════════════════════════════════════════════════════════════

    bool isActiveOnScreen(const QString& screenId) const override;
    using IEngineLifecycle::windowOpened; // Expose 2-arg convenience overload
    void windowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight) override;
    void windowClosed(const QString& windowId) override;
    void windowFocused(const QString& windowId, const QString& screenId) override;
    void toggleWindowFloat(const QString& windowId, const QString& screenId) override;
    void setWindowFloat(const QString& windowId, bool shouldFloat) override;
    void saveState() override;
    void loadState() override;

    // Navigation is daemon-driven for snap mode — handled by
    // WindowTrackingAdaptor's moveWindowToAdjacentZone / focusAdjacentZone
    // / swapWindowWithAdjacentZone / rotateWindowsInLayout / snapToZoneByNumber
    // pipeline. SnapEngine does NOT implement IEngineLifecycle navigation
    // methods because IEngineLifecycle no longer has any — see
    // src/core/iwindowengine.h for the rationale.

    /**
     * @brief Resnap windows from previous layout to current layout after layout switch
     *
     * Maps windows by zone number (1->1, 2->2, etc.) with wrapping when new
     * layout has fewer zones.
     */
    void resnapToNewLayout();

    /**
     * @brief Resnap windows to their current zone assignments (re-apply geometries)
     * @param screenFilter Optional screen name filter (empty = all screens)
     */
    void resnapCurrentAssignments(const QString& screenFilter = QString());

    /**
     * @brief Resnap windows using autotile window order as assignment source
     * @param autotileWindowOrder Ordered list of window IDs from autotile engine
     * @param screenId Screen to resnap on
     * @note Falls back to resnapCurrentAssignments if no entries are calculated
     */
    void resnapFromAutotileOrder(const QStringList& autotileWindowOrder, const QString& screenId);

    /**
     * @brief Calculate resnap entries from autotile order WITHOUT emitting signal
     *
     * Returns the computed ZoneAssignmentEntry vector so the caller can batch entries
     * from multiple screens into a single resnapToNewLayoutRequested emission.
     * Falls back to current-assignment entries if autotile order yields nothing.
     *
     * @param autotileWindowOrder Ordered list of window IDs from autotile engine
     * @param screenId Screen to resnap on
     * @return Vector of ZoneAssignmentEntry (may be empty)
     */
    QVector<ZoneAssignmentEntry> calculateResnapEntriesFromAutotileOrder(const QStringList& autotileWindowOrder,
                                                                         const QString& screenId);

    /**
     * @brief Calculate snap-all-windows assignments without applying them
     * @param windowIds List of window IDs to snap
     * @param screenId Screen to snap on
     * @return JSON array of zone assignment entries for KWin effect to apply
     */
    SnapAllResultList calculateSnapAllWindows(const QStringList& windowIds, const QString& screenId);

    /**
     * @brief Emit a single batched resnapToNewLayoutRequested signal
     *
     * Serializes the given entries and emits the D-Bus signal once.
     * Used by the daemon to combine entries from multiple screens into
     * one signal, eliminating the per-screen race condition.
     *
     * @param entries Combined ZoneAssignmentEntry vector from all screens
     */
    void emitBatchedResnap(const QVector<ZoneAssignmentEntry>& entries);

    /**
     * @brief Request the KWin effect to collect and snap all unsnapped windows
     * @param screenId Screen to operate on
     */
    void snapAllWindows(const QString& screenId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Window restore (auto-snap on window open)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Resolve auto-snap for a newly opened window
     *
     * Runs the 4-level fallback chain:
     *   1. App rules (highest priority)
     *   2. Persisted zone (session restore)
     *   3. Auto-assign to empty zone
     *   4. Snap to last zone (final fallback)
     *
     * Returns a SnapResult so the D-Bus adaptor can unpack geometry for the
     * KWin effect. Also handles floating windows (skips snap, emits feedback).
     *
     * @param windowId Window identifier
     * @param screenId Screen where the window appeared
     * @param sticky Whether the window is on all desktops
     * @return SnapResult with geometry and zone info, or SnapResult::noSnap()
     */
    SnapResult resolveWindowRestore(const QString& windowId, const QString& screenId, bool sticky);

    // ═══════════════════════════════════════════════════════════════════════════
    // Autotile engine reference (for isActiveOnScreen routing)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Set the autotile engine for screen ownership checks
     *
     * SnapEngine is active on screens where AutotileEngine is NOT active.
     * This must be called after the AutotileEngine is created.
     *
     * @param engine AutotileEngine instance (not owned, must outlive SnapEngine)
     */
    void setAutotileEngine(AutotileEngine* engine);

    // ═══════════════════════════════════════════════════════════════════════════
    // Zone detection adaptor (for daemon-driven navigation)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Set ZoneDetectionAdaptor for adjacency queries
     *
     * @param adaptor ZoneDetectionAdaptor instance (not owned, must outlive SnapEngine)
     */
    void setZoneDetectionAdaptor(ZoneDetectionAdaptor* adaptor);

    // ═══════════════════════════════════════════════════════════════════════════
    // Persistence delegate
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Set persistence callbacks for save/load
     *
     * KConfig persistence is owned by WindowTrackingAdaptor (WTS is KConfig-free).
     * These callbacks allow SnapEngine to fulfill the IEngineLifecycle persistence
     * contract without introducing KConfig as a dependency.
     *
     * @param saveFn Called by saveState() to persist WTS state
     * @param loadFn Called by loadState() to restore WTS state
     */
    void setPersistenceDelegate(std::function<void()> saveFn, std::function<void()> loadFn)
    {
        m_saveFn = std::move(saveFn);
        m_loadFn = std::move(loadFn);
    }

    /// Last screen the engine saw via windowFocused. Used for OSD fallback
    /// when a navigation failure needs to cite a screen and the live cursor
    /// hasn't landed on one yet. Exposed as a const getter so tests can
    /// verify the focus-tracking contract without dummy signal stubs.
    QString lastActiveScreenId() const
    {
        return m_lastActiveScreenId;
    }

Q_SIGNALS:
    // ═══════════════════════════════════════════════════════════════════════════
    // Signals (relayed via SnapAdaptor → WTA → D-Bus → effect)
    // ═══════════════════════════════════════════════════════════════════════════

    /// Navigation feedback for OSD display
    void navigationFeedback(bool success, const QString& action, const QString& reason, const QString& sourceZoneId,
                            const QString& targetZoneId, const QString& screenId);

    /// Window floating state changed (for KWin effect sync)
    void windowFloatingChanged(const QString& windowId, bool floating, const QString& screenId);

    /// Daemon-driven geometry application (used by autotile float restore)
    void applyGeometryRequested(const QString& windowId, int x, int y, int width, int height, const QString& zoneId,
                                const QString& screenId, bool sizeOnly);

    /// Batched resnap data (routed through WTA::handleBatchedResnap for bookkeeping)
    void resnapToNewLayoutRequested(const QString& resnapData);

    /// Request KWin effect to collect unsnapped windows and snap them all
    void snapAllWindowsRequested(const QString& screenId);

private:
    LayoutManager* m_layoutManager = nullptr;
    WindowTrackingService* m_windowTracker = nullptr;
    IZoneDetector* m_zoneDetector = nullptr;
    ISettings* m_settings = nullptr;
    VirtualDesktopManager* m_virtualDesktopManager = nullptr;
    QPointer<AutotileEngine> m_autotileEngine;
    QPointer<ZoneDetectionAdaptor> m_zoneDetectionAdaptor;

    // ═══════════════════════════════════════════════════════════════════════════
    // Float helpers (snapengine/float.cpp)
    // ═══════════════════════════════════════════════════════════════════════════

    bool unfloatToZone(const QString& windowId, const QString& screenId);
    bool applyGeometryForFloat(const QString& windowId, const QString& screenId);
    void clearFloatingStateForSnap(const QString& windowId, const QString& screenId);
    void assignToZones(const QString& windowId, const QStringList& zoneIds, const QString& screenId);

    // Persistence delegates (KConfig stays in adaptor layer)
    std::function<void()> m_saveFn;
    std::function<void()> m_loadFn;

    // Last-focused screen (updated by windowFocused)
    QString m_lastActiveScreenId;
};

} // namespace PlasmaZones
