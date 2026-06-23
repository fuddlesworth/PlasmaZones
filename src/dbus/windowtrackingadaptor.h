// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorSnapEngine/INavigationStateProvider.h>
#include <PhosphorProtocol/AutotileMarshalling.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <PhosphorProtocol/ZoneMarshalling.h>
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QVariantMap>
#include <QJsonArray>
#include <QQueue>
#include <QRect>
#include <QTimer>
#include <QPointer>
#include <functional>
#include <memory>

#include <PhosphorConfig/IBackend.h>

namespace PhosphorContext {
class IContextResolver;
} // namespace PhosphorContext

namespace PhosphorZones {
class IZoneDetector;
class Layout;
class Zone;
class LayoutRegistry;
}

namespace PhosphorTileEngine {
class AutotileEngine;
}

namespace PhosphorSnapEngine {
class SnapEngine;
class SnapNavigationTargetResolver;
}

namespace PhosphorWorkspaces {
class VirtualDesktopManager;
class ActivityManager;
}

namespace PhosphorWindowRules {
class WindowRuleStore;
class RuleEvaluator;
}

namespace PlasmaZones {

class ScreenModeRouter;

class PersistenceWorker;
class ISettings;

class ZoneDetectionAdaptor;

/**
 * @brief D-Bus adaptor for window-zone tracking
 *
 * Provides D-Bus interface: org.plasmazones.WindowTracking
 *  Window-zone assignment tracking
 */
class PLASMAZONES_EXPORT WindowTrackingAdaptor : public QDBusAbstractAdaptor,
                                                 public PhosphorSnapEngine::INavigationStateProvider
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.WindowTracking")

public:
    explicit WindowTrackingAdaptor(PhosphorZones::LayoutRegistry* layoutManager,
                                   PhosphorZones::IZoneDetector* zoneDetector,
                                   PhosphorScreens::ScreenManager* screenManager, ISettings* settings,
                                   PhosphorWorkspaces::VirtualDesktopManager* virtualDesktopManager,
                                   PhosphorWorkspaces::ActivityManager* activityManager = nullptr,
                                   QObject* parent = nullptr);
    ~WindowTrackingAdaptor() override;

    /**
     * @brief Last screen reported by the KWin effect's windowActivated call
     *
     * The KWin effect has reliable screen info on both X11 and Wayland.
     * Use this as a fallback when cursor screen is unavailable.
     *
     * Implementation: prefers the active window's current daemon-tracked
     * screen assignment over the cached value. KWin only fires
     * `windowActivated` on focus changes, so a window that gets dragged or
     * snapped to a different VS without losing focus leaves
     * `m_lastActiveScreenId` pointing at the OLD screen — which then
     * misroutes shortcut handlers (e.g. the float shortcut going to the
     * autotile engine for the source VS instead of the snap engine for the
     * destination VS). Reading the live screenAssignment closes that gap
     * without requiring a separate signal/cache invalidation path.
     */
    QString lastActiveScreenName() const override;

    /**
     * @brief Last screen the cursor was on, reported by the KWin effect
     *
     * Updated whenever the cursor crosses to a different monitor.
     * This is the primary source for shortcut screen detection on Wayland,
     * since QCursor::pos() is unreliable for background daemons.
     */
    QString lastCursorScreenName() const override
    {
        return m_lastCursorScreenId;
    }

    /**
     * @brief Get the last activated window's ID
     */
    QString lastActiveWindowId() const override
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
     * Also forwards the pointer to the underlying PhosphorPlacement::WindowTrackingService and
     * subscribes to metadataChanged so we can refresh tracking that mirrors
     * the app class (e.g. last-used-zone class tag).
     */
    void setWindowRegistry(PhosphorEngine::WindowRegistry* registry);

    /**
     * @brief Bind the unified window-rule store (daemon-owned) for per-window
     *        RestorePosition evaluation.
     *
     * Non-owning. Used by the restore-position predicate (see enginewiring.cpp)
     * to override the per-engine `*RestoreFloatedWindowsOnLogin` settings for a
     * matched window. A lazily-built RuleEvaluator binds to the store's full
     * rule set; it self-invalidates on in-place rule edits via the set's
     * revision counter, so no rulesChanged subscription is required.
     */
    void setWindowRuleStore(PhosphorWindowRules::WindowRuleStore* store);

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
    void setEngines(PhosphorEngine::PlacementEngineBase* snapEngine,
                    PhosphorEngine::PlacementEngineBase* autotileEngine);

    /**
     * @brief Set the frozen-snapshot resolver used by saveload's disable
     *        gate to short-circuit restore on a disabled context.
     *
     * Late-bound for the same reason as setEngines / setShortcutRegistrar —
     * the resolver is constructed after this adaptor. Daemon calls this
     * once after `m_contextResolver` lands. Pass nullptr during shutdown.
     */
    void setContextResolver(PhosphorContext::IContextResolver* resolver)
    {
        m_contextResolver = resolver;
    }

    PhosphorSnapEngine::SnapEngine* snapEngine() const;

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
     * @brief Access the underlying PhosphorPlacement::WindowTrackingService
     *
     * Used by the daemon to share the single WTS instance with other components
     * (e.g., AutotileEngine) instead of creating duplicate services.
     */
    PhosphorPlacement::WindowTrackingService* service() const
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
     * @param instanceId     Opaque compositor handle (stable for window lifetime)
     * @param appId          Current app class (mutable)
     * @param desktopFile    Current desktop file name (mutable, may be empty)
     * @param title          Current caption (mutable, may be empty)
     * @param windowRole     X11 WM_WINDOW_ROLE (empty for Wayland-native windows)
     * @param pid            Process id (0 = unknown)
     * @param virtualDesktop 1-based x11 desktop number (0 = all desktops / unknown)
     * @param activity       Activity UUID (empty = all activities / unknown)
     * @param windowType     PhosphorProtocol::WindowType underlying value; out-of-range
     *                       values are clamped to WindowType::Unknown
     * @param extended       Extended window-property snapshot keyed by
     *                       PhosphorProtocol::Service::WindowMetadataKey (state flags,
     *                       geometry, accessory flags, captionNormal). A key is
     *                       present only when the value is known; absent keys leave
     *                       the corresponding WindowMetadata optional disengaged so a
     *                       window-rule predicate over it stays inert. Lets the
     *                       daemon's resolvers match the same KWin-property fields the
     *                       effect path resolves live (window_query.cpp).
     *
     * Emits no D-Bus signal. Populates the daemon's WindowRegistry; consumers
     * subscribe to the registry's Qt signals directly.
     *
     * Safe to call unconditionally on every observation — no-op if metadata
     * is unchanged.
     */
    void setWindowMetadata(const QString& instanceId, const QString& appId, const QString& desktopFile,
                           const QString& title, const QString& windowRole, int pid, int virtualDesktop,
                           const QString& activity, int windowType, const QVariantMap& extended);

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
     * @param zoneId Output: zone ID to snap to, or empty if none
     * @return true if the window had a zone before it was floated
     *
     * No in-tree caller: the effect's unfloat flow moved to
     * SnapAdaptor::calculateUnfloatRestore. Kept as external contract
     * surface (scripting/automation query into the pre-float state),
     * same policy as AutotileAdaptor::retileAllScreens.
     */
    bool getPreFloatZone(const QString& windowId, QString& zoneId);

    /**
     * Clear the saved "zone before float" after restoring on unfloat.
     * @param windowId Window ID from the effect
     *
     * No in-tree caller (see getPreFloatZone) — kept as the write half of
     * the same external contract surface.
     */
    void clearPreFloatZone(const QString& windowId);

    // calculateUnfloatRestore moved to SnapAdaptor (org.plasmazones.Snap D-Bus interface).

    /**
     * Store geometry before tiling (unified snap + autotile)
     * @param windowId Window ID
     * @param x Window X position
     * @param y Window Y position
     * @param width Window width
     * @param height Window height
     * @param screenId Screen the geometry was captured on
     * @param overwrite If false (snap mode), skip if entry exists. If true (autotile), always overwrite.
     */
    void storePreTileGeometry(const QString& windowId, int x, int y, int width, int height, const QString& screenId,
                              bool overwrite);

    /**
     * Check if a window has stored pre-tile geometry
     *
     * No in-tree caller (the effect restores via getValidatedPreTileGeometry
     * without a pre-check) — kept as external contract surface, same policy
     * as AutotileAdaptor::retileAllScreens.
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
    PhosphorProtocol::PreTileGeometryList getPreTileGeometries();

    /**
     * Clean up all tracking data for a closed window
     * @param windowId Window ID that was closed
     * @param windowKind PhosphorEngine::WindowKind wire value (Unknown/Normal/
     *        Transient) — gates the snap-restore consume on reopen
     * @param screenId The window's authoritative current screen at close (KWin's
     *        getWindowScreenId). Threaded into the final placement capture so a
     *        window dragged cross-screen and closed records its float-back on the
     *        screen it actually closed on — by close time a cross-screen move has
     *        torn down both engines' tracking, so neither capturePlacement can
     *        report the real screen. Empty = legacy/opt-out.
     * @note Call this when KWin reports a window has been closed to prevent memory leaks
     */
    void windowClosed(const QString& windowId, int windowKind, const QString& screenId = QString());

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
     * @param x/y/width/height Current frame geometry in compositor coordinates
     */
    void setFrameGeometry(const QString& windowId, int x, int y, int width, int height);

    /**
     * Notify the daemon that a tiled window finished an interactive resize.
     *
     * Called by the compositor plugin from windowFinishUserMovedResized when
     * the interaction was a resize (not a move). Forwards to the autotile
     * engine so it can reflow neighbouring windows to fill the gap (GitHub
     * #652). The old/new frames are supplied directly by the plugin (latched at
     * resize start / read at finish) because the debounced frame shadow updates
     * mid-drag and can't serve as a reliable baseline. No-op for windows the
     * autotile engine doesn't track.
     *
     * @param windowId Window identifier
     * @param oldX,oldY,oldWidth,oldHeight Frame geometry before the resize
     * @param newX,newY,newWidth,newHeight Frame geometry after the resize
     */
    void notifyWindowResized(const QString& windowId, int oldX, int oldY, int oldWidth, int oldHeight, int newX,
                             int newY, int newWidth, int newHeight);

    /**
     * Update cursor screen when cursor crosses to a different monitor
     * Called by the KWin effect's slotMouseChanged when screen changes.
     * @param screenId Name of the screen the cursor is now on
     */
    void cursorScreenChanged(const QString& screenId);

    /**
     * Record a screen's current virtual desktop (Plasma 6.7 per-output virtual
     * desktops). Called by the KWin effect on KWin::EffectsHandler::desktopChanged.
     * Forwarded to VirtualDesktopManager::updateScreenDesktop — KWin's own D-Bus
     * VirtualDesktopManager interface only exposes the global current desktop.
     * @param screenId Physical screen whose desktop changed
     * @param desktop  The screen's current virtual desktop, 1-based
     */
    void screenDesktopChanged(const QString& screenId, int desktop);

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

    // Window tracking queries
    QString getZoneForWindow(const QString& windowId);
    QStringList getMultiZoneForWindow(const QString& windowId);
    QStringList getWindowsInZone(const QString& zoneId);
    QStringList getSnappedWindows();

    /// Remove zone/screen/desktop assignments for windows not in the alive set.
    /// Called by the KWin effect after daemon ready to clean up stale KConfig entries
    /// from windows that no longer exist (closed between save and daemon restart).
    void pruneStaleWindows(const QStringList& aliveWindowIds);

    /// Re-drive compositor-side per-window appearance (snap border / hidden
    /// title bar, autotile border) for every window each engine manages. Called
    /// by the KWin effect once the daemon is ready: on a daemon or effect
    /// restart the compositor drops its window-chrome state, so it must be
    /// re-applied from the daemon's authoritative placement state. Delegates to
    /// the common IPlacementEngine::reapplyManagedWindowAppearance() on both
    /// engines — does not move windows.
    void reapplyWindowAppearance();

    /**
     * Get typed list of empty zones for Snap Assist continuation
     * @param screenId Screen ID (e.g. DP-1)
     * @return PhosphorProtocol::EmptyZoneList of empty zone entries with overlay-local geometry
     */
    PhosphorProtocol::EmptyZoneList getEmptyZones(const QString& screenId);

    /**
     * Get the last zone a window was snapped to
     * @return PhosphorZones::Zone ID of last used zone, or empty string if none
     *
     * No in-tree caller (snap-to-last-zone moved to SnapAdaptor) — kept as
     * external contract surface, same policy as
     * AutotileAdaptor::retileAllScreens.
     */
    QString getLastUsedZoneId();

    // snapToLastZone, recordSnapIntent, snapToAppRule, snapToEmptyZone,
    // restoreToPersistedZone, resolveWindowRestore moved to SnapAdaptor
    // (org.plasmazones.Snap D-Bus interface).

    /**
     * Get updated geometries for all tracked windows (for resolution change handling)
     * @return Typed PhosphorProtocol::WindowGeometryList — entries carry
     *         (windowId, x, y, width, height, screenId), the same wire shape
     *         as applyGeometriesBatch
     * @note Returns empty if keepWindowsInZonesOnResolutionChange is disabled
     */
    PhosphorProtocol::WindowGeometryList getUpdatedWindowGeometries();

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
     * @return PhosphorProtocol::WindowStateEntry with windowId, zoneId, screenId,
     *         isFloating, changeType, zoneIds (multi-zone spans), isSticky
     */
    PhosphorProtocol::WindowStateEntry getWindowState(const QString& windowId);

    /**
     * @brief Get state for all tracked windows (TUI dashboard)
     * @return List of PhosphorProtocol::WindowStateEntry structs
     */
    PhosphorProtocol::WindowStateList getAllWindowStates();

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
     * @brief Get geometry for a specific zone ID (uses primary screen)
     * @param zoneId PhosphorZones::Zone UUID string
     * @return PhosphorProtocol::ZoneGeometryRect with x, y, width, height (all zero if not found)
     */
    PhosphorProtocol::ZoneGeometryRect getZoneGeometry(const QString& zoneId);

    /**
     * @brief Get geometry for a specific zone ID on a specific screen
     * @param zoneId PhosphorZones::Zone UUID string
     * @param screenId Screen ID (empty = primary screen)
     * @return PhosphorProtocol::ZoneGeometryRect with x, y, width, height (all zero if not found)
     */
    PhosphorProtocol::ZoneGeometryRect getZoneGeometryForScreen(const QString& zoneId, const QString& screenId);

    // handleBatchedResnap moved to SnapAdaptor.

public:
    // Internal-only members below — declared as plain public methods (NOT
    // under Q_SLOTS, and without Q_INVOKABLE, which would re-export them) so
    // QDBusAbstractAdaptor's runtime introspection does NOT expose them on
    // the bus regardless of XML content. Same pattern as
    // `WindowDragAdaptor::handleWindowClosed` (NOT its
    // clearForCompositorReconnect, which must STAY a slot — the effect
    // invokes that one over the bus at shutdown). Every caller here is
    // in-process and reaches these via direct C++ invocation through the
    // daemon, never through D-Bus.

    // getPreTileGeometry (out-param forwarder) removed: zero callers
    // remained — getValidatedPreTileGeometry is the single read path.

    /**
     * Query the daemon's shadow for a window's last-known frame geometry
     * (INavigationStateProvider override; daemon-local shortcut handlers
     * reach it via the typed interface, never the bus).
     *
     * Returns an invalid QRect if the window has not pushed a geometry yet.
     */
    QRect frameGeometry(const QString& windowId) const override;

    /**
     * @brief Find the first empty zone in the current layout
     * @return PhosphorZones::Zone ID of first empty zone, or empty string if all occupied
     */
    QString findEmptyZone();

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

    /// Unified placement capture orchestrator: ask each engine for @p windowId's
    /// current placement, stamp it with the live frame geometry, and record it in
    /// the WindowPlacementStore (or clear the record if no engine manages it).
    /// Shadow-written in P1; the single funnel every state-change + close hook
    /// calls so the persisted record always reflects the window's live state.
    ///
    /// @p authoritativeScreen, when non-empty (the close path passes KWin's
    /// getWindowScreenId), is the window's true current screen. It is used ONLY
    /// as a fallback when NEITHER engine produces a placement — the case where a
    /// cross-screen move has removed the window from the source engine's tracking
    /// and the destination engine has not adopted it, so both capturePlacement
    /// calls return nullopt and the live screen would otherwise be lost. In that
    /// case the float-back is recorded on @p authoritativeScreen via
    /// WindowTrackingService::recordFloatingClose.
    void captureWindowPlacement(const QString& windowId, const QString& authoritativeScreen = QString());

    /// Re-capture every open floating window's live geometry into the unified
    /// store at save time (no per-move hook fires for drags). Called before the
    /// dirty snapshot in saveState so a dragged floated window persists its
    /// current position across a daemon restart.
    void refreshOpenWindowPlacements();

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

    /// Resolve whether a FLOATED window should have its previous position restored
    /// on open. Consulted by the restore-position predicate the daemon injects into
    /// BOTH engines (in-process, not via D-Bus); @p mode selects which per-engine
    /// global default applies (snap-floated vs autotile-floated). A matched
    /// RestorePosition window rule wins (engine-neutral); otherwise the
    /// per-engine `*RestoreFloatedWindowsOnLogin` setting decides. Builds a
    /// WindowQuery from the window registry metadata.
    bool shouldRestoreFloatedPosition(const QString& windowId, PhosphorZones::AssignmentEntry::Mode mode);

    /// Resolve whether an opening window should start FLOATING because a "Float
    /// this app" window rule matched it. Consulted by the float predicate the
    /// daemon injects into BOTH engines (in-process, not via D-Bus). Unlike
    /// RestorePosition there is no global default — Float is purely rule-driven,
    /// so the answer is false unless a Float rule matches. The Float action's
    /// params are free-form, so the verdict is the presence of the filled slot.
    bool shouldFloatByRule(const QString& windowId);

    /// Resolve the 1-based zone ordinals an opening window should snap into
    /// because a `SnapToZone` window rule matched it. Consulted by the placement
    /// resolver the daemon injects into the SnapEngine (in-process, not via
    /// D-Bus). Returns an empty list when no SnapToZone rule matches; multiple
    /// ordinals request a zone span. Builds a WindowQuery from the window
    /// registry metadata, pins it to @p screenId (the screen the window is
    /// opening on) so a `ScreenId`-constrained rule resolves, and reads the
    /// `Placement` slot — mirrors shouldFloatByRule.
    QList<int> placementZonesByRule(const QString& windowId, const QString& screenId);
    /**
     * @brief Drop unified WindowPlacement records for excluded appIds.
     *
     * Single `placementStore().removeIf(...)` over the unified store, dropping any
     * record whose appId matches one of @p patterns (via
     * PhosphorIdentity::WindowId::appIdMatches), across BOTH engines' records — there
     * is no longer a per-engine pending-restore queue to walk. Marks
     * DirtyWindowPlacements when anything was removed so the next debounced save
     * persists the pruned store.
     *
     * Plain `public:` (not Q_SLOTS): the bus surface deliberately excludes this —
     * every caller is in-process and reaches it via direct C++ invocation through the
     * daemon. Same pattern as `WindowDragAdaptor::handleWindowClosed`.
     *
     * Called from three daemon-side sites:
     *   1. Daemon::init's init-prologue priming call — runs once, synchronously,
     *      before the `rulesChanged` subscription connects, pruning what loadState
     *      just deserialized into the store.
     *   2. Daemon::init's `refilterExcludeRules` lambda, fired on every
     *      `WindowRuleStore::rulesChanged` whose post-filter Exclude slice differs
     *      from the cached one (equality-guarded). Drives live rule edits into the prune.
     *   3. Daemon::finalizeStartup, after AutotileEngine::loadState has restored its
     *      placement records, so any autotile records loaded then are pruned too.
     * The daemon derives @p patterns from the unified WindowRule store via
     * `PhosphorWindowRules::ExclusionRules::applicationExcludePatternsFrom`.
     *
     * Safe to call at any time. An empty @p patterns short-circuits.
     */
    void pruneExcludedPendingRestores(const QStringList& patterns);

    /**
     * @brief Emit reapplyWindowGeometriesRequested (called by daemon after geometry settles).
     * Not a D-Bus method; used internally so the daemon timer can trigger the signal.
     */
    void requestReapplyWindowGeometries();

Q_SIGNALS:
    void windowZoneChanged(const QString& windowId, const QString& zoneId);

    /**
     * @brief Qt signal emitted after the windowClosed() D-Bus method
     * processes a close. Used to drive sibling-adaptor cleanup (e.g.
     * WindowDragAdaptor's drag-state teardown when a window closes mid-drag)
     * without re-introducing a D-Bus-visible WindowDrag.handleWindowClosed
     * surface that no one outside the daemon was wiring up.
     *
     * Distinct name from the D-Bus method so MOC/QtDBus don't conflate the
     * two; the method runs first, then we emit this for in-process listeners.
     * NOTE: like all adaptor signals it IS auto-relayed onto the bus by
     * QDBusAbstractAdaptor; it is simply not part of the documented wire
     * contract (absent from the XML) and nothing external subscribes.
     */
    void windowClosedNotification(const QString& windowId);

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
     * @param state PhosphorProtocol::WindowStateEntry with windowId, zoneId, screenId,
     *        isFloating, changeType, zoneIds (multi-zone spans), isSticky;
     *        changeType: "snapped", "unsnapped", "floated", "unfloated", "screen_changed"
     */
    void windowStateChanged(const QString& windowId, const PhosphorProtocol::WindowStateEntry& state);

    /**
     * @brief Emitted when pending window restores become available
     *
     * This signal is emitted when:
     * 1. The active layout becomes available after startup
     * 2. There are pending zone assignments waiting to be applied
     *
     * The KWin effect should respond by calling resolveWindowRestore()
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

    /// Cross-desktop directional move: KWin should move @p windowId to virtual
    /// desktop @p desktop (1-based). The effect calls windowToDesktops.
    void windowDesktopMoveRequested(const QString& windowId, int desktop);

    /// Daemon-initiated cross-output move: the daemon has migrated its own
    /// tiling state for @p windowId onto @p targetScreenId and scheduled both
    /// reflows. The window's resulting outputChanged is expected; the effect
    /// must update bookkeeping + decoration only, not re-issue windowClosed/
    /// windowOpened. User-drag cross-output moves carry no marker.
    void windowOutputMoveExpected(const QString& windowId, const QString& targetScreenId);

    /**
     * @brief Daemon requests KWin to apply geometries for a batch of windows
     * @param geometries List of window geometry entries to apply
     * @param action Navigation action type ("rotate", "resnap", "vs_reconfigure") for feedback
     * @note Daemon handles windowSnapped bookkeeping internally before emitting.
     *       Effect just applies geometry with stagger — no windowsSnappedBatch callback.
     */
    void applyGeometriesBatch(const PhosphorProtocol::WindowGeometryList& geometries, const QString& action);

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

public:
    // Internal-only members below — plain `public:` placement (not Q_SLOTS)
    // to keep QDBusAbstractAdaptor's runtime introspection from exposing
    // them on the bus when the XML doesn't list them. Same pattern as the
    // `pruneExcludedPendingRestores` / `requestReapplyWindowGeometries`
    // pair above.
    /**
     * @brief Apply pre-snap/pre-autotile geometry for a floated window (call from daemon when autotile engine floats).
     * Gets validated geometry, emits applyGeometryRequested if found, clears stored geometry.
     * @return true if geometry was applied, false if none stored
     */
    bool applyGeometryForFloat(const QString& windowId, const QString& screenId);

    /**
     * @brief Emit moveSpecificWindowToZoneRequested — called when user selects from Snap Assist.
     * Takes a `QRect` payload; not D-Bus marshallable without a typeName annotation, so
     * keeping it off the wire entirely is the only safe shape.
     */
    void requestMoveSpecificWindowToZone(const QString& windowId, const QString& zoneId, const QRect& geometry);

private Q_SLOTS:
    /**
     * @brief Orchestrate a cross-MODE directional move handoff.
     *
     * Wired to both engines' crossModeMoveRequested. The source engine (the
     * signal sender) reached a context boundary whose target is a different
     * tiling mode and deferred here. This resolves the target mode at the
     * destination context, relinquishes the window from the source engine
     * (handoffRelease + source reflow for an autotile source), and hands it to
     * the target engine (handoffReceive): autotile inserts it per the
     * insertion-order setting; snap snaps it into the entry zone (monitor
     * crossing) or the equivalent zone (snap→snap desktop crossing). For a
     * cross-desktop crossing it then asks the compositor to move the real window
     * to @p targetDesktop.
     */
    void handleCrossModeMove(const QString& windowId, const QString& targetScreenId, int targetDesktop,
                             const QString& direction);

    /**
     * @brief Orchestrate a cross-MODE directional swap handoff (two-way).
     *
     * Wired to both engines' crossModeSwapRequested. Resolves the swap partner —
     * the target surface's entry-edge window facing the source in @p direction
     * (autotile: the edge tile; snap: the entry zone's occupant). With no partner
     * the entry slot is empty, so it degrades to a plain cross-mode move. With a
     * partner it captures both landing slots, relinquishes both windows from their
     * engines, and re-places them swapped: the focused window takes the partner's
     * slot on the target, the partner takes the focused window's vacated slot on
     * the source. Emits windowOutputMoveExpected for each window that crosses
     * outputs so the effect doesn't tear the placements down.
     */
    void handleCrossModeSwap(const QString& windowId, const QString& targetScreenId, int targetDesktop,
                             const QString& direction);

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
     * Called when PhosphorScreens::ScreenManager reports panel geometry is known.
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
     * @brief Test whether the given (screen, virtualDesktop, activity) tuple is currently disabled.
     *
     * Used by the save/load filters to drop entries persisted before the user
     * disabled a monitor / virtual desktop / activity. Routes through
     * `PhosphorContext::IContextResolver::handleForPersisted`, which queries
     * the screen's current mode internally via its bound `IModeProvider`.
     * Returns `false` when the resolver has not yet been wired (e.g. during
     * the adaptor's own construction, before `Daemon` calls
     * `setContextResolver`) — keeping the entry is safe at that point because
     * no save/load can race the ctor on the same thread. Empty screenId
     * carries through to the resolver, which treats it as a sentinel
     * (matches no per-screen disable entry).
     *
     * The activity parameter is optional and defaults to empty — snap-mode
     * storage carries no per-window activity tag (SnapState does not track it)
     * so snap callers leave it unset and the activity-mode disable list never
     * applies to them. The WindowPlacementStore serialize keep-predicate passes
     * each record's activity tag explicitly so autotile records gate correctly.
     */
    bool isPersistedContextDisabled(const QString& screenId, int virtualDesktop,
                                    const QString& activity = QString()) const;

    /**
     * @brief Current virtual desktop index, or 0 when no VirtualDesktopManager
     *        is wired. Centralises the null-guarded read shared by the
     *        disabled-context gates and last-used-zone tracking.
     */
    int currentDesktop() const;
    /// This screen's current virtual desktop (Plasma 6.7 per-output virtual
    /// desktops, #648), falling back to the global currentDesktop().
    int currentDesktopForScreen(const QString& screenId) const;

    // clearFloatingStateForSnap was removed — PhosphorSnapEngine::SnapEngine::commitSnap
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

    // Last floating value broadcast via windowFloatingChanged, per window. The
    // setWindowFloating broadcast gate compares against THIS, not a re-query of
    // the service's float state: with the per-engine float model the owning
    // engine flips its float bit BEFORE the daemon's sync slot reaches the
    // writer, so a re-query already reports the post-transition value and would
    // suppress every autotile float broadcast. Absent entry == not-floating.
    // Entries are removed on windowClosed and swept by pruneStaleWindows
    // (defensive, for a window that died without a close signal).
    QHash<QString, bool> m_broadcastFloating;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Dependencies (kept for signal connections and settings access)
    // ═══════════════════════════════════════════════════════════════════════════════
    ZoneDetectionAdaptor* m_zoneDetectionAdaptor = nullptr;
    PhosphorZones::LayoutRegistry* m_layoutManager;
    ISettings* m_settings;
    /// Non-owning resolver pointer, late-bound via setContextResolver after
    /// Daemon constructs `m_contextResolver`. Replaces the previous
    /// `(m_screenModeRouter->modeFor → currentVirtualDesktop → currentActivity
    /// → isContextDisabled)` cascade rebuild in `saveload.cpp`.
    PhosphorContext::IContextResolver* m_contextResolver = nullptr;
    PhosphorWorkspaces::VirtualDesktopManager* m_virtualDesktopManager;
    PhosphorWorkspaces::ActivityManager* m_activityManager;
    std::unique_ptr<PhosphorConfig::IBackend> m_sessionBackend; // Session state (session.json)

    // Engine references for per-screen routing (set via setEngines())
    // QPointer auto-nulls on engine destruction, guarding against late D-Bus calls
    QPointer<PhosphorEngine::PlacementEngineBase> m_snapEngine;
    QPointer<PhosphorEngine::PlacementEngineBase> m_autotileEngine;
    QPointer<PhosphorSnapEngine::SnapEngine> m_cachedSnapEngine;
    QPointer<PhosphorTileEngine::AutotileEngine> m_cachedAutotileEngine;

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
    //
    // INVARIANT: post-construction, `m_service` is non-null for the
    // lifetime of this adaptor. The constructor `qFatal`s on any null
    // dependency, so reaching any member function with a null `m_service`
    // is impossible under the current contract. The few `if (!m_service)
    // return;` guards in public slots are belt-and-braces against a
    // future regression that introduces a clear-to-null path (none
    // currently exists); the qFatal is the authoritative gate.
    // ═══════════════════════════════════════════════════════════════════════════════
    // Owned: DaemonGeometryResolver is a plain non-QObject and the
    // adaptor's destructor would otherwise leak it. WindowTrackingService
    // borrows the resolver by raw pointer (no ownership transfer), so this
    // unique_ptr must outlive m_service — declare it BEFORE m_service so
    // reverse-order member destruction tears m_service down first.
    std::unique_ptr<PhosphorPlacement::IGeometryResolver> m_geometryResolver;
    PhosphorPlacement::WindowTrackingService* m_service = nullptr;

    // Shared registry: compositor-supplied instance id → current metadata.
    // Not owned (daemon root owns it). Populated via setWindowMetadata D-Bus calls
    // and cleared from the windowClosed path.
    QPointer<PhosphorEngine::WindowRegistry> m_windowRegistry;

    // Unified window-rule store (daemon-owned, not owned here) + a lazily-built
    // evaluator over its full rule set, shared by shouldRestoreFloatedPosition
    // and shouldFloatByRule (resolveCached returns every matched slot, so one
    // evaluator serves both per-window resolvers). The evaluator self-invalidates
    // on in-place rule edits via the set revision, so it is built once on first
    // use. Reset in setWindowRuleStore only when the store pointer actually
    // changes (a same-store rebind keeps the evaluator).
    PhosphorWindowRules::WindowRuleStore* m_windowRuleStore = nullptr;
    std::unique_ptr<PhosphorWindowRules::RuleEvaluator> m_windowRuleEvaluator;

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
    QQueue<PhosphorPlacement::WindowTrackingService::DirtyMask> m_pendingWriteMasks;

    // One-shot warning latch for the test-only synchronous fallback path
    // in saveState(). Production always uses PhosphorConfig::JsonBackend + the
    // async worker, so hitting the sync path indicates either a test
    // harness or an unexpected misconfiguration.
    bool m_syncFallbackWarned = false;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Startup timing coordination
    // ═══════════════════════════════════════════════════════════════════════════════

    /**
     * @brief Try to emit pendingRestoresAvailable if conditions are met
     *
     * Conditions required:
     * 1. PhosphorZones::Layout is available with pending restores
     * 2. Panel geometry has been received by PhosphorScreens::ScreenManager
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
