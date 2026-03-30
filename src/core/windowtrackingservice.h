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
    explicit WindowTrackingService(LayoutManager* layoutManager, IZoneDetector* zoneDetector, ISettings* settings,
                                   VirtualDesktopManager* vdm, QObject* parent = nullptr);
    ~WindowTrackingService() override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Zone Assignment Management
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Assign a window to a zone
     * @param windowId Full window ID
     * @param zoneId Zone UUID string
     * @param screenId Screen where the zone is located
     * @param virtualDesktop Virtual desktop number (1-based, 0 = all)
     */
    void assignWindowToZone(const QString& windowId, const QString& zoneId, const QString& screenId,
                            int virtualDesktop);

    /**
     * @brief Assign a window to multiple zones (multi-zone snap)
     * @param windowId Full window ID
     * @param zoneIds List of zone UUID strings (first is primary)
     * @param screenId Screen where the zones are located
     * @param virtualDesktop Virtual desktop number (1-based, 0 = all)
     */
    void assignWindowToZones(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                             int virtualDesktop);

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
    // Pre-Tile Geometry Storage (unified snap + autotile)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Stored pre-tile geometry with screen context
     *
     * Tracks both the geometry and the screen where it was captured, so that
     * cross-screen restores can detect coordinate mismatch and adjust.
     */
    struct PreTileGeometry
    {
        QRect geometry;
        QString connectorName; ///< connector name at time of save (may be empty for legacy entries)
    };

    /**
     * @brief Store geometry before tiling (snap or autotile)
     * @param windowId Full window ID
     * @param geometry Window geometry before tiling
     * @param connectorName Screen connector name where the window currently is
     * @param overwrite If false (snap mode), skip if entry already exists (first-only).
     *                  If true (autotile mode), always overwrite.
     */
    void storePreTileGeometry(const QString& windowId, const QRect& geometry, const QString& connectorName = QString(),
                              bool overwrite = false);

    /**
     * @brief Get stored pre-tile geometry
     * @param windowId Full window ID
     * @return Geometry if stored, nullopt otherwise
     */
    std::optional<QRect> preTileGeometry(const QString& windowId) const;

    /**
     * @brief Check if window has stored pre-tile geometry
     */
    bool hasPreTileGeometry(const QString& windowId) const;

    /**
     * @brief Clear stored pre-tile geometry (after restore)
     */
    void clearPreTileGeometry(const QString& windowId);

    /**
     * @brief Get validated pre-tile geometry within screen bounds
     * @param windowId Full window ID
     * @param currentScreenName Screen where the window currently is (for cross-screen adjustment).
     *        If empty, uses existing isGeometryOnScreen/adjustGeometryToScreen logic.
     * @return Adjusted geometry within visible screens, nullopt if not found
     */
    std::optional<QRect> validatedPreTileGeometry(const QString& windowId,
                                                  const QString& currentScreenName = QString()) const;

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
     * @brief Mark a window's float as originating from autotile mode
     *
     * Used to distinguish autotile-originated floats from manual snapping-mode
     * floats. Only autotile floats are cleared on autotile→snapping transitions;
     * manual floats are preserved. Must be called AFTER setWindowFloating(true).
     */
    void markAutotileFloated(const QString& windowId);

    /**
     * @brief Clear autotile-floated origin for a window
     */
    void clearAutotileFloated(const QString& windowId);

    /**
     * @brief Check if a window was floated by the autotile engine (not manually in snapping mode)
     */
    bool isAutotileFloated(const QString& windowId) const;

    /**
     * @brief Save a snap-mode floating window for later restoration
     *
     * Called when a snap-mode-floated window enters autotile and its WTS
     * floating state is cleared. Mirrors the engine's m_savedFloatingWindows.
     */
    void saveSnapFloating(const QString& windowId);

    /**
     * @brief Consume and restore a saved snap-mode floating window
     * @return true if the window was in the saved set
     */
    bool restoreSnapFloating(const QString& windowId);

    /**
     * @brief Clear all saved snap-mode floating state
     */
    void clearSavedSnapFloating();

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

    /**
     * @brief Clear floating state when snapping a floating window
     *
     * Atomically clears floating flag and pre-float zone data.
     * Shared logic used by both SnapEngine and WindowTrackingAdaptor
     * to avoid duplicating the isFloating → clear → clearPreFloat pattern.
     *
     * @param windowId Window identifier
     * @return true if the window was floating (caller should emit windowFloatingChanged)
     */
    bool clearFloatingForSnap(const QString& windowId);

    /**
     * @brief Resolve unfloat geometry for a floating window
     *
     * Shared logic: get pre-float zones → validate saved screen →
     * compute single/multi-zone geometry → return result.
     * Used by both SnapEngine::unfloatToZone() and WTA::calculateUnfloatRestore().
     *
     * @param windowId Full window ID
     * @param fallbackScreen Screen to use if saved screen no longer exists
     * @return UnfloatResult with geometry and zone info, or {found=false}
     */
    UnfloatResult resolveUnfloatGeometry(const QString& windowId, const QString& fallbackScreen) const;

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
    SnapResult calculateSnapToAppRule(const QString& windowId, const QString& windowScreenName, bool isSticky) const;

    /**
     * @brief Calculate snap result for new window to last used zone
     * @param windowId Full window ID
     * @param windowScreenId Screen where window currently is
     * @param isSticky Whether window is on all desktops
     * @return SnapResult with geometry and zone info
     */
    SnapResult calculateSnapToLastZone(const QString& windowId, const QString& windowScreenId, bool isSticky) const;

    /**
     * @brief Calculate snap result for new window to first empty zone (auto-assign)
     * @param windowId Full window ID
     * @param windowScreenId Screen where window currently is
     * @param isSticky Whether window is on all desktops
     * @return SnapResult with geometry and zone info
     */
    SnapResult calculateSnapToEmptyZone(const QString& windowId, const QString& windowScreenId, bool isSticky) const;

    /**
     * @brief Calculate snap result to restore from persisted session
     * @param windowId Full window ID
     * @param screenId Screen for geometry calculation
     * @param isSticky Whether window is on all desktops
     * @return SnapResult with geometry and zone info
     */
    SnapResult calculateRestoreFromSession(const QString& windowId, const QString& screenId, bool isSticky) const;

    /**
     * @brief Record that a window class was user-snapped
     * @param windowId Full window ID to extract class from
     * @param wasUserInitiated true if user-initiated snap
     */
    void recordSnapIntent(const QString& windowId, bool wasUserInitiated);

    /**
     * @brief Get last used zone ID
     */
    QString lastUsedZoneId() const
    {
        return m_lastUsedZoneId;
    }

    /**
     * @brief Update last used zone tracking
     */
    void updateLastUsedZone(const QString& zoneId, const QString& screenId, const QString& windowClass,
                            int virtualDesktop);

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
     * @brief Mark a window as reported by the effect (confirmed live)
     *
     * Called when the effect reports a window via windowOpened/resolveWindowRestore.
     * Used by calculateRestoreFromSession's sibling check to distinguish live windows
     * (daemon-only restart) from stale config entries (KWin restart where UUIDs changed).
     */
    void markWindowReported(const QString& windowId);

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
     * @param screenId Screen to find layout for (empty = active layout)
     * @return Zone ID or empty string if all occupied
     */
    QString findEmptyZone(const QString& screenId = QString()) const;

    /**
     * @brief Get JSON array of all empty zones for Snap Assist continuation
     * @param screenId Screen to find layout for (e.g. DP-1)
     * @return JSON array of {zoneId, x, y, width, height, borderWidth, borderRadius} in overlay coordinates
     */
    QString getEmptyZonesJson(const QString& screenId) const;

    /**
     * @brief Get geometry for a zone on a specific screen
     * @param zoneId Zone UUID string
     * @param screenId Screen identifier (empty = primary)
     * @return Zone geometry in pixels, or invalid QRect if not found
     */
    QRect zoneGeometry(const QString& zoneId, const QString& screenId = QString()) const;

    /**
     * @brief Get combined geometry for multiple zones on a specific screen
     * @param zoneIds List of zone UUID strings
     * @param screenId Screen identifier (empty = primary)
     * @return Union of all zone geometries, or invalid QRect if none found
     */
    QRect multiZoneGeometry(const QStringList& zoneIds, const QString& screenId = QString()) const;

    /**
     * @brief Calculate rotation data for windows on a specific screen
     * @param clockwise true for clockwise rotation
     * @param screenFilter When non-empty, only rotate windows on this screen
     * @return List of rotation entries
     */
    QVector<RotationEntry> calculateRotation(bool clockwise, const QString& screenFilter = QString()) const;

    /**
     * @brief Calculate snap assignments for all unsnapped windows
     * @param windowIds List of unsnapped window IDs (from KWin effect)
     * @param screenId Screen for layout/geometry resolution
     * @return List of RotationEntry with target zone assignments
     *
     * Assigns windows to zones in zone-number order, skipping already-occupied
     * zones. If more windows than zones, extra windows are left unassigned.
     */
    QVector<RotationEntry> calculateSnapAllWindows(const QStringList& windowIds, const QString& screenId) const;

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

    /**
     * @brief Populate the resnap buffer for all screens independently.
     *
     * For each window, looks up its current zone assignment and determines
     * the zone position using a global zoneId→position map built from all
     * loaded layouts. This avoids relying on the global activeLayout/previousLayout
     * which only tracks one layout at a time.
     *
     * Used by the KCM save path where multiple screens can have different
     * layout assignments changed simultaneously.
     *
     * @param excludeScreens Screens to skip (e.g. autotile screens handled separately)
     */
    void populateResnapBufferForAllScreens(const QSet<QString>& excludeScreens = {});

    /**
     * @brief Calculate resnap data from current zone assignments
     *
     * Used when restoring windows after autotile toggle-off: the autotile engine
     * repositioned windows, but m_windowZoneAssignments still holds the pre-autotile
     * zone assignments. This method computes zone geometries for those assignments
     * so windows can be moved back to their zone positions.
     *
     * @param screenFilter When non-empty, only include windows on this screen
     * @return List of rotation entries for KWin to apply
     */
    QVector<RotationEntry> calculateResnapFromCurrentAssignments(const QString& screenFilter = QString()) const;

    /**
     * @brief Calculate resnap data from an explicit autotile window order
     *
     * Maps autotile position to manual zone number: windowOrder[0] → zone 1,
     * [1] → zone 2, etc. If there are more windows than zones, excess windows
     * are not resnapped (they stay where they are) — no cycling.
     * Uses the current (manual) layout's zones for geometry calculation.
     *
     * @param autotileWindowOrder Ordered list of window IDs from autotile (master first)
     * @param screenId Screen for layout/geometry resolution
     * @return List of rotation entries for KWin to apply
     */
    QVector<RotationEntry> calculateResnapFromAutotileOrder(const QStringList& autotileWindowOrder,
                                                            const QString& screenId) const;

    /**
     * @brief Build a zone-ordered window list for a screen from current zone assignments
     *
     * Iterates all window-zone assignments for the given screen, resolves each window's
     * primary zone number from the active layout, and returns the window IDs sorted by
     * zone number ascending. Used to pre-seed autotile window order during transitions.
     *
     * @param screenId Screen identifier to filter windows by
     * @return Window IDs sorted by zone number ascending
     */
    QStringList buildZoneOrderedWindowList(const QString& screenId) const;

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
    const QHash<QString, QStringList>& zoneAssignments() const
    {
        return m_windowZoneAssignments;
    }

    /**
     * @brief Get all screen assignments for persistence
     */
    const QHash<QString, QString>& screenAssignments() const
    {
        return m_windowScreenAssignments;
    }

    /**
     * @brief Get all desktop assignments for persistence
     */
    const QHash<QString, int>& desktopAssignments() const
    {
        return m_windowDesktopAssignments;
    }

    /**
     * @brief Get all pre-tile geometries for persistence
     */
    const QHash<QString, PreTileGeometry>& preTileGeometries() const
    {
        return m_preTileGeometries;
    }

    /**
     * @brief Pending restore entry for a single window instance
     *
     * Stores all data needed to restore a window to its previous zone.
     * Multiple entries per appId support multi-instance apps (e.g. 3 Konsole windows).
     */
    struct PendingRestore
    {
        QStringList zoneIds;
        QString screenId;
        int virtualDesktop = 0;
        QString layoutId;
        QList<int> zoneNumbers;
    };

    /**
     * @brief Get pending restore queues (consumption queue: appId -> list of pending restores)
     */
    const QHash<QString, QList<PendingRestore>>& pendingRestoreQueues() const
    {
        return m_pendingRestoreQueues;
    }

    /**
     * @brief Get user-snapped classes
     */
    const QSet<QString>& userSnappedClasses() const
    {
        return m_userSnappedClasses;
    }

    /**
     * @brief Set active zone/screen/desktop assignments (loaded from KConfig by adaptor)
     *
     * Used to restore exact window-to-zone mappings after daemon-only restart
     * (KWin still running, so internalId UUIDs are stable). Prevents wrong-instance
     * restore for multi-instance apps (e.g. 2 Ghostty windows, only 1 was snapped).
     */
    void setActiveAssignments(const QHash<QString, QStringList>& zones, const QHash<QString, QString>& screens,
                              const QHash<QString, int>& desktops)
    {
        m_windowZoneAssignments = zones;
        m_windowScreenAssignments = screens;
        m_windowDesktopAssignments = desktops;
    }

    /**
     * @brief Set pending restore queues (loaded from KConfig by adaptor)
     */
    void setPendingRestoreQueues(const QHash<QString, QList<PendingRestore>>& queues)
    {
        m_pendingRestoreQueues = queues;
    }

    /**
     * @brief Set pre-tile geometries (loaded from KConfig by adaptor)
     */
    void setPreTileGeometries(const QHash<QString, PreTileGeometry>& geometries)
    {
        m_preTileGeometries = geometries;
    }

    /**
     * @brief Set user-snapped classes (loaded from KConfig by adaptor)
     */
    void setUserSnappedClasses(const QSet<QString>& classes)
    {
        m_userSnappedClasses = classes;
    }

    /**
     * @brief Set last used zone info (loaded from KConfig by adaptor)
     */
    void setLastUsedZone(const QString& zoneId, const QString& screenId, const QString& zoneClass, int desktop);

    /**
     * @brief Set floating windows (loaded from KConfig by adaptor)
     */
    void setFloatingWindows(const QSet<QString>& windows)
    {
        m_floatingWindows = windows;
    }

    /**
     * @brief Get pre-float zone assignments for persistence
     */
    const QHash<QString, QStringList>& preFloatZoneAssignments() const
    {
        return m_preFloatZoneAssignments;
    }
    const QHash<QString, QString>& preFloatScreenAssignments() const
    {
        return m_preFloatScreenAssignments;
    }

    /**
     * @brief Set pre-float zone assignments (loaded from KConfig by adaptor)
     */
    void setPreFloatZoneAssignments(const QHash<QString, QStringList>& assignments)
    {
        m_preFloatZoneAssignments = assignments;
    }
    void setPreFloatScreenAssignments(const QHash<QString, QString>& assignments)
    {
        m_preFloatScreenAssignments = assignments;
    }

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
    QString findEmptyZoneInLayout(Layout* layout, const QString& screenId) const;

public:
    /// Build set of occupied zone UUIDs, optionally filtered by screen.
    /// Uses Utils::screensMatch() for format-agnostic screen comparison.
    QSet<QUuid> buildOccupiedZoneSet(const QString& screenFilter = QString()) const;

private:
    // Dependencies
    LayoutManager* m_layoutManager;
    IZoneDetector* m_zoneDetector;
    ISettings* m_settings;
    VirtualDesktopManager* m_virtualDesktopManager;

    // Zone assignments: windowId -> zoneIds (supports multi-zone snap)
    QHash<QString, QStringList> m_windowZoneAssignments;
    // Screen tracking: windowId -> screenId
    QHash<QString, QString> m_windowScreenAssignments;
    // Desktop tracking: windowId -> virtual desktop
    QHash<QString, int> m_windowDesktopAssignments;

    // Pre-tile geometries (unified snap + autotile): full windowId + appId at runtime,
    // appId only for session-restored entries. Converted on window close for persistence.
    // Each entry includes the screen name where the geometry was captured, enabling
    // cross-screen restore to detect coordinate mismatch and center on the target screen.
    QHash<QString, PreTileGeometry> m_preTileGeometries;

    // Last used zone tracking
    QString m_lastUsedZoneId;
    QString m_lastUsedScreenId;
    QString m_lastUsedZoneClass;
    int m_lastUsedDesktop = 0;

    // Floating windows: full windowId at runtime, appId for session-restored entries
    // Converted from windowId to appId on window close for persistence
    QSet<QString> m_floatingWindows;

    // Subset of m_floatingWindows: windows whose float originated from autotile mode.
    // NOT persisted — on session restore all floats are treated as manual-mode.
    // Used to distinguish autotile floats from manual snapping-mode floats during
    // mode transitions (only autotile floats are cleared on autotile→snapping).
    QSet<QString> m_autotileFloatedWindows;

    // Saved snap-mode floating windows. When a snap-mode-floated window enters
    // autotile and its WTS floating is cleared, we save it here so it can be
    // restored when returning to snap mode. Mirrors the engine's m_savedFloatingWindows.
    QSet<QString> m_savedSnapFloatingWindows;

    // Session persistence: consumption queue (appId -> list of pending restores, consumed FIFO)
    QHash<QString, QList<PendingRestore>> m_pendingRestoreQueues;

    // Pre-float zone and screen (for unfloat restore to correct monitor).
    // Keyed by full windowId at runtime (to distinguish multiple instances of
    // the same app). Converted to appId on window close and session save.
    QHash<QString, QStringList> m_preFloatZoneAssignments;
    QHash<QString, QString> m_preFloatScreenAssignments;

    // User-snapped classes (for auto-snap eligibility)
    QSet<QString> m_userSnappedClasses;

    // Sticky window states
    QHash<QString, bool> m_windowStickyStates;

    // Auto-snapped windows (to avoid updating last-used zone)
    QSet<QString> m_autoSnappedWindows;

    // Windows confirmed as live by the effect (runtime only, not persisted).
    // Used by the sibling check in calculateRestoreFromSession to distinguish
    // live siblings (daemon-only restart) from stale config entries (KWin restart).
    QSet<QString> m_effectReportedWindows;

    // Resnap buffer: when layout changes, store (windowId, zonePosition, screenId, vd)
    // for windows that were in the previous layout, so resnapToNewLayout can map them
    struct ResnapEntry
    {
        QString windowId;
        int zonePosition; // Primary zone: 1-based position in sorted-by-zoneNumber order
        QList<int> allZonePositions; // All zones (for multi-zone windows); empty = single-zone
        QString screenId; // Stable EDID-based screen identifier (or connector name fallback)
        int virtualDesktop = 0;
    };
    QVector<ResnapEntry> m_resnapBuffer;

    // Note: No save timer - persistence handled by WindowTrackingAdaptor via KConfig
    // Service emits stateChanged() signal when state needs saving
};

} // namespace PlasmaZones
