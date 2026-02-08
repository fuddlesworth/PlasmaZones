// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "types.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QRect>
#include <optional>

namespace PlasmaZones {

class LayoutManager;
class IZoneDetector;
class ISettings;
class VirtualDesktopManager;
class Layout;
class Zone;

/**
 * @brief Window-zone tracking service (business logic layer)
 *
 * This service encapsulates all window tracking business logic that was
 * previously in WindowTrackingAdaptor. Following separation of concerns,
 * Principle, it handles:
 *
 * - Zone assignment management (which window is in which zone)
 * - Pre-snap geometry storage (for restoring original size)
 * - Floating window state tracking
 * - Session persistence (save/load state across restarts)
 * - Auto-snap logic (snap new windows to last zone)
 * - Window rotation calculations
 *
 * The WindowTrackingAdaptor becomes a thin D-Bus facade that delegates
 * all business logic to this service.
 *
 * Design benefits:
 * - Testable: Service can be unit tested without D-Bus
 * - Reusable: Logic can be used by other components
 * - Maintainable: Clear separation of concerns
 * - Debuggable: Easier to trace logic flow
 */
class PLASMAZONES_EXPORT WindowTrackingService : public QObject
{
    Q_OBJECT

public:
    explicit WindowTrackingService(LayoutManager* layoutManager, IZoneDetector* zoneDetector,
                                   ISettings* settings, VirtualDesktopManager* vdm,
                                   QObject* parent = nullptr);
    ~WindowTrackingService() override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Zone Assignment Management
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Assign a window to a zone
     * @param windowId Full window ID
     * @param zoneId Zone UUID string
     * @param screenName Screen where the zone is located
     * @param virtualDesktop Virtual desktop number (1-based, 0 = all)
     */
    void assignWindowToZone(const QString& windowId, const QString& zoneId,
                            const QString& screenName, int virtualDesktop);

    /**
     * @brief Assign a window to multiple zones (multi-zone snap)
     * @param windowId Full window ID
     * @param zoneIds List of zone UUID strings (first is primary)
     * @param screenName Screen where the zones are located
     * @param virtualDesktop Virtual desktop number (1-based, 0 = all)
     */
    void assignWindowToZones(const QString& windowId, const QStringList& zoneIds,
                             const QString& screenName, int virtualDesktop);

    /**
     * @brief Remove window from its assigned zone
     * @param windowId Full window ID
     */
    void unassignWindow(const QString& windowId);

    /**
     * @brief Get the primary zone ID for a window
     * @param windowId Full window ID
     * @return Zone ID or empty string if not assigned
     */
    QString zoneForWindow(const QString& windowId) const;

    /**
     * @brief Get all zone IDs for a window (multi-zone support)
     * @param windowId Full window ID
     * @return List of zone IDs (empty if not assigned)
     */
    QStringList zonesForWindow(const QString& windowId) const;

    /**
     * @brief Get all windows in a specific zone
     * @param zoneId Zone UUID string
     * @return List of window IDs
     */
    QStringList windowsInZone(const QString& zoneId) const;

    /**
     * @brief Get all snapped windows
     * @return List of window IDs that are currently snapped
     */
    QStringList snappedWindows() const;

    /**
     * @brief Check if a window is assigned to any zone
     */
    bool isWindowSnapped(const QString& windowId) const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Pre-Snap Geometry Storage
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Store geometry before snapping (only on first snap)
     * @param windowId Full window ID
     * @param geometry Window geometry before snapping
     */
    void storePreSnapGeometry(const QString& windowId, const QRect& geometry);

    /**
     * @brief Get stored pre-snap geometry
     * @param windowId Full window ID
     * @return Geometry if stored, nullopt otherwise
     */
    std::optional<QRect> preSnapGeometry(const QString& windowId) const;

    /**
     * @brief Check if window has stored pre-snap geometry
     */
    bool hasPreSnapGeometry(const QString& windowId) const;

    /**
     * @brief Clear stored pre-snap geometry (after restore)
     */
    void clearPreSnapGeometry(const QString& windowId);

    /**
     * @brief Get validated pre-snap geometry within screen bounds
     * @param windowId Full window ID
     * @return Adjusted geometry within visible screens, nullopt if not found
     */
    std::optional<QRect> validatedPreSnapGeometry(const QString& windowId) const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Floating Window State
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Check if a window is floating (excluded from snapping)
     */
    bool isWindowFloating(const QString& windowId) const;

    /**
     * @brief Set window floating state
     * @param windowId Full window ID
     * @param floating true to float, false to unfloat
     */
    void setWindowFloating(const QString& windowId, bool floating);

    /**
     * @brief Get all floating window IDs
     */
    QStringList floatingWindows() const;

    /**
     * @brief Unsnap window for floating (saves zone for later restore)
     * @param windowId Full window ID
     */
    void unsnapForFloat(const QString& windowId);

    /**
     * @brief Get primary zone to restore to when unfloating
     * @param windowId Full window ID
     * @return Zone ID or empty string if none
     */
    QString preFloatZone(const QString& windowId) const;

    /**
     * @brief Get all zones to restore to when unfloating (multi-zone support)
     * @param windowId Full window ID
     * @return List of zone IDs (empty if none)
     */
    QStringList preFloatZones(const QString& windowId) const;

    /**
     * @brief Get the screen name where the window was snapped before floating
     * @param windowId Full window ID
     * @return Screen name or empty string if unknown
     */
    QString preFloatScreen(const QString& windowId) const;

    /**
     * @brief Clear pre-float zone after restore
     */
    void clearPreFloatZone(const QString& windowId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Sticky Window Handling
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Record whether a window is sticky (on all desktops)
     */
    void setWindowSticky(const QString& windowId, bool sticky);

    /**
     * @brief Check if window is sticky
     */
    bool isWindowSticky(const QString& windowId) const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Auto-Snap Logic
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Calculate snap result based on app-to-zone rules
     * @param windowId Full window ID
     * @param windowScreenName Screen where window currently is
     * @param isSticky Whether window is on all desktops
     * @return SnapResult with geometry and zone info, or noSnap if no rule matches
     */
    SnapResult calculateSnapToAppRule(const QString& windowId, const QString& windowScreenName,
                                       bool isSticky) const;

    /**
     * @brief Calculate snap result for new window to last used zone
     * @param windowId Full window ID
     * @param windowScreenName Screen where window currently is
     * @param isSticky Whether window is on all desktops
     * @return SnapResult with geometry and zone info
     */
    SnapResult calculateSnapToLastZone(const QString& windowId, const QString& windowScreenName,
                                        bool isSticky) const;

    /**
     * @brief Calculate snap result to restore from persisted session
     * @param windowId Full window ID
     * @param screenName Screen for geometry calculation
     * @param isSticky Whether window is on all desktops
     * @return SnapResult with geometry and zone info
     */
    SnapResult calculateRestoreFromSession(const QString& windowId, const QString& screenName,
                                            bool isSticky) const;

    /**
     * @brief Record that a window class was user-snapped
     * @param windowId Full window ID to extract class from
     * @param wasUserInitiated true if user-initiated snap
     */
    void recordSnapIntent(const QString& windowId, bool wasUserInitiated);

    /**
     * @brief Get last used zone ID
     */
    QString lastUsedZoneId() const { return m_lastUsedZoneId; }

    /**
     * @brief Update last used zone tracking
     */
    void updateLastUsedZone(const QString& zoneId, const QString& screenName,
                            const QString& windowClass, int virtualDesktop);

    /**
     * @brief Clear stale pending assignment for a window
     *
     * When a user explicitly snaps a window, this clears any stale pending
     * assignment from a previous session. This prevents the window from
     * restoring to the wrong zone if it's closed and reopened.
     *
     * @param windowId Full window ID
     * @return true if a stale pending assignment was cleared
     */
    bool clearStalePendingAssignment(const QString& windowId);

    /**
     * @brief Mark a window as auto-snapped
     *
     * Auto-snapped windows should not update the last-used zone tracking
     * when snapped. This prevents unwanted zone changes when windows are
     * automatically restored on open.
     *
     * @param windowId Full window ID
     */
    void markAsAutoSnapped(const QString& windowId);

    /**
     * @brief Check if a window was auto-snapped
     * @param windowId Full window ID
     * @return true if the window was auto-snapped (not user-initiated)
     */
    bool isAutoSnapped(const QString& windowId) const;

    /**
     * @brief Clear auto-snapped flag for a window
     * @param windowId Full window ID
     * @return true if the window had the auto-snapped flag
     */
    bool clearAutoSnapped(const QString& windowId);

    /**
     * @brief Consume pending zone assignment after successful restore
     *
     * After a window is successfully restored to its persisted zone, the pending
     * assignment should be removed so that:
     * 1. The same window won't be restored again if reopened
     * 2. Other windows of the same class won't incorrectly restore to this zone
     *
     * @param windowId Full window ID
     */
    void consumePendingAssignment(const QString& windowId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Navigation Helpers
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Find the first empty zone in the layout for a screen
     * @param screenName Screen to find layout for (empty = active layout)
     * @return Zone ID or empty string if all occupied
     */
    QString findEmptyZone(const QString& screenName = QString()) const;

    /**
     * @brief Get geometry for a zone on a specific screen
     * @param zoneId Zone UUID string
     * @param screenName Screen name (empty = primary)
     * @return Zone geometry in pixels, or invalid QRect if not found
     */
    QRect zoneGeometry(const QString& zoneId, const QString& screenName = QString()) const;

    /**
     * @brief Get combined geometry for multiple zones on a specific screen
     * @param zoneIds List of zone UUID strings
     * @param screenName Screen name (empty = primary)
     * @return Union of all zone geometries, or invalid QRect if none found
     */
    QRect multiZoneGeometry(const QStringList& zoneIds, const QString& screenName = QString()) const;

    /**
     * @brief Calculate rotation data for windows on a specific screen
     * @param clockwise true for clockwise rotation
     * @param screenFilter When non-empty, only rotate windows on this screen
     * @return List of rotation entries
     */
    QVector<RotationEntry> calculateRotation(bool clockwise, const QString& screenFilter = QString()) const;

    /**
     * @brief Calculate resnap data for windows from previous layout to current layout
     *
     * When layout changes, windows that were in the previous layout are buffered.
     * This method maps them to the current layout by zone number (1->1, 2->2, etc.)
     * with cycling when the new layout has fewer zones (e.g. zone 4->1, 5->2 when
     * going from 5 zones to 3).
     *
     * @return List of rotation entries (same format as rotate) for KWin to apply
     */
    QVector<RotationEntry> calculateResnapFromPreviousLayout();

    // ═══════════════════════════════════════════════════════════════════════════
    // Resolution Change Handling
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get updated geometries for all tracked windows
     * @return Map of windowId -> new geometry
     *
     * Used when screen resolution changes to recalculate zone positions.
     */
    QHash<QString, QRect> updatedWindowGeometries() const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Window Lifecycle
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Clean up all tracking data for a closed window
     * @param windowId Full window ID
     */
    void windowClosed(const QString& windowId);

    /**
     * @brief Handle layout change - validate/clear stale zone assignments
     */
    void onLayoutChanged();

    // ═══════════════════════════════════════════════════════════════════════════
    // State Access (for adaptor persistence via KConfig)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get all zone assignments for persistence
     * @return Map of windowId -> zoneIds (list of zone UUIDs)
     */
    const QHash<QString, QStringList>& zoneAssignments() const { return m_windowZoneAssignments; }

    /**
     * @brief Get all screen assignments for persistence
     */
    const QHash<QString, QString>& screenAssignments() const { return m_windowScreenAssignments; }

    /**
     * @brief Get all desktop assignments for persistence
     */
    const QHash<QString, int>& desktopAssignments() const { return m_windowDesktopAssignments; }

    /**
     * @brief Get all pre-snap geometries for persistence
     */
    const QHash<QString, QRect>& preSnapGeometries() const { return m_preSnapGeometries; }

    /**
     * @brief Get pending zone assignments (for session restore)
     */
    const QHash<QString, QStringList>& pendingZoneAssignments() const { return m_pendingZoneAssignments; }

    /**
     * @brief Get pending screen assignments
     */
    const QHash<QString, QString>& pendingScreenAssignments() const { return m_pendingZoneScreens; }

    /**
     * @brief Get pending desktop assignments
     */
    const QHash<QString, int>& pendingDesktopAssignments() const { return m_pendingZoneDesktops; }

    /**
     * @brief Get pending layout assignments (for layout validation on restore)
     */
    const QHash<QString, QString>& pendingLayoutAssignments() const { return m_pendingZoneLayouts; }

    /**
     * @brief Get user-snapped classes
     */
    const QSet<QString>& userSnappedClasses() const { return m_userSnappedClasses; }

    /**
     * @brief Set pending zone assignments (loaded from KConfig by adaptor)
     */
    void setPendingZoneAssignments(const QHash<QString, QStringList>& assignments) { m_pendingZoneAssignments = assignments; }

    /**
     * @brief Set pending screen assignments
     */
    void setPendingScreenAssignments(const QHash<QString, QString>& assignments) { m_pendingZoneScreens = assignments; }

    /**
     * @brief Set pending desktop assignments
     */
    void setPendingDesktopAssignments(const QHash<QString, int>& assignments) { m_pendingZoneDesktops = assignments; }

    /**
     * @brief Set pending layout assignments (loaded from KConfig by adaptor)
     */
    void setPendingLayoutAssignments(const QHash<QString, QString>& assignments) { m_pendingZoneLayouts = assignments; }

    /**
     * @brief Set pre-snap geometries (loaded from KConfig by adaptor)
     */
    void setPreSnapGeometries(const QHash<QString, QRect>& geometries) { m_preSnapGeometries = geometries; }

    /**
     * @brief Set user-snapped classes (loaded from KConfig by adaptor)
     */
    void setUserSnappedClasses(const QSet<QString>& classes) { m_userSnappedClasses = classes; }

    /**
     * @brief Set last used zone info (loaded from KConfig by adaptor)
     */
    void setLastUsedZone(const QString& zoneId, const QString& screenName,
                         const QString& zoneClass, int desktop);

    /**
     * @brief Set floating windows (loaded from KConfig by adaptor)
     */
    void setFloatingWindows(const QSet<QString>& windows) { m_floatingWindows = windows; }

    /**
     * @brief Get pre-float zone assignments for persistence
     */
    const QHash<QString, QStringList>& preFloatZoneAssignments() const { return m_preFloatZoneAssignments; }
    const QHash<QString, QString>& preFloatScreenAssignments() const { return m_preFloatScreenAssignments; }

    /**
     * @brief Set pre-float zone assignments (loaded from KConfig by adaptor)
     */
    void setPreFloatZoneAssignments(const QHash<QString, QStringList>& assignments) { m_preFloatZoneAssignments = assignments; }
    void setPreFloatScreenAssignments(const QHash<QString, QString>& assignments) { m_preFloatScreenAssignments = assignments; }

Q_SIGNALS:
    /**
     * @brief Emitted when a window's zone assignment changes
     */
    void windowZoneChanged(const QString& windowId, const QString& zoneId);

    /**
     * @brief Emitted when state needs to be saved
     */
    void stateChanged();

private:
    // Minimum visible area for geometry validation
    static constexpr int MinVisibleWidth = 100;
    static constexpr int MinVisibleHeight = 100;

    // Helpers
    void scheduleSaveState();
    bool isGeometryOnScreen(const QRect& geometry) const;
    QRect adjustGeometryToScreen(const QRect& geometry) const;
    Zone* findZoneById(const QString& zoneId) const;

    // Dependencies
    LayoutManager* m_layoutManager;
    IZoneDetector* m_zoneDetector;
    ISettings* m_settings;
    VirtualDesktopManager* m_virtualDesktopManager;

    // Zone assignments: windowId -> zoneIds (supports multi-zone snap)
    QHash<QString, QStringList> m_windowZoneAssignments;
    // Screen tracking: windowId -> screenName
    QHash<QString, QString> m_windowScreenAssignments;
    // Desktop tracking: windowId -> virtual desktop
    QHash<QString, int> m_windowDesktopAssignments;

    // Pre-snap geometries: windowId -> geometry
    QHash<QString, QRect> m_preSnapGeometries;

    // Last used zone tracking
    QString m_lastUsedZoneId;
    QString m_lastUsedScreenName;
    QString m_lastUsedZoneClass;
    int m_lastUsedDesktop = 0;

    // Floating windows
    QSet<QString> m_floatingWindows;

    // Session persistence
    QHash<QString, QStringList> m_pendingZoneAssignments;  // stableId -> zoneIds
    QHash<QString, QString> m_pendingZoneScreens;      // stableId -> screenName
    QHash<QString, int> m_pendingZoneDesktops;         // stableId -> desktop
    QHash<QString, QString> m_pendingZoneLayouts;      // stableId -> layoutId (for layout validation on restore)

    // Pre-float zone and screen (for unfloat restore to correct monitor)
    QHash<QString, QStringList> m_preFloatZoneAssignments;
    QHash<QString, QString> m_preFloatScreenAssignments;

    // User-snapped classes (for auto-snap eligibility)
    QSet<QString> m_userSnappedClasses;

    // Sticky window states
    QHash<QString, bool> m_windowStickyStates;

    // Auto-snapped windows (to avoid updating last-used zone)
    QSet<QString> m_autoSnappedWindows;

    // Resnap buffer: when layout changes, store (windowId, zonePosition, screenName, vd)
    // for windows that were in the previous layout, so resnapToNewLayout can map them
    struct ResnapEntry {
        QString windowId;
        int zonePosition; // 1-based position in sorted-by-zoneNumber order
        QString screenName;
        int virtualDesktop = 0;
    };
    QVector<ResnapEntry> m_resnapBuffer;

    // Note: No save timer - persistence handled by WindowTrackingAdaptor via KConfig
    // Service emits stateChanged() signal when state needs saving
};

} // namespace PlasmaZones
