// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorsnapengine_export.h>
#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/IVirtualDesktopManager.h>
#include <PhosphorSnapEngine/ISnapSettings.h>
#include <PhosphorEngine/IWindowTrackingService.h>
#include <PhosphorEngine/PerScreenStates.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/ScreenContextTracker.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorSnapEngine/PlacementDirective.h>
#include <PhosphorProtocol/NavigationTypes.h>
#include <PhosphorProtocol/WindowTypes.h>
#include <PhosphorRules/RuleEvaluator.h>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>
#include <memory>

namespace PhosphorZones {
class IZoneDetector;
class LayoutRegistry;
}

namespace PhosphorEngine {
class IWindowRegistry;
}

// PhosphorRules::RuleEvaluator is included as a member type of
// std::optional below (needs a complete type at declaration); RuleSet
// is referenced only by pointer / reference, so a forward declaration would
// suffice — but including RuleEvaluator.h pulls in RuleSet.h
// transitively anyway, so leave the explicit forward decls out and let
// RuleEvaluator.h provide both.

namespace PhosphorSnapEngine {

class INavigationStateProvider;
class IZoneAdjacencyResolver;
class SnapNavigationTargetResolver;

/**
 * @brief Engine for manual zone-based window snapping
 *
 * Implements IPlacementEngine for screens using manual zone layouts (non-autotile).
 * Handles auto-snap on window open, zone-based navigation, floating state,
 * rotation, and resnap operations.
 *
 * Uses WindowTrackingService as a shared state store for zone assignments,
 * pre-tile geometries, and floating state. The snap engine adds behavior
 * on top: auto-snap fallback chains, directional navigation via zone
 * adjacency, and layout-change resnapping.
 *
 * @see PhosphorEngine::IPlacementEngine, AutotileEngine, WindowTrackingService
 */
class PHOSPHORSNAPENGINE_EXPORT SnapEngine : public PhosphorEngine::PlacementEngineBase
{
    Q_OBJECT

public:
    explicit SnapEngine(PhosphorZones::LayoutRegistry* layoutManager,
                        PhosphorEngine::IWindowTrackingService* windowTracker,
                        PhosphorZones::IZoneDetector* zoneDetector, PhosphorEngine::IVirtualDesktopManager* vdm,
                        QObject* parent = nullptr);
    ~SnapEngine() override;

    /// Current virtual desktop (1-based; 0 when no virtual-desktop manager
    /// is wired) and activity, forwarded from the injected managers.
    /// Public for symmetry with AutotileEngine's analogous accessors and
    /// to keep the engine's "current context" surface coherent — the
    /// daemon uses `Daemon::currentDesktop()` / `Daemon::currentActivity()`
    /// directly rather than going through the engine. The only current
    /// in-tree caller of these two is `lifecycle.cpp` (snap-engine-
    /// internal restore logic); kept public so a future adaptor that
    /// wants the engine's own view (e.g. for a per-engine OSD) doesn't
    /// have to wire its own VDM.
    int currentVirtualDesktop() const;
    /// This screen's current virtual desktop (Plasma 6.7 per-output virtual
    /// desktops, #648), falling back to the global currentVirtualDesktop().
    int currentVirtualDesktopForScreen(const QString& screenId) const;
    QString currentActivity() const;

    /// Resolve the zone on @p screenId's @p targetDesktop layout that is
    /// positionally equivalent to @p currentZoneId (1-based index of zones sorted
    /// by number), plus its pixel geometry. Returns an empty pair when the target
    /// desktop has no layout, no matching slot, or invalid geometry. Public (like
    /// the desktop/activity accessors) so cross-surface handoff logic and tests
    /// can map a window's slot onto another desktop's layout.
    std::pair<QString, QRect> resolveCrossDesktopZone(const QString& currentZoneId, const QString& screenId,
                                                      int targetDesktop) const;

    /// The zone a window ENTERS when it crosses onto @p neighbourScreen moving in
    /// @p direction: the first zone on the edge facing back toward the source
    /// (crossing "right" enters the neighbour's left-edge zone). Empty when no
    /// zone-adjacency resolver is wired or the neighbour has no such zone. Used by
    /// the daemon cross-mode handoff to place a window arriving on a snap monitor.
    QString entryZoneForCrossing(const QString& direction, const QString& neighbourScreen) const;

    /// The window snapped to @p zoneId on @p screenId (the daemon's stored
    /// assignment pins it to that output), or empty if the zone is unoccupied
    /// there. Used by the cross-mode swap to find the snap partner when THIS
    /// engine is the swap target.
    QString windowInZoneOnScreen(const QString& zoneId, const QString& screenId) const;

    // ═══════════════════════════════════════════════════════════════════════════
    // IPlacementEngine — lifecycle
    // ═══════════════════════════════════════════════════════════════════════════

    bool isActiveOnScreen(const QString& screenId) const override;
    /// True when snapping is globally enabled. Mirrors AutotileEngine::isEnabled()
    /// so callers (daemon shortcut dispatch, mode routing) can gate snap-mode
    /// operations through the IPlacementEngine interface uniformly. Without this
    /// override SnapEngine inherits IPlacementEngine's `return false` default,
    /// which made every snap engine a no-op to any isEnabled() caller.
    bool isEnabled() const noexcept override;
    using IPlacementEngine::windowOpened;
    void windowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight) override;

    /**
     * @brief Predicate consulted on the auto-snap entry path to suppress
     *        zone restores onto a context the user disabled.
     *
     * Returns true if the screenId is currently active for snap mode, false
     * if disabled. The engine library is intentionally settings-agnostic
     * (LGPL boundary) so the daemon adaptor injects the predicate; SnapEngine
     * itself has no notion of disabled contexts. The daemon-side closure is
     * responsible for resolving the current virtual desktop / activity at
     * call time — SnapState does not track those.
     *
     * Applied inside `resolveWindowRestore` so BOTH the engine's own
     * `windowOpened` path AND the D-Bus `SnapAdaptor::resolveWindowRestore`
     * path (used by the KWin effect for per-window restores) hit the same
     * gate. A PendingRestore authored before the user disabled the context
     * can no longer drag a freshly opened window into a zone the user told
     * us to stay out of (discussion #461 item 7). Without this gate,
     * restarting the daemon was the only way to evict stale in-memory
     * entries — the `isPersistedContextDisabled` filter on disk load only
     * fires on startup, so any restore queued during the running session
     * leaked through.
     *
     * When unset (default), the engine behaves as if every context is
     * active — the historical default that unit tests rely on.
     */
    using ShouldRestorePredicate = std::function<bool(const QString& screenId)>;

    /**
     * @brief Inject the auto-snap-restore gate. See ShouldRestorePredicate.
     *
     * Ownership: the caller keeps any captured state valid for the engine's
     * lifetime. To detach safely, clear via `setShouldRestorePredicate({})`
     * before destroying the captured object.
     */
    void setShouldRestorePredicate(ShouldRestorePredicate predicate)
    {
        m_shouldRestorePredicate = std::move(predicate);
    }

    /**
     * @brief Predicate consulted in `resolveWindowRestore` to decide whether a
     *        FLOATED window should have its previous global position restored on
     *        open. (Snapping is two-state — snapped or floated; "floated" is the
     *        only unsnapped state.)
     *
     * Keyed by the live windowId so the daemon closure can build a full
     * WindowQuery (window class / title / role) from its WindowRegistry and
     * evaluate the per-window RestorePosition rule, falling back to the
     * `snappingRestoreFloatedWindowsOnLogin` setting. Like @ref ShouldRestorePredicate
     * the engine stays settings-agnostic (LGPL boundary) — it only asks.
     *
     * Returns true to restore the recorded position (cross-screen allowed —
     * stored geometry is in global compositor coordinates, so re-applying it
     * lands the window back on its original monitor).
     *
     * The gate governs two things for FLOATED records (snapped-to-zone restore is
     * unaffected):
     *   - cross-screen CONSUMPTION eligibility — a record whose recorded screen
     *     differs from the reopening screen is only consumed when the predicate
     *     opts the window in; otherwise consumption stays gated on the opening
     *     screen;
     *   - the geometry MOVE — a floated record ALWAYS re-marks the window floating
     *     (windowFloatingChanged), but its recorded position is re-applied only
     *     when the predicate opts in.
     *
     * When the predicate is UNSET (default), the engine preserves its historical
     * behaviour: a floated record is consumed only on the screen it reopens on and
     * is marked floating without a position move — the path unit tests rely on.
     */
    using RestorePositionPredicate = std::function<bool(const QString& windowId)>;

    /**
     * @brief Inject the unsnapped-position-restore gate. See
     *        RestorePositionPredicate. Same lifetime contract as
     *        setShouldRestorePredicate — clear with `{}` before destroying any
     *        captured state.
     */
    void setRestorePositionPredicate(RestorePositionPredicate predicate)
    {
        m_restorePositionPredicate = std::move(predicate);
    }

    /**
     * @brief Predicate consulted in `resolveWindowRestore` to decide whether a
     *        window that was SNAPPED at logout should be restored to its recorded
     *        zone on reopen/login (the "managed restore").
     *
     * This is the snapped-to-zone analogue of @ref RestorePositionPredicate
     * (which governs FLOATED records). Keyed by the live windowId so the daemon
     * closure stays settings-agnostic (LGPL boundary) — the engine only asks.
     * The daemon wires it to the `restoreWindowsToZonesOnLogin` setting.
     *
     * When the predicate returns false the stored snap is not re-applied; the
     * window falls through to the normal auto-snap policy chain, exactly as a
     * disabled-context rejection does. When UNSET (default) the engine restores
     * snapped records unconditionally — the historical behaviour unit tests rely
     * on. This gate is independent of @ref ShouldRestorePredicate (the
     * disabled-context gate); both must opt in for a managed restore to proceed.
     */
    using ManagedRestorePredicate = std::function<bool(const QString& windowId)>;

    /**
     * @brief Inject the managed (snapped-to-zone) restore gate. See
     *        ManagedRestorePredicate. Same lifetime contract as
     *        setRestorePositionPredicate — clear with `{}` before destroying any
     *        captured state.
     */
    void setManagedRestorePredicate(ManagedRestorePredicate predicate)
    {
        m_managedRestorePredicate = std::move(predicate);
    }

    /**
     * @brief Predicate deciding whether an opening window should start FLOATING
     *        because a "Float this app" rule matched it. Daemon-injected,
     *        keyed by the live windowId, evaluated on the window-open path. When
     *        UNSET (default) no window is rule-floated and the engine keeps its
     *        historical open behaviour (path unit tests rely on this). Same
     *        lifetime contract as setRestorePositionPredicate — clear with `{}`
     *        before destroying any state the closure captured.
     */
    using FloatPredicate = std::function<bool(const QString& windowId)>;

    void setFloatPredicate(FloatPredicate predicate)
    {
        m_floatPredicate = std::move(predicate);
    }

    /**
     * @brief Resolver yielding the open-placement directive — SnapToZone ordinals
     *        plus an optional RouteToScreen target and an optional RouteToDesktop
     *        target — for an opening window because a placement rule matched
     *        it. Daemon-injected, keyed by the live windowId plus the screen the
     *        window is opening on (so a rule carrying a `ScreenId` constraint
     *        resolves against the window's current screen), evaluated on the
     *        window-open path (`calculateSnapToPlacementRule`, the highest-priority
     *        restore chain level). See PhosphorSnapEngine::PlacementDirective. Empty
     *        ordinals ⇒ no SnapToZone rule matched; multiple ordinals request a zone
     *        span (their unioned bounding rect). The engine stays settings/rule-
     *        store-agnostic (LGPL boundary) — it only asks. When UNSET (default) no
     *        window is rule-snapped and the engine keeps its historical open
     *        behaviour (path unit tests rely on this). Same lifetime contract as
     *        setFloatPredicate — clear with `{}` before destroying any captured state.
     */
    using PlacementZonesResolver = std::function<PlacementDirective(const QString& windowId, const QString& screenId)>;

    void setPlacementZonesResolver(PlacementZonesResolver resolver)
    {
        m_placementZonesResolver = std::move(resolver);
    }

    void windowClosed(const QString& windowId) override;
    void windowFocused(const QString& windowId, const QString& screenId) override;
    void toggleWindowFloat(const QString& windowId, const QString& screenId) override;
    void setWindowFloat(const QString& windowId, bool shouldFloat, const QString& screenId = QString()) override;
    void saveState() override;
    void loadState() override;

    // Cross-engine handoff
    QString engineId() const override
    {
        return QStringLiteral("snap");
    }
    void handoffReceive(const HandoffContext& ctx) override;
    void handoffRelease(const QString& windowId) override;
    QString screenForTrackedWindow(const QString& windowId) const override;
    /// Whether this engine considers the window owned (snapped, snap-floated,
    /// or otherwise carried in SnapState's screen/zone maps). Used by the
    /// daemon to disambiguate which engine should handle a shortcut and to
    /// decide whether a cross-engine handoff is needed.
    bool isWindowTracked(const QString& windowId) const override;

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
     * Returns the computed PhosphorEngine::ZoneAssignmentEntry vector so the caller can batch entries
     * from multiple screens into a single resnapToNewLayoutRequested emission.
     * Falls back to current-assignment entries if autotile order yields nothing.
     *
     * @param autotileWindowOrder Ordered list of window IDs from autotile engine
     * @param screenId Screen to resnap on
     * @param preClaimedZoneIds Zone IDs already reserved by OTHER restore producers
     *        (e.g. the daemon's windowsReleased snap-zone restores for windows that
     *        were floated in autotile and are absent from the tile order). Seeds the
     *        claim ledger so the positional fallback never re-uses a zone another
     *        producer is reclaiming — the two-windows-one-zone collision.
     * @return Vector of PhosphorEngine::ZoneAssignmentEntry (may be empty)
     */
    QVector<PhosphorEngine::ZoneAssignmentEntry>
    calculateResnapEntriesFromAutotileOrder(const QStringList& autotileWindowOrder, const QString& screenId,
                                            const QStringList& preClaimedZoneIds = {});

    /**
     * @brief Calculate snap-all-windows assignments without applying them
     * @param windowIds List of window IDs to snap
     * @param screenId Screen to snap on
     * @return JSON array of zone assignment entries for KWin effect to apply
     */
    PhosphorProtocol::SnapAllResultList calculateSnapAllWindows(const QStringList& windowIds, const QString& screenId);

    /**
     * @brief Emit a single batched resnapToNewLayoutRequested signal
     *
     * Serializes the given entries and emits the D-Bus signal once.
     * Used by the daemon to combine entries from multiple screens into
     * one signal, eliminating the per-screen race condition.
     *
     * @param entries Combined PhosphorEngine::ZoneAssignmentEntry vector from all screens
     */
    void emitBatchedResnap(const QVector<PhosphorEngine::ZoneAssignmentEntry>& entries);

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
     * A matched SnapToZone placement rule has highest priority and overrides any
     * stored placement (the store still re-binds the record first, so the window's
     * float-back geometry survives the override). Otherwise the unified
     * WindowPlacementStore reopens the window from its snapped or floated record
     * (cross-screen where the predicates allow). With neither, the fallback chain
     * runs:
     *   1. Auto-assign to empty zone
     *   2. Snap to last zone (final fallback)
     *
     * Returns a PhosphorEngine::SnapResult so the D-Bus adaptor can unpack geometry for the
     * KWin effect. Also handles floating windows (skips snap, emits feedback).
     *
     * @param windowId Window identifier
     * @param screenId Screen where the window appeared
     * @param sticky Whether the window is on all desktops
     * @param kind Structural kind of the opening window. Accepted for D-Bus
     *             wire-compatibility but no longer gates restore — the matched
     *             WindowPlacement record carries its own kind.
     * @return PhosphorEngine::SnapResult with geometry and zone info, or PhosphorEngine::SnapResult::noSnap()
     */
    PhosphorEngine::SnapResult
    resolveWindowRestore(const QString& windowId, const QString& screenId, bool sticky,
                         PhosphorEngine::WindowKind kind = PhosphorEngine::WindowKind::Unknown);

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
    void setAutotileEngine(PhosphorEngine::IPlacementEngine* engine);

    /// Attach the daemon's shared window registry. Threaded into every SnapState
    /// (the per-screen stores, the global-scalar holder, and any created later)
    /// plus the engine's own canonicalization so the reverse map keys on the same
    /// stable first-seen composite the stores do (issue #628). Not owned.
    ///
    /// Overrides IPlacementEngine::setWindowRegistry(QObject*): the interface hands
    /// engines a QObject carrying the registry and each casts to its concrete type.
    void setWindowRegistry(QObject* registry) override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-screen state resolution
    //
    // The engine owns one SnapState per (screen, desktop, activity)
    // PlacementStateKey (via PerScreenStates) plus a single global-scalar holder
    // (m_globals, keyed under the empty-screen sentinel). Each per-key store now
    // tracks its OWN last-used zone; m_globals keeps the user-snapped classes (a
    // per-app preference, not a placement) and the single representative last-used
    // restored from disk. A window is placed
    // under the key derived from its screen on first snap/float and stays there
    // for its lifetime (the reverse map is authoritative); its screen is recorded
    // as a per-window value, updated in place, exactly as the former single store.
    // ═══════════════════════════════════════════════════════════════════════════

    /// The SnapState that owns @p windowId (via the reverse map), or nullptr when
    /// the window is untracked. Never creates.
    SnapState* stateForWindow(const QString& windowId);
    const SnapState* stateForWindow(const QString& windowId) const;

    /// The global-scalar holder (last-used-zone + user-snapped classes). Also the
    /// fallback home for screenless float bookkeeping. Never null after construction.
    SnapState* globalState() const
    {
        return m_globals;
    }

    /// The global-scalar holder (alias of globalState). Callers/tests that only need
    /// the still-global last-used-zone / user-snapped scalars use this; per-window
    /// data now lives in the per-screen stores, reached via stateForWindow.
    SnapState* snapState() const
    {
        return m_globals;
    }

    /// Every SnapState the engine owns, including the global holder, for whole-store
    /// enumerations (occupied zones, snapped/floating windows, flat-map views).
    QList<SnapState*> allSnapStates() const;

    /// Resolve-or-register the owning state for @p windowId placed/acting on
    /// @p screenId, and return it. On first placement it derives the key from the
    /// screen, lazily creates the state, and records the reverse-map entry; an
    /// already-tracked window keeps its existing owning state (its screen value is
    /// updated in place by the store call the caller makes). A screenless call
    /// resolves to the global holder. Public so the WTS facade routes its
    /// screen-carrying writes here.
    SnapState* stateForWindowOnScreen(const QString& windowId, const QString& screenId);

    /// Drop the reverse-map entry for @p windowId (window closed / fully removed).
    /// Does not touch state objects.
    void forgetWindow(const QString& windowId);

    /// Re-home a tracked window's snap state onto @p newScreenId's per-key store
    /// when it crosses monitors. Moves the window's per-window entries (zone, live
    /// screen, desktop, floating bit, pre-float zone/screen, auto-snap flag) from
    /// its current owning store to the store for @p newScreenId's current context
    /// and updates the reverse map. The live screen value is rewritten to
    /// @p newScreenId so screenForTrackedWindow reflects the destination (the #724
    /// cross-monitor determinism requirement); the pre-float zone is preserved so an
    /// unfloat back on the source monitor still restores the home zone. No-op when
    /// the window is untracked here (e.g. adopted fresh from another engine) or the
    /// resolved key is unchanged. Returns true when a migration happened. Driven by
    /// the daemon's per-window screen handlers (windowScreenChanged / windowActivated)
    /// and by handoffReceive; the analogue of AutotileEngine's windowFocused
    /// cross-screen migration.
    bool migrateWindowToScreen(const QString& windowId, const QString& newScreenId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Current-context feed (ScreenContextTracker)
    //
    // The daemon pushes the current virtual desktop / per-output desktop (#648) /
    // activity here so currentKeyForScreen resolves a window's owning
    // (screen, desktop, activity) key, mirroring the pushes it already makes into
    // AutotileEngine. Snap keys are per-monitor first (the load-bearing #724 fix);
    // the desktop/activity dimensions match autotile's per-context semantics.
    // ═══════════════════════════════════════════════════════════════════════════

    void setCurrentDesktop(int desktop) override;
    void setCurrentDesktopForScreen(const QString& screenId, int desktop) override;
    void setCurrentActivity(const QString& activity) override;

    // Reclaim per-(screen,desktop,activity) stores whose context no longer exists.
    // Without these the per-monitor stores accumulate across desktop/activity/output
    // removal (the global holder has an empty screenId, so it is never a prune
    // target). The daemon drives them from its desktop-count / activities-changed /
    // screenRemoved signals, mirroring AutotileEngine.
    QSet<int> desktopsWithActiveState() const override;
    void pruneStatesForDesktop(int removedDesktop) override;
    void pruneStatesForActivities(const QStringList& validActivities) override;
    void pruneStatesForRemovedScreen(const QString& physicalScreenId) override;

    // Float facade over the per-screen stores (the daemon's engine float
    // resolver/writer/lister route here instead of a single SnapState).
    bool isFloating(const QString& windowId) const;
    void setFloating(const QString& windowId, bool floating);
    QStringList floatingWindows() const;

    /// Primary zone of @p windowId across the per-screen stores (empty if none).
    /// Used by the cross-mode handoff to read a snap partner's slot.
    QString zoneForWindow(const QString& windowId) const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Zone adjacency resolver (for daemon-driven navigation)
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Set typed zone-adjacency resolver for directional navigation.
     *
     * Replaces the opaque QObject* setZoneDetectionAdaptor() that dispatched
     * via QMetaObject::invokeMethod. The daemon's ZoneDetectionAdaptor must
     * implement (or wrap) IZoneAdjacencyResolver.
     *
     * @param resolver Non-owning pointer; must outlive SnapEngine.
     */
    void setZoneAdjacencyResolver(IZoneAdjacencyResolver* resolver);

    /// Inject the cross-surface resolver (neighbour output / desktop lookup),
    /// threaded into the navigation target resolver so a no-adjacent-zone
    /// boundary crosses into the neighbouring output instead of failing.
    void setCrossSurfaceResolver(PhosphorEngine::ICrossSurfaceResolver* resolver) override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Navigation state provider
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Wire the typed navigation-state provider.
     *
     * Replaces the opaque QObject* setWindowTrackingAdaptor() that dispatched
     * lastActiveWindowId / lastActiveScreenName / lastCursorScreenName /
     * frameGeometry via QMetaObject::invokeMethod. The daemon's
     * WindowTrackingAdaptor must implement (or wrap) INavigationStateProvider.
     *
     * Must be set after construction and before any navigation method is
     * called. Not owned; must outlive SnapEngine.
     */
    void setNavigationStateProvider(INavigationStateProvider* provider);

    // ═══════════════════════════════════════════════════════════════════════════
    // Navigation (moved out of WindowTrackingAdaptor)
    //
    // Every method takes a PhosphorEngine::NavigationContext populated by the daemon's
    // shortcut handler. The engine uses ctx.windowId directly rather than
    // re-reading it from the WTA shadow, which is a step toward retiring
    // the SnapEngine -> WTA back-reference entirely. When ctx.windowId is
    // empty, the engine may consult m_navState->lastActiveWindowId() as a
    // best-effort fallback -- but in the normal path the daemon always
    // provides a resolved target.
    // ═══════════════════════════════════════════════════════════════════════════

    // ═══════════════════════════════════════════════════════════════════════════
    // IPlacementEngine — navigation overrides
    // ═══════════════════════════════════════════════════════════════════════════

    /// Walk to the adjacent window in @p direction and transfer keyboard focus.
    /// Empty direction is a no-op with feedback.
    void focusInDirection(const QString& direction, const PhosphorEngine::NavigationContext& ctx) override;

    /// Move the focused window into the adjacent zone in @p direction
    /// (displacing or filling the target). Empty direction is a no-op.
    void moveFocusedInDirection(const QString& direction, const PhosphorEngine::NavigationContext& ctx) override;

    /// Swap the focused window with whatever's in the adjacent zone in
    /// @p direction. Empty direction is a no-op.
    void swapFocusedInDirection(const QString& direction, const PhosphorEngine::NavigationContext& ctx) override;

    /// Move the focused window to the layout zone with @p zoneNumber
    /// (1-based) on ctx.screenId. PhosphorZones::Zone numbers outside [1,9] are rejected.
    void moveFocusedToPosition(int zoneNumber, const PhosphorEngine::NavigationContext& ctx) override;

    /// Rotate snapped windows through the layout's zone order, dispatched
    /// via IPlacementEngine. Forwards to rotateWindowsInLayout().
    void rotateWindows(bool clockwise, const PhosphorEngine::NavigationContext& ctx) override;

    /// Re-apply the current layout to all managed windows. Forwards to
    /// resnapToNewLayout().
    void reapplyLayout(const PhosphorEngine::NavigationContext& ctx) override;

    /// Re-emit the snap geometry for every currently-snapped (non-floating)
    /// window so the compositor re-applies its snap border / hidden title bar
    /// after a bridge reconnect. Does not recompute zone assignments. See
    /// IPlacementEngine::reapplyManagedWindowAppearance().
    void reapplyManagedWindowAppearance() override;

    /// Unified placement model — report this window's current snap state
    /// (snapped or floated) for persistence, or nullopt if untracked.
    std::optional<PhosphorEngine::WindowPlacement> capturePlacement(const QString& windowId) const override;

    /// Snap every unmanaged window on the screen. The IPlacementEngine
    /// override takes PhosphorEngine::NavigationContext; coexists with the existing
    /// snapAllWindows(const QString&) method which it delegates to.
    void snapAllWindows(const PhosphorEngine::NavigationContext& ctx) override;

    /// Move the focused window to the first empty zone on ctx.screenId.
    void pushToEmptyZone(const PhosphorEngine::NavigationContext& ctx) override;

    /// Restore the focused window to its captured pre-snap size and unsnap.
    void restoreFocusedWindow(const PhosphorEngine::NavigationContext& ctx) override;

    /// Toggle the focused window between snapped and floating.
    void toggleFocusedFloat(const PhosphorEngine::NavigationContext& ctx) override;

    /// Cycle keyboard focus forward/backward through managed windows in
    /// the active zone (or the layout cycle order if single-window per
    /// zone).
    void cycleFocus(bool forward, const PhosphorEngine::NavigationContext& ctx) override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Snap commit orchestration
    //
    // Full snap lifecycle: clear floating, assign to zone, update tracking,
    // emit state-changed signals. Owns the orchestration that was previously
    // on WindowTrackingService.
    // ═══════════════════════════════════════════════════════════════════════════

    /// @p virtualDesktop pins the assignment to a specific 1-based desktop; 0
    /// (default) records it on the window's current desktop. Non-zero is used by
    /// the RouteToDesktop placement path (track the assignment on the destination
    /// desktop) and by batch resnap entries that preserve a window's recorded
    /// desktop through the commit (see ZoneAssignmentEntry::virtualDesktop).
    void commitSnap(const QString& windowId, const QString& zoneId, const QString& screenId,
                    PhosphorEngine::SnapIntent intent = PhosphorEngine::SnapIntent::UserInitiated,
                    int virtualDesktop = 0);

    void commitMultiZoneSnap(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                             PhosphorEngine::SnapIntent intent = PhosphorEngine::SnapIntent::UserInitiated,
                             int virtualDesktop = 0);

    void uncommitSnap(const QString& windowId);

    PhosphorEngine::UnfloatResult resolveUnfloatGeometry(const QString& windowId, const QString& fallbackScreen) const;

    /// Fallback unfloat target for a window with NO pre-float zone (a never-snapped
    /// window that defaulted to floating). Returns a found result ONLY when the
    /// `unfloatFallbackToZone` setting is on, resolving last-used → first-empty →
    /// first zone in the window's screen's layout. Returns not-found when the
    /// setting is off or no zone can be resolved (so the caller keeps the window
    /// floating with feedback).
    PhosphorEngine::UnfloatResult resolveFallbackUnfloatGeometry(const QString& windowId,
                                                                 const QString& fallbackScreen) const;

    PhosphorProtocol::WindowGeometryList
    applyBatchAssignments(const QVector<PhosphorEngine::ZoneAssignmentEntry>& entries,
                          PhosphorEngine::SnapIntent intent = PhosphorEngine::SnapIntent::UserInitiated,
                          std::function<QString()> fallbackScreenResolver = {});

    // ═══════════════════════════════════════════════════════════════════════════
    // Auto-snap calculations (moved from WTS)
    // ═══════════════════════════════════════════════════════════════════════════

    PhosphorEngine::SnapResult calculateSnapToPlacementRule(const QString& windowId, const QString& windowScreenName,
                                                            bool isSticky) const;
    PhosphorEngine::SnapResult calculateSnapToLastZone(const QString& windowId, const QString& windowScreenId,
                                                       bool isSticky) const;
    PhosphorEngine::SnapResult calculateSnapToEmptyZone(const QString& windowId, const QString& windowScreenId,
                                                        bool isSticky) const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Resnap / rotation calculations (moved from WTS)
    // ═══════════════════════════════════════════════════════════════════════════

    QVector<PhosphorEngine::ZoneAssignmentEntry> calculateResnapFromPreviousLayout();
    QVector<PhosphorEngine::ZoneAssignmentEntry>
    calculateResnapFromCurrentAssignments(const QString& screenFilter = QString()) const;
    QVector<PhosphorEngine::ZoneAssignmentEntry>
    calculateResnapFromAutotileOrder(const QStringList& autotileWindowOrder, const QString& screenId,
                                     const QStringList& preClaimedZoneIds = {}) const;
    QVector<PhosphorEngine::ZoneAssignmentEntry> calculateSnapAllWindowEntries(const QStringList& windowIds,
                                                                               const QString& screenId) const;
    QVector<PhosphorEngine::ZoneAssignmentEntry> calculateRotation(bool clockwise,
                                                                   const QString& screenFilter = QString()) const;

    // ═══════════════════════════════════════════════════════════════════════════
    // Effect-reported windows (runtime flag — not persisted)
    // ═══════════════════════════════════════════════════════════════════════════

    void markWindowReported(const QString& windowId);
    const QSet<QString>& effectReportedWindows() const
    {
        return m_effectReportedWindows;
    }

    int pruneStaleWindows(const QSet<QString>& aliveWindowIds) override;

    // ═══════════════════════════════════════════════════════════════════════════
    // IPlacementEngine — state access
    //
    // Resolves the per-(screen,desktop,activity) SnapState for a screen via the
    // shared ScreenContextTracker + PerScreenStates. The non-const overload lazily
    // creates the store; an empty screenId resolves to the global-scalar holder.
    // ═══════════════════════════════════════════════════════════════════════════

    PhosphorEngine::IPlacementState* stateForScreen(const QString& screenId) override;
    const PhosphorEngine::IPlacementState* stateForScreen(const QString& screenId) const override;

    // ═══════════════════════════════════════════════════════════════════════════
    // Internal navigation helpers (concrete SnapEngine methods that the
    // IPlacementEngine overrides delegate to)
    // ═══════════════════════════════════════════════════════════════════════════

    /// Move the focused window to the first empty zone on ctx.screenId.
    /// The IPlacementEngine override pushToEmptyZone() delegates here.
    void pushFocusedToEmptyZone(const PhosphorEngine::NavigationContext& ctx);

    /// Rotate snapped windows through the layout's zone order on @p screenId.
    void rotateWindowsInLayout(bool clockwise, const QString& screenId);

    // ═══════════════════════════════════════════════════════════════════════════
    // Persistence delegate
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Set persistence callbacks for save/load
     *
     * KConfig persistence is owned by WindowTrackingAdaptor (WTS is KConfig-free).
     * These callbacks allow SnapEngine to fulfill the IPlacementEngine persistence
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

    /// Wire the daemon's filtered Exclude rule set into the snap engine.
    /// See the comment block on the private members and the impl in
    /// navigation_actions.cpp for the lifetime contract — the pointer is
    /// borrowed and the cached evaluator drops on a pointer change.
    ///
    /// The borrowed rule set MUST outlive every subsequent exclusion
    /// resolve (`isWindowExcluded` / `evaluateExcludeRules`, and the
    /// legacy `isAppIdExcluded` seam). The Daemon currently guarantees this
    /// through member-declaration order (`m_excludeRuleSet` is declared
    /// before `m_snapEngine` so reverse-order destruction tears the
    /// engine down first), AND additionally clears the borrow
    /// symmetrically by calling `setExcludeRuleSet(nullptr)` in
    /// `Daemon::stop()` before `m_snapEngine.reset()` — that explicit
    /// teardown survives a future reordering or ownership move that
    /// would otherwise silently introduce a dangling pointer.
    ///
    /// Declared OUTSIDE the Q_SIGNALS section below: MOC treats every
    /// declaration in `Q_SIGNALS:` as a signal and generates a stub body
    /// for it, so placing this setter inside that section makes the
    /// translation unit redefine the function and the link fails.
    void setExcludeRuleSet(const PhosphorRules::RuleSet* ruleSet);

    /// Provider that builds the full WindowQuery (window class / title / role /
    /// frame size / flags) for a live windowId. Daemon-injected, keyed by the
    /// live windowId — the daemon resolves it from its WindowRegistry (the same
    /// `buildRuleQueryForWindow` the float / restore predicates use). When set,
    /// exclusion evaluates a window's FULL attributes (matching the autotile
    /// engine) instead of appId alone, and the frame size carried in the query
    /// is checked against the minimum-window-size thresholds. When UNSET
    /// (default) the engine falls back to the appId-only query — the historical
    /// behaviour unit tests rely on. A null/empty optional from the provider
    /// (metadata not yet known) also falls back to appId-only.
    using ExclusionQueryProvider = std::function<std::optional<PhosphorRules::WindowQuery>(const QString& windowId)>;

    /// Inject the exclusion query provider. See ExclusionQueryProvider. Same
    /// lifetime contract as setExcludeRuleSet — the caller keeps captured state
    /// valid for the engine's lifetime; clear with `{}` before destroying it.
    void setExclusionQueryProvider(ExclusionQueryProvider provider)
    {
        m_exclusionQueryProvider = std::move(provider);
    }

    /// True if @p appId matches an enabled `Exclude`-action Rule
    /// resolved against an appId-ONLY `WindowQuery`. This is a narrow seam:
    /// the runtime exclusion path is @ref isWindowExcluded, which evaluates the
    /// FULL window attributes; this method survives as (a) the early-init /
    /// no-metadata fallback inside isWindowExcluded and (b) a directly testable
    /// hook for the rule-set wiring (nullptr borrow, empty-set short-circuit,
    /// evaluator rebind on pointer change, revision-bump invalidation). An
    /// Exclude rule keyed on a non-`AppId` field (`WindowClass Contains …`,
    /// `Title Regex …`) does NOT match here — use isWindowExcluded for that.
    /// Pure const observer — no side effects beyond the `mutable` evaluator cache.
    bool isAppIdExcluded(const QString& appId) const;

    /// Resolve exclusion for a live windowId using the FULL window attributes
    /// when the exclusion query provider is wired (matching the autotile
    /// engine): evaluates the Exclude rule set against the complete WindowQuery
    /// and applies the minimum-window-size thresholds to the query's frame
    /// size. Falls back to the appId-only path (@ref isAppIdExcluded's query
    /// shape) when no provider is set or window metadata is not yet known.
    /// Public so the unit-test layer can drive the wiring directly.
    bool isWindowExcluded(const QString& windowId) const;

Q_SIGNALS:
    // ═══════════════════════════════════════════════════════════════════════════
    // Signals (relayed via SnapAdaptor -> WTA -> D-Bus -> effect)
    // ═══════════════════════════════════════════════════════════════════════════

    /// Snap state changed (commit / uncommit). WTA relays to D-Bus windowStateChanged.
    void windowSnapStateChanged(const QString& windowId, const PhosphorProtocol::WindowStateEntry& entry);

    /// Floating state cleared as part of a commit. WTA relays as windowFloatingChanged(id, false, screen).
    void windowFloatingClearedForSnap(const QString& windowId, const QString& screenId);

    /// Daemon-driven geometry application (used by autotile float restore)
    void applyGeometryRequested(const QString& windowId, int x, int y, int width, int height, const QString& zoneId,
                                const QString& screenId, bool sizeOnly);

    /// Batched resnap data (routed through WTA::handleBatchedResnap for bookkeeping)
    void resnapToNewLayoutRequested(const QString& resnapData);

    /// Request KWin effect to collect unsnapped windows and snap them all
    void snapAllWindowsRequested(const QString& screenId);

    /// Batch of window-geometry updates, applied by the KWin effect in a
    /// single operation. The engine itself only ever emits action="rotate";
    /// the SnapAdaptor layer attaches "resnap" / "vs_reconfigure" when it
    /// relays its own batches over the same WTA D-Bus signal ("snap_all"
    /// batches never cross the wire — the effect builds those locally). The
    /// @p action label disambiguates the cause downstream.
    void applyGeometriesBatch(const PhosphorProtocol::WindowGeometryList& geometries, const QString& action);

private:
    PhosphorEngine::ISnapSettings* snapSettings() const;

    /// Canonicalize a raw windowId to its stable first-seen composite via the
    /// shared registry (passthrough when no registry is attached). The reverse map
    /// and every per-state lookup key on this, matching SnapState's own internal
    /// canonicalization.
    QString canonicalWindowId(const QString& rawWindowId) const;

    /// The owning key for a screen in the current context. Delegates to the shared
    /// ScreenContextTracker (an empty screenId maps to the global holder's key).
    PhosphorEngine::PlacementStateKey currentKeyForScreen(const QString& screenId) const
    {
        return m_context.currentKeyForScreen(screenId);
    }

    /// Lazily create (or fetch) the SnapState for @p key. The empty-screen key
    /// resolves to the global holder; a non-empty key creates a per-screen store on
    /// first use, seeding it with the shared window registry.
    SnapState* ensureStateForKey(const PhosphorEngine::PlacementStateKey& key);

    /// Invoke @p fn once per zone-assigned window across every snap store, with
    /// the window's zones, screen, and desktop read from the store that OWNS it —
    /// the engine-side sibling of WindowTrackingService::forEachZoneAssignedWindow,
    /// shared by the resnap / rotation producers so their per-store walks stay in
    /// lockstep. A window lives in exactly one store (the reverse map is
    /// authoritative), so each window is visited exactly once. @p fn must not
    /// mutate the snap stores.
    void forEachSnapAssignment(const std::function<void(const QString& windowId, const QStringList& zoneIds,
                                                        const QString& screenId, int desktop)>& fn) const;

    /// Clear the last-used zone on every store (per-screen + the global holder)
    /// that currently points at one of @p removedZones. Last-used is per-key now, so
    /// a per-screen unassign has to sweep all stores in case another context pointed
    /// at the same zone. Call with the window's zones captured BEFORE the unassign.
    void syncGlobalLastUsedForRemovedZones(const QStringList& removedZones);

    /// The store whose last-used zone should drive a placement on @p screenId: the
    /// screen's own per-key store when it has a recorded last-used, else the global
    /// holder (which carries the single representative restored from disk, screen
    /// context dropped). Keeps single-monitor auto-snap-to-last-zone behaviour while
    /// scoping the live last-used to the acting screen on multi-monitor. Never null.
    const SnapState* lastUsedStateForScreen(const QString& screenId) const;

    void commitSnapImpl(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                        PhosphorEngine::SnapIntent intent, int virtualDesktop = 0);

    /// Resolve an unfloat target screen: take @p primaryScreen if it still exists
    /// (resolving virtual IDs), otherwise fall back to @p fallbackScreen. Returns an
    /// empty string when neither resolves. Shared by resolveUnfloatGeometry (primary
    /// = pre-float screen) and resolveFallbackUnfloatGeometry (primary = tracked
    /// float screen) so the screen-existence handling stays in one place.
    QString resolveUnfloatScreen(const QString& primaryScreen, const QString& fallbackScreen) const;

    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    PhosphorEngine::IWindowTrackingService* m_windowTracker = nullptr;
    // Per-(screen,desktop,activity) snap stores + the current-context tracker that
    // resolves a screen to its owning key. The daemon feeds the tracker the current
    // desktop / per-output desktop (#648) / activity, so each screen resolves to a
    // real {screenId, desktop, activity} key and gets its own SnapState (created
    // lazily on first placement). m_globals holds the global user-snapped classes
    // (and the representative last-used restored from disk, plus any screenless
    // float) under the empty-screen key so whole-store iterations pick it up
    // transparently; live last-used now lives in each per-screen store.
    PhosphorEngine::PerScreenStates<SnapState> m_states;
    PhosphorEngine::ScreenContextTracker m_context;
    SnapState* m_globals = nullptr;
    PhosphorEngine::IWindowRegistry* m_windowRegistry = nullptr;
    PhosphorZones::IZoneDetector* m_zoneDetector = nullptr;
    PhosphorEngine::IVirtualDesktopManager* m_virtualDesktopManager = nullptr;
    QPointer<QObject> m_autotileEngineObj;
    PhosphorEngine::IPlacementEngine* m_autotileEngineTyped = nullptr;
    IZoneAdjacencyResolver* m_zoneAdjacencyResolver = nullptr;
    PhosphorEngine::ICrossSurfaceResolver* m_crossSurfaceResolver = nullptr;
    // Typed navigation-state provider — replaces the opaque QObject* m_wta
    // back-reference. Provides read-only access to compositor-layer shadows
    // (last-active window, last-active screen, last-cursor screen, frame
    // geometry). Not owned; must outlive SnapEngine.
    INavigationStateProvider* m_navState = nullptr;
    // Snap-mode navigation target resolver. Owned by SnapEngine — moved
    // here in Phase 5E from WindowTrackingAdaptor. Constructed lazily on
    // first navigation call (so the construction order isn't constrained
    // by the fact that SnapEngine has to exist before a resolver that
    // takes WTS + PhosphorZones::LayoutRegistry can be built).
    std::unique_ptr<SnapNavigationTargetResolver> m_targetResolver;
    QSet<QString> m_effectReportedWindows;

    // ═══════════════════════════════════════════════════════════════════════════
    // Float helpers (snapengine/float.cpp)
    //
    // The historical clearFloatingStateForSnap / assignToZones pair was
    // removed — all snap commits now route through commitSnapImpl.
    // ═══════════════════════════════════════════════════════════════════════════

    bool unfloatToZone(const QString& windowId, const QString& screenId);
    bool applyGeometryForFloat(const QString& windowId, const QString& screenId);

    /// Lazy-constructs m_targetResolver on first call. Returns nullptr if
    /// the service or layout manager is missing (unit tests with stub
    /// deps, or shutdown race where WTS/PhosphorZones::LayoutRegistry are already gone).
    ///
    /// When @p action is non-empty and the resolver can't be built, emits
    /// navigationFeedback(false, action, "engine_unavailable", ...) so the
    /// OSD shows a specific reason instead of a silent no-op. Pass an
    /// empty @p action to skip the emit (for call sites that want to
    /// handle the null case themselves).
    SnapNavigationTargetResolver* ensureTargetResolver(const QString& action = QString());

    /// Move @p windowId to the virtual desktop adjacent to the current one in
    /// @p direction. Re-snaps the window into the EQUIVALENT zone on the target
    /// desktop's layout (same zone id when the layout is shared, else the
    /// positionally-equivalent zone), updating SnapState + the placement-store
    /// record, asking the compositor to relocate the real window
    /// (windowDesktopMoveRequested) and applying the target zone's geometry so it
    /// lands snapped rather than floating. Falls back to a bare desktop re-stamp
    /// when no equivalent zone is resolvable. Used when directional move reaches a
    /// zone-layout boundary with no neighbour output. Returns false when there is
    /// no neighbour desktop or the window is not snapped.
    bool tryCrossDesktopMove(const QString& windowId, const QString& direction, const QString& screenId);

    /// If the neighbour OUTPUT in @p direction is a DIFFERENT mode (autotile),
    /// defer to the daemon cross-mode handoff and return true: a move
    /// (@p swap false) emits crossModeMoveRequested so autotile inserts the
    /// window into its stack; a swap (@p swap true) emits crossModeSwapRequested
    /// so it trades the window with the neighbour's entry-edge tile. Returns
    /// false when there is no neighbour output or it is also snap-mode (handled
    /// by the resolver's entry-zone / cross-output-swap path).
    bool tryCrossModeOutput(const QString& windowId, const QString& direction, const QString& screenId, bool swap);

    /// Focus a window on the virtual desktop adjacent to the current one in
    /// @p direction (the entry window on @p screenId there), switching KWin to
    /// it. Used when directional focus reaches a zone-layout boundary with no
    /// neighbour output. Returns false when there is no neighbour desktop or no
    /// window on it. @p focusedWindowId is excluded from the target desktop's
    /// occupants so an on-all-desktops (sticky) source window can't be picked as
    /// its own cross-desktop focus target.
    bool tryCrossDesktopFocus(const QString& focusedWindowId, const QString& direction, const QString& screenId);

    /// Check whether the window is excluded from the given navigation
    /// action by a terminal `Exclude` action in the unified Rule
    /// store. Emits navigationFeedback(false, action, "excluded", ...)
    /// and returns true when excluded so callers can early-return. False
    /// otherwise.
    bool isWindowExcludedForAction(const QString& windowId, const QString& action, const QString& screenId);

    // `isAppIdExcluded` and `isWindowExcluded` are declared in the public
    // section above (so the unit tests can drive the wiring directly); full
    // docstrings live with those declarations.

    /// Shared tail of both exclusion entry points: bind the lazy evaluator to
    /// the current Exclude rule set (empty/null set short-circuits) and resolve
    /// @p query. Keeps the rule-set/evaluator invariant in one place.
    bool evaluateExcludeRules(const PhosphorRules::WindowQuery& query) const;

    /// Borrowed pointer to the daemon's filtered Exclude rule set. nullptr
    /// in early-init paths (before the daemon wires the store) — the
    /// `evaluateExcludeRules` fast path short-circuits to false in that case.
    ///
    /// @note Daemon-main-thread-only. Every access path runs on the
    /// daemon's main thread (rulesChanged signal delivery, navigation
    /// slot dispatch, in-thread exclusion-resolve calls). `setExcludeRuleSet`
    /// writes the raw pointer non-atomically and the paired
    /// `m_excludeEvaluator` mutates a lazy priority-order index that
    /// must be externally serialised (see RuleEvaluator.h's thread-
    /// safety note). Do not access from another thread without adding
    /// locking.
    const PhosphorRules::RuleSet* m_excludeRuleSet = nullptr;
    /// Lazily constructed evaluator bound to @ref m_excludeRuleSet. Reset
    /// in `setExcludeRuleSet` when the pointer changes; the evaluator's
    /// internal prio-sort index and resolve cache key off the bound rule
    /// set's revision counter, so an in-place rule edit through the store
    /// invalidates the evaluator's per-revision state automatically — only
    /// a different rule-set pointer needs the explicit reset.
    ///
    /// @note Same daemon-main-thread-only contract as @ref m_excludeRuleSet.
    mutable std::optional<PhosphorRules::RuleEvaluator> m_excludeEvaluator;

    /// Daemon-injected full-query provider for exclusion. Empty until the
    /// daemon wires it; while empty exclusion uses the appId-only query and the
    /// minimum-window-size thresholds are not applied. See ExclusionQueryProvider.
    ExclusionQueryProvider m_exclusionQueryProvider{};

    // Persistence delegates (KConfig stays in adaptor layer)
    std::function<void()> m_saveFn;
    std::function<void()> m_loadFn;

    // Last-focused screen (updated by windowFocused)
    QString m_lastActiveScreenId;

    // Auto-snap entry gate. Empty until the daemon wires it; while empty
    // the engine treats every screen as active — preserving the
    // historical default that unit tests rely on. (The predicate's
    // signature is `bool(const QString& screenId)` — the desktop and
    // activity dimensions are resolved by the daemon-side closure at
    // call time, not passed in here; see ShouldRestorePredicate doc
    // above and discussion #461 item 7.)
    ShouldRestorePredicate m_shouldRestorePredicate{};

    // Unsnapped-position-restore gate. Empty until the daemon wires it; while
    // empty the engine marks floated windows floating but restores their position
    // only on the reopening screen — the historical behaviour unit tests rely on.
    // See RestorePositionPredicate doc above.
    RestorePositionPredicate m_restorePositionPredicate{};

    // Managed (snapped-to-zone) restore gate. Empty until the daemon wires it;
    // while empty the engine restores snapped records unconditionally — the
    // historical behaviour unit tests rely on. See ManagedRestorePredicate.
    ManagedRestorePredicate m_managedRestorePredicate{};

    // Rule-driven open-floating gate. Empty until the daemon wires it; while
    // empty no window is rule-floated. See FloatPredicate doc above.
    FloatPredicate m_floatPredicate{};

    // Rule-driven open-placement resolver (SnapToZone). Empty until the daemon
    // wires it; while empty no window is rule-snapped. See PlacementZonesResolver
    // doc above.
    PlacementZonesResolver m_placementZonesResolver{};
};

} // namespace PhosphorSnapEngine
