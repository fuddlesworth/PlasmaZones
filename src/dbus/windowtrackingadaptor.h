// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "../core/windowregistry.h"
#include "../core/windowtrackingservice.h"
#include <PhosphorEngineApi/PlacementEngineBase.h>
#include <PhosphorProtocol/WireTypes.h>
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QJsonArray>
#include <QQueue>
#include <QRect>
#include <QTimer>
#include <QPointer>
#include <functional>
#include <memory>

#include <PhosphorConfig/IBackend.h>

namespace PhosphorZones {
class IZoneDetector;
class Layout;
class Zone;
class LayoutRegistry;
}

namespace PlasmaZones {

using PhosphorProtocol::EmptyZoneList;
using PhosphorProtocol::PreTileGeometryEntry;
using PhosphorProtocol::PreTileGeometryList;
using PhosphorProtocol::WindowGeometryEntry;
using PhosphorProtocol::WindowGeometryList;
using PhosphorProtocol::WindowStateEntry;
using PhosphorProtocol::WindowStateList;
using PhosphorProtocol::ZoneGeometryRect;

class AutotileEngine;
class ScreenModeRouter;
class SnapNavigationTargetResolver;

class PersistenceWorker;
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
    explicit WindowTrackingAdaptor(PhosphorZones::LayoutRegistry* layoutManager,
                                   PhosphorZones::IZoneDetector* zoneDetector,
                                   Phosphor::Screens::ScreenManager* screenManager, ISettings* settings,
                                   VirtualDesktopManager* virtualDesktopManager, QObject* parent = nullptr);
    ~WindowTrackingAdaptor() override;

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
     * @brief Get the last activated window's ID
     */
    QString lastActiveWindowId() const
    {
        return m_lastActiveWindowId;
    }

    /**
     * @brief Set ZoneDetectionAdaptor for daemon-driven navigation (getAdjacentZone, getFirstZoneInDirection)
     * @param adaptor ZoneDetectionAdaptor instance (must outlive this adaptor)
     */
    void setZoneDetectionAdaptor(ZoneDetectionAdaptor* adaptor);

    /**
     * @brief Wire up the compositor-facing WindowRegistry.
     *
     * The registry is populated by the kwin-effect bridge via the new
     * setWindowMetadata() D-Bus method and cleared via the existing
     * windowClosed() path. Consumers (WTS, AutotileEngine, SnapEngine) query
     * it for current appId instead of parsing composite windowId strings.
     *
     * Must be set before start. Not owned.
     *
     * Also forwards the pointer to the underlying WindowTrackingService and
     * subscribes to metadataChanged so we can refresh tracking that mirrors
     * the app class (e.g. last-used-zone class tag).
     */
    void setWindowRegistry(WindowRegistry* registry);

    /**
     * @brief Set engine references for routing operations per-screen
     *
     * The adaptor routes IPlacementEngine operations to the correct engine:
     * AutotileEngine for autotile screens, SnapEngine for manual-zone screens.
     * Both must be set before navigation/float D-Bus calls work.
     *
     * Signal connections from SnapEngine to adaptor D-Bus signals are established here.
     * The snap-specific signal (windowSnapStateChanged) is connected via qobject_cast.
     *
     * @param snapEngine PlacementEngineBase for snap mode (not owned, must outlive adaptor)
     * @param autotileEngine PlacementEngineBase for autotile mode (not owned, must outlive adaptor)
     */
    void setEngines(PhosphorEngineApi::PlacementEngineBase* snapEngine,
                    PhosphorEngineApi::PlacementEngineBase* autotileEngine);

    SnapEngine* snapEngine() const;

    /**
     * @brief Wire the daemon's central ScreenModeRouter.
     *
     * REQUIRED for correct dispatch on window-lifecycle entry points
     * (resolveWindowRestore, resnapCurrentAssignments, etc.) — those
     * methods route through the router instead of direct engine pointer
     * checks so engines can stay pure and the mode lookup has exactly
     * one source of truth.
     *
     * @param router ScreenModeRouter instance (not owned, must outlive adaptor)
     */
    void setScreenModeRouter(ScreenModeRouter* router);

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

    // Note: targetResolver() accessor was deleted in Phase 5E. The
    // SnapNavigationTargetResolver instance now lives on SnapEngine,
    // lazy-constructed via SnapEngine::ensureTargetResolver(). Consumers
    // previously using m_wta->targetResolver() go through SnapEngine
    // directly.

    // resnapForVirtualScreenReconfigure moved to SnapAdaptor.

public Q_SLOTS:
    /**
     * @brief Register or update metadata for a live window.
     *
     * Called by the kwin-effect bridge on window-added and on every mutation
     * of the window's app class (windowClassChanged / desktopFileNameChanged).
     * The @p instanceId is the compositor-supplied stable token (KWin's
     * internalId(); Hyprland's address on a future bridge). It is opaque to
     * the daemon — never parsed.
     *
     * @param instanceId  Opaque compositor handle (stable for window lifetime)
     * @param appId       Current app class (mutable)
     * @param desktopFile Current desktop file name (mutable, may be empty)
     * @param title       Current caption (mutable, may be empty)
     *
     * Emits no D-Bus signal. Populates the daemon's WindowRegistry; consumers
     * subscribe to the registry's Qt signals directly.
     *
     * Safe to call unconditionally on every observation — no-op if metadata
     * is unchanged.
     */
    void setWindowMetadata(const QString& instanceId, const QString& appId, const QString& desktopFile,
                           const QString& title);

    // windowSnapped, windowSnappedMultiZone, windowUnsnapped, windowsSnappedBatch,
    // recordSnapIntent moved to SnapAdaptor (org.plasmazones.Snap D-Bus interface).

    /**
     * Notify that a snapped window was dragged without the activation trigger.
     * If the window was tracked as snapped, treat it as a drag-out unsnap:
     * save pre-float zone, mark floating, and clear zone assignment so the
     * window doesn't auto-restore to the zone on close/reopen.
     * @param windowId Window ID from the effect
     */
    void notifyDragOutUnsnap(const QString& windowId);

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

    // windowUnsnappedForFloat moved to SnapAdaptor (org.plasmazones.Snap D-Bus interface).

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

    // calculateUnfloatRestore moved to SnapAdaptor (org.plasmazones.Snap D-Bus interface).

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
     * Get all pre-tile geometries as a typed list (for effect pre-population on restart).
     * Each entry carries appId, geometry rect, and the screen it was on.
     */
    PlasmaZones::PreTileGeometryList getPreTileGeometries();

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
     * Push current frame geometry for a window into the daemon's shadow.
     *
     * Called by the compositor plugin on windowFrameGeometryChanged (debounced
     * at ~50ms per window). The shadow is read by daemon-local shortcut
     * handlers (float toggle, etc.) so they can compose pre-tile geometry
     * without a round-trip back to the effect.
     *
     * @param windowId Window identifier
     * @param rect Current frame geometry in compositor coordinates
     */
    void setFrameGeometry(const QString& windowId, int x, int y, int width, int height);

    /**
     * Query the daemon's shadow for a window's last-known frame geometry.
     *
     * Returns an invalid QRect if the window has not pushed a geometry yet.
     * Used by daemon-local shortcut handlers.
     */
    QRect frameGeometry(const QString& windowId) const;

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

    /// Remove zone/screen/desktop assignments for windows not in the alive set.
    /// Called by the KWin effect after daemon ready to clean up stale KConfig entries
    /// from windows that no longer exist (closed between save and daemon restart).
    void pruneStaleWindows(const QStringList& aliveWindowIds);

    /**
     * Get typed list of empty zones for Snap Assist continuation
     * @param screenId Screen ID (e.g. DP-1)
     * @return EmptyZoneList of empty zone entries with overlay-local geometry
     */
    PlasmaZones::EmptyZoneList getEmptyZones(const QString& screenId);

    /**
     * Get the last zone a window was snapped to
     * @return PhosphorZones::Zone ID of last used zone, or empty string if none
     */
    QString getLastUsedZoneId();

    // snapToLastZone, recordSnapIntent, snapToAppRule, snapToEmptyZone,
    // restoreToPersistedZone, resolveWindowRestore moved to SnapAdaptor
    // (org.plasmazones.Snap D-Bus interface).

    /**
     * Get updated geometries for all tracked windows (for resolution change handling)
     * @return JSON array of objects: [{windowId, x, y, width, height}, ...]
     * @note Returns empty if keepWindowsInZonesOnResolutionChange is disabled
     */
    PlasmaZones::WindowGeometryList getUpdatedWindowGeometries();

    /**
     * @brief Pre-computed zone geometries for pending restore entries.
     * @return JSON object: { appId: {x, y, width, height}, ... }
     *
     * The effect caches these so that slotWindowAdded can teleport windows
     * to their zone position immediately, without waiting for a D-Bus round-trip.
     */
    QString getPendingRestoreGeometries();

    // moveWindowToAdjacentZone, focusAdjacentZone, swapWindowWithAdjacentZone,
    // pushToEmptyZone, snapToZoneByNumber, cycleWindowsInZone, restoreWindowSize,
    // rotateWindowsInLayout, moveWindowToZone, swapWindowsById moved to SnapAdaptor
    // (org.plasmazones.Snap D-Bus interface).

    /**
     * @brief Get comprehensive state for a single window
     * @param windowId Window to query
     * @return WindowStateEntry with windowId, zoneId, screenId, isFloating, changeType
     */
    PlasmaZones::WindowStateEntry getWindowState(const QString& windowId);

    /**
     * @brief Get state for all tracked windows (TUI dashboard)
     * @return List of WindowStateEntry structs
     */
    PlasmaZones::WindowStateList getAllWindowStates();

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
     * @return PhosphorZones::Zone ID of first empty zone, or empty string if all occupied
     */
    QString findEmptyZone();

    /**
     * @brief Get geometry for a specific zone ID (uses primary screen)
     * @param zoneId PhosphorZones::Zone UUID string
     * @return ZoneGeometryRect with x, y, width, height (all zero if not found)
     */
    PlasmaZones::ZoneGeometryRect getZoneGeometry(const QString& zoneId);

    /**
     * @brief Get geometry for a specific zone ID on a specific screen
     * @param zoneId PhosphorZones::Zone UUID string
     * @param screenId Screen ID (empty = primary screen)
     * @return ZoneGeometryRect with x, y, width, height (all zero if not found)
     */
    PlasmaZones::ZoneGeometryRect getZoneGeometryForScreen(const QString& zoneId, const QString& screenId);

    /// Internal: returns QRect directly (avoids JSON round-trip for daemon-internal callers)
    QRect zoneGeometryRect(const QString& zoneId, const QString& screenId);

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
     * @brief Schedule a debounced save of all tracked state
     *
     * Starts/restarts the 500ms debounce timer. After the timer fires,
     * saveState() is called once. Used by the daemon to trigger saves
     * when autotile state changes (placementChanged signal).
     */
    void scheduleSaveState();

    /**
     * @brief Set tiling state serialization delegates
     *
     * These delegates are called during saveState()/loadState() to include
     * autotile per-screen tiling state alongside window tracking state.
     *
     * @param serializeFn Returns JSON array of per-screen tiling states
     * @param deserializeFn Restores tiling states from JSON array
     */
    // MOC misparses std::function<void(const QJsonArray&)> as function<const void(QJsonArray&)>,
    // generating code that fails to compile. This method is not a signal/slot, but MOC still
    // processes all public member declarations in Q_OBJECT classes. Guard is required.
#ifndef Q_MOC_RUN
    void setTilingStateDelegates(std::function<QJsonArray()> serializeFn,
                                 std::function<void(const QJsonArray&)> deserializeFn);

    /**
     * @brief Set delegates for autotile pending restore queue persistence
     *
     * Separate from tiling state delegates — pending restores have their own
     * config key to keep the window-orders array homogeneous.
     */
    void setTilingPendingRestoreDelegates(std::function<QJsonObject()> serializeFn,
                                          std::function<void(const QJsonObject&)> deserializeFn);
#endif

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

    // handleBatchedResnap moved to SnapAdaptor.

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
     * @param state WindowStateEntry with windowId, zoneId, screenId, isFloating, changeType
     *        changeType: "snapped", "unsnapped", "floated", "unfloated", "screen_changed"
     */
    void windowStateChanged(const QString& windowId, const PlasmaZones::WindowStateEntry& state);

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
     * @brief Request KWin effect to collect unsnapped windows and snap them all
     * @param screenId Screen to operate on
     */
    void snapAllWindowsRequested(const QString& screenId);

    /**
     * @brief Request to move a specific window to a zone (e.g. from Snap Assist selection)
     * @param windowId Window identifier to move
     * @param zoneId Target zone UUID
     * @param x, y, width, height PhosphorZones::Zone geometry
     */
    void moveSpecificWindowToZoneRequested(const QString& windowId, const QString& zoneId, int x, int y, int width,
                                           int height);

    /**
     * @brief Daemon requests KWin to apply geometry (daemon-driven flow)
     * @param windowId Window to apply geometry to
     * @param x Left edge of target geometry
     * @param y Top edge of target geometry
     * @param width Width of target geometry
     * @param height Height of target geometry
     * @param zoneId PhosphorZones::Zone to snap to (empty for float restore - do not call windowSnapped)
     * @param screenId Screen for OSD placement
     * @param sizeOnly When true, only width/height are meaningful (x/y ignored, window stays at current position)
     */
    void applyGeometryRequested(const QString& windowId, int x, int y, int width, int height, const QString& zoneId,
                                const QString& screenId, bool sizeOnly);

    /**
     * @brief Daemon requests KWin to activate (focus) a window
     * @param windowId Window to activate
     * @note Used by daemon-driven focus/cycle navigation — daemon resolves the target,
     *       effect just calls KWin::effects->activateWindow()
     */
    void activateWindowRequested(const QString& windowId);

    /**
     * @brief Daemon requests KWin to apply geometries for a batch of windows
     * @param geometries List of window geometry entries to apply
     * @param action Navigation action type ("rotate", "resnap", "snap_all") for feedback
     * @note Daemon handles windowSnapped bookkeeping internally before emitting.
     *       Effect just applies geometry with stagger — no windowsSnappedBatch callback.
     */
    void applyGeometriesBatch(const PlasmaZones::WindowGeometryList& geometries, const QString& action);

    /**
     * @brief Daemon requests KWin to raise windows in order (z-order restoration)
     * @param windowIds Ordered list of window IDs (bottom-to-top)
     */
    void raiseWindowsRequested(const QStringList& windowIds);

    // toggleFloatForWindow moved to SnapAdaptor (org.plasmazones.Snap D-Bus interface).

public Q_SLOTS:
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
    void requestMoveSpecificWindowToZone(const QString& windowId, const QString& zoneId, const QRect& geometry);

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
     * Called when Phosphor::Screens::ScreenManager reports panel geometry is known.
     * If there are pending restores waiting for geometry, emits pendingRestoresAvailable.
     */
    void onPanelGeometryReady();

public:
    /// Resolve a resnap filter (empty / physical / virtual screen id) into
    /// the concrete list of snap-mode screens the resnap should touch.
    /// Consults the shared ScreenModeRouter to drop autotile screens from
    /// the candidate set — the router lives on WTA so this helper is
    /// exposed publicly so SnapEngine's navigation methods can reuse it.
    QStringList resolveSnapModeScreensForResnap(const QString& screenFilter) const;

    /**
     * @brief Resolve screen name for a snap operation with 3-tier fallback
     *
     * 1. Caller-provided screenId (from KWin effect)
     * 2. detectScreenForZone auto-detection
     * 3. lastCursorScreenName or lastActiveScreenName
     *
     * Public so SnapAdaptor can reuse the zone-center screen detection.
     */
    QString resolveScreenForSnap(const QString& callerScreen, const QString& zoneId) const;

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
     * @brief Detect which screen a zone is on by finding where its center falls
     * @param zoneId PhosphorZones::Zone UUID string
     * @return Screen name, or empty string if not determinable
     */
    QString detectScreenForZone(const QString& zoneId) const;

    // applySnapResult moved to SnapAdaptor.

    /**
     * @brief Build a unified window state JSON object for windowStateChanged emission
     * @param windowId Window identifier
     * @param zoneId Primary zone ID (may be empty for unsnap)
     * @param zoneIds All zone IDs (QJsonArray)
     * @param screenId Screen identifier
     * @param isFloating Current float state
     * @param changeType One of: "snapped", "unsnapped", "floated", "unfloated", "screen_changed"
     * @return QJsonObject ready for serialization
     */
    QJsonObject buildStateObject(const QString& windowId, const QString& zoneId, const QJsonArray& zoneIds,
                                 const QString& screenId, bool isFloating, const QString& changeType) const;

    // clearFloatingStateForSnap was removed — WindowTrackingService::commitSnap
    // now handles floating-state clearing internally (and emits
    // windowFloatingClearedForSnap which the adaptor relays to its own
    // windowFloatingChanged D-Bus signal).

    // ═══════════════════════════════════════════════════════════════════════════════
    // Screen tracking (from KWin effect's D-Bus calls)
    // ═══════════════════════════════════════════════════════════════════════════════
    QString m_lastActiveWindowId; // From windowActivated (focused window's ID)
    QString m_lastActiveScreenId; // From windowActivated (focused window's screen)
    QString m_lastCursorScreenId; // From cursorScreenChanged (cursor's screen)

    // Frame-geometry shadow: populated via setFrameGeometry D-Bus pushes from
    // the compositor plugin. Entries are removed on windowClosed. Used by
    // daemon-local shortcut handlers (float toggle, etc.) so they can read
    // fresh geometry without round-tripping through the effect.
    QHash<QString, QRect> m_frameGeometry;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Dependencies (kept for signal connections and settings access)
    // ═══════════════════════════════════════════════════════════════════════════════
    ZoneDetectionAdaptor* m_zoneDetectionAdaptor = nullptr;
    PhosphorZones::LayoutRegistry* m_layoutManager;
    ISettings* m_settings;
    VirtualDesktopManager* m_virtualDesktopManager;
    std::unique_ptr<PhosphorConfig::IBackend> m_sessionBackend; // Session state (session.json)

    // Engine references for per-screen routing (set via setEngines())
    // QPointer auto-nulls on engine destruction, guarding against late D-Bus calls
    QPointer<PhosphorEngineApi::PlacementEngineBase> m_snapEngine;
    QPointer<PhosphorEngineApi::PlacementEngineBase> m_autotileEngine;
    QPointer<SnapEngine> m_cachedSnapEngine; // Cached qobject_cast result from setEngines()

    // Central dispatcher: adaptor methods route lifecycle / resnap /
    // restore calls through this instead of direct engine pointer checks.
    // Null until setScreenModeRouter is called (Daemon wires during init).
    ScreenModeRouter* m_screenModeRouter = nullptr;

    // Pure-compute helper that owns snap-mode navigation target
    // computation. Constructed eagerly in the adaptor constructor with
    // m_service + m_layoutManager and a feedback callback that forwards
    // into the adaptor's navigationFeedback signal. The zone detector is
    // wired late via setZoneDetectionAdaptor which also pushes it into
    // the resolver. Engine pure: never emits Qt signals directly.
    // Note: SnapNavigationTargetResolver ownership moved to SnapEngine in
    // Phase 5E — see SnapEngine::ensureTargetResolver.

    // ═══════════════════════════════════════════════════════════════════════════════
    // Business logic service
    // ═══════════════════════════════════════════════════════════════════════════════
    WindowTrackingService* m_service = nullptr;

    // Shared registry: compositor-supplied instance id → current metadata.
    // Not owned (daemon root owns it). Populated via setWindowMetadata D-Bus calls
    // and cleared from the windowClosed path.
    QPointer<WindowRegistry> m_windowRegistry;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Persistence (adaptor responsibility: session.json save/load)
    // ═══════════════════════════════════════════════════════════════════════════════
    QTimer* m_saveTimer = nullptr;
    std::unique_ptr<PersistenceWorker> m_persistenceWorker;

    // FIFO queue of dirty masks for writes currently in flight on the
    // persistence worker thread. saveState() enqueues the committed mask
    // when it hands a write off to the worker; the writeCompleted handler
    // dequeues the head in the same FIFO order the worker processes
    // requestWrite signals, so a mask survives even when saveState() is
    // called again before the previous write lands. On success the head
    // is dropped; on failure the head bits are OR'd back into the
    // service's dirty mask so the retry picks them up without stomping
    // on any newer mutations.
    QQueue<WindowTrackingService::DirtyMask> m_pendingWriteMasks;

    // One-shot warning latch for the test-only synchronous fallback path
    // in saveState(). Production always uses PhosphorConfig::JsonBackend + the
    // async worker, so hitting the sync path indicates either a test
    // harness or an unexpected misconfiguration.
    bool m_syncFallbackWarned = false;

    // Tiling state serialization delegates (autotile engine → WTA persistence)
    std::function<QJsonArray()> m_serializeTilingStatesFn;
    std::function<void(const QJsonArray&)> m_deserializeTilingStatesFn;
    std::function<QJsonObject()> m_serializePendingRestoresFn;
    std::function<void(const QJsonObject&)> m_deserializePendingRestoresFn;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Startup timing coordination
    // ═══════════════════════════════════════════════════════════════════════════════

    /**
     * @brief Try to emit pendingRestoresAvailable if conditions are met
     *
     * Conditions required:
     * 1. PhosphorZones::Layout is available with pending restores
     * 2. Panel geometry has been received by Phosphor::Screens::ScreenManager
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
