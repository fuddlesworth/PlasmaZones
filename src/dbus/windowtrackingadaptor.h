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
class Layout;
class Zone;
class IZoneDetector;
class ISettings;
class VirtualDesktopManager;
class AutoTileService;

/**
 * @brief D-Bus adaptor for window-zone tracking
 *
 * Provides D-Bus interface: org.plasmazones.WindowTracking
 *  Window-zone assignment tracking
 */
class PLASMAZONES_EXPORT WindowTrackingAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.WindowTracking")

public:
    explicit WindowTrackingAdaptor(LayoutManager* layoutManager, IZoneDetector* zoneDetector, ISettings* settings,
                                   VirtualDesktopManager* virtualDesktopManager, QObject* parent = nullptr);
    ~WindowTrackingAdaptor() override = default;

    /**
     * @brief Set the auto-tile service for dynamic layout management (#108, #106, #107)
     * @param service AutoTileService instance (owned by Daemon, not this adaptor)
     */
    void setAutoTileService(AutoTileService* service) { m_autoTileService = service; }

    /**
     * @brief Emit the autoTileGeometriesChanged D-Bus signal
     * @param screenName Screen where geometries changed
     * @param assignmentsJson JSON array of window assignments
     */
    void emitAutoTileGeometriesChanged(const QString& screenName, const QJsonArray& assignments);

    /**
     * @brief Last screen reported by the KWin effect's windowActivated call
     *
     * The KWin effect has reliable screen info on both X11 and Wayland.
     * Use this as a fallback when cursor screen is unavailable.
     */
    QString lastActiveScreenName() const { return m_lastActiveScreenName; }

    /**
     * @brief Last screen the cursor was on, reported by the KWin effect
     *
     * Updated whenever the cursor crosses to a different monitor.
     * This is the primary source for shortcut screen detection on Wayland,
     * since QCursor::pos() is unreliable for background daemons.
     */
    QString lastCursorScreenName() const { return m_lastCursorScreenName; }

public Q_SLOTS:
    // Window snapping notifications (from KWin script)
    void windowSnapped(const QString& windowId, const QString& zoneId, const QString& screenName);
    void windowSnappedMultiZone(const QString& windowId, const QStringList& zoneIds, const QString& screenName);
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
     * Calculate unfloat restore geometry and zone IDs in a single call.
     * Returns JSON: {"found":true/false, "zoneIds":["..."], "x":N, "y":N, "width":N, "height":N}
     * If found is false, the window had no pre-float zone.
     * Supports multi-zone: if the window was snapped to multiple zones before floating,
     * the geometry will be the combined (united) geometry of all zones.
     * @param windowId Window ID from the effect
     * @param screenName Screen name for geometry calculation
     * @return JSON string with restore info
     */
    QString calculateUnfloatRestore(const QString& windowId, const QString& screenName);

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
     * Notify daemon that a window was activated/focused
     * @param windowId Window identifier from KWin
     * @param screenName Screen where the window is located
     */
    void windowActivated(const QString& windowId, const QString& screenName);

    /**
     * Update cursor screen when cursor crosses to a different monitor
     * Called by the KWin effect's slotMouseChanged when screen changes.
     * @param screenName Name of the screen the cursor is now on
     */
    void cursorScreenChanged(const QString& screenName);

    /**
     * Report navigation feedback from KWin effect (D-Bus method)
     * @param success Whether the navigation succeeded
     * @param action Action attempted (e.g., "move", "focus", "swap")
     * @param reason Failure reason if !success
     * @param sourceZoneId Source zone ID for OSD highlighting (optional)
     * @param targetZoneId Target zone ID for OSD highlighting (optional)
     * @param screenName Screen name where navigation occurred (for OSD placement)
     * @note This method is called by KWin effect to report navigation results.
     *       It emits the Qt navigationFeedback signal which triggers the OSD.
     */
    void reportNavigationFeedback(bool success, const QString& action, const QString& reason,
                                  const QString& sourceZoneId, const QString& targetZoneId,
                                  const QString& screenName);

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
     * Snap a window to its app-rule-defined zone (highest priority auto-snap)
     * @param windowId Full window ID (including pointer address)
     * @param windowScreenName Screen name for geometry calculation
     * @param sticky Whether window is on all desktops
     * @param snapX Output: X position to snap to
     * @param snapY Output: Y position to snap to
     * @param snapWidth Output: Width to snap to
     * @param snapHeight Output: Height to snap to
     * @param shouldSnap Output: True if an app rule matched and window should be snapped
     */
    void snapToAppRule(const QString& windowId, const QString& windowScreenName, bool sticky,
                       int& snapX, int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap);

    void snapToEmptyZone(const QString& windowId, const QString& windowScreenName, bool sticky,
                         int& snapX, int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap);

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
     * @param screenName Screen to find layout/geometry for (empty = active layout)
     * @note Emits moveWindowToZoneRequested signal for KWin script to handle
     */
    void pushToEmptyZone(const QString& screenName = QString());

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
     * @param screenName Screen to resolve layout for (empty = active layout)
     * @note Finds zone with matching zoneNumber property in the screen's layout and snaps window to it
     */
    void snapToZoneByNumber(int zoneNumber, const QString& screenName = QString());

    /**
     * @brief Rotate windows in the layout for a specific screen
     * @param clockwise true for clockwise rotation, false for counterclockwise
     * @param screenName Screen to rotate on (empty = all screens)
     * @note Windows in zone N move to zone N+1 (clockwise) or N-1 (counterclockwise)
     * @note Last zone wraps around to first zone and vice versa
     * @note Emits rotateWindowsRequested signal for KWin effect to handle
     */
    void rotateWindowsInLayout(bool clockwise, const QString& screenName = QString());

    /**
     * @brief Cycle focus between windows stacked in the same zone
     * @param forward true to cycle to next window, false to cycle to previous
     * @note This is useful for monocle-style workflows where multiple windows are snapped
     *       to the same zone and the user wants to cycle through them without using Alt+Tab
     * @note Emits cycleWindowsInZoneRequested signal for KWin effect to handle
     */
    void cycleWindowsInZone(bool forward);

    /**
     * @brief Resnap all windows from the previous layout to the current layout
     *
     * When switching layouts (e.g. A -> B), windows that were snapped to layout A
     * are remapped to layout B by zone number: 1->1, 2->2, etc. If the new layout
     * has fewer zones, cycles: e.g. 5 zones -> 3 zones means zone 4->1, 5->2.
     *
     * @note Only works if layout was switched recently; buffers windows on layout change.
     * @note Emits resnapToNewLayoutRequested signal for KWin effect to handle
     */
    void resnapToNewLayout();

    /**
     * @brief Calculate snap assignments for all provided windows
     * @param windowIds List of unsnapped window IDs
     * @param screenName Screen for layout/geometry resolution
     * @return JSON array [{windowId, targetZoneId, x, y, width, height}, ...]
     * @note Called by KWin effect after collecting unsnapped windows
     */
    QString calculateSnapAllWindows(const QStringList& windowIds, const QString& screenName);

    /**
     * @brief Trigger snap-all-windows from daemon shortcut
     * @param screenName Screen where cursor is located
     * @note Emits snapAllWindowsRequested signal to KWin effect
     */
    void snapAllWindows(const QString& screenName);

    // ═══════════════════════════════════════════════════════════════════════════
    // Auto-Tile Methods (#108, #106, #107)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Handle a window being opened on a dynamic layout screen
     *
     * Called by the KWin effect BEFORE the existing auto-snap chain.
     * If the screen's layout is Dynamic, regenerates zones for the new window count
     * and returns geometry for all windows (including the new one).
     *
     * @param windowId Window identifier
     * @param screenName Screen where the window appeared
     * @param isSticky Whether the window is on all desktops
     * @param x Output: X position for this window (if handled)
     * @param y Output: Y position for this window (if handled)
     * @param w Output: Width for this window (if handled)
     * @param h Output: Height for this window (if handled)
     * @param handled Output: True if auto-tile handled this window (Dynamic layout)
     * @param allGeometriesJson Output: JSON array of ALL window assignments for resnapping
     */
    void autoTileWindowOpened(const QString& windowId, const QString& screenName, bool isSticky,
                              int& x, int& y, int& w, int& h,
                              bool& handled, QString& allGeometriesJson);

    /**
     * @brief Notify auto-tile that a window was minimized or restored (#108)
     * @param windowId Window identifier
     * @param screenName Screen the window is on
     * @param minimized True if minimized, false if restored
     */
    void autoTileWindowMinimized(const QString& windowId, const QString& screenName, bool minimized);

    /**
     * @brief Promote the focused window to master position (#106)
     * @param screenName Screen to operate on
     */
    void promoteMasterWindow(const QString& screenName);

    /**
     * @brief Adjust the master area ratio (#107)
     * @param screenName Screen to operate on
     * @param delta Ratio change (positive = increase, negative = decrease)
     */
    void adjustMasterRatio(const QString& screenName, double delta);

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
     * @brief Emitted when a window's floating state changes
     *
     * The KWin effect should listen to this to keep its local floating cache in sync.
     * This is emitted when:
     * - A floating window is snapped (floating cleared automatically)
     * - toggleWindowFloat changes the state
     * - setWindowFloating is called explicitly
     *
     * @param windowId Window identifier (stable ID portion)
     * @param isFloating The new floating state
     */
    void windowFloatingChanged(const QString& windowId, bool isFloating);

    /**
     * @brief Emitted when pending window restores become available
     *
     * This signal is emitted when:
     * 1. The active layout becomes available after startup
     * 2. There are pending zone assignments waiting to be applied
     *
     * The KWin effect should respond by calling restoreToPersistedZone()
     * for all visible windows that haven't yet been tracked.
     *
     * @note This solves startup timing issues where windows appear before
     * the daemon has fully initialized its layout.
     */
    void pendingRestoresAvailable();

    /**
     * @brief Navigation feedback signal for UI/audio feedback
     * @param success Whether the navigation succeeded
     * @param action Action attempted (e.g., "move", "focus", "push", "restore", "float")
     * @param reason Failure reason if !success (e.g., "no_adjacent_zone", "no_empty_zone", "not_snapped")
     * @param sourceZoneId Source zone ID for OSD highlighting
     * @param targetZoneId Target zone ID for OSD highlighting
     * @param screenName Screen name where navigation occurred (for OSD placement)
     */
    void navigationFeedback(bool success, const QString& action, const QString& reason,
                            const QString& sourceZoneId, const QString& targetZoneId,
                            const QString& screenName);

    // Keyboard Navigation signals (for KWin script)
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

    /**
     * @brief Request to cycle focus within the same zone as the currently focused window
     * @param directive Cycle directive (e.g., "cycle:forward", "cycle:backward")
     * @param unused Reserved for future use (currently empty)
     * @note The KWin effect will determine the active window and cycle within its zone
     */
    void cycleWindowsInZoneRequested(const QString& directive, const QString& unused);

    /**
     * @brief Request to resnap windows from previous layout to current layout
     * @param resnapData JSON array of window moves: [{windowId, targetZoneId, x, y, w, h}, ...]
     * @note Same format as rotateWindowsRequested; KWin effect applies geometries and calls windowSnapped
     */
    void resnapToNewLayoutRequested(const QString& resnapData);

    /**
     * @brief Request KWin effect to collect unsnapped windows and snap them all
     * @param screenName Screen to operate on
     */
    void snapAllWindowsRequested(const QString& screenName);

    /**
     * @brief Emitted when auto-tile zones regenerate and all windows need repositioning
     * @param screenName Screen where geometries changed
     * @param geometriesJson JSON array: [{windowId, zoneId, x, y, w, h}, ...]
     */
    void autoTileGeometriesChanged(const QString& screenName, const QString& geometriesJson);

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

    /**
     * @brief Handle panel geometry becoming ready
     *
     * Called when ScreenManager reports panel geometry is known.
     * If there are pending restores waiting for geometry, emits pendingRestoresAvailable.
     */
    void onPanelGeometryReady();

private:
    // ═══════════════════════════════════════════════════════════════════════════════
    // Constants
    // ═══════════════════════════════════════════════════════════════════════════════

    // Minimum visible area for isGeometryOnScreen check (pixels)
    // A window must have at least this much area visible on a screen to be considered "on screen"
    static constexpr int MinVisibleWidth = 100;
    static constexpr int MinVisibleHeight = 100;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Helper Methods - Private
    // ═══════════════════════════════════════════════════════════════════════════════

    /**
     * @brief Get validated active layout with logging
     * @param operation Name of the operation (for logging)
     * @return Pointer to active layout, or nullptr if unavailable
     */
    Layout* getValidatedActiveLayout(const QString& operation) const;

    /**
     * @brief Validate window ID and log warning if empty
     * @param windowId Window ID to validate
     * @param operation Name of the operation (for logging)
     * @return true if windowId is valid, false if empty
     */
    bool validateWindowId(const QString& windowId, const QString& operation) const;

    /**
     * @brief Validate direction parameter and emit feedback if invalid
     * @param direction Direction string to validate
     * @param action Action name for feedback signal
     * @return true if direction is valid, false if empty
     */
    bool validateDirection(const QString& direction, const QString& action);

    /**
     * @brief Convert QRect to JSON geometry string for D-Bus
     * @param rect Rectangle to convert
     * @return JSON string with x, y, width, height
     */
    QString rectToJson(const QRect& rect) const;

    /**
     * @brief Detect which screen a zone is on by finding where its center falls
     * @param zoneId Zone UUID string
     * @return Screen name, or empty string if not determinable
     */
    QString detectScreenForZone(const QString& zoneId) const;

    /**
     * @brief Resolve screen name for a snap operation with 3-tier fallback
     *
     * 1. Caller-provided screenName (from KWin effect)
     * 2. detectScreenForZone auto-detection
     * 3. lastCursorScreenName or lastActiveScreenName
     */
    QString resolveScreenForSnap(const QString& callerScreen, const QString& zoneId) const;

    /**
     * @brief Clear floating state when a window is being snapped
     * @param windowId Window ID being snapped
     */
    void clearFloatingStateForSnap(const QString& windowId);

    // ═══════════════════════════════════════════════════════════════════════════════
    // Screen tracking (from KWin effect's D-Bus calls)
    // ═══════════════════════════════════════════════════════════════════════════════
    QString m_lastActiveWindowId;     // From windowActivated (focused window's ID)
    QString m_lastActiveScreenName;   // From windowActivated (focused window's screen)
    QString m_lastCursorScreenName;   // From cursorScreenChanged (cursor's screen)

    // ═══════════════════════════════════════════════════════════════════════════════
    // Dependencies (kept for signal connections and settings access)
    // ═══════════════════════════════════════════════════════════════════════════════
    LayoutManager* m_layoutManager;
    IZoneDetector* m_zoneDetector;
    ISettings* m_settings;
    VirtualDesktopManager* m_virtualDesktopManager;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Business logic services
    // ═══════════════════════════════════════════════════════════════════════════════
    WindowTrackingService* m_service = nullptr;
    AutoTileService* m_autoTileService = nullptr;  // Set via setAutoTileService(), owned by Daemon

    // ═══════════════════════════════════════════════════════════════════════════════
    // Persistence (adaptor responsibility: KConfig save/load)
    // ═══════════════════════════════════════════════════════════════════════════════
    QTimer* m_saveTimer = nullptr;
    void scheduleSaveState();

    // ═══════════════════════════════════════════════════════════════════════════════
    // Startup timing coordination
    // ═══════════════════════════════════════════════════════════════════════════════

    /**
     * @brief Try to emit pendingRestoresAvailable if conditions are met
     *
     * Conditions required:
     * 1. Layout is available with pending restores
     * 2. Panel geometry has been received by ScreenManager
     *
     * This prevents windows from restoring with incorrect geometry
     * before panel positions are known.
     */
    void tryEmitPendingRestoresAvailable();

    bool m_hasPendingRestores = false;  // True if layout has pending restores waiting
    bool m_pendingRestoresEmitted = false;  // True if we already emitted pendingRestoresAvailable
};

} // namespace PlasmaZones
