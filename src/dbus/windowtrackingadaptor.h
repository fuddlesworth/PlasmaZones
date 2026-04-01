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
#include <QTimer>
#include <QPointer>

namespace PlasmaZones {

class AutotileEngine;
class LayoutManager; // Concrete type needed for signal connections
class Layout;
class QSettingsConfigBackend;
class Zone;
class IZoneDetector;
class ISettings;
class SnapEngine;
class VirtualDesktopManager;
class ZoneDetectionAdaptor;

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
                                   VirtualDesktopManager* virtualDesktopManager, QObject* parent = nullptr,
                                   QSettingsConfigBackend* configBackend = nullptr);
    ~WindowTrackingAdaptor() override = default;

    /**
     * @brief Last screen reported by the KWin effect's windowActivated call
     *
     * The KWin effect has reliable screen info on both X11 and Wayland.
     * Use this as a fallback when cursor screen is unavailable.
     */
    QString lastActiveScreenName() const
    {
        return m_lastActiveScreenId;
    }

    /**
     * @brief Last screen the cursor was on, reported by the KWin effect
     *
     * Updated whenever the cursor crosses to a different monitor.
     * This is the primary source for shortcut screen detection on Wayland,
     * since QCursor::pos() is unreliable for background daemons.
     */
    QString lastCursorScreenName() const
    {
        return m_lastCursorScreenId;
    }

    /**
     * @brief Set ZoneDetectionAdaptor for daemon-driven navigation (getAdjacentZone, getFirstZoneInDirection)
     * @param adaptor ZoneDetectionAdaptor instance (must outlive this adaptor)
     */
    void setZoneDetectionAdaptor(ZoneDetectionAdaptor* adaptor)
    {
        m_zoneDetectionAdaptor = adaptor;
    }

    /**
     * @brief Set engine references for routing operations per-screen
     *
     * The adaptor routes IWindowEngine operations to the correct engine:
     * AutotileEngine for autotile screens, SnapEngine for manual-zone screens.
     * Both must be set before navigation/float D-Bus calls work.
     *
     * Signal connections from SnapEngine to adaptor D-Bus signals are established here.
     *
     * @param snapEngine SnapEngine instance (not owned, must outlive adaptor)
     * @param autotileEngine AutotileEngine instance (not owned, must outlive adaptor)
     */
    void setEngines(SnapEngine* snapEngine, AutotileEngine* autotileEngine);

    /**
     * @brief Access the underlying WindowTrackingService
     *
     * Used by the daemon to share the single WTS instance with other components
     * (e.g., AutotileEngine) instead of creating duplicate services.
     */
    WindowTrackingService* service() const
    {
        return m_service;
    }

public Q_SLOTS:
    // Window snapping notifications (from KWin script)
    void windowSnapped(const QString& windowId, const QString& zoneId, const QString& screenId);
    void windowSnappedMultiZone(const QString& windowId, const QStringList& zoneIds, const QString& screenId);
    void windowUnsnapped(const QString& windowId);

    /**
     * Notify that a snapped window was dragged without the activation trigger.
     * If the window was tracked as snapped, treat it as a drag-out unsnap:
     * save pre-float zone, mark floating, and clear zone assignment so the
     * window doesn't auto-restore to the zone on close/reopen.
     * @param windowId Window ID from the effect
     */
    void notifyDragOutUnsnap(const QString& windowId);

    /**
     * Batch snap confirmations: process multiple snap/unsnap in one D-Bus call.
     * Used by KWin effect after resnap stagger completes to avoid per-window D-Bus round-trips.
     * @param batchJson JSON array of {windowId, zoneId, screenId, isRestore}
     */
    void windowsSnappedBatch(const QString& batchJson);
    /**
     * Handle window screen change: unsnap only if the new screen differs
     * from the stored assignment (user-initiated move). Programmatic moves
     * (restore/resnap/snap assist) assign the zone first, so the stored
     * screen matches and no unsnap occurs.
     */
    void windowScreenChanged(const QString& windowId, const QString& newScreenId);
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
     * @param screenId Screen ID for geometry calculation
     * @return JSON string with restore info
     */
    QString calculateUnfloatRestore(const QString& windowId, const QString& screenId);

    /**
     * Store window geometry before snapping (for unsnap restoration)
     * @param windowId Window ID
     * @param x Window X position
     * @param y Window Y position
     * @param width Window width
     * @param height Window height
     * @note Only stores on FIRST snap - subsequent snaps (A→B) keep original
     */
    /**
     * Store geometry before tiling (unified snap + autotile)
     * @param windowId Window ID
     * @param x Window X position
     * @param y Window Y position
     * @param width Window width
     * @param height Window height
     * @param overwrite If false (snap mode), skip if entry exists. If true (autotile), always overwrite.
     */
    void storePreTileGeometry(const QString& windowId, int x, int y, int width, int height, const QString& screenId,
                              bool overwrite);

    /**
     * Get stored pre-tile geometry for a window
     * @return true if geometry was found, false otherwise
     */
    bool getPreTileGeometry(const QString& windowId, int& x, int& y, int& width, int& height);

    /**
     * Check if a window has stored pre-tile geometry
     */
    bool hasPreTileGeometry(const QString& windowId);

    /**
     * Clear stored pre-tile geometry for a window (called after restore)
     */
    void clearPreTileGeometry(const QString& windowId);

    /**
     * Get all pre-tile geometries as JSON (for effect pre-population on restart)
     * @return JSON object: {"appId": {"x":N, "y":N, "width":N, "height":N}, ...}
     */
    QString getPreTileGeometriesJson();

    /**
     * Clean up all tracking data for a closed window
     * @param windowId Window ID that was closed
     * @note Call this when KWin reports a window has been closed to prevent memory leaks
     */
    void windowClosed(const QString& windowId);

    /**
     * Notify daemon that a window was activated/focused
     * @param windowId Window identifier from KWin
     * @param screenId Screen where the window is located
     */
    void windowActivated(const QString& windowId, const QString& screenId);

    /**
     * Update cursor screen when cursor crosses to a different monitor
     * Called by the KWin effect's slotMouseChanged when screen changes.
     * @param screenId Name of the screen the cursor is now on
     */
    void cursorScreenChanged(const QString& screenId);

    /**
     * Report navigation feedback from KWin effect (D-Bus method)
     * @param success Whether the navigation succeeded
     * @param action Action attempted (e.g., "move", "focus", "swap")
     * @param reason Failure reason if !success
     * @param sourceZoneId Source zone ID for OSD highlighting (optional)
     * @param targetZoneId Target zone ID for OSD highlighting (optional)
     * @param screenId Screen ID where navigation occurred (for OSD placement)
     * @note This method is called by KWin effect to report navigation results.
     *       It emits the Qt navigationFeedback signal which triggers the OSD.
     */
    void reportNavigationFeedback(bool success, const QString& action, const QString& reason,
                                  const QString& sourceZoneId, const QString& targetZoneId, const QString& screenId);

    /**
     * Get validated pre-tile geometry (pre-snap or pre-autotile), ensuring it's within visible screen bounds
     * @param windowId Window ID
     * @param x Output: X position (adjusted if off-screen)
     * @param y Output: Y position (adjusted if off-screen)
     * @param width Output: Width (adjusted if off-screen)
     * @param height Output: Height (adjusted if off-screen)
     * @return true if geometry was found and validated, false otherwise
     * @note If original geometry is off-screen, it will be adjusted to fit within
     *       the nearest visible screen while preserving dimensions where possible
     */
    bool getValidatedPreTileGeometry(const QString& windowId, int& x, int& y, int& width, int& height);

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
     * Get JSON array of empty zones for Snap Assist continuation
     * @param screenId Screen ID (e.g. DP-1)
     * @return JSON array of {zoneId, x, y, width, height, borderWidth, borderRadius}
     */
    QString getEmptyZonesJson(const QString& screenId);

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
    void snapToLastZone(const QString& windowId, const QString& windowScreenId, bool sticky, int& snapX, int& snapY,
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
    void snapToAppRule(const QString& windowId, const QString& windowScreenName, bool sticky, int& snapX, int& snapY,
                       int& snapWidth, int& snapHeight, bool& shouldSnap);

    void snapToEmptyZone(const QString& windowId, const QString& windowScreenId, bool sticky, int& snapX, int& snapY,
                         int& snapWidth, int& snapHeight, bool& shouldSnap);

    /**
     * Restore a window to its persisted zone from the previous session
     * This uses stable window identifiers (windowClass:resourceName) to match
     * windows across sessions, even though KWin internal IDs change.
     *
     * @param windowId Full window ID (including pointer address)
     * @param screenId Screen ID to use for zone geometry calculation
     * @param snapX Output: X position to snap to
     * @param snapY Output: Y position to snap to
     * @param snapWidth Output: Width to snap to
     * @param snapHeight Output: Height to snap to
     * @param shouldRestore Output: True if window should be restored to persisted zone
     * @note This method is called BEFORE snapToLastZone to prioritize session restoration
     */
    void restoreToPersistedZone(const QString& windowId, const QString& screenId, bool sticky, int& snapX, int& snapY,
                                int& snapWidth, int& snapHeight, bool& shouldRestore);

    /**
     * Run the full 4-level snap-restore fallback chain in one call:
     * appRule → persisted → emptyZone → lastZone.
     * Returns geometry on first match, avoiding multiple sequential D-Bus round-trips.
     *
     * @param windowId Window to restore
     * @param screenId Screen where the window is located
     * @param sticky Whether window is on all desktops
     * @param snapX Output: X position to snap to
     * @param snapY Output: Y position to snap to
     * @param snapWidth Output: Width to snap to
     * @param snapHeight Output: Height to snap to
     * @param shouldSnap Output: True if any strategy matched
     */
    void resolveWindowRestore(const QString& windowId, const QString& screenId, bool sticky, int& snapX, int& snapY,
                              int& snapWidth, int& snapHeight, bool& shouldSnap);

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
     * @brief Move the focused window to an adjacent zone (daemon-driven)
     * @param direction Direction to move ("left", "right", "up", "down")
     * @note Computes geometry internally, emits applyGeometryRequested
     */
    void moveWindowToAdjacentZone(const QString& direction);

    /**
     * @brief Focus a window in an adjacent zone (daemon-driven)
     * @param direction Direction to look for windows ("left", "right", "up", "down")
     * @note Computes target internally, emits activateWindowRequested
     */
    void focusAdjacentZone(const QString& direction);

    /**
     * @brief Push the focused window to the first empty zone (daemon-driven)
     * @param screenId Screen to find layout/geometry for (empty = active layout)
     * @note Computes geometry internally, emits applyGeometryRequested
     */
    void pushToEmptyZone(const QString& screenId = QString());

    /**
     * @brief Restore the focused window to its original size (daemon-driven)
     * @note Computes restore geometry, emits applyGeometryRequested with empty zoneId
     */
    void restoreWindowSize();

    /**
     * @brief Toggle float state for the focused window
     * @note Emits toggleWindowFloatRequested for effect to call toggleFloatForWindow
     */
    void toggleWindowFloat();

    /**
     * @brief Swap the focused window with the window in an adjacent zone (daemon-driven)
     * @param direction Direction to swap ("left", "right", "up", "down")
     * @note If target zone is empty, behaves like regular move
     * @note Computes both geometries, emits applyGeometryRequested for each window
     */
    void swapWindowWithAdjacentZone(const QString& direction);

    /**
     * @brief Snap the focused window to a zone by its number (daemon-driven)
     * @param zoneNumber Zone number (1-9)
     * @param screenId Screen to resolve layout for (empty = active layout)
     * @note Computes geometry internally, emits applyGeometryRequested
     */
    void snapToZoneByNumber(int zoneNumber, const QString& screenId = QString());

    /**
     * @brief Rotate windows in the layout for a specific screen (daemon-driven)
     * @param clockwise true for clockwise rotation, false for counterclockwise
     * @param screenId Screen to rotate on (empty = all screens)
     * @note Handles windowSnapped bookkeeping, emits applyGeometriesBatch
     */
    void rotateWindowsInLayout(bool clockwise, const QString& screenId = QString());

    /**
     * @brief Cycle focus between windows stacked in the same zone (daemon-driven)
     * @param forward true to cycle to next window, false to cycle to previous
     * @note Computes target internally, emits activateWindowRequested
     */
    void cycleWindowsInZone(bool forward);

    /**
     * @brief Resnap all windows from the previous layout to the current layout (daemon-driven)
     *
     * When switching layouts (e.g. A -> B), windows that were snapped to layout A
     * are remapped to layout B by zone number: 1->1, 2->2, etc. If the new layout
     * has fewer zones, cycles: e.g. 5 zones -> 3 zones means zone 4->1, 5->2.
     *
     * @note Handles windowSnapped bookkeeping, emits applyGeometriesBatch
     */
    void resnapToNewLayout();

    /**
     * @brief Resnap windows to their current zone assignments (daemon-driven)
     *
     * Used when restoring windows after autotile toggle-off.
     * @param screenFilter When non-empty, only resnap windows on this screen
     * @note Handles windowSnapped bookkeeping, emits applyGeometriesBatch
     */
    void resnapCurrentAssignments(const QString& screenFilter = QString());

    /**
     * @brief Resnap windows from autotile to manual zones using explicit window order
     *
     * Maps autotile positions to zone numbers: windowOrder[0] → zone 1, etc.
     * Falls back to resnapCurrentAssignments if the order is empty.
     *
     * @param autotileWindowOrder Ordered list from AutotileEngine::tiledWindowOrder()
     * @param screenId Screen for layout/geometry resolution
     */
    void resnapFromAutotileOrder(const QStringList& autotileWindowOrder, const QString& screenId);

    /**
     * @brief Calculate snap assignments for all provided windows
     * @param windowIds List of unsnapped window IDs
     * @param screenId Screen for layout/geometry resolution
     * @return JSON array [{windowId, targetZoneId, x, y, width, height}, ...]
     * @note Called by KWin effect after collecting unsnapped windows
     */
    QString calculateSnapAllWindows(const QStringList& windowIds, const QString& screenId);

    // Daemon-driven navigation: KWin calls with windowId, daemon returns result JSON
    /**
     * @brief Get move target for a window (zone + geometry)
     * @param windowId Window to move
     * @param direction Direction ("left", "right", "up", "down")
     * @param screenId Screen for geometry
     * @return JSON: {success, reason, zoneId, geometryJson, sourceZoneId, screenName}
     */
    QString getMoveTargetForWindow(const QString& windowId, const QString& direction, const QString& screenId);

    /**
     * @brief Get focus target (window to activate) in adjacent zone
     * @param windowId Current focused window
     * @param direction Direction to look
     * @param screenId Screen for zone resolution
     * @return JSON: {success, reason, windowIdToActivate, sourceZoneId, targetZoneId, screenName}
     */
    QString getFocusTargetForWindow(const QString& windowId, const QString& direction, const QString& screenId);

    /**
     * @brief Get restore geometry for a snapped window
     * @param windowId Window to restore
     * @param screenId Screen for validation
     * @return JSON: {success, found, x, y, width, height}
     */
    QString getRestoreForWindow(const QString& windowId, const QString& screenId);

    /**
     * @brief Get cycle target (next/prev window in same zone)
     * @param windowId Current focused window
     * @param forward true for next, false for previous
     * @param screenId Screen for zone resolution
     * @return JSON: {success, reason, windowIdToActivate, zoneId, screenName}
     */
    QString getCycleTargetForWindow(const QString& windowId, bool forward, const QString& screenId);

    /**
     * @brief Get swap target data (both windows' geometries and zone IDs)
     * @param windowId Window to swap
     * @param direction Direction to swap
     * @param screenId Screen for geometry
     * @return JSON: {success, reason, windowId1, x1, y1, w1, h1, zoneId1, windowId2, x2, y2, w2, h2, zoneId2,
     * screenName, sourceZoneId, targetZoneId}
     */
    QString getSwapTargetForWindow(const QString& windowId, const QString& direction, const QString& screenId);

    /**
     * @brief Get push-to-empty-zone target (zone + geometry)
     * @param windowId Window to push
     * @param screenId Screen for layout/geometry
     * @return JSON: {success, reason, zoneId, geometryJson, sourceZoneId, screenName}
     */
    QString getPushTargetForWindow(const QString& windowId, const QString& screenId);

    /**
     * @brief Get snap-to-zone-by-number target (zone + geometry)
     * @param windowId Window to snap
     * @param zoneNumber Zone number (1-9)
     * @param screenId Screen for layout/geometry
     * @return JSON: {success, reason, zoneId, geometryJson, sourceZoneId, screenName}
     */
    QString getSnapToZoneByNumberTarget(const QString& windowId, int zoneNumber, const QString& screenId);

    /**
     * @brief Trigger snap-all-windows from daemon shortcut
     * @param screenId Screen where cursor is located
     * @note Emits snapAllWindowsRequested signal to KWin effect
     */
    void snapAllWindows(const QString& screenId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Phase 1 Convenience Methods (TUI/CLI support)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Convenience: snap a window to a specific zone by ID
     * @param windowId Window to snap
     * @param zoneId Target zone UUID
     * @note Resolves geometry from zone ID, handles windowSnapped bookkeeping,
     *       emits applyGeometryRequested for the compositor bridge
     */
    void moveWindowToZone(const QString& windowId, const QString& zoneId);

    /**
     * @brief Convenience: swap two specific windows by ID
     * @param windowId1 First window
     * @param windowId2 Second window
     * @note Resolves both geometries, handles windowSnapped for both,
     *       emits two applyGeometryRequested signals
     */
    void swapWindowsById(const QString& windowId1, const QString& windowId2);

    /**
     * @brief Get comprehensive state for a single window
     * @param windowId Window to query
     * @return JSON: {windowId, zoneId, screenId, isFloating, isSticky}
     */
    QString getWindowState(const QString& windowId);

    /**
     * @brief Get state for all tracked windows (TUI dashboard)
     * @return JSON array of window state objects
     */
    QString getAllWindowStates();

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
     * @param screenId Screen ID (empty = primary screen)
     * @return JSON string with x, y, width, height, or empty if not found
     */
    QString getZoneGeometryForScreen(const QString& zoneId, const QString& screenId);

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
     * @brief Flush window tracking state to disk on daemon shutdown
     *
     * Stops the debounced save timer and immediately persists state.
     * Call this from Daemon::stop() so snapped windows are saved before exit.
     */
    void saveStateOnShutdown();

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

    /**
     * @brief Emit reapplyWindowGeometriesRequested (called by daemon after geometry settles)
     *
     * Not a D-Bus method; used internally so the daemon timer can trigger the signal.
     */
    void requestReapplyWindowGeometries();

    /**
     * @brief Process a batch of resnap entries: bookkeeping + emit applyGeometriesBatch
     *
     * Called by SnapEngine::emitBatchedResnap (via SnapAdaptor relay) for the
     * autotile→snapping transition. The Daemon layer calls emitBatchedResnap
     * directly on the SnapEngine, bypassing the WTA's navigation methods.
     *
     * @param resnapData Serialized RotationEntry JSON array
     */
    void handleBatchedResnap(const QString& resnapData);

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
    void windowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId);

    /**
     * @brief Unified window state change stream
     * @param windowId Window whose state changed
     * @param stateJson JSON: {windowId, zoneId, screenId, isFloating, changeType}
     *        changeType: "snapped", "unsnapped", "floated", "unfloated", "screen_changed"
     */
    void windowStateChanged(const QString& windowId, const QString& stateJson);

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
     * @brief Request that the KWin effect re-apply window geometries from zone positions
     *
     * Emitted after panel geometry has settled (e.g. after closing the KDE panel editor)
     * so the effect fetches getUpdatedWindowGeometries and moves snapped windows to
     * match the current zone rects. Fixes windows that were shifted by Plasma or by
     * an earlier wrong geometry update.
     */
    void reapplyWindowGeometriesRequested();

    /**
     * @brief Navigation feedback signal for UI/audio feedback
     * @param success Whether the navigation succeeded
     * @param action Action attempted (e.g., "move", "focus", "push", "restore", "float")
     * @param reason Failure reason if !success (e.g., "no_adjacent_zone", "no_empty_zone", "not_snapped")
     * @param sourceZoneId Source zone ID for OSD highlighting
     * @param targetZoneId Target zone ID for OSD highlighting
     * @param screenId Screen ID where navigation occurred (for OSD placement)
     */
    void navigationFeedback(bool success, const QString& action, const QString& reason, const QString& sourceZoneId,
                            const QString& targetZoneId, const QString& screenId);

    // Navigation signals (daemon → effect)
    /**
     * @brief Request to toggle float state for the focused window
     * @param shouldFloat true to float (exclude), false to unfloat
     */
    void toggleWindowFloatRequested(bool shouldFloat);

    /**
     * @brief Request KWin effect to collect unsnapped windows and snap them all
     * @param screenId Screen to operate on
     */
    void snapAllWindowsRequested(const QString& screenId);

    /**
     * @brief Request to move a specific window to a zone (e.g. from Snap Assist selection)
     * @param windowId Window identifier to move
     * @param zoneId Target zone UUID
     * @param geometryJson JSON {x, y, width, height} for the zone
     */
    void moveSpecificWindowToZoneRequested(const QString& windowId, const QString& zoneId, const QString& geometryJson);

    /**
     * @brief Daemon requests KWin to apply geometry (daemon-driven flow)
     * @param windowId Window to apply geometry to
     * @param geometryJson JSON {x, y, width, height}
     * @param zoneId Zone to snap to (empty for float restore - do not call windowSnapped)
     * @param screenId Screen for OSD placement
     */
    void applyGeometryRequested(const QString& windowId, const QString& geometryJson, const QString& zoneId,
                                const QString& screenId);

    /**
     * @brief Daemon requests KWin to activate (focus) a window
     * @param windowId Window to activate
     * @note Used by daemon-driven focus/cycle navigation — daemon resolves the target,
     *       effect just calls KWin::effects->activateWindow()
     */
    void activateWindowRequested(const QString& windowId);

    /**
     * @brief Daemon requests KWin to apply geometries for a batch of windows
     * @param batchJson JSON array of [{windowId, x, y, width, height, targetZoneId, sourceZoneId}]
     * @param action Navigation action type ("rotate", "resnap", "snap_all") for feedback
     * @note Daemon handles windowSnapped bookkeeping internally before emitting.
     *       Effect just applies geometry with stagger — no windowsSnappedBatch callback.
     */
    void applyGeometriesBatch(const QString& batchJson, const QString& action);

    /**
     * @brief Daemon requests KWin to raise windows in order (z-order restoration)
     * @param windowIds Ordered list of window IDs (bottom-to-top)
     */
    void raiseWindowsRequested(const QStringList& windowIds);

public Q_SLOTS:
    /**
     * @brief Daemon-driven float toggle. KWin calls with active window; daemon does logic and emits
     * applyGeometryRequested.
     */
    void toggleFloatForWindow(const QString& windowId, const QString& screenId);

    /**
     * @brief Set a window's floating state explicitly (directional, not toggle).
     * Routes to autotile engine for autotile screens, handles snap mode locally.
     * Used by minimize/unminimize, drag-to-float, and monocle unmaximize handlers.
     */
    void setWindowFloatingForScreen(const QString& windowId, const QString& screenId, bool floating);

    /**
     * @brief Apply pre-snap/pre-autotile geometry for a floated window (call from daemon when autotile engine floats)
     * Gets validated geometry, emits applyGeometryRequested if found, clears stored geometry.
     * @return true if geometry was applied, false if none stored
     */
    bool applyGeometryForFloat(const QString& windowId, const QString& screenId);

    /**
     * @brief Emit moveSpecificWindowToZoneRequested - called when user selects from Snap Assist
     */
    void requestMoveSpecificWindowToZone(const QString& windowId, const QString& zoneId, const QString& geometryJson);

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
     * @brief Check if a window is excluded from keyboard shortcut operations
     * @param windowId Full window ID to check
     * @param action Action name for feedback signal (e.g. "move", "swap")
     * @return true if window is excluded (caller should abort), false to proceed
     */
    bool isWindowExcluded(const QString& windowId, const QString& action);

    /**
     * @brief Detect which screen a zone is on by finding where its center falls
     * @param zoneId Zone UUID string
     * @return Screen name, or empty string if not determinable
     */
    QString detectScreenForZone(const QString& zoneId) const;

    /**
     * @brief Resolve screen name for a snap operation with 3-tier fallback
     *
     * 1. Caller-provided screenId (from KWin effect)
     * 2. detectScreenForZone auto-detection
     * 3. lastCursorScreenName or lastActiveScreenName
     */
    QString resolveScreenForSnap(const QString& callerScreen, const QString& zoneId) const;

    /**
     * @brief Apply a successful SnapResult: assign outputs, mark auto-snapped,
     *        clear floating state, and track the zone assignment.
     *
     * Shared by snapToLastZone, snapToAppRule, snapToEmptyZone,
     * restoreToPersistedZone, and resolveWindowRestore to eliminate
     * ~13 lines of identical boilerplate per call site.
     */
    void applySnapResult(const SnapResult& result, const QString& windowId, int& snapX, int& snapY, int& snapWidth,
                         int& snapHeight, bool& shouldSnap);

    /**
     * @brief Clear floating state when a window is being snapped
     * @param windowId Window ID being snapped
     * @param screenId Screen where the snap is occurring (for windowFloatingChanged signal)
     */
    void clearFloatingStateForSnap(const QString& windowId, const QString& screenId);

    // ═══════════════════════════════════════════════════════════════════════════════
    // Screen tracking (from KWin effect's D-Bus calls)
    // ═══════════════════════════════════════════════════════════════════════════════
    QString m_lastActiveWindowId; // From windowActivated (focused window's ID)
    QString m_lastActiveScreenId; // From windowActivated (focused window's screen)
    QString m_lastCursorScreenId; // From cursorScreenChanged (cursor's screen)

    // ═══════════════════════════════════════════════════════════════════════════════
    // Dependencies (kept for signal connections and settings access)
    // ═══════════════════════════════════════════════════════════════════════════════
    ZoneDetectionAdaptor* m_zoneDetectionAdaptor = nullptr;
    LayoutManager* m_layoutManager;
    ISettings* m_settings;
    VirtualDesktopManager* m_virtualDesktopManager;
    QSettingsConfigBackend* m_configBackend = nullptr;

    // Engine references for per-screen routing (set via setEngines())
    // QPointer auto-nulls on engine destruction, guarding against late D-Bus calls
    QPointer<SnapEngine> m_snapEngine;
    QPointer<AutotileEngine> m_autotileEngine;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Business logic service
    // ═══════════════════════════════════════════════════════════════════════════════
    WindowTrackingService* m_service = nullptr;

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

    bool m_hasPendingRestores = false; // True if layout has pending restores waiting
    bool m_pendingRestoresEmitted = false; // True if we already emitted pendingRestoresAvailable
    bool m_shutdownSaveGuard = false; // True after saveStateOnShutdown() to prevent destruction-phase saves
};

} // namespace PlasmaZones
