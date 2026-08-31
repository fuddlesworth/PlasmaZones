// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// FILE-SIZE EXCEPTION (sanctioned): this header exceeds the 1150-line ceiling
// because it declares BOTH the D-Bus wire surface (slots whose signatures are
// pinned by the interface XML) and the in-process orchestration API the daemon
// wires. Splitting would sever the wire methods from the state they document
// against; the cost of the split outweighs the ceiling here.

#pragma once

#include "plasmazones_export.h"
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorSnapEngine/INavigationStateProvider.h>
#include <PhosphorSnapEngine/PlacementDirective.h>
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
#include <QJsonObject>
#include <QQueue>
#include <QRect>
#include <QTimer>
#include <QPointer>
#include <functional>
#include <memory>
#include <optional>

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

namespace PhosphorScrollEngine {
class ScrollEngine;
}

namespace PhosphorWorkspaces {
class VirtualDesktopManager;
class ActivityManager;
}

namespace PhosphorRules {
class RuleStore;
class RuleEvaluator;
class ResolvedActions;
struct WindowQuery;
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
     * screen assignment over the cached value, probing the snap, autotile
     * and scrolling engines in that order. KWin only fires
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
    void setRuleStore(PhosphorRules::RuleStore* store);

    /**
     * @brief Set engine references for routing operations per-screen
     *
     * The adaptor routes IPlacementEngine operations to the correct engine:
     * AutotileEngine for autotile screens, ScrollEngine for scrolling
     * screens, SnapEngine for manual-zone screens. All must be set before
     * navigation/float D-Bus calls work.
     *
     * Signal connections from SnapEngine to adaptor D-Bus signals are established here.
     * The snap-specific signal (windowSnapStateChanged) is connected via qobject_cast.
     *
     * @param snapEngine PlacementEngineBase for snap mode (not owned, must outlive adaptor)
     * @param autotileEngine PlacementEngineBase for autotile mode (not owned, must outlive adaptor)
     * @param scrollEngine PlacementEngineBase for scrolling mode (not owned;
     *        explicit at every call site — no default, so a production
     *        caller cannot silently drop the scroll engine)
     */
    void setEngines(PhosphorEngine::PlacementEngineBase* snapEngine,
                    PhosphorEngine::PlacementEngineBase* autotileEngine,
                    PhosphorEngine::PlacementEngineBase* scrollEngine);

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

    /**
     * @brief Wire the scrolling strip-structure snapshot provider.
     *
     * saveState() calls it under DirtyScrollStrips to fetch
     * ScrollEngine::serializeStripState's blob at write time (the engine is
     * constructed after this adaptor, so the provider is late-bound like
     * setEngines). Pass {} during shutdown teardown, but only AFTER the final
     * saveStateOnShutdown(): an absent provider makes saveState SKIP the
     * strips write entirely (it cannot answer, so it leaves the stored blob
     * alone rather than deleting it), so clearing early silently drops every
     * strip mutation from the last debounce window.
     */
    void setScrollStripStateProvider(std::function<QJsonObject()> provider)
    {
        m_scrollStripStateProvider = std::move(provider);
    }

    /**
     * @brief The ScrollStrips blob read by the last loadState(), empty when
     *        the key was absent or unparsable. The daemon feeds it to
     *        ScrollEngine::restoreStripState once the engine exists.
     */
    QJsonObject loadedScrollStripState() const
    {
        return m_loadedScrollStripState;
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
     *                       geometry, accessory flags, captionNormal, multi-desktop
     *                       span list). A key is
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
     * same policy as TilingAdaptor::retileAllScreens.
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
     * as TilingAdaptor::retileAllScreens.
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
     * ABSENCE IS NOT LATENCY. The shadow has exactly three writers: this
     * motion-driven flush, the bulk seed on daemon (re)registration
     * (kwin-effect daemon_bringup.cpp, which states the consequence), and
     * the open-path seed the snap handler pushes immediately before
     * resolveWindowRestore. A window that has never MOVED and is not being
     * opened therefore has no entry at all — not one that arrives 50 ms
     * later — so callers deciding policy on an invalid read must not assume
     * a retry would populate it.
     *
     * The open-path seed exists because that assumption was made and was
     * wrong: applyOpenScreenRouting translates a bare RouteToScreen from
     * this shadow, and with only the first two writers a freshly opened
     * window read back invalid, so the route silently never moved it while
     * still suppressing the remembered-placement fallback. The same rule
     * worked on an already-open window purely because a daemon restart had
     * bulk-seeded it.
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

    /// Remove per-window state for windows not in the alive set. The snap side
    /// goes through the tracking service's pruneStaleAssignments: zone / screen
    /// / desktop assignments, SnapState membership, the sticky map and legacy
    /// float set. The two TILING-family engines (autotile and scrolling) prune
    /// themselves via their pruneStaleWindows overrides — TilingState / strip
    /// membership, pending orders, min-size and last-rect caches. On top of
    /// that: the registry's metadata + canonical entries, the tab-colour memo,
    /// the rule evaluator's shared per-window memo, and the adaptor's own
    /// frame-geometry / broadcast shadow maps.
    /// Called by the KWin effect after daemon ready to clean up stale entries
    /// from windows that no longer exist (closed between save and daemon restart).
    void pruneStaleWindows(const QStringList& aliveWindowIds);

    /// Re-drive compositor-side per-window appearance (snap border / hidden
    /// title bar, autotile border) for every window each engine manages. Called
    /// by the KWin effect once the daemon is ready: on a daemon or effect
    /// restart the compositor drops its window-chrome state, so it must be
    /// re-applied from the daemon's authoritative placement state. Delegates to
    /// the common IPlacementEngine::reapplyManagedWindowAppearance() on all
    /// three engines (snap, autotile, scrolling) — does not move windows.
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
     * TilingAdaptor::retileAllScreens.
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
     * Every window the effect has reported a frame for, i.e. every open
     * window, as canonical shadow-store ids (safe to feed straight back to
     * captureWindowPlacement — the same contract refreshOpenWindowPlacements
     * relies on). Daemon-internal; used by the mode-exit presave to snapshot
     * ALL windows on a screen, not only the explicitly-floated set.
     */
    QStringList knownWindowIds() const;

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
    /// the WindowPlacementStore. When NO engine manages the window, any existing
    /// record is left intact (never cleared here — records are per-mode memory,
    /// only merge-updated, consumed, or explicitly pruned); with an
    /// @p authoritativeScreen it records a floating-close placement instead.
    ///
    /// "Left intact" is no longer inert. The cross-screen reclaim
    /// (IPlacementEngine::claimCrossScreenReopen) reads a managed slot plus
    /// the record-level screenId as a HOME to pull a window back to, so a
    /// record this capture leaves unrepaired can later MOVE a live window
    /// between monitors rather than merely restoring it to a slightly stale
    /// spot. Two safeguards keep that sound and both must be preserved: an
    /// engine that knowingly gives a window up clears its own slot
    /// (WindowPlacementStore::clearEngineSlot, called from handoffRelease),
    /// and the claims validate the record against LIVE state (live screen
    /// set, context compatibility, membership after adoption) instead of
    /// trusting it. downgradeMismatchedManagedSlots is the repair for the
    /// close paths that do run.
    /// Shadow-written in P1; the single funnel every state-change + close hook
    /// calls so the persisted record always reflects the window's live state.
    ///
    /// MINIMIZED windows (registry tri-state engaged-true, unknown-while-
    /// tracked, or a classified suspension float) are a special case unless
    /// @p fromStateChange: their live frame and engine view describe the
    /// suspension, not intent, so the capture never overwrites the record. On
    /// a live capture that is a plain no-op; on the CLOSE path the existing
    /// record is preserved with only its context refreshed (close screen from
    /// @p authoritativeScreen, desktop/activity from the window's live
    /// registry metadata) so a reopen restores the pre-minimize placement in
    /// the right place.
    ///
    /// @p authoritativeScreen (the close path passes KWin's getWindowScreenId)
    /// therefore plays TWO roles when non-empty: it marks the capture as a
    /// close-with-known-screen (enabling the minimize preserve branch above
    /// and the pure-float sibling collapse), and it is the fallback screen
    /// when NEITHER engine produces a placement — the case where a
    /// cross-screen move removed the window from the source engine's tracking
    /// and the destination never adopted it, so both capturePlacement calls
    /// return nullopt and the live screen would otherwise be lost. In that
    /// case the float-back is recorded on @p authoritativeScreen via
    /// WindowTrackingService::recordFloatingClose. NOTE a screen-LESS call is
    /// not necessarily a live capture: TilingAdaptor::windowClosed runs the
    /// funnel screen-less while its engine still tracks the closing window,
    /// deliberately forfeiting the three close-only branches above in
    /// exchange for an authoritative engine slot; the WindowTracking close
    /// then re-runs the funnel WITH the screen.
    /// @param fromStateChange Pass true when the capture is triggered by an
    ///        authoritative engine state change (snap commit/uncommit relay):
    ///        such a capture must run even for a minimized window, because the
    ///        engine now reports the NEW committed state, not the transient
    ///        minimize-suspension float the minimize guard exists to keep out
    ///        of the store.
    void captureWindowPlacement(const QString& windowId, const QString& authoritativeScreen = QString(),
                                bool fromStateChange = false);

    /// Re-capture EVERY open window's live placement into the unified store
    /// at save time — engine-agnostic, not floating-only: floated windows
    /// contribute their live geometry (no per-move hook fires for drags),
    /// snapped/tiled windows their current slot state. Called before the
    /// dirty snapshot in saveState so open-window state survives a daemon
    /// restart.
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

    /// Build the per-window rule query, with the screen-derived context fields
    /// stamped on top of the WindowRegistry metadata. The registry records no
    /// screen, so `ScreenId` and `ActiveLayout` can only be filled here: @p
    /// screenIdHint names the screen the window is landing on (the open /
    /// routing paths know it before the service does), and an empty hint falls
    /// back to the service's live screen-for-window. `ActiveLayout` resolves to
    /// the layout assigned to that screen's (desktop, activity) context, the
    /// same id the windowless context cascade stamps.
    ///
    /// EVERY per-window resolver must go through this rather than the bare
    /// buildRuleQueryForWindow free function. Seven of them share one
    /// RuleEvaluator::resolveCached entry keyed on (windowId, rule-set
    /// revision), and resolveCached returns the cached actions WITHOUT
    /// consulting the query on a hit — so whichever of those seven touches a
    /// window first fixes the context every later one reuses for that window's
    /// lifetime. (The other two consumers do not share it:
    /// shouldRestoreSizeOnUnsnap calls the uncached resolve(), and the snap
    /// engine's exclusion-query provider feeds SnapEngine's separate
    /// exclusion evaluator.)
    ///
    /// Uniform stamping is therefore necessary but NOT sufficient: the hinted
    /// and unhinted paths resolve different screens, so the ORDER matters too.
    /// The hint-bearing resolver has to seed first, and does on both engines
    /// today — SnapEngine::resolveWindowRestore calls calculateSnapToPlacementRule
    /// (libs/phosphor-snap-engine/src/lifecycle.cpp) ahead of the restore, managed-restore
    /// and float predicates, and TilingAdaptor::dispatchWindowOpened calls
    /// applyOpenRoutingForTiling ahead of the tile engine's windowOpened.
    /// Reordering either would silently revert ActiveLayout / ScreenId matching
    /// on the open path without failing a test.
    ///
    /// nullopt when no metadata is tracked for @p windowId.
    std::optional<PhosphorRules::WindowQuery> buildContextualRuleQuery(const QString& windowId,
                                                                       const QString& screenIdHint = QString()) const;

    /// Mode-neutral screen lookup for @p windowId: the snap service first (it
    /// canonicalizes the composite id), then each engine's own tracker. Returns
    /// empty when neither engine has placed the window and it holds no snap
    /// state, and equally when neither engine is wired at all (a test fixture, or
    /// before Daemon::initEngines runs).
    /// Used by buildContextualRuleQuery when no caller supplied a hint;
    /// the service accessor alone is snap-only and reports nothing for
    /// autotile-tracked windows.
    QString resolveScreenForWindow(const QString& windowId) const;

    /// Resolve whether a FLOATED window should have its previous position restored
    /// on open. Consulted by the restore-position predicate the daemon injects into
    /// BOTH engines (in-process, not via D-Bus); @p mode selects which per-engine
    /// global default applies (snap-floated vs autotile-floated). A matched
    /// RestorePosition rule wins (engine-neutral); otherwise the
    /// per-engine `*RestoreFloatedWindowsOnLogin` setting decides. Builds a
    /// WindowQuery from the window registry metadata.
    ///
    /// @p useCache selects the memoised per-window verdict (the open path, where
    /// the answer is resolved once per window lifetime) or a fresh resolve. The
    /// autotile wiring passes false because its restore-position predicate also
    /// runs mid-session, from insertWindow via backfillWindows.
    ///
    /// @p screenIdHint pins the query's ScreenId / ActiveLayout to the screen the
    /// window is landing on. It is required on the uncached autotile path: an
    /// uncached resolve builds its query fresh, and a hintless
    /// resolveScreenForWindow consults the service and the snap engine before the
    /// autotile engine — stale snap state resolves the WRONG screen, and the
    /// consults reached from context seeding can run before the engine keys the
    /// window at all. Empty is legal for an already-tracked window, where
    /// resolveScreenForWindow's engine fallbacks answer.
    bool shouldRestoreFloatedPosition(const QString& windowId, PhosphorZones::AssignmentEntry::Mode mode,
                                      bool useCache = true, const QString& screenIdHint = QString());

    /// Resolve whether a SNAPPED window should be restored to its zone on login.
    /// The snapped-to-zone analogue of shouldRestoreFloatedPosition: a matched
    /// SetRestoreToZoneOnLogin rule wins, otherwise the global
    /// `restoreWindowsToZonesOnLogin` setting decides. Consulted by the
    /// managed-restore predicate the daemon injects into the SnapEngine.
    bool shouldRestoreToZoneOnLogin(const QString& windowId);

    /// Resolve whether a window's ORIGINAL (pre-snap) size should be restored when
    /// it is unsnapped. A matched SetRestoreSizeOnUnsnap rule wins, otherwise the
    /// global `restoreOriginalSizeOnUnsnap` setting decides. Consulted on the
    /// drag-out / drop / cursor-left-zones unsnap paths (the latter two live in
    /// WindowDragAdaptor and call through here), so this is public.
    bool shouldRestoreSizeOnUnsnap(const QString& windowId);

    /// Resolve whether an unfloat with no remembered pre-float zone should
    /// fall back to a zone anyway. A matched SetUnfloatFallbackToZone rule
    /// wins, otherwise the global `snapUnfloatFallbackToZone` setting
    /// decides. Consulted by the unfloat-fallback predicate the daemon
    /// injects into the SnapEngine; @p screenId is the engine's RESOLVED
    /// restore screen, stamped for ScreenId / Mode scoped rules.
    bool shouldUnfloatFallbackToZone(const QString& windowId, const QString& screenId);

    /// Resolve whether an opening window should start FLOATING because a "Float
    /// this app" rule matched it. Consulted by the float predicate the
    /// daemon injects into BOTH engines (in-process, not via D-Bus). Unlike
    /// RestorePosition there is no global default — Float is purely rule-driven,
    /// so the answer is false unless a Float rule matches. The Float action's
    /// params are free-form, so the verdict is the presence of the filled slot.
    /// @p screenId is the OPENING screen; it stamps ScreenId and the derived
    /// Mode onto the query, without which a rule pairing either with Float is
    /// silently inert. Empty is tolerated (neither is stamped).
    bool shouldFloatByRule(const QString& windowId, const QString& screenId = QString());

    /// Per-window scrolling open-behaviour rule slots (openColumnWidth /
    /// openWindowHeight / openTabbed / openColumnPlacement / openMaximized /
    /// openFocused), returned as a loose map so the header stays free of
    /// scroll-engine types. Keys, present only when the slot matched, and
    /// spelled by the ScrollOpenKeys namespace in internal.h rather than
    /// literals: widthFraction (double), heightFraction (double), tabbed
    /// (bool), consume (bool), maximized (bool), focused (bool).
    /// Resolves UNCACHED, like shouldFloatByRule and unlike the
    /// Restore predicates: the query carries ScreenId and Mode stamps, and the
    /// evaluator cache is keyed on windowId and rule revision alone, so a hit
    /// would silently discard both. See rules.cpp.
    QVariantMap scrollOpenRuleParams(const QString& windowId, const QString& screenId);

    /// Per-window tab-colour rule slots — niri's `tab-indicator` WINDOW rule,
    /// which recolours only the matched window's own tab. Returned as a loose
    /// map with the key names the KWin effect reads
    /// (PhosphorProtocol::Service::ScrollTabKey), present only when the slot
    /// matched: "activeColor", "inactiveColor", "urgentColor" (all QString).
    /// These outrank the per-context colours, which outrank the config, which
    /// falls back to the theme — niri's resolution order.
    ///
    /// Resolves once per (window, rule revision, matchable window state)
    /// through a PRIVATE memo (see m_tabColorMemo) — not the shared evaluator
    /// cache, which cannot serve this query in either direction. Unlike
    /// scrollOpenRuleParams this runs per tab the effect queries
    /// (TilingAdaptor::scrollTabColors) and per title change (the per-window
    /// relay) rather than once per window open, so it is deliberately kept to
    /// a slot read with no screen or mode stamping.
    QVariantMap tabColorRuleParams(const QString& windowId);

    /// Per-window drop-indicator colours for @p windowId, keyed by the QML
    /// property names the overlay slot reads. Resolved once at DRAG START from
    /// the dragged window's rules — the only per-window slice of the drop
    /// indicator with a coherent referent, since exactly one window is dragged
    /// at a time. Unmemoised, unlike tabColorRuleParams: once per drag rather
    /// than per tab per effect query.
    QVariantMap dropIndicatorRuleParams(const QString& windowId);

    /// Drop every per-window rule memo this adaptor holds, because the system
    /// colour scheme flipped.
    ///
    /// ColorScheme is the one field in buildRuleQueryForWindow's query that
    /// changes without a rules edit, so neither memo notices it on its own: the
    /// shared evaluator cache is keyed on (window id, rule revision) and the
    /// private tab-colour memo compares a fixed field list. Both are dropped
    /// here so the next resolve re-reads the live token.
    ///
    /// Wired by the daemon to ISettings::systemColorSchemeChanged. Cheap and
    /// rare: a scheme flip is a user action, and the cost is one cold resolve
    /// per window on the paths that resolve again.
    void invalidateRuleMemosForColorSchemeChange();

private:
    /// Extract the three tab-colour slots from an already-resolved verdict.
    /// Shared by the memo-hit and memo-miss paths so both produce the same map.
    static QVariantMap tabColorsFromResolved(const PhosphorRules::ResolvedActions& resolved);

    /// tabColorRuleParams' PRIVATE memo, deliberately separate from the
    /// RuleEvaluator's shared one.
    ///
    /// The shared memo cannot serve this path in either direction. Seeding it
    /// would poison it (its key excludes the admit filter, and this query is
    /// unstamped) and would break the stamper-first ordering invariant. Merely
    /// reading it is no good either: all six of its seeders run on the OPEN
    /// path, and a rules save bumps the revision, so the peek would miss
    /// forever for every already-open window — and this runs once per tab
    /// every time the effect queries the colours, plus once per window on
    /// every title change.
    /// Caches the extracted COLOUR MAP rather than the ResolvedActions: the
    /// three slots are all this path ever reads, the map is what every caller
    /// wants back, and it keeps this header free of the rules-engine include.
    ///
    /// The key carries the rule revision plus title, captionNormal
    /// (title-derived), virtual desktop, activity and the colour-scheme token.
    /// Title especially — the daemon re-drives this memo's consumer per window
    /// on a title change, so keying on the revision alone would leave a `Title contains …`
    /// tab-colour rule stuck on its first verdict until the next rules save.
    ///
    /// ColorScheme is in the key because it is the one context field
    /// buildRuleQueryForWindow stamps that moves WITHOUT any rules edit: a
    /// light/dark flip changes the verdict of a `ColorScheme Equals dark`
    /// tab-colour rule while the revision stands still. The key alone is enough
    /// here — unlike the extended fields below, this path re-reads the token on
    /// every call, so the compare sees the flip on the next refresh, and
    /// invalidateRuleMemosForColorSchemeChange() drops the stale entries at the
    /// moment of the flip rather than waiting for one.
    ///
    /// KNOWN GAP, deliberate. buildRuleQueryForWindow also copies ~20 EXTENDED
    /// fields that move under a live window (isMaximized, isFocused,
    /// isMinimized, keepAbove, the geometry quartet, and the rest of the state
    /// flags). None is in this key, so a tab-colour rule conditioned on one
    /// resolves once and stays pinned until the title, desktop, activity,
    /// colour scheme or rule revision moves. Widening the key would NOT fix
    /// such a rule: nothing re-drives the effect's query on those fields
    /// either. The re-drive set is exactly three edges. The daemon broadcasts
    /// scrollTabColorsChanged on a rules change and on a colour-scheme change,
    /// which makes the effect re-query every tab, and it relays the signal for
    /// a single window when that window's title changes or its first registry
    /// record lands (TilingAdaptor::relayScrollTabColorsForWindow). None fires on
    /// isMaximized or any other extended field, so the verdict would still be
    /// stale between re-drives.
    /// ColorScheme is the exception that proves the shape of the argument, and
    /// that is why it IS keyed: it has a re-drive signal
    /// (ISettings::systemColorSchemeChanged, routed here through
    /// invalidateRuleMemosForColorSchemeChange), so keying it actually buys
    /// freshness rather than just extra compares. The
    /// honest fix is a second trigger, not a bigger key, and it is not worth ~20
    /// extra comparisons per tab per refresh until someone wants those pairings.
    /// Genuinely immutable for a given window id: windowRole, pid and
    /// windowType. appId and desktopFile are NOT — setWindowMetadata documents
    /// both as mutable (a class-mutating app renames mid-life), and the memo key
    /// is the INSTANCE-derived shadow id, which survives such a rename, so an
    /// AppId-matched tab-colour rule would stay pinned across one. They are left
    /// out of the key for the same reason as the extended fields: nothing
    /// re-drives the effect's query on an appId change either, so a wider key
    /// would not make that verdict fresh.
    struct TabColorMemoEntry
    {
        quint64 revision = 0;
        std::optional<QString> title;
        std::optional<QString> captionNormal;
        int virtualDesktop = 0;
        QString activity;
        QString colorScheme;
        QVariantMap colors;
    };
    QHash<QString, TabColorMemoEntry> m_tabColorMemo;

public:
    /// Stamp @p screenId and the placement mode that screen resolves to onto
    /// @p query. buildRuleQueryForWindow knows neither, and without them a rule
    /// pairing ScreenId or Mode with a window action never matches — a pairing
    /// the rules editor offers. An empty @p screenId stamps nothing. The mode
    /// comes from the WINDOW's own context (WindowContext::effectiveDesktop /
    /// effectiveActivity), so an open-time verdict agrees with the daemon's
    /// live float resolver for the same window. ONLY for resolvers that skip
    /// the evaluator cache: the memo is keyed on window id and rule revision
    /// alone, so a stamped query is discarded on a hit.
    void stampScreenAndMode(PhosphorRules::WindowQuery& query, const QString& windowId, const QString& screenId);

    /// Stamp the screen-derived context trio onto @p query: ScreenId, the
    /// ActiveLayout resolved for that screen's CURRENT desktop and activity
    /// (the same id the windowless context cascade stamps and the daemon
    /// publishes to the KWin effect, so the leaf means one thing everywhere),
    /// and ScreenOrientation. An empty @p screenId stamps nothing. Called by
    /// every resolver that pins a screen, cached and uncached alike, so the
    /// shared evaluator memo is always seeded with the full trio.
    void stampScreenContext(PhosphorRules::WindowQuery& query, const QString& screenId) const;

    /// Resolve the open-placement directive for a window from its matched window
    /// rules: the 1-based `SnapToZone` ordinals and/or zone names to snap into
    /// (both lists empty when no SnapToZone rule matches; multiple targets across
    /// the two lists request a zone span) plus the
    /// `RouteToScreen` target monitor (empty when unrouted) plus the
    /// `RouteToDesktop` target desktop (unset when unrouted, and only ever a
    /// 1-based desktop). The desktop slot steers where the zones RESOLVE, so a
    /// combined SnapToZone + RouteToDesktop rule lands in the right zone of the
    /// destination desktop; the desktop MOVE itself is emitted separately by
    /// applyOpenDesktopRouting. Consulted by the
    /// placement resolver the daemon injects into the SnapEngine (in-process, not
    /// via D-Bus). Builds a WindowQuery from the window registry metadata, pins it
    /// to @p screenId (the screen the window is opening on) so a
    /// `ScreenId`-constrained rule resolves, and reads the `Placement` /
    /// `RouteScreen` slots — mirrors shouldFloatByRule.
    PhosphorSnapEngine::PlacementDirective placementZonesByRule(const QString& windowId, const QString& screenId);

    /// Engine-neutral RouteToDesktop: if a matched rule pins @p windowId to
    /// a virtual desktop, emit windowDesktopMoveRequested so the compositor moves
    /// it there on open. Independent of snapping/tiling — composes with the
    /// window's placement. Called from the snap open-path facade. Pins @p screenId
    /// so a ScreenId-scoped rule resolves; reuses the per-window evaluator cache
    /// placementZonesByRule seeds.
    ///
    /// Returns whether a RouteToDesktop rule MATCHED — true even when its target
    /// failed the 1-based guard and no move was emitted, because the caller uses
    /// this to suppress applyPersistedDesktopRestore and a rule that owns the
    /// window's desktop must win whether or not its payload was usable.
    bool applyOpenDesktopRouting(const QString& windowId, const QString& screenId);

    /// Tiling-family open-path routing. Emits RouteToDesktop (as
    /// applyOpenDesktopRouting) AND resolves a RouteToScreen pin: when the
    /// matched rule routes the window to a DIFFERENT monitor that an engine
    /// (autotile or scrolling) owns, emits windowOutputMoveExpected and
    /// returns that screen id so the caller hands the window to that
    /// screen's claiming engine. Returns an empty string when there is no
    /// engine-owned redirect (no rule, snap/disabled target, or same
    /// screen) — the caller then uses the spawn screen. Snap-mode targets
    /// are handled by the snap placement directive, not here.
    /// Returns the redirect target screen (empty = insert on the spawn
    /// screen). @p directiveMatched, when non-null, is set true whenever a
    /// routing/placement directive MATCHED — including already-on-target and
    /// target-not-connected, where the return stays empty — so the caller's
    /// cross-screen-reclaim veto applies the same precedence the snap facade
    /// does. The two answers are deliberately separate; overloading the empty
    /// return let the two channels drift apart.
    ///
    /// @p desktopDirectiveMatched, when non-null, is set true only when a
    /// RouteToDesktop directive matched. It is deliberately SEPARATE from
    /// @p directiveMatched, which is also set by RouteToScreen and SnapToZone:
    /// a rule that pins a window to a monitor says nothing about which virtual
    /// desktop it belongs on, so it must not suppress the cross-desktop session
    /// restore. This is the same split the snap facade applies.
    QString applyOpenRoutingForTiling(const QString& windowId, const QString& screenId,
                                      bool* directiveMatched = nullptr, bool* desktopDirectiveMatched = nullptr);

    /// Canonical key for daemon-local per-window shadow maps, and the canonical
    /// form sibling adaptors must agree on for per-window state. Window ids
    /// reach the daemon in both a raw compositor form and the registry's
    /// canonical form for the same window (a class-mutating app renames
    /// mid-life), so any map keyed on the caller-supplied id splits into two
    /// independent entries. Returns @p windowId unchanged when no registry is
    /// wired.
    QString shadowWindowId(const QString& windowId) const;

    /// Engine-neutral RouteToScreen: if a matched rule pins @p windowId to a
    /// different monitor, move the window there free. The route is honoured
    /// whether or not the same rule also carries SnapToZone — a route + snap
    /// normally places on the target through the snap placement directive and
    /// never reaches here, but calculateSnapToPlacementRule declines whenever the
    /// routed target is not in Snapping mode or resolves no layout, ordinal or
    /// geometry, and those declines land here with nothing placed.
    /// Translates the window's current frame geometry onto the target screen's
    /// available area (preserving its relative position, clamped to fit) and emits
    /// applyGeometryRequested with an empty zone id (a free placement, no snap
    /// chrome) plus the windowOutputMoveExpected marker. Called from the snap
    /// open-path facade only when nothing snapped the window, so a SnapToZone
    /// restore or a remembered snap takes precedence and the explicit route wins
    /// over a remembered float position. No-ops when the target is unset, the spawn
    /// screen, or not currently connected, or when the window has pushed no geometry
    /// yet. A target in autotile mode is moved (not tiled) — cross-engine tiling
    /// insertion stays with the autotile spawn path (applyOpenRoutingForTiling).
    /// Returns true when a RouteToScreen (or placement) directive MATCHED —
    /// whether or not a move was physically possible — so the caller knows the
    /// rule system owns this window's monitor and must not apply a
    /// remembered-placement fallback (the cross-screen tile reclaim).
    bool applyOpenScreenRouting(const QString& windowId, const QString& screenId);

    /// Shared by the two open-routing entry points: if @p resolved carries a
    /// RouteToDesktop action, emit windowDesktopMoveRequested for @p windowId.
    /// Returns whether the action MATCHED (see applyOpenDesktopRouting).
    bool emitRouteToDesktopIfMatched(const PhosphorRules::ResolvedActions& resolved, const QString& windowId);

    /// Cross-desktop session restore, run on BOTH open channels ahead of any
    /// placement (TilingAdaptor::dispatchWindowOpened for the tiling engines,
    /// SnapAdaptor::resolveWindowRestore for snapping).
    ///
    /// A Wayland session restores no virtual-desktop membership of its own, so
    /// every window reopens on whichever desktop is current and a multi-desktop
    /// layout collapses onto one. The placement records already carry the
    /// desktop; this asks the compositor to put the window back on it.
    ///
    /// Returns TRUE when a move was requested, and the caller must then place
    /// NOTHING: the window is on its way to a desktop this screen is not
    /// showing, and the engines insert into the screen's CURRENT context
    /// (AutotileEngine::currentKeyForScreen and its twins), so placing now would
    /// tile the window into the wrong desktop's state and immediately strand it
    /// off-screen. The effect's desktop-return catch-scan
    /// (TilingHandler::slotScreensChanged) re-announces it when that desktop is
    /// next shown, and the ordinary restore machinery then runs with the record
    /// context and the live context finally in agreement — which is also why the
    /// record must not be consumed here.
    ///
    /// Gated on ISettings::restoreWindowsToDesktopOnLogin AND on the record
    /// carrying WindowPlacement::fromPersistedSession, so it can only fire for a
    /// placement that predates this daemon's start. It CLEARS that flag on the
    /// record it acted on, which is what makes "at most once per record" hold
    /// even when the engine restore declines and never consumes the record.
    /// Suppressed on BOTH channels by a matched RouteToDesktop directive, and by
    /// that directive alone: such a rule is an explicit instruction and outranks
    /// a remembered desktop. It is deliberately NOT suppressed by a RouteToScreen
    /// or SnapToZone match, which pin the window's monitor or zone and say
    /// nothing about which virtual desktop it belongs on.
    bool applyPersistedDesktopRestore(const QString& windowId);

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
     *      `RuleStore::rulesChanged` whose post-filter placement-exclusion
     *      slice (Exclude ∪ ExcludePlacement) differs
     *      from the cached one (equality-guarded). Drives live rule edits into the prune.
     *   3. Daemon::finalizeStartup, after AutotileEngine::loadState has restored its
     *      placement records, so any autotile records loaded then are pruned too.
     * The daemon derives @p patterns from the unified Rule store via
     * `PhosphorRules::ExclusionRules::applicationExcludePatternsFrom`.
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
     * @brief Qt signal emitted during pruneStaleWindows with the INSTANCE-id
     * view of the alive set, so sibling adaptors can sweep their own
     * per-window caches in the same key space (TilingAdaptor's
     * float-broadcast and tab-colour-relay dedup maps are the current
     * consumers). Same in-process,
     * not-part-of-the-wire-contract stance as windowClosedNotification —
     * and like every adaptor signal it IS auto-relayed onto the bus, which
     * is why the payload is a marshallable QStringList rather than QSet.
     */
    void stalePruned(const QStringList& aliveInstances);

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
     *        changeType: "snapped", "unsnapped", "floated", "unfloated", "screen_changed".
     *        BEST-EFFORT fields: the float-toggle and screen-changed emitters
     *        deliberately send empty zoneIds and isSticky=false rather than
     *        re-querying — subscribers needing those must pull them
     *        (getMultiZoneForWindow / sticky query) instead of trusting this
     *        stream's snapshot.
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
    ///
    /// @p sourceScreenId names the screen the window is leaving, for the arm
    /// sites that know it authoritatively before the placement runs. It is
    /// empty when the marker is armed ahead of any placement work (engine
    /// relays, open-time routing): the compositor's own notified-screen record
    /// is still the pre-move screen at that point, so it can serve as the
    /// source itself. Sites that arm AFTER placing the window must pass the
    /// source explicitly — by then the tile requests have already re-pointed
    /// the compositor's record at the destination.
    void windowOutputMoveExpected(const QString& windowId, const QString& targetScreenId,
                                  const QString& sourceScreenId);

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
     *
     * Routes by the (validated/recovered) screen's mode to a DEST engine, with
     * the other engine as SOURCE (autotile or scrolling adopt via the
     * cross-engine handoff; snap is handled locally). A window the destination
     * does not yet track goes through the cross-engine handoff contract first:
     * floats adopt unconditionally (releasing a tracked source); an unfloat
     * whose float bit lives in the source is adopted by a tiling destination
     * (tiling the arrival) or, for a snap destination, released from the source
     * with a not-floating broadcast. The suspension-float classification is
     * stamped before routing. Used by minimize/unminimize, drag-to-float, and
     * monocle unmaximize handlers.
     */
    void setWindowFloatingForScreen(const QString& windowId, const QString& screenId, bool floating);

public:
    // Internal-only members below — plain `public:` placement (not Q_SLOTS)
    // to keep QDBusAbstractAdaptor's runtime introspection from exposing
    // them on the bus when the XML doesn't list them. Same pattern as the
    // `pruneExcludedPendingRestores` / `requestReapplyWindowGeometries`
    // pair above.
    /**
     * @brief Single broadcast chokepoint for engine-relayed float changes.
     *
     * Updates m_broadcastFloating and emits windowFloatingChanged, deduped
     * against the last value actually broadcast. Engine signal relays MUST
     * route through this (never signal-to-signal into windowFloatingChanged,
     * never a bare Q_EMIT): a direct emission leaves m_broadcastFloating
     * stale, and the setWindowFloating gate then suppresses the next genuine
     * change on the engine-sync channel (e.g. a cross-mode handoff's
     * float-cleared sync after a snap-side float). Returns whether a
     * broadcast was actually emitted, so setWindowFloating (which delegates
     * its gate here) can ride its extra side emissions on the same edge.
     */
    bool relayWindowFloatingChanged(const QString& windowId, bool floating, const QString& screenId);
    /**
     * @brief Apply the remembered float-back geometry for a floated window
     * (call from daemon when the autotile engine floats it).
     * Resolves via WindowTrackingService::validatedUnmanagedGeometry and emits
     * applyGeometryRequested when a rect is found. The stored geometry is NOT
     * consumed — the record stays put for the next float/restore (per-screen
     * clears happen only on the consume-once drag-out paths).
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

private:
    /**
     * @brief Body of handleCrossModeMove with the source engine explicit.
     *
     * The slot recovers its source from sender(), which is only valid inside
     * a signal dispatch — the swap handler's no-partner degrade calls this
     * directly with the engine it already holds, so the degrade keeps working
     * from any invocation context (tests, invokeMethod), not just a nested
     * slot call where sender() happens to survive.
     */
    void crossModeMoveImpl(PhosphorEngine::PlacementEngineBase* sourceEngine, const QString& windowId,
                           const QString& targetScreenId, int targetDesktop, const QString& direction);

    /**
     * @brief The engine owning @p mode, or null when that engine is absent.
     *
     * Single mode→engine mapping for the three cross-mode handlers, so a
     * fourth mode (or engine) is added in one place.
     */
    PhosphorEngine::PlacementEngineBase* engineForMode(PhosphorZones::AssignmentEntry::Mode mode) const;

private Q_SLOTS:

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
     * @brief Orchestrate a cross-MODE directional FOCUS crossing.
     *
     * Wired to the scroll and autotile engines' crossModeFocusRequested (each
     * probes its own same-mode neighbour first and defers here only for a
     * different-mode one). Resolves the target
     * mode at the destination context, asks that engine for its entry-edge
     * window facing the source in @p direction (autotile/scroll:
     * entryWindowForCrossing; snap: the entry zone's occupant), and activates
     * it. No window travels and no engine state is touched — the compositor's
     * focus report updates each engine naturally.
     *
     * @p handled (nullable) is set true only when an activation was actually
     * issued. The connection is DirectConnection, so the emitting engine
     * reads the verdict on return and reports no_target for an empty entry
     * edge instead of announcing a crossing that never happened.
     */
    void handleCrossModeFocus(const QString& targetScreenId, const QString& direction, bool* handled);

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

    /// Tile-rect poison guard, shared by captureWindowPlacement's primary
    /// free-geometry write and its engine-miss close fallback: true when the
    /// live @p frame still equals the tile rect a tiling-family engine
    /// (autotile or scrolling) last applied to @p windowId (each engine
    /// remembers it PAST the tiled-bit
    /// clear, past a cross-engine handoff, and past its own windowClosed
    /// teardown — see AutotileEngine::lastManagedRect). Such a frame is a
    /// managed rect, not a genuine free position, and must never become the
    /// float-back. The autotile analogue of the snap-side stillOnSnapRect
    /// zone comparison.
    ///
    /// The close ordering makes the engine's retention load-bearing: the
    /// effect notifies autotile of a close BEFORE WindowTracking (same
    /// connection, in-order delivery), so a window closing tiled on an
    /// autotile screen reaches this capture already untracked — both
    /// engines' capturePlacement decline and the isWindowEngineTiled gate
    /// reads false — and takes the close-path fallback with its live frame
    /// still on the tile rect. Only the retained memory lets this guard
    /// refuse that frame. The guard therefore covers: a float toggle in
    /// autotile mode (performToggleFloat clears the tiled bit before this
    /// capture reaches it — the primary regression), a tiled window handed
    /// off to a non-autotile screen and captured or closed there, and a
    /// tiled close on the autotile screen itself. In each, the live frame
    /// has not yet moved off the tile rect.
    bool isFrameStillOnTileRect(const QString& windowId, const QRect& frame) const;

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
    //
    // Keyed on CANONICAL window ids. The effect pushes the window's current
    // composite, but captureWindowPlacement reads this map with canonical ids
    // on the engine-relay path, so both writes and reads translate through
    // shadowWindowId() and the stale sweep uses the canonical alive set.
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
    /// Late-bound scrolling strip-snapshot provider (setScrollStripStateProvider)
    /// and the blob the last loadState() read for the daemon to hand to the
    /// engine once it exists.
    std::function<QJsonObject()> m_scrollStripStateProvider;
    QJsonObject m_loadedScrollStripState;
    PhosphorWorkspaces::VirtualDesktopManager* m_virtualDesktopManager;
    PhosphorWorkspaces::ActivityManager* m_activityManager;
    std::unique_ptr<PhosphorConfig::IBackend> m_sessionBackend; // Session state (session.json)

    // Engine references for per-screen routing (set via setEngines())
    // QPointer auto-nulls on engine destruction, guarding against late D-Bus calls
    QPointer<PhosphorEngine::PlacementEngineBase> m_snapEngine;
    QPointer<PhosphorEngine::PlacementEngineBase> m_autotileEngine;
    QPointer<PhosphorEngine::PlacementEngineBase> m_scrollEngine;
    QPointer<PhosphorSnapEngine::SnapEngine> m_cachedSnapEngine;
    QPointer<PhosphorTileEngine::AutotileEngine> m_cachedAutotileEngine;
    QPointer<PhosphorScrollEngine::ScrollEngine> m_cachedScrollEngine;

    // Central dispatcher: adaptor methods route lifecycle / resnap /
    // restore calls through this instead of direct engine pointer checks.
    // Null until setScreenModeRouter is called (Daemon wires during init).
    ScreenModeRouter* m_screenModeRouter = nullptr;

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
    // evaluator over its full rule set. One evaluator serves every per-window
    // resolver: the cacheable ones share its resolveCached memo, while the
    // rest stamp per-call context or need a per-query filter and go through
    // resolve()/resolveFiltered() on the same instance, which neither reads
    // nor seeds that memo — see rules.cpp and rules_placement.cpp for which
    // resolver takes which path (the enumeration lives with the code, not
    // here, so it cannot go stale). The evaluator self-invalidates
    // on in-place rule edits via the set revision, so it is built once on first
    // use. Reset in setRuleStore only when the store pointer actually
    // changes (a same-store rebind keeps the evaluator).
    PhosphorRules::RuleStore* m_ruleStore = nullptr;
    /// Full-store evaluator serving every per-window policy resolver,
    /// scope-narrowed to the placement-exclusion family — construction and
    /// scope both live in ensureRuleEvaluator() below.
    std::unique_ptr<PhosphorRules::RuleEvaluator> m_ruleEvaluator;

    /// Build m_ruleEvaluator on first use (no-op when already built). The one
    /// construction site for the full-store evaluator, so the terminal-action
    /// scope is set in exactly one place: this adaptor resolves PLACEMENT
    /// policy (open placement, routing, restore, scroll open params), so only
    /// the placement-exclusion family (Exclude / ExcludePlacement) may stop
    /// its walks. Without the scope, a terminal ExcludeDecorations /
    /// ExcludeAnimations action on a matching rule would cancel every
    /// lower-priority rule's placement actions — the leak the scoped actions'
    /// docs promise cannot happen. Callers must check m_ruleStore first.
    void ensureRuleEvaluator();

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
