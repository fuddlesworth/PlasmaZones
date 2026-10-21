// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// This header declares BOTH the D-Bus wire surface (slots whose signatures are
// pinned by the interface XML) and the in-process orchestration API the daemon
// wires. Splitting would sever the wire methods from the state they document
// against.

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
#include <PhosphorConfig/IBackend.h>

#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QVariantMap>
#include <QJsonObject>
#include <QQueue>
#include <QRect>
#include <QTimer>
#include <QPointer>
#include <functional>
#include <memory>
#include <optional>

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
struct EvacueeLedger;
class WindowLifecycleRelay;

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

    /// The focused window's screen, read from the engine that holds it (snap,
    /// autotile, scroll), else the last activation or crossing report
    /// (windowActivated, activeWindowScreenChanged). Window shortcuts act on it.
    QString lastActiveScreenName() const override;

    /// The cursor's screen, the fallback shortcuts use when no window has focus.
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

    /// The in-process close and prune notices for sibling adaptors.
    WindowLifecycleRelay* lifecycleRelay() const
    {
        return m_lifecycleRelay;
    }

    /**
     * @brief Set ZoneDetectionAdaptor for daemon-driven navigation (getAdjacentZone, getFirstZoneInDirection)
     * @param adaptor ZoneDetectionAdaptor instance (not owned; Daemon::stop() clears it).
     *        Must be set before setEngines, which hands it to the snap engine as its adjacency resolver.
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
     * @brief Bind the unified window-rule store (daemon-owned) for the
     *        per-window rule resolvers.
     *
     * Non-owning. Every per-window rule resolver here reads it: placement,
     * float, routing, restore, the scroll open parameters, tab colours and the
     * drop indicator. A lazily-built RuleEvaluator binds to the store's full
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
     * SnapAdaptor's ctor relays most snap engine signals; this wires
     * windowSnapStateChanged and windowFloatingClearedForSnap.
     *
     * @param snapEngine PlacementEngineBase for snap mode (not owned; a QPointer,
     *        and Daemon::stop() rewires all three to null)
     * @param autotileEngine PlacementEngineBase for autotile mode (likewise)
     * @param scrollEngine PlacementEngineBase for scrolling mode (likewise;
     *        explicit at every call site — no default, so a production
     *        caller cannot silently drop the scroll engine)
     */
    void setEngines(PhosphorEngine::PlacementEngineBase* snapEngine,
                    PhosphorEngine::PlacementEngineBase* autotileEngine,
                    PhosphorEngine::PlacementEngineBase* scrollEngine);

    /// The frozen-snapshot resolver for the disable gates, late-bound like
    /// setEngines (it is constructed after this adaptor); nullptr at shutdown.
    void setContextResolver(PhosphorContext::IContextResolver* resolver)
    {
        m_contextResolver = resolver;
    }

    /**
     * @brief Wire the scrolling strip-structure snapshot provider, which
     * saveState() calls under DirtyScrollStrips for
     * ScrollEngine::serializeStripState's blob (late-bound like setEngines).
     * Pass {} at teardown only AFTER the final saveStateOnShutdown(): an
     * absent provider makes saveState SKIP the strips write (leaving the
     * stored blob alone), so clearing early silently drops every strip
     * mutation from the last debounce window.
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

    /// The virtual desktop ids the session last saw, in position order (KnownDesktopIds). The daemon hands them to
    /// VirtualDesktopManager::setBaselineDesktopIds at start-up and sets the live list on every change.
    QStringList knownDesktopIds() const
    {
        return m_knownDesktopIds;
    }
    /// Marks DirtyKnownDesktops; the list already held marks nothing.
    void setKnownDesktopIds(const QStringList& ids);

    PhosphorSnapEngine::SnapEngine* snapEngine() const;

    /**
     * @brief Wire the daemon's central ScreenModeRouter.
     *
     * Read by resolveSnapModeScreensForResnap, which keeps a resnap off the
     * screens snapping does not run on.
     *
     * @param router Not owned; Daemon::stop() clears it before freeing it
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

    // The SnapNavigationTargetResolver lives on SnapEngine (ensureTargetResolver);
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
     * Populates the daemon's WindowRegistry (its first record and each title or app
     * change reach the bus as windowMetadataChanged); consumers subscribe to its Qt signals.
     *
     * Safe to call unconditionally on every observation — no-op if metadata
     * is unchanged.
     */
    void setWindowMetadata(const QString& instanceId, const QString& appId, const QString& desktopFile,
                           const QString& title, const QString& windowRole, int pid, int virtualDesktop,
                           const QString& activity, int windowType, const QVariantMap& extended);

    // windowSnapped, windowSnappedMultiZone, windowUnsnapped, windowsSnappedBatch,
    // recordSnapIntent moved to SnapAdaptor (org.plasmazones.Snap D-Bus interface).

    /// A snapped window was dragged without the activation trigger and dropped
    /// on its own screen: unsnap it as a drag-out (floating, zone cleared).
    void notifyDragOutUnsnap(const QString& windowId);

    /// KWin moved a window between screens no tiling engine runs: its snap memory off
    /// the new one goes (its zone, unless the daemon put it there; a float's home).
    void windowScreenChanged(const QString& windowId, const QString& newScreenId);
    /// KWin moved a window across screens a tiling engine runs: drop every hold off the new one.
    void windowCrossedScreens(const QString& windowId, const QString& fromScreenId, const QString& toScreenId);
    /// The effect's screen-change settle report: one verdict per window (see WindowTracking.xml).
    PhosphorProtocol::OutputSettleVerdictList reportOutputSettle(const PhosphorProtocol::OutputSettleRowList& rows);
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
     * No in-tree caller: the effect unfloats through
     * setWindowFloatingForScreen(false). Kept as external contract
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
     * @param overwrite True when the frame is a known-fresh free position (always written); false keeps an
     *        existing entry
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
     * Pre-tile geometry for every live record (appId, rect, screen, windowId).
     * The consumer applies an entry only to the window it names. An
     * effect-(re)start seed.
     */
    PhosphorProtocol::PreTileGeometryList getPreTileGeometries();

    /**
     * Clean up all tracking data for a closed window
     * @param windowId Window ID that was closed
     * @param windowKind PhosphorEngine::WindowKind wire value (Unknown/Normal/
     *        Transient). Unused: it gated the pending-restore queue, which was
     *        removed; kept so the wire signature does not change
     * @param screenId The window's authoritative current screen at close (the effect's
     *        getWindowScreenId). Threaded into the final placement capture so a
     *        window dragged cross-screen and closed records its float-back on the
     *        screen it actually closed on — by close time a cross-screen move has
     *        torn down every engine's tracking, so no capturePlacement can
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
     * Called by the compositor plugin on windowFrameGeometryChanged (throttled to one push per window per
     * 50 ms) for every window it manages, kept-above ones included, never for one an exclusion rule matches.
     * Daemon-local shortcut handlers (float toggle, etc.) read the shadow to compose pre-tile geometry
     * without a round-trip back to the effect.
     *
     * ABSENCE IS NOT LATENCY. Three effect pushes call this setter: the
     * motion-driven flush, the bulk seed on daemon (re)registration
     * (kwin-effect daemon_bringup.cpp, which states the consequence), and
     * the open-path seed the snap handler pushes immediately before
     * resolveWindowRestore; notifyWindowResized writes the shadow too at a
     * resize's end. A window that has never MOVED and is not being opened therefore has no
     * entry at all — not one that arrives 50 ms later — so callers deciding policy on an
     * invalid read must not assume a retry would populate it.
     *
     * The open-path seed exists because that assumption was made and was
     * wrong: applyOpenScreenRouting translates a bare RouteToScreen from
     * this shadow, and with only the first two pushes a freshly opened
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
     * the interaction was a resize (not a move). Routes to the scroll engine when it holds the
     * window in view (the column takes the size), else to autotile, which reflows the neighbours
     * (GitHub #652). The old/new frames are supplied directly by the plugin (latched at
     * resize start / read at finish) because the throttled frame shadow updates
     * mid-drag and can't serve as a reliable baseline. No-op for a window
     * neither tiling engine holds.
     *
     * @param windowId Window identifier
     * @param oldX,oldY,oldWidth,oldHeight Frame geometry before the resize
     * @param newX,newY,newWidth,newHeight Frame geometry after the resize
     */
    void notifyWindowResized(const QString& windowId, int oldX, int oldY, int oldWidth, int oldHeight, int newX,
                             int newY, int newWidth, int newHeight);

    // The effect's reports to the daemon are named for the event they report
    // (windowActivated, windowScreenChanged, cursorScreenChanged,
    // screenDesktopChanged, activeWindowScreenChanged). That is this interface's
    // convention for notices; the daemon's own requests use verbs.

    /// The cursor's screen changed: across monitors, across the virtual screens
    /// of one, or a re-send at daemon-ready. @p screenId is the effective id the
    /// effect resolved; one the daemon does not know yet is held (F81).
    void cursorScreenChanged(const QString& screenId);

    /// The focused window changed output without a new activation (a user or
    /// KWin move, or a daemon apply). Shortcuts act on the focused window's
    /// screen, otherwise refreshed only by windowActivated. Updates only that
    /// record, for the focused window, with none of an activation's effects.
    void activeWindowScreenChanged(const QString& windowId, const QString& screenId);

    /**
     * Record a screen's current virtual desktop (Plasma 6.7 per-output virtual
     * desktops). The effect calls it on desktopChanged, on a window activation
     * and in its bring-up re-sync. Forwarded to updateScreenDesktop: KWin's own
     * D-Bus interface only exposes the global current desktop.
     * @param screenId Physical screen whose desktop changed
     * @param desktop  The screen's current virtual desktop, 1-based
     */
    void screenDesktopChanged(const QString& screenId, int desktop);
    /// The desktop an output shows as it is (re)connected: a seed, not a
    /// switch (VirtualDesktopManager::seedScreenDesktop, F700).
    void seedScreenDesktop(const QString& screenId, int desktop);

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
     * The window's own free geometry on the screen it is tracked on (its record's screen when untracked)
     * @param windowId Window ID
     * @param x Output: X position (adjusted if off-screen)
     * @param y Output: Y position (adjusted if off-screen)
     * @param width Output: Width (adjusted if off-screen)
     * @param height Output: Height (adjusted if off-screen)
     * @return true if geometry was found and validated, false otherwise
     * @note A rect that does not lie on that screen answers false. It is moved onto a screen
     *       only when no screen manager can measure the output
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
    /// the rule evaluator's memo, and the adaptor's own shadow maps. Called by
    /// the effect after daemon ready. The first call per daemon also releases
    /// the engine slots of every live window that left its recorded screen
    /// while no daemon was watching.
    void pruneStaleWindows(const QStringList& aliveWindowIds);

    /// Re-drive compositor-side per-window appearance (snap border / hidden
    /// title bar, autotile border) for every window each engine manages. Called
    /// by the KWin effect once the daemon is ready: on a daemon or effect
    /// restart the compositor drops its window-chrome state, so it must be
    /// re-applied from the daemon's authoritative placement state. Delegates to
    /// IPlacementEngine::reapplyManagedWindowAppearance() on all three engines;
    /// snap re-states only the zones it places in view, so nothing moves.
    void reapplyWindowAppearance();

    /**
     * Get typed list of empty zones for Snap Assist continuation
     * @param screenId Screen id (EDID-based, or a virtual-screen id)
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

    // snapToLastZone, recordSnapIntent, snapToAppRule, snapToEmptyZone and
    // resolveWindowRestore moved to SnapAdaptor
    // (org.plasmazones.Snap D-Bus interface).

    /**
     * The zone rect each snapped window holds in the context in view, for the
     * re-apply after a work-area settle, a resolution or virtual-screen change.
     * Entries are (windowId, x, y, width, height, screenId), applyGeometriesBatch's
     * shape. Empty while keepWindowsInZonesOnResolutionChange or snapping is off.
     */
    PhosphorProtocol::WindowGeometryList getUpdatedWindowGeometries();

    /**
     * @brief Zone rects for pending restores, one ARRAY per app, newest first:
     *        { appId: [ {x, y, width, height, screenId, windowId}, ... ], ... }
     * The effect caches these to teleport an opener to its zone without a round
     * trip, taking the newest entry on the opener's output whose instance is not
     * live, so a second window never gets the rect its open sibling still uses.
     * Empty while snapping or the zone restore is off, or while an enabled rule
     * can turn that restore off for a window, since the async restore would refuse.
     * The effect fetches it again on pendingRestoresChanged.
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
     * @brief The state of every window an engine holds, in any context (TUI dashboard, shell maps).
     * @return List of PhosphorProtocol::WindowStateEntry structs
     */
    PhosphorProtocol::WindowStateList getAllWindowStates();

    // ── Phosphor shell surface (placement map click/drag verbs). All four
    // read the WindowRegistry, so "tracked" here means the compositor has
    // registered the window through setWindowMetadata, which it does for
    // every window before any report naming its context (only the sticky bit
    // goes first). Bodies in shellsurface.cpp. ──

    /**
     * @brief Ask the compositor to activate (focus) a tracked window.
     *
     * Emits activateWindowRequested for @p windowId, the same signal the
     * daemon-driven focus verbs use and the effect turns into KWin's
     * activateWindow. A window the registry does not know is a silent
     * no-op: the id came from a stale model, not a user intent the daemon
     * should second-guess.
     */
    void activateWindow(const QString& windowId);

    /**
     * @brief A tracked window's identity as the compositor last reported it.
     *
     * Returns the app id, with @p title and @p desktopFile as the further
     * out-arguments. Every field is empty for an unknown window, and title
     * or desktopFile are empty when the compositor reported them so.
     */
    QString getWindowMetadata(const QString& windowId, QString& title, QString& desktopFile);

    /**
     * @brief Ask the compositor to move a tracked window to virtual desktop
     *        @p desktop (1-based).
     *
     * Emits windowDesktopMoveRequested, which the effect already consumes
     * for the cross-desktop directional move (windowToDesktops). Silent for
     * an unknown window or a desktop outside 1..desktopCount.
     */
    void moveWindowToDesktop(const QString& windowId, int desktop);

    /**
     * @brief Every tracked window currently demanding attention.
     *
     * The daemon keeps this set from the isDemandingAttention field of the
     * compositor's metadata pushes, which the KWin effect re-sends on every
     * demandsAttentionChanged edge, and drops a window on close.
     */
    QStringList getUrgentWindows();

    // ── Phosphor shell surface, per-desktop reads (shellsurface_desktop.cpp).
    // Both answer from the desktop / pid ledger the registry mirror keeps
    // beside the urgent set: one row per registered window, refreshed when the
    // window's registry record changes (a push repeating it does not count) and
    // dropped on close. ──

    /**
     * @brief getAllWindowStates, filtered to one screen and desktop.
     *
     * @p virtualDesktop is 1-based; 0 means the screen's current desktop.
     * A window is on a desktop when its last metadata push said so
     * (virtualDesktop, or any entry of the spanned list), or when it is
     * sticky (isSticky, or virtualDesktop 0 meaning "all"), which lands it
     * on every desktop. An empty @p screenId means any screen, except that a
     * desktop of 0 then answers nothing. Windows the registry does not know
     * are left out: without a push there is no desktop to compare against.
     */
    PhosphorProtocol::WindowStateList getWindowStatesForDesktop(const QString& screenId, int virtualDesktop);

    /**
     * @brief The tracked window whose metadata pid is @p pid.
     *
     * When several windows share the pid (a multi-window app), the one whose
     * registry record changed last wins; a push that repeats a record does
     * not move it. Empty when no window matches or @p pid is not positive.
     */
    QString findWindowByPid(int pid);

    /**
     * @brief Whether the engine running the window's current screen mode floats it
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

    /// @brief The bus verb: setWindowFloatingForScreen on the window's live screen. Refused (logged) for an
    /// unregistered or screenless window, one off the desktop or activity its screen shows, or a disabled context.
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
     * @param screenId Screen ID. It must name a resolvable output: an empty or
     *        unknown id yields an all-zero rect rather than the zone measured
     *        against the primary monitor. Callers that mean the primary screen
     *        want the getZoneGeometry() overload, which resolves it by name.
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

    /// The in-process engine sync: the engine already flipped its float bit, so this writes WTS and
    /// broadcasts. Never route it through setWindowFloatingForScreen, which would toggle the engine again.
    void syncWindowFloating(const QString& windowId, bool floating);

    /// Drop what every engine holds of @p windowId off @p keepScreenId (screenleave.cpp).
    void releaseLeftScreens(const QString& windowId, const QString& keepScreenId,
                            const PhosphorEngine::IPlacementEngine* arrival);
    /// True if it handled a drop with no snap commit: off the screen snap holds it on, or plain on a tiling screen.
    bool dragEndedOnScreen(const QString& windowId, const QString& releaseScreenId, bool floatIfSnapped);

    /// Internal: the QRect itself, without the D-Bus ZoneGeometryRect wrapper, for daemon-internal callers.
    QRect zoneGeometryRect(const QString& zoneId, const QString& screenId);

    /**
     * @brief Save window tracking state to disk
     *
     * Writes the fields marked dirty since the last save: the active layout id,
     * the last used zone id, the user-snapped classes, the unified placement
     * records (every open window re-captured first, see
     * refreshOpenWindowPlacements) and the scrolling strips. Float state is
     * session-only and never written. Called from the debounced save timer;
     * can also be called to force a save.
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
     * saveState() is called once. Wired to WindowTrackingService::stateChanged
     * in the constructor, so every markDirty schedules it; pruneStaleWindows
     * calls it when it pruned persisted state.
     */
    void scheduleSaveState();

    /// Unified placement capture orchestrator: ask each engine for @p windowId's
    /// current placement, stamp it with the live frame geometry, and record it in
    /// the WindowPlacementStore. When NO engine manages the window, any existing
    /// record is left intact (never cleared here — records are per-mode memory,
    /// only merge-updated, consumed, or explicitly pruned); with an
    /// @p authoritativeScreen it records a floating-close placement instead.
    ///
    /// A record left unrepaired is read at the next open: the reopen claim
    /// (IPlacementEngine::claimCrossScreenReopen) takes a managed slot plus
    /// the record-level screenId as a restorable home on that output. An
    /// engine that gives a window up releases its slot (releaseEngineSlot,
    /// from handoffRelease), and downgradeMismatchedManagedSlots repairs
    /// the close paths that do run.
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
    /// @p authoritativeScreen (the close path passes the effect's getWindowScreenId)
    /// therefore plays TWO roles when non-empty: it marks the capture as a
    /// close-with-known-screen (enabling the minimize preserve branch above
    /// and the pure-float sibling collapse), and it is the fallback screen
    /// when no engine produces a placement, the case where a
    /// cross-screen move removed the window from the source engine's tracking
    /// and the destination never adopted it, so every capturePlacement
    /// returns nullopt and the live screen would otherwise be lost. In that
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

    /// Announce that @p windowId no longer occupies a zone in the context it
    /// was released from, as an "unsnapped" windowStateChanged entry. For the
    /// releases that do not run through SnapEngine::uncommitSnap:
    /// TilingAdaptor::applyMembershipResult, for the per-window
    /// reconcileWindowMembership and for the per-screen
    /// reconcileDesktopMemberships the daemon runs after a desktop or activity
    /// switch. That pass emits no windowSnapStateChanged, whose per-window tiling float-marker
    /// clear would pull a window floating on its new desktop into the layout.
    void relayWindowReleasedFromContext(const QString& windowId, const QString& screenId);
    /// Relay a prune: "unsnapped" per window left in no zone, spans re-stated.
    void relayZonePrune(const PhosphorPlacement::WindowTrackingService::ZonePruneResult& result);
    /// A layout switch on @p screens (all when empty) minus @p excludeScreens,
    /// on @p desktop: populate, resnap silently, then prune and relay what the
    /// switch could not carry (F474, F475).
    void resnapScreensToTheirLayouts(const QSet<QString>& excludeScreens, const QSet<QString>& screens, int desktop);

    // ── Evacuee park (evacuees.cpp; see EvacueeLedger) ──────────────────────
    /// Park every engine's windows on @p physicalScreenId before the prunes,
    /// and release their record's snap slot naming it. Returns the windows.
    QStringList parkOutput(const QString& physicalScreenId);
    /// The tiling engines' record slots of the windows parked for @p physicalScreenId, after their prunes.
    void releaseParkedSlots(const QString& physicalScreenId);
    /// The daemon's retire primitive, run when a settle reports an output gone before screenRemoved did.
    void setOutputRetirer(std::function<void(const QString&)> retirer);
    /// The window an interactive drag moves (empty clears): its frames are not settled managed frames.
    void setInteractiveDragWindow(const QString& windowId);
    /// The drag subject: no placement verb acts on it until the drop (F773).
    bool isInteractiveDragWindow(const QString& windowId) const;
    /// Told (windowId, keepScreenId) at the end of every releaseLeftScreens, for the daemon's own per-screen memory.
    void setWindowLeftScreenHook(std::function<void(const QString&, const QString&)> hook);
    /// Whether a unique bus name is the registered compositor bridge's, whose screen-form float is not gated (F1108).
    void setBridgeCallerCheck(std::function<bool(const QString&)> check);
    /// The mode a (screen, desktop, activity) context runs, hidden or not, the feature switches and the disabled gate
    /// included (Daemon::runningModeForContext). Unset, the configured mode answers (F1122).
    void setContextModeResolver(
        std::function<PhosphorZones::AssignmentEntry::Mode(const QString&, int, const QString&)> resolver);
    /// An evacuee that could not be adopted floating when its output went away, arriving on the output it
    /// landed on in a mode that owes it the float: floated there now. Taken once on any arrival. True when floated.
    bool adoptPendingEvacueeFloat(const QString& windowId, const QString& screenId);
    /// A window announced on the output it is parked for: re-seated from the park. True when it was.
    bool readoptOnArrival(const QString& windowId, const QString& screenId);
    /// Drop the window's parked contexts on desktops and activities @p span no longer covers.
    void dropParkedOutsideSpan(const QString& windowId, const PhosphorEngine::DesktopSpan& span);

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
     * @note Records of windows that died meanwhile are dropped by
     * pruneStaleWindows once the effect reports the live set.
     */
    void loadState();

    /// Build the per-window rule query: the screen trio, Mode and the live frame
    /// stamped on top of the WindowRegistry metadata, which holds no screen and
    /// only the open-time size. @p
    /// screenIdHint names the screen the window is landing on (the open /
    /// routing paths know it before the service does), and an empty hint falls
    /// back to the service's live screen-for-window. `ActiveLayout` resolves to
    /// the layout assigned to that screen's (desktop, activity) context, the
    /// same id the windowless context cascade stamps.
    ///
    /// This is the hint-bearing helper the snap engine's exclusion-query
    /// provider uses (enginewiring.cpp, its only caller). It is NOT the
    /// enforcement point for uniform stamping — the per-window resolvers call
    /// the bare buildRuleQueryForWindow and stamp their context through
    /// stampScreenContext, which is the mechanism that actually keeps them
    /// consistent.
    ///
    /// The stamping matters because the cached resolvers share one
    /// RuleEvaluator::resolveCached entry keyed on (windowId, rule-set
    /// revision), and resolveCached returns the cached actions WITHOUT
    /// consulting the query on a hit, so whichever of them touches a window
    /// first fixes the context every later one reuses for that window's
    /// lifetime. Which resolver takes the cached path, and when, is documented
    /// with the code in rules.cpp and rules_placement.cpp.
    ///
    /// Uniform stamping is therefore necessary but NOT sufficient: the hinted
    /// and unhinted paths resolve different screens, so the ORDER matters too.
    /// The hint-bearing resolver has to seed first, and does on both open paths today (snap's,
    /// and the tiling adaptor's for autotile and scrolling): SnapEngine::resolveWindowRestore calls
    /// calculateSnapToPlacementRule (phosphor-snap-engine lifecycle.cpp) ahead of the restore, managed-restore
    /// and float predicates, and TilingAdaptor::dispatchWindowOpened calls
    /// applyOpenRoutingForTiling ahead of the tiling engine's windowOpened.
    /// Reordering either would silently revert ActiveLayout / ScreenId matching
    /// on the open path without failing a test.
    ///
    /// nullopt when no metadata is tracked for @p windowId.
    std::optional<PhosphorRules::WindowQuery> buildContextualRuleQuery(const QString& windowId,
                                                                       const QString& screenIdHint = QString()) const;

    /// Mode-neutral screen lookup for @p windowId: the snap service first (it
    /// canonicalizes the composite id), then each engine's own tracker; empty
    /// when no engine placed the window or none is wired. Used by
    /// buildContextualRuleQuery when no caller supplied a hint (the service
    /// accessor alone is snap-only).
    QString resolveScreenForWindow(const QString& windowId) const;

    /// Resolve whether a FLOATED window should have its previous position restored
    /// on open. Consulted by the restore-position predicate the daemon injects into
    /// all three engines (in-process, not via D-Bus); @p mode selects which per-engine
    /// global default applies (snapping, autotile or scrolling). A matched
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
    /// daemon injects into all three engines (in-process, not via D-Bus). Unlike
    /// RestorePosition there is no global default — Float is purely rule-driven,
    /// so the answer is false unless a Float rule matches. The Float action's
    /// params are free-form, so the verdict is the presence of the filled slot.
    /// @p screenId is the OPENING screen; it stamps ScreenId and the derived
    /// Mode onto the query, without which a rule pairing either with Float is
    /// silently inert. Empty is tolerated (neither is stamped).
    bool shouldFloatByRule(const QString& windowId, const QString& screenId = QString());

    /// Per-window scrolling open-behaviour rule slots (openColumnWidth /
    /// openWindowHeight / openTabbed / openColumnPlacement / openMaximized /
    /// openFocused / openTabGroup), returned as a loose map so the header
    /// stays free of scroll-engine types. Keys, present only when the slot
    /// matched, and spelled by the ScrollOpenKeys namespace in internal.h
    /// rather than literals: widthFraction (double), heightFraction (double),
    /// tabbed (bool), consume (bool), maximized (bool), focused (bool),
    /// tabGroup (trimmed non-empty string).
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
    /// (TilingAdaptor::scrollTabColors) and per registry record change (the
    /// per-window relay) rather than once per window open, so it is kept to
    /// a slot read with no screen or mode stamping.
    QVariantMap tabColorRuleParams(const QString& windowId);

    /// Per-window drop-indicator colours for @p windowId, keyed by the QML
    /// property names the overlay slot reads. Resolved once at DRAG START from
    /// the dragged window's rules — the only per-window slice of the drop
    /// indicator with a coherent referent, since exactly one window is dragged
    /// at a time. Unmemoised, unlike tabColorRuleParams: once per drag rather
    /// than per tab per effect query.
    QVariantMap dropIndicatorRuleParams(const QString& windowId);

    /// Drop the tab-colour memo because the system colour scheme flipped.
    ///
    /// Only that memo, and as a determinism belt: its key already carries the
    /// scheme token. The shared evaluator memo is kept on purpose (rules.cpp):
    /// its cached verdicts are open-time answers that stand until the rule
    /// revision moves, and clearing it mid-session would let an unstamped
    /// resolver seed it first.
    ///
    /// Wired by the daemon to ISettings::systemColorSchemeChanged.
    void invalidateRuleMemosForColorSchemeChange();

private:
    /// Extract the three tab-colour slots from an already-resolved verdict.
    /// Shared by the memo-hit and memo-miss paths so both produce the same map.
    static QVariantMap tabColorsFromResolved(const PhosphorRules::ResolvedActions& resolved);

    /// tabColorRuleParams' PRIVATE memo, deliberately separate from the
    /// RuleEvaluator's shared one. Seeding the shared memo would poison it (its
    /// key excludes the admit filter, and this query is unstamped) and break
    /// the stamper-first ordering invariant; reading it would miss forever for
    /// every already-open window (four seeders run only at open, two on unfloat
    /// and restore, and a rules save bumps the revision), and this runs per tab per colour
    /// query plus once per window per record change. It caches the extracted
    /// COLOUR MAP: the three slots are all this path reads, and the map keeps
    /// this header free of the rules-engine include.
    ///
    /// Keyed by the rule revision, the window's whole registry record and the
    /// colour-scheme token. The daemon re-drives this per window on every
    /// registry record change (the metadataChanged relay) and on a scheme flip,
    /// so a verdict follows every field the effect pushes (F699). isFocused and
    /// the live geometry are as fresh as the last push, which the effect sends
    /// on state, class, caption, desktop, activity and output edges, not on
    /// focus or every move.
    struct TabColorMemoEntry
    {
        quint64 revision = 0;
        PhosphorEngine::WindowMetadata metadata;
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
    void stampScreenAndMode(PhosphorRules::WindowQuery& query, const QString& windowId, const QString& screenId) const;

    /// Stamp the screen-derived context trio onto @p query: ScreenId, the
    /// ActiveLayout resolved for that screen's CURRENT desktop and activity
    /// (the same id the windowless context cascade stamps and the daemon
    /// publishes to the KWin effect, so the leaf means one thing everywhere),
    /// and ScreenOrientation. An empty @p screenId stamps nothing. Called by
    /// every resolver that pins a screen, cached and uncached alike, so the
    /// shared evaluator memo is always seeded with the full trio.
    void stampScreenContext(PhosphorRules::WindowQuery& query, const QString& screenId) const;

    /// Resolve the placement directive for a window from its matched window rules, for the open path, the Meta+F
    /// unfloat's rule tier and the Snap.snapToAppRule slot: the 1-based `SnapToZone` ordinals and/or zone names to
    /// snap into (both lists empty when no SnapToZone rule matches; multiple targets across the two lists request a
    /// zone span) plus the
    /// `RouteToScreen` target monitor (empty when unrouted) plus the
    /// `RouteToDesktop` target desktop (unset when unrouted, and only ever a
    /// 1-based desktop). The desktop slot steers where the zones RESOLVE, so a
    /// combined SnapToZone + RouteToDesktop rule lands in the right zone of the
    /// destination desktop; the desktop MOVE itself is emitted separately by
    /// applyOpenDesktopRouting. Consulted by the
    /// placement resolver the daemon injects into the SnapEngine (in-process, not
    /// via D-Bus). Builds a WindowQuery from the window registry metadata, pins it
    /// to @p screenId (the screen the window is on) so a
    /// `ScreenId`-constrained rule resolves, and reads the `Placement` /
    /// `RouteScreen` slots — mirrors shouldFloatByRule.
    PhosphorSnapEngine::PlacementDirective placementZonesByRule(const QString& windowId, const QString& screenId);

    /// Drop @p windowId's cached rule verdict: the next resolve reads it fresh.
    void evictRuleVerdicts(const QString& windowId);

    /// Engine-neutral RouteToDesktop: if a matched rule pins @p windowId to a
    /// desktop, emit windowDesktopMoveRequested so the compositor moves it there
    /// on open. Called from the snap open path for first placements only, with
    /// @p screenId pinned so a ScreenId-scoped rule resolves. Returns whether a
    /// rule MATCHED, true even when its target failed the 1-based guard.
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
    /// reopen-claim veto applies the same precedence the snap facade does. The two answers are deliberately separate;
    /// overloading the empty return let the two channels drift apart.
    QString applyOpenRoutingForTiling(const QString& windowId, const QString& screenId,
                                      bool* directiveMatched = nullptr);

    /// Canonical key for daemon-local per-window shadow maps, and the canonical
    /// form sibling adaptors must agree on for per-window state. Window ids
    /// reach the daemon in both a raw compositor form and the registry's
    /// canonical form for the same window (a class-mutating app renames
    /// mid-life), so any map keyed on the caller-supplied id splits into two
    /// independent entries. Returns @p windowId unchanged when no registry is
    /// wired.
    QString shadowWindowId(const QString& windowId) const;

    /// RouteToScreen on the snap open path, reached when nothing snapped the
    /// window: move it to the rule's monitor at the same relative spot, free.
    /// On a snapping target its open float goes with it; on a tiling target the
    /// target's engine gets it as an open (a genuine one only for @p reason
    /// Open), unless the rule also sent it to a desktop not in view there.
    /// No-ops when the target is unset, the spawn screen, or not connected, or
    /// when the window has pushed no geometry yet. Returns true when a
    /// RouteToScreen (or placement) directive MATCHED, moved or not.
    bool applyOpenScreenRouting(const QString& windowId, const QString& screenId,
                                PhosphorEngine::RestoreReason reason = PhosphorEngine::RestoreReason::Open);
    /// How a routed open reaches a tiling target's engine (TilingAdaptor wires it).
    void setRoutedOpenDispatcher(std::function<void(const PhosphorProtocol::WindowOpenedEntry&)> dispatcher);

    /// Shared by the two open-routing entry points: if @p resolved carries a
    /// RouteToDesktop action, emit windowDesktopMoveRequested for @p windowId.
    /// Returns whether the action MATCHED (see applyOpenDesktopRouting).
    bool emitRouteToDesktopIfMatched(const PhosphorRules::ResolvedActions& resolved, const QString& windowId);

    /**
     * @brief Drop unified WindowPlacement records for excluded appIds.
     *
     * Single `placementStore().removeIf(...)` over the unified store, dropping any
     * record whose appId matches one of @p patterns (via
     * PhosphorIdentity::WindowId::appIdMatches), across every engine's records — there
     * is no longer a per-engine pending-restore queue to walk. Marks
     * DirtyWindowPlacements when anything was removed so the next debounced save
     * persists the pruned store.
     *
     * Plain `public:` (not Q_SLOTS): the bus surface deliberately excludes this —
     * every caller is in-process and reaches it via direct C++ invocation through the
     * daemon. Same pattern as `WindowDragAdaptor::handleWindowClosed`.
     *
     * Called from two daemon-side sites:
     *   1. Daemon::init's init-prologue priming call — runs once, synchronously,
     *      before the `rulesChanged` subscription connects, pruning what loadState
     *      just deserialized into the store.
     *   2. Daemon::init's `refilterExcludeRules` lambda, fired on every
     *      `RuleStore::rulesChanged` whose post-filter placement-exclusion
     *      slice (Exclude ∪ ExcludePlacement) differs
     *      from the cached one (equality-guarded). Drives live rule edits into the prune.
     * The daemon derives @p patterns from the unified Rule store via
     * `PhosphorRules::ExclusionRules::applicationExcludePatternsFrom`.
     *
     * Safe to call at any time. An empty @p patterns short-circuits.
     */
    void pruneExcludedPlacements(const QStringList& patterns);

    /// Emit reapplyWindowGeometriesRequested (the daemon's timer after geometry settles); not a D-Bus method.
    void requestReapplyWindowGeometries();

Q_SIGNALS:
    void windowZoneChanged(const QString& windowId, const QString& zoneId);

    /// @p windowId no longer parked for @p outputUuid (empty: one output no settle row read so far carried as S0).
    void parkDropped(const QString& windowId, const QString& outputUuid);
    /// @p windowId seated back on @p outputUuid; its parkDropped follows unless a context stays parked there (F1147).
    void parkReseated(const QString& windowId, const QString& outputUuid);

    /**
     * @brief A window's floating state changed from what was last broadcast.
     *
     * Emitted only through relayWindowFloatingChanged: snap's own float
     * changes and its float cleared by a snap (relayed), syncWindowFloating,
     * setWindowFloatingForScreen, a screen leave that dropped a float bit, an
     * evacuee re-seat, and the daemon's tiling batch sync. The KWin effect
     * keeps its floating cache from it.
     *
     * @param windowId The id the caller passed (the dedup keys on the canonical id)
     * @param isFloating The new floating state
     * @param screenId The screen the change was resolved on
     */
    void windowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId);

    /**
     * @brief Unified window state change stream
     * @param windowId Window whose state changed
     * @param state PhosphorProtocol::WindowStateEntry with windowId, zoneId, screenId,
     *        isFloating, changeType, zoneIds (multi-zone spans), isSticky;
     *        changeType: "snapped", "unsnapped", "floated", "unfloated", "screen_changed".
     *        "screen_changed" sets isFloating for a floating window, false for an unsnapped one.
     *        isSticky is query-only and always false here, and zoneIds is filled
     *        only on "snapped" entries; a subscriber needing either asks getWindowState.
     */
    void windowStateChanged(const QString& windowId, const PhosphorProtocol::WindowStateEntry& state);

    /**
     * @brief Pending window restores became available. Emitted at most once per
     * session, once the placement store held a record at load or at a layout
     * change AND panel geometry has been received. The KWin effect answers with
     * resolveWindowRestore() for every visible window it has not tracked yet,
     * which closes the startup race of windows appearing before the layout.
     */
    void pendingRestoresAvailable();

    /**
     * @brief Request that the KWin effect re-apply window geometries from zone positions
     *
     * Emitted after every work-area settle, resolution or virtual-screen change
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
     * @param x, y, width, height Target frame
     * @param zoneId Zone already committed daemon-side (empty for a free placement)
     * @param screenId Screen for OSD placement
     * @param sizeOnly When true, only width/height are meaningful: a drag-out keeps the window's
     *        top-left, the open path keeps its centre and clamps it to the screen
     * @param purpose PhosphorProtocol::PlacementPurpose: a user verb, or a re-statement that keeps maximize
     */
    void applyGeometryRequested(const QString& windowId, int x, int y, int width, int height, const QString& zoneId,
                                const QString& screenId, bool sizeOnly, int purpose);

    /**
     * @brief Daemon requests KWin to activate (focus) a window
     * @param windowId Window to activate
     * @note Sent for the snap engine's focus, cycle and layer-switch verbs and its "Focus new windows"
     *       activation after a restore, the cross-mode focus crossing, and the shell's activateWindow verb.
     *       The daemon resolves the target; the effect calls KWin::effects->activateWindow().
     */
    void activateWindowRequested(const QString& windowId);

    /// KWin should move @p windowId to desktop @p desktop (1-based); a target
    /// not in view parks it for its placement on arrival.
    void windowDesktopMoveRequested(const QString& windowId, int desktop);

    /// A tracked window's app id or title changed (or the window was just
    /// registered). The Phosphor shell relabels its map cell from this
    /// rather than polling getWindowMetadata.
    void windowMetadataChanged(const QString& windowId, const QString& appId, const QString& title);

    /// A tracked window started (@p urgent true) or stopped demanding
    /// attention, or closed while urgent (reported as false). Emitted once
    /// per edge, never for a push that repeats the current state.
    void windowUrgencyChanged(const QString& windowId, bool urgent);

    /// Daemon-initiated cross-output move: the daemon is moving @p windowId onto
    /// @p targetScreenId. Its placement follows on the same connection (an
    /// engine's move, open-time routing) or already ran (a cross-mode move).
    /// The window's resulting outputChanged is expected; the effect updates
    /// bookkeeping and decoration only, without re-issuing windowClosed /
    /// windowOpened. User-drag cross-output moves carry no marker.
    ///
    /// @p sourceScreenId names the screen the window is leaving, for the arm
    /// sites that know it authoritatively before the placement runs. It is
    /// empty for an engine relay, armed before the engine re-keys the window,
    /// where the compositor's own notified-screen record is still the pre-move
    /// screen and serves as the source. Open-time routing passes the spawn
    /// screen. Sites that arm AFTER placing the window must pass the
    /// source explicitly — by then the tile requests have already re-pointed
    /// the compositor's record at the destination.
    void windowOutputMoveExpected(const QString& windowId, const QString& targetScreenId,
                                  const QString& sourceScreenId);

    /**
     * @brief Daemon requests KWin to apply geometries for a batch of windows
     * @param geometries One entry per window: a non-empty screenId is the zone's screen (a snap placement), an
     *        EMPTY one a float or restore entry (the effect clears the snapped mark and skips a minimized window)
     * @param action "rotate", "resnap", "restate" or "vs_reconfigure": it picks the effect's animation profile,
     *        and only "resnap" offers Snap Assist afterwards
     * @note Daemon handles windowSnapped bookkeeping internally before emitting.
     *       Effect just applies geometry with stagger — no windowsSnappedBatch callback.
     */
    void applyGeometriesBatch(const PhosphorProtocol::WindowGeometryList& geometries, const QString& action);

    /// Reserved: the daemon does not emit it. Kept because shipped bridges subscribe
    /// (the KWin effect, PhosphorCompositor::DaemonClient for IGeometryHandler::onRaiseWindows).
    void raiseWindowsRequested(const QStringList& windowIds);
    /// A user verb on @p windowId the daemon places no geometry for: KWin ends its own fullscreen in place.
    void fullscreenHandBackRequested(const QString& windowId);
    /// getPendingRestoreGeometries may answer differently now: a layout assigned, a desktop or activity switch, a
    /// context disabled, an output added, removed or moved. One emission per burst, a short debounce (F625).
    void pendingRestoresChanged();

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
     * stamped before routing. Used by minimize/unminimize, drag-to-float, the
     * monocle unmaximize and the shell placement map; a tiling engine answers it passively.
     * A bus caller other than the compositor bridge is held to setWindowFloating's gate, and refused (logged) unless
     * it names the screen the window is on (F1108, F1123).
     */
    void setWindowFloatingForScreen(const QString& windowId, const QString& screenId, bool floating);

public:
    // Internal-only, plain `public:` (not Q_SLOTS) so the bus never exposes
    // them, like the pruneExcludedPlacements block above.
    /**
     * @brief Single broadcast chokepoint for engine-relayed float changes.
     *
     * Updates m_broadcastFloating and emits windowFloatingChanged, deduped
     * against the last value actually broadcast. Engine signal relays MUST
     * route through this (never signal-to-signal into windowFloatingChanged,
     * never a bare Q_EMIT): a direct emission leaves m_broadcastFloating
     * stale, and the syncWindowFloating gate then suppresses the next genuine
     * change on the engine-sync channel (e.g. a cross-mode handoff's
     * float-cleared sync after a snap-side float). Returns whether a
     * broadcast was actually emitted, so syncWindowFloating (which delegates
     * its gate here) can ride its extra side emissions on the same edge.
     */
    bool relayWindowFloatingChanged(const QString& windowId, bool floating, const QString& screenId);
    /**
     * @brief Apply the remembered float-back geometry for a window a tiling
     * engine (autotile or scrolling) floated on a user float.
     * Resolves via WindowTrackingService::validatedUnmanagedGeometry and emits
     * applyGeometryRequested when a rect is found. The stored geometry is not
     * consumed. One screen's float-back is consumed by the drag-out size
     * restores, the Restore shortcut and a batch's RestoreSentinel entry.
     * @return true if geometry was applied, false when no valid float-back resolves for @p screenId
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
     * Wired to every engine's crossModeMoveRequested. The source engine (the
     * signal sender) reached a context boundary whose target is a different
     * tiling mode and deferred here. This resolves the target mode at the
     * destination context, relinquishes the window from the source engine
     * (handoffRelease + source reflow for an autotile source), and hands it to
     * the target engine (handoffReceive): autotile inserts it per the
     * insertion-order setting; snap snaps it into the entry zone of the layout
     * it lands on. A cross-desktop crossing then asks the compositor to move the
     * real window to @p targetDesktop, where a tiling engine places it once
     * that desktop is shown.
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
     * Wired to every engine's crossModeSwapRequested. Resolves the swap partner —
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
     * Wired to every engine's crossModeFocusRequested (each
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

    /// Active layout changed: prune and relay unless mid-removal, then restore.
    void onLayoutChanged();

    /**
     * @brief Panel geometry became known (PhosphorScreens::ScreenManager).
     * Emits pendingRestoresAvailable when the placement store held records at
     * load and the signal has not been emitted yet this session.
     */
    void onPanelGeometryReady();

public:
    void forgetClosedFullscreenHold(const QString& windowId); ///< the scroll engine's closed-mid-hold memory is spent
    /// Resolve a resnap filter (empty / physical / virtual screen id) into
    /// the concrete list of snap-mode screens the resnap should touch.
    /// Keeps only the screens snapping runs on (the shared ScreenModeRouter) whose
    /// context is not disabled, and none when snapping is switched off. SnapAdaptor's
    /// resnapCurrentAssignments, reapplySnapZones and resnapForVirtualScreenReconfigure call it.
    QStringList resolveSnapModeScreensForResnap(const QString& screenFilter) const;

    /// The screen id for a snap: the caller's (the effect, or SnapAdaptor's gate),
    /// else detectScreenForZone, else the cursor's then the focused window's last
    /// reported screen (the raw members, not the engine-probing accessors).
    QString resolveScreenForSnap(const QString& callerScreen, const QString& zoneId) const;
    /// The effective screen a bus caller's @p reported names: a connector name
    /// maps to its id, a known effective id or another spelling of one maps to
    /// it, and a split monitor's physical id maps to the virtual screen
    /// @p windowId's frame is in, else the cursor's, else its first. Empty for a
    /// screen the daemon does not know; @p reported as is without a ScreenManager.
    QString resolveBusScreen(const QString& reported, const QString& windowId = QString()) const;
    int desktopCount() const; ///< virtual desktops, 0 without a VirtualDesktopManager
    /// The effective screen @p windowId's frame is on, else the one an engine tracks it on.
    QString liveScreenForWindow(const QString& windowId) const;
    /// The registry's desktop and activity for @p windowId; nullopt without one.
    std::optional<PhosphorEngine::WindowDesktopContext> windowDesktopContext(const QString& windowId) const;
    PhosphorZones::LayoutRegistry* layoutRegistry() const
    {
        return m_layoutManager;
    }
    /// True when the registry holds a record for @p windowId's instance.
    bool isRegistryTracked(const QString& windowId) const;

    /// This screen's current virtual desktop (Plasma 6.7 per-output virtual
    /// desktops, #648), falling back to the global current desktop; 0 without
    /// a VirtualDesktopManager.
    ///
    /// Public so TilingAdaptor can stamp managedScreensChanged with the
    /// per-screen desktop the announced set was resolved against. A pure
    /// query of the VirtualDesktopManager this adaptor borrows.
    int currentDesktopForScreen(const QString& screenId) const;

private:
    /// The screen the focused-window record should hold for @p windowId reported
    /// on @p screenId: the effect can send a physical id before the virtual-screen
    /// definitions load, so a tracked virtual screen of the same monitor wins.
    QString resolveFocusedWindowScreen(const QString& windowId, const QString& screenId) const;
    bool dragEndedOnTilingScreen(PhosphorEngine::PlacementEngineBase* engine, const QString& windowId,
                                 const QString& releaseScreenId, bool plainDrop);

    // ═══════════════════════════════════════════════════════════════════════════════
    // Helper Methods - Private
    // ═══════════════════════════════════════════════════════════════════════════════

    /// False, with a warning naming @p operation, when @p windowId is empty.
    bool validateWindowId(const QString& windowId, const QString& operation) const;

    // ── Shell-surface registry mirror (shellsurface.cpp) ──
    /// The id the shell surface speaks: the registry's canonical composite
    /// for @p instanceId, the same id the engines key their models on.
    QString shellWindowIdFor(const QString& instanceId) const;
    /// Registry subscriber: refreshes the urgent set and announces
    /// identity changes. @p previous is null on first registration.
    void onShellRegistryMetadata(const QString& instanceId, const PhosphorEngine::WindowMetadata* previous,
                                 const PhosphorEngine::WindowMetadata& current);
    /// Registry subscriber for a closed window: retires its urgency.
    void onShellRegistryWindowGone(const QString& instanceId);
    /// Windows currently demanding attention, keyed by shell window id.
    QSet<QString> m_urgentWindowIds;

    // ── Shell-surface desktop / pid ledger (shellsurface_desktop.cpp) ──
    /// What the window's registry record says about its desktop and process,
    /// keyed by shell window id. `seq` orders the record changes, so a pid
    /// shared by several windows resolves to the one whose record changed last.
    struct ShellWindowFacts
    {
        int pid = 0;
        int virtualDesktop = 0;
        QList<int> virtualDesktops;
        bool sticky = false;
        quint64 seq = 0;
    };
    /// Registry subscriber, called from onShellRegistryMetadata: records the
    /// new record (called on every registry record change; `previous` unused).
    void recordShellWindowFacts(const QString& windowId, const PhosphorEngine::WindowMetadata& current);
    /// Subscriptions to the current WindowRegistry, severed on a swap.
    QList<QMetaObject::Connection> m_registryConnections;

    /// True when the ledger row puts the window on @p desktop (1-based).
    static bool factsOnDesktop(const ShellWindowFacts& facts, int desktop);
    /// getWindowState's row (convenience.cpp): the tiling engine's view of a window it tracks
    /// unless snap holds the window on a context that runs snapping, else snap's.
    PhosphorProtocol::WindowStateEntry stateRowFor(const QString& windowId) const;
    QHash<QString, ShellWindowFacts> m_shellWindowFacts;
    quint64 m_shellWindowFactsSeq = 0;

    /// The first screen whose layout for the desktop and activity it shows holds
    /// @p zoneId, else the screen the zone's centre lands on under the active
    /// layout. A screen id, or empty.
    QString detectScreenForZone(const QString& zoneId) const;

    // applySnapResult moved to SnapAdaptor.

    /**
     * @brief Test whether the given (screen, virtualDesktop, activity) tuple is currently disabled.
     *
     * Routes through `PhosphorContext::IContextResolver::handleForPersisted`,
     * which resolves the screen's mode through its bound `IModeProvider`.
     * Callers: the save-time keep predicate (a disabled context's record is
     * not written), the snap restore predicate, the instant-restore cache, the
     * work-area re-apply and the resnap screen filter. Records load
     * unfiltered; only those gates filter. `false` until Daemon calls
     * `setContextResolver`, and for an empty screenId (it matches no
     * per-screen disable entry).
     *
     * The live gates (the snap restore predicate, the instant-restore cache)
     * pass the current activity; the store keep-predicate passes the record's.
     */
    bool isPersistedContextDisabled(const QString& screenId, int virtualDesktop,
                                    const QString& activity = QString()) const;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Screen tracking (from KWin effect's D-Bus calls)
    // ═══════════════════════════════════════════════════════════════════════════════
    QString m_lastActiveWindowId; // From windowActivated (focused window's ID)
    QString m_lastActiveScreenId; // From windowActivated (focused window's screen)
    QString m_lastCursorScreenId; // From cursorScreenChanged (cursor's screen)
    /// Effect reports naming a screen the daemon does not know yet (KWin's output
    /// can reach the effect before the daemon's QScreen). The latest per key
    /// replays once the screen resolves (F81).
    struct HeldScreenReport
    {
        QString screenId;
        std::function<void()> replay;
    };
    QHash<QString, HeldScreenReport> m_heldScreenReports;
    void holdScreenReport(const QString& key, const QString& screenId, std::function<void()> replay);
    void replayHeldScreenReports();

    // Frame-geometry shadow: populated via setFrameGeometry D-Bus pushes from
    // the compositor plugin, removed on windowClosed, read by daemon-local
    // shortcut handlers without a round trip. Keyed on CANONICAL ids, so
    // writes and reads go through shadowWindowId().
    QHash<QString, QRect> m_frameGeometry;

    // What the open path just asked the effect to apply (a float-position
    // rect, or a free-size restore's size, #1106), until the next frame
    // report, the close, or a prune. RouteToScreen reads these first.
    QHash<QString, QRect> m_pendingOpenGeometry;
    QHash<QString, QSize> m_pendingOpenSize;

    // Last floating value broadcast via windowFloatingChanged, per window: the
    // broadcast gate compares against THIS, because the owning engine flips its
    // float bit before the sync slot runs, so a re-query would suppress every
    // autotile float broadcast. An absent entry never matches, so a window's
    // first relay always broadcasts (see relayWindowFloatingChanged). Dropped on close and prune.
    QHash<QString, bool> m_broadcastFloating;

    // The last frame each window reported while managed (tiled in view, or in a
    // zone in view), not the drag subject: the frame it SETTLED at, which the
    // float-back refusal reads (F421). Canonical keys; dropped on close and prune.
    QHash<QString, QRect> m_lastManagedFrame;
    QString m_interactiveDragWindow; ///< canonical id of the window being dragged

    // ═══════════════════════════════════════════════════════════════════════════════
    // Dependencies (kept for signal connections and settings access)
    // ═══════════════════════════════════════════════════════════════════════════════
    ZoneDetectionAdaptor* m_zoneDetectionAdaptor = nullptr;
    PhosphorZones::LayoutRegistry* m_layoutManager;
    ISettings* m_settings;
    /// Non-owning, late-bound via setContextResolver after Daemon constructs its resolver.
    PhosphorContext::IContextResolver* m_contextResolver = nullptr;
    /// setScrollStripStateProvider's provider, and the blob the last loadState() read for the engine once it exists.
    std::function<QJsonObject()> m_scrollStripStateProvider;
    std::function<void(const PhosphorProtocol::WindowOpenedEntry&)> m_routedOpenDispatcher;
    std::function<void(const QString&, const QString&)> m_windowLeftScreenHook;
    std::function<bool(const QString&)> m_isBridgeCaller; ///< setBridgeCallerCheck
    std::function<PhosphorZones::AssignmentEntry::Mode(const QString&, int, const QString&)>
        m_contextModeResolver; ///< setContextModeResolver
    QJsonObject m_loadedScrollStripState;
    QStringList m_knownDesktopIds;
    PhosphorWorkspaces::VirtualDesktopManager* m_virtualDesktopManager;
    PhosphorWorkspaces::ActivityManager* m_activityManager;
    std::unique_ptr<PhosphorConfig::IBackend> m_sessionBackend; // Session state (session.json)

    // Engine references for per-screen routing (setEngines()); QPointer nulls on destruction.
    QPointer<PhosphorEngine::PlacementEngineBase> m_snapEngine;
    QPointer<PhosphorEngine::PlacementEngineBase> m_autotileEngine;
    QPointer<PhosphorEngine::PlacementEngineBase> m_scrollEngine;
    QPointer<PhosphorSnapEngine::SnapEngine> m_cachedSnapEngine;
    QPointer<PhosphorTileEngine::AutotileEngine> m_cachedAutotileEngine;
    QPointer<PhosphorScrollEngine::ScrollEngine> m_cachedScrollEngine;
    bool m_layoutRemovalPending = false; ///< layoutRemoved .. layoutsChanged

    // Central dispatcher: adaptor methods route lifecycle / resnap /
    // restore calls through this instead of direct engine pointer checks.
    // Null until setScreenModeRouter is called (Daemon wires during init).
    ScreenModeRouter* m_screenModeRouter = nullptr;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Business logic service
    //
    // m_service is created in the constructor as a QObject child, so it is
    // non-null for the adaptor's life; the few if (!m_service) guards are
    // belt-and-braces. The constructor qFatals on a null layout registry, zone
    // detector or settings; the screen, desktop and activity managers may be null.
    // ═══════════════════════════════════════════════════════════════════════════════
    // Owned: DaemonGeometryResolver is a plain non-QObject. WindowTrackingService
    // borrows it by raw pointer, and as a QObject child the service is deleted by
    // ~QObject after this member, so the resolver goes first. That is safe only
    // because ~WindowTrackingService does not touch the resolver.
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

    // One-shot warning latch for saveState()'s synchronous path on a backend that
    // is not a JsonBackend (none today: createSessionBackend builds one). The
    // shutdown save takes the synchronous path on purpose and logs at debug.
    bool m_syncFallbackWarned = false;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Startup timing coordination
    // ═══════════════════════════════════════════════════════════════════════════════

    /**
     * @brief Try to emit pendingRestoresAvailable if conditions are met
     *
     * Conditions required:
     * 1. The placement store held records at load or at a layout change
     *    (m_hasPendingRestores)
     * 2. Panel geometry has been received by PhosphorScreens::ScreenManager
     * 3. The signal has not been emitted yet this session
     *
     * This prevents windows from restoring with incorrect geometry
     * before panel positions are known.
     */
    void tryEmitPendingRestoresAvailable();

    bool m_hasPendingRestores = false; // True once the placement store held records at load or a layout change
    bool m_pendingRestoresEmitted = false; // True if we already emitted pendingRestoresAvailable
    bool m_shutdownSaveGuard = false; // True after saveStateOnShutdown() to prevent destruction-phase saves
    bool m_startupLeaveSweepDone = false; // True once pruneStaleWindows ran its once-per-daemon leave release

    // Evacuee park helpers (evacuees.cpp).
    void wireEvacueeTouches();
    void noteEvacueeTouched(const QString& windowId, const QString& placedOnScreen);
    void dropEvacueeParks(const QString& windowId);
    bool readoptEvacuee(const QString& windowId, const QString& onScreen, const QString& parkedPhysicalId);
    void floatEvacuee(const PhosphorProtocol::OutputSettleRow& row, bool snapFloating);
    QString reassertEvacuee(const PhosphorProtocol::OutputSettleRow& row, QSet<QString>& reassertedScreens);
    std::unique_ptr<EvacueeLedger> m_evacuees;
    WindowLifecycleRelay* m_lifecycleRelay = nullptr; ///< child; see lifecycleRelay()
    /// releaseLeftScreens, then the windowOutputMoveExpected marker (screenleave.cpp).
    void announceOutputMove(const QString& windowId, const QString& targetScreenId, const QString& sourceScreenId,
                            const PhosphorEngine::IPlacementEngine* arrival);
};

} // namespace PlasmaZones
