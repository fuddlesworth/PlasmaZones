// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "../core/windowtrackingservice.h"
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QRect>
#include <QSet>
#include <QTimer>

namespace PlasmaZones {

class LayoutManager; // Concrete type needed for signal connections
class IZoneDetector;
class ISettings;
class VirtualDesktopManager;

/**
 * @brief D-Bus adaptor for window-zone tracking (SRP)
 *
 * Provides D-Bus interface: org.plasmazones.WindowTracking
 * Single responsibility: Window-zone assignment tracking
 */
class PLASMAZONES_EXPORT WindowTrackingAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.WindowTracking")

public:
    explicit WindowTrackingAdaptor(LayoutManager* layoutManager, IZoneDetector* zoneDetector, ISettings* settings,
                                   VirtualDesktopManager* virtualDesktopManager, QObject* parent = nullptr);
    ~WindowTrackingAdaptor() override = default;

public Q_SLOTS:
    // Window snapping notifications (from KWin script)
    void windowSnapped(const QString& windowId, const QString& zoneId);
    void windowUnsnapped(const QString& windowId);
    /**
     * Record whether a window is sticky (on all virtual desktops).
     * @param windowId Window ID from the effect
     * @param sticky True if window is on all desktops
     */
    void setWindowSticky(const QString& windowId, bool sticky);

    /**
     * Unsnap a window for floating: save its zone to restore on unfloat, then clear assignment.
     * No-op if the window was not snapped (avoids "Window not found for unsnap" when floating
     * a never-snapped window). Use this instead of windowUnsnapped when the unsnap is due to
     * the user toggling float.
     * @param windowId Window ID from the effect
     */
    void windowUnsnappedForFloat(const QString& windowId);

    /**
     * Get the zone to restore to when unfloating (if any).
     * @param windowId Window ID from the effect
     * @param zoneIdOut Output: zone ID to snap to, or empty if none
     * @return true if the window had a zone before it was floated
     */
    bool getPreFloatZone(const QString& windowId, QString& zoneIdOut);

    /**
     * Clear the saved "zone before float" after restoring on unfloat.
     * @param windowId Window ID from the effect
     */
    void clearPreFloatZone(const QString& windowId);

    /**
     * Store window geometry before snapping (for unsnap restoration)
     * @param windowId Window ID
     * @param x Window X position
     * @param y Window Y position
     * @param width Window width
     * @param height Window height
     * @note Only stores on FIRST snap - subsequent snaps (A→B) keep original
     */
    void storePreSnapGeometry(const QString& windowId, int x, int y, int width, int height);

    /**
     * Get stored pre-snap geometry for a window
     * @param windowId Window ID
     * @param x Output: X position (0 if not found)
     * @param y Output: Y position (0 if not found)
     * @param width Output: Width (0 if not found)
     * @param height Output: Height (0 if not found)
     * @return true if geometry was found, false otherwise
     */
    bool getPreSnapGeometry(const QString& windowId, int& x, int& y, int& width, int& height);

    /**
     * Check if a window has stored pre-snap geometry
     * @param windowId Window ID
     * @return true if pre-snap geometry exists for this window
     */
    bool hasPreSnapGeometry(const QString& windowId);

    /**
     * Clear stored pre-snap geometry for a window (called after restore)
     * @param windowId Window ID
     */
    void clearPreSnapGeometry(const QString& windowId);

    /**
     * Clean up all tracking data for a closed window
     * @param windowId Window ID that was closed
     * @note Call this when KWin reports a window has been closed to prevent memory leaks
     */
    void windowClosed(const QString& windowId);

    /**
     * Get validated pre-snap geometry, ensuring it's within visible screen bounds
     * @param windowId Window ID
     * @param x Output: X position (adjusted if off-screen)
     * @param y Output: Y position (adjusted if off-screen)
     * @param width Output: Width (adjusted if off-screen)
     * @param height Output: Height (adjusted if off-screen)
     * @return true if geometry was found and validated, false otherwise
     * @note If original geometry is off-screen, it will be adjusted to fit within
     *       the nearest visible screen while preserving dimensions where possible
     */
    bool getValidatedPreSnapGeometry(const QString& windowId, int& x, int& y, int& width, int& height);

    /**
     * Check if a geometry rectangle is within any visible screen
     * @param x X position
     * @param y Y position
     * @param width Width
     * @param height Height
     * @return true if geometry is fully or partially visible on any screen
     */
    bool isGeometryOnScreen(int x, int y, int width, int height) const;

    // Window tracking queries
    QString getZoneForWindow(const QString& windowId);
    QStringList getMultiZoneForWindow(const QString& windowId);
    QStringList getWindowsInZone(const QString& zoneId);
    QStringList getSnappedWindows();

    /**
     * Get the last zone a window was snapped to
     * @return Zone ID of last used zone, or empty string if none
     */
    QString getLastUsedZoneId();

    /**
     * Snap a new window to the last used zone (for moveNewWindowsToLastZone setting)
     * @param windowId Window to snap
     * @param windowScreenName Screen where the window is currently located (for multi-monitor support)
     * @param snapX Output: X position to snap to
     * @param snapY Output: Y position to snap to
     * @param snapWidth Output: Width to snap to
     * @param snapHeight Output: Height to snap to
     * @param shouldSnap Output: True if window should be snapped
     * @note This checks the moveNewWindowsToLastZone setting internally
     * @note Will NOT snap if window is on a different screen than the last used zone
     *       (prevents cross-monitor snapping bug)
     */
    void snapToLastZone(const QString& windowId, const QString& windowScreenName, bool sticky, int& snapX, int& snapY,
                        int& snapWidth, int& snapHeight, bool& shouldSnap);

    /**
     * Record that a window class was USER-snapped (not auto-snapped)
     * This is used to determine if new windows of this class should be auto-snapped.
     * Only classes that have been explicitly snapped by the user will have their
     * new windows auto-snapped.
     * @param windowId Full window ID to extract class from
     * @param wasUserInitiated True if this snap was user-initiated (drag), false if auto-snap
     */
    void recordSnapIntent(const QString& windowId, bool wasUserInitiated);

    /**
     * Restore a window to its persisted zone from the previous session
     * This uses stable window identifiers (windowClass:resourceName) to match
     * windows across sessions, even though KWin internal IDs change.
     *
     * @param windowId Full window ID (including pointer address)
     * @param screenName Screen name to use for zone geometry calculation
     * @param snapX Output: X position to snap to
     * @param snapY Output: Y position to snap to
     * @param snapWidth Output: Width to snap to
     * @param snapHeight Output: Height to snap to
     * @param shouldRestore Output: True if window should be restored to persisted zone
     * @note This method is called BEFORE snapToLastZone to prioritize session restoration
     */
    void restoreToPersistedZone(const QString& windowId, const QString& screenName, bool sticky, int& snapX, int& snapY,
                                int& snapWidth, int& snapHeight, bool& shouldRestore);

    /**
     * Get updated geometries for all tracked windows (for resolution change handling)
     * @return JSON array of objects: [{windowId, x, y, width, height}, ...]
     * @note Returns empty if keepWindowsInZonesOnResolutionChange is disabled
     */
    QString getUpdatedWindowGeometries();

    // ═══════════════════════════════════════════════════════════════════════════
    // Phase 1 Keyboard Navigation Methods
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Move the focused window to an adjacent zone
     * @param direction Direction to move ("left", "right", "up", "down")
     * @note Emits moveWindowToZoneRequested signal for KWin script to handle
     */
    void moveWindowToAdjacentZone(const QString& direction);

    /**
     * @brief Focus a window in an adjacent zone
     * @param direction Direction to look for windows ("left", "right", "up", "down")
     * @note Emits focusWindowInZoneRequested signal for KWin script to handle
     */
    void focusAdjacentZone(const QString& direction);

    /**
     * @brief Push the focused window to the first empty zone
     * @note Emits moveWindowToZoneRequested signal for KWin script to handle
     */
    void pushToEmptyZone();

    /**
     * @brief Restore the focused window to its original size
     * @note Emits restoreWindowRequested signal for KWin script to handle
     */
    void restoreWindowSize();

    /**
     * @brief Toggle float state for the focused window
     * @note Emits toggleWindowFloatRequested signal for KWin script to handle
     */
    void toggleWindowFloat();

    /**
     * @brief Swap the focused window with the window in an adjacent zone
     * @param direction Direction to swap ("left", "right", "up", "down")
     * @note If target zone is empty, behaves like regular move
     * @note Emits swapWindowsRequested signal for KWin script to handle
     */
    void swapWindowWithAdjacentZone(const QString& direction);

    /**
     * @brief Snap the focused window to a zone by its number
     * @param zoneNumber Zone number (1-9)
     * @note Finds zone with matching zoneNumber property in current layout and snaps window to it
     */
    void snapToZoneByNumber(int zoneNumber);

    /**
     * @brief Rotate all windows in the current layout clockwise or counterclockwise
     * @param clockwise true for clockwise rotation, false for counterclockwise
     * @note Windows in zone N move to zone N+1 (clockwise) or N-1 (counterclockwise)
     * @note Last zone wraps around to first zone and vice versa
     * @note Emits rotateWindowsRequested signal for KWin effect to handle
     */
    void rotateWindowsInLayout(bool clockwise);

    /**
     * @brief Check if a window is temporarily floating (excluded from snapping)
     * @param windowId Window ID
     * @return true if window is floating
     */
    bool isWindowFloating(const QString& windowId);

    /**
     * @brief Query float state for a window (D-Bus callable for effect sync)
     * @param windowId Window ID
     * @return true if window is floating
     */
    bool queryWindowFloating(const QString& windowId);

    /**
     * @brief Set a window's float state
     * @param windowId Window ID
     * @param floating true to float, false to unfloat
     */
    void setWindowFloating(const QString& windowId, bool floating);

    /**
     * @brief Get all floating window IDs (for effect startup sync)
     * @return List of window IDs that are currently floating
     */
    QStringList getFloatingWindows();

    /**
     * @brief Find the first empty zone in the current layout
     * @return Zone ID of first empty zone, or empty string if all occupied
     */
    QString findEmptyZone();

    /**
     * @brief Get geometry for a specific zone ID (uses primary screen)
     * @param zoneId Zone UUID string
     * @return JSON string with x, y, width, height, or empty if not found
     */
    QString getZoneGeometry(const QString& zoneId);

    /**
     * @brief Get geometry for a specific zone ID on a specific screen
     * @param zoneId Zone UUID string
     * @param screenName Screen name (empty = primary screen)
     * @return JSON string with x, y, width, height, or empty if not found
     */
    QString getZoneGeometryForScreen(const QString& zoneId, const QString& screenName);

    /**
     * @brief Save window tracking state to disk
     *
     * Persists all tracked window states including:
     * - Window-zone assignments
     * - Pre-snap geometries
     * - Last used zone/screen
     * - Floating window list
     *
     * Called automatically when state changes. Can also be called
     * explicitly to force a save.
     */
    void saveState();

    /**
     * @brief Load window tracking state from disk
     *
     * Restores previously persisted window tracking state.
     * Called automatically on construction.
     *
     * @note Stale entries (windows that no longer exist) are not
     * automatically cleaned up - they will be removed when the
     * daemon next encounters those window IDs.
     */
    void loadState();

Q_SIGNALS:
    void windowZoneChanged(const QString& windowId, const QString& zoneId);

    /**
     * @brief Navigation feedback signal for UI/audio feedback
     * @param success Whether the navigation succeeded
     * @param action Action attempted (e.g., "move", "focus", "push", "restore", "float")
     * @param reason Failure reason if !success (e.g., "no_adjacent_zone", "no_empty_zone", "not_snapped")
     */
    void navigationFeedback(bool success, const QString& action, const QString& reason);

    // Phase 1 Keyboard Navigation signals (for KWin script)
    /**
     * @brief Request to move a window to a specific zone
     * @param targetZoneId Zone ID to move to
     * @param zoneGeometry JSON geometry {x, y, width, height}
     */
    void moveWindowToZoneRequested(const QString& targetZoneId, const QString& zoneGeometry);

    /**
     * @brief Request to focus a window in a specific zone
     * @param targetZoneId Zone ID containing target window
     * @param windowId Window ID to focus (first window in zone)
     */
    void focusWindowInZoneRequested(const QString& targetZoneId, const QString& windowId);

    /**
     * @brief Request to restore the focused window to its original size
     */
    void restoreWindowRequested();

    /**
     * @brief Request to toggle float state for the focused window
     * @param shouldFloat true to float (exclude), false to unfloat
     */
    void toggleWindowFloatRequested(bool shouldFloat);

    /**
     * @brief Request to swap two windows between zones
     * @param targetZoneId Zone ID containing target window
     * @param targetWindowId Window ID to swap with (may be empty if zone is empty)
     * @param zoneGeometry JSON geometry {x, y, width, height} for the target zone
     */
    void swapWindowsRequested(const QString& targetZoneId, const QString& targetWindowId, const QString& zoneGeometry);

    /**
     * @brief Request to rotate all windows in the layout
     * @param clockwise true for clockwise rotation, false for counterclockwise
     * @param rotationData JSON array of window moves: [{windowId, targetZoneId, x, y, w, h}, ...]
     */
    void rotateWindowsRequested(bool clockwise, const QString& rotationData);

private Q_SLOTS:
    /**
     * @brief Handle layout change by validating zone assignments
     *
     * When the active layout changes, windows may be assigned to zones that
     * no longer exist in the new layout. This slot:
     * 1. Validates all zone assignments against the new layout
     * 2. Removes assignments for zones that no longer exist
     * 3. Emits windowZoneChanged for each removed assignment
     *
     * This prevents stale zone references that cause navigation failures
     * and incorrect "was snapped" detection.
     */
    void onLayoutChanged();

private:
    // Minimum visible area for isGeometryOnScreen check (pixels)
    // A window must have at least this much area visible on a screen to be considered "on screen"
    static constexpr int MinVisibleWidth = 100;
    static constexpr int MinVisibleHeight = 100;

    // Window-zone tracking: windowId -> zoneId (primary zone only)
    QHash<QString, QString> m_windowZoneAssignments;

    // Window-screen tracking: windowId -> screenName (for multi-monitor support)
    // Used by getUpdatedWindowGeometries() to recalculate zone geometry on correct screen
    QHash<QString, QString> m_windowScreenAssignments;

    // Window-desktop tracking: windowId -> virtual desktop (1-based)
    QHash<QString, int> m_windowDesktopAssignments;

    // Pre-snap geometry storage: windowId -> geometry before first snap
    // Used for "restore original size on unsnap" feature
    // Key design: Only stores on FIRST snap, not overwritten when moving A→B
    QHash<QString, QRect> m_preSnapGeometries;

    // Last used zone tracking (for moveNewWindowsToLastZone feature)
    QString m_lastUsedZoneId;
    QString m_lastUsedScreenName; // Track which screen the zone was on
    QString m_lastUsedZoneClass; // Track which class last used the zone
    int m_lastUsedDesktop = 0; // Track which virtual desktop last used the zone

    // Temporarily floating windows (Phase 1 keyboard navigation)
    // These windows are excluded from zone snapping until toggled back
    QSet<QString> m_floatingWindows;

    // Pending zone assignments from previous session (stableId -> zoneId)
    // Used to restore windows to their previous zones after relog
    // Keyed by stable ID (windowClass:resourceName, without pointer address)
    QHash<QString, QString> m_pendingZoneAssignments;
    // Pending screen assignments for closed windows (stableId -> screenName)
    // Used to restore windows to the correct monitor on reopen.
    QHash<QString, QString> m_pendingZoneScreens;
    // Pending desktop assignments for closed windows (stableId -> virtual desktop)
    // Used to restore windows to the correct desktop on reopen.
    QHash<QString, int> m_pendingZoneDesktops;

    // Zone to restore to when unfloating (stableId -> zoneId). Saved when floating a snapped
    // window; cleared when unfloating and restoring, or when the window is closed.
    // Not persisted across daemon restarts.
    QHash<QString, QString> m_preFloatZoneAssignments;

    // Window classes that have been USER-snapped (not auto-snapped)
    // Only windows whose class is in this set will be auto-snapped to last zone
    // This prevents auto-snapping windows that were never manually snapped by the user.
    // Keyed by window class (first part of stable ID, before first colon)
    QSet<QString> m_userSnappedClasses;

    // Sticky window tracking (windowId -> sticky)
    QHash<QString, bool> m_windowStickyStates;

    // Windows that were auto-snapped (restore or moveNewWindowsToLastZone)
    // Used to avoid updating last-used zone from automatic snaps.
    QSet<QString> m_autoSnappedWindows;

    // Dependencies
    LayoutManager* m_layoutManager; // Concrete type for signal connections
    IZoneDetector* m_zoneDetector;
    ISettings* m_settings;
    VirtualDesktopManager* m_virtualDesktopManager;

    // Business logic service (SRP: separates tracking logic from D-Bus interface)
    WindowTrackingService* m_service = nullptr;

    // Debounced save timer to batch multiple state changes
    QTimer* m_saveTimer = nullptr;

    // Schedule a debounced save (batches rapid changes into a single disk write)
    void scheduleSaveState();
};

} // namespace PlasmaZones
