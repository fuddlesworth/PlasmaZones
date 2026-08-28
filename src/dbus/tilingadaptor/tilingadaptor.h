// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorProtocol/AutotileMarshalling.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <QDBusAbstractAdaptor>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <functional>
#include <optional>

namespace PhosphorScreens {
class ScreenManager;
}

namespace PhosphorEngine {
class IPlacementEngine;
}

namespace PlasmaZones {

class WindowTrackingAdaptor;

/**
 * @brief D-Bus adaptor for the shared tiling-family transport
 *
 * Provides D-Bus interface: org.plasmazones.Tiling
 *
 * The engine-NEUTRAL wire surface shared by every tiling-family placement
 * engine (autotile, scrolling): the effect keeps ONE engine-managed screen
 * set and ONE tile-apply pipeline, so window lifecycle notifications flow
 * in through this interface and geometry/focus/float requests flow out of
 * it, regardless of which engine owns the screen. The adaptor dispatches
 * per screen/window through IPlacementEngine only — it never sees a
 * concrete engine type. Engine-SPECIFIC verbs live on the sibling
 * interfaces org.plasmazones.Autotile and org.plasmazones.Scrolling.
 *
 * All engine→adaptor signal connections are made at the composition root
 * (init_engines.cpp), not by the adaptor itself. Most land on a relay entry
 * point that gates or reshapes the payload before the D-Bus signal goes out.
 * tilingChanged and focusWindowRequested are the exceptions: they are direct
 * signal→signal forwards from each engine, because there is nothing to gate.
 */
class PLASMAZONES_EXPORT TilingAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Tiling")

    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(QStringList managedScreens READ managedScreens NOTIFY managedScreensChanged)
    Q_PROPERTY(QVariantMap activeLayouts READ activeLayouts NOTIFY activeLayoutsChanged)

public:
    /**
     * @brief Construct a TilingAdaptor
     *
     * @param screenManager Screen manager (for panel-geometry deferral)
     * @param parent Parent QObject (typically the daemon)
     */
    explicit TilingAdaptor(PhosphorScreens::ScreenManager* screenManager, QObject* parent = nullptr);
    ~TilingAdaptor() override = default;

    /// Wire the WindowTrackingAdaptor (post-construction, to break the
    /// construction-order cycle) so the tiling open path can resolve
    /// RouteToScreen / RouteToDesktop rules. Pass nullptr on shutdown.
    void setWindowTrackingAdaptor(WindowTrackingAdaptor* wta)
    {
        m_windowTrackingAdaptor = wta;
    }

    /// Set the ordered list of engines sharing this interface's lifecycle
    /// pipeline (primary first — it is the fallback when no engine claims a
    /// screen/window mid-transition). The adaptor dispatches each inbound
    /// lifecycle call to whichever listed engine owns the screen/window —
    /// through IPlacementEngine only. A further engine joining the pipeline
    /// is one list entry here plus its outbound-signal connects at the
    /// composition root (wired to the relay entry points below).
    /// Engines are borrowed. Teardown goes through clearEngine(), which also
    /// voids the announce generation and every per-session queue/dedup cache
    /// — the empty-list form of THIS setter clears only the parked opens and
    /// exists for symmetry, with no production caller.
    void setLifecycleEngines(const QVector<PhosphorEngine::IPlacementEngine*>& engines);

    // ── Engine relay entry points ────────────────────────────────────────
    // The composition root connects every pipeline engine's signals here
    // (this adaptor makes no connections itself, keeping it type-agnostic).

    /// Tile-request JSON from any pipeline engine (the engines' windowsTiled
    /// payload contract) — parsed and re-emitted as windowsTileRequested.
    void relayTileRequestsJson(const QString& tileRequestsJson);
    /// Float-state relay with the last-broadcast dedup gate — REQUIRED for
    /// every producer since multiple engines feed one D-Bus signal.
    void relayWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId);
    /// Released-window relay (the in-process signal's QSet argument is not
    /// D-Bus-marshallable; the caller strips it).
    void relayWindowsReleased(const QStringList& windowIds);
    /// Screen-set change on any pipeline engine: re-emits the UNION set
    /// plus the enabled state (they flip together). COALESCED onto a 0ms
    /// single shot: a mode flip moves a screen between two engines in one
    /// synchronous pass, and announcing after the FIRST engine's change
    /// would broadcast an intermediate union without the screen — the
    /// effect would run its full per-window restore and immediately re-add
    /// (restore-then-retile churn). One deferred emission carries the final
    /// state of the whole pass.
    void notifyEngineScreensChanged(bool isDesktopSwitch);
    /// Per-screen tab-indicator model from the scroll engine, cached for the
    /// scrollTabStrips replay query and re-emitted as scrollTabStripsChanged.
    /// A "[]" (or empty) payload retracts the screen: the cache entry is
    /// dropped rather than stored, so a replaying effect sees the screen
    /// absent instead of having to special-case an empty array. Ungated
    /// otherwise — the scroll engine already gates on its own last payload
    /// (m_lastTabStripPayload in engine_apply.cpp) and emits only on a real
    /// model change, so a second gate here could only duplicate that one.
    void relayScrollTabStrips(const QString& screenId, const QString& stripsJson);
    /// Per-screen tab-indicator PAINT overrides resolved from context rules
    /// (a Set tab style / colour rule scoped to a screen, desktop or
    /// activity): tabStyle, gapsBetweenTabs, cornerRadius and the three colours,
    /// keyed by WindowPaintKeys / WindowColorKeys spellings. Only the keys a
    /// rule actually set are present. An EMPTY map clears the screen (the
    /// cache entry is dropped, the signal carries the empty map so the effect
    /// falls back to the global settings). Change-gated: the daemon resolves
    /// these on every per-screen config pass, most of which change nothing.
    void setScrollTabPaintOverrides(const QString& screenId, const QVariantMap& overrides);
    /// Clear the tab paint overrides of every screen @p pred accepts, by
    /// pushing an empty map through setScrollTabPaintOverrides (so each
    /// cleared screen gets its empty-map signal and the effect falls back to
    /// the global look). The predicate lives with the caller for the same
    /// reason as OverlayService::clearScrollDropIndicatorOverridesWhere: which
    /// screen ids still exist is the daemon's knowledge, which ids this
    /// adaptor holds state for is ours. Needed because the ordinary
    /// departing-screen clear runs off the engine's active set, which a
    /// hot-removed screen has already left.
    void clearScrollTabPaintOverridesWhere(const std::function<bool(const QString&)>& pred);
    /// All-windows tab-colour invalidation broadcast (empty id, empty map).
    /// Fired from the daemon's rules-changed and colour-scheme triggers, both
    /// of which move every window's verdict at once. Nothing is resolved here:
    /// the receiver re-queries scrollTabColors for the windows it paints.
    void relayScrollTabColorsChanged();
    /// Targeted tab-colour re-drive for ONE window, fired by the daemon when
    /// that window's title changes (a `Title contains …` tab-colour rule can
    /// flip on a retitle with no rules edit, so the revision-keyed memo alone
    /// would never re-resolve it). Resolves through the WTA and carries the
    /// map, so the effect does not have to round-trip a query.
    ///
    /// Gated on the strip cache naming the window: the effect only paints
    /// pills for windows that are tabs, so relaying for anything else would
    /// grow its per-window colour cache with entries it never draws, on every
    /// retitle of every window in the session. The effect evicts a window's
    /// cached verdict when it stops being a tab anywhere, so a window that
    /// becomes a tab (again) is re-queried when its strip arrives and cannot
    /// carry a verdict this relay skipped while it was not a tab. Change-gated
    /// on the resolved map as well.
    void relayScrollTabColorsForWindow(const QString& windowId);
    /// Enabled-state change on any pipeline engine: re-emits the combined
    /// any-engine-enabled state (deduped — multiple engines feed the one
    /// signal, same rationale as the float-broadcast gate).
    void relayEnabledChanged();

    /**
     * @brief Number of windowOpened entries waiting for panel geometry
     *
     * Observable state for tests: returns the size of the deferred
     * windowOpened queue (see windowsOpenedBatch rationale).
     * Always zero once @c panelGeometryReady has fired.
     */
    int pendingWindowOpensCount() const;
    /// Test seam: parked mid-flip opens awaiting the screens-announce
    /// retry (the m_unclaimedOpens queue).
    int pendingUnclaimedOpensCount() const
    {
        return m_unclaimedOpens.size();
    }

    // Property accessors
    bool enabled() const;
    QStringList managedScreens() const;
    QVariantMap activeLayouts() const
    {
        return m_activeLayouts;
    }

    /// Daemon-facing push of the per-screen rules-visible active layout map
    /// (screenId → snapping UUID / "autotile:<algo>" / "scrolling:<templateUuid>"
    /// / bare sentinel). Owns the emit-on-change gate — the daemon pushes
    /// unconditionally from every updateEngineScreens pass, and multiple
    /// triggers (desktop switch, rule edit, template swap) collapse to one
    /// broadcast per actual change (same rationale as relayEnabledChanged).
    /// Deliberately NOT cleared by clearEngine(): a restart's fresh push
    /// covers it, and broadcasting an empty map at shutdown would make the
    /// effect misfire negated ActiveLayout rules against a daemon that is
    /// merely restarting (mirrors the empty-union guard in the coalesced
    /// screens announce). The effect's bring-up Properties.Get self-corrects
    /// any window where a rule query ran before the first push landed.
    void setActiveLayouts(const QVariantMap& activeLayouts);

public Q_SLOTS:
    /**
     * @brief Force retiling of windows
     * @param screenId Screen to retile, or empty for all screens
     */
    void retile(const QString& screenId);

    /**
     * @brief Force retiling of all engine-managed screens
     *
     * External-client convenience (equivalent to retile("")) — no in-tree
     * caller remains; the effect's border-width handler stopped retiling
     * when the geometry-inset border era ended. Kept as contract surface.
     */
    void retileAllScreens();

    /**
     * @brief Notify the engine that a window was opened
     *
     * Called by KWin effect when a new window is added. Routes the window
     * to whichever pipeline engine owns the (possibly rule-routed) screen.
     *
     * @param windowId Window identifier from KWin
     * @param screenId Screen where the window appeared
     * @param minWidth Window minimum width in pixels (0 if unconstrained)
     * @param minHeight Window minimum height in pixels (0 if unconstrained)
     */
    void windowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight);

    /**
     * @brief Batch window-opened notifications
     *
     * Processes multiple windowOpened in one D-Bus call. Used on daemon
     * startup/restart and mode toggle-on to avoid per-window D-Bus
     * round-trips.
     *
     * Trusted-peer boundary: batch size is deliberately unbounded on the
     * panel-ready path (the deferral queue's kMaxPendingOpens is a depth
     * valve that processes on overflow, never a drop cap), and the numeric
     * in-args are sign-clamped only. The KWin effect is the sole producer;
     * a foreign caller can spend daemon CPU proportional to the batch it
     * sends, nothing more.
     *
     * @param entries Array of (windowId, screenId, minWidth, minHeight) structs
     */
    void windowsOpenedBatch(const PhosphorProtocol::WindowOpenedList& entries);

    /**
     * @brief Update a window's minimum size at runtime
     *
     * Called by KWin effect when a window's minimum size changes after
     * initial windowOpened. Triggers retiling if the value differs.
     *
     * @param windowId Window identifier from KWin
     * @param minWidth New minimum width in pixels (0 if unconstrained)
     * @param minHeight New minimum height in pixels (0 if unconstrained)
     */
    void windowMinSizeUpdated(const QString& windowId, int minWidth, int minHeight);

    /**
     * @brief Notify the engine that a window was closed
     *
     * Called by KWin effect when a window is closed. Removes the window
     * from its owning engine's tracking and triggers retiling.
     *
     * @param windowId Window identifier from KWin
     */
    void windowClosed(const QString& windowId);

    /**
     * @brief Drop a LIVE window from engine tracking without a close
     *
     * The drag-bypass revert's tracking drop (the effect's drag_snap /
     * lifecycle_wiring transitions). Unlike windowClosed, NO placement
     * capture runs: the window is not dying, and its current frame is
     * transient drag state that must never be recorded as a float-back.
     *
     * @param windowId Window identifier from KWin
     */
    void releaseWindowTracking(const QString& windowId);

    /**
     * @brief Notify the daemon that a window has been focused
     *
     * Called by KWin effect when a window gains focus. This allows the
     * owning engine to track the focused window for focus cycling and
     * updates the window-to-screen mapping for correct per-screen tiling.
     *
     * @param windowId Window ID that gained focus
     * @param screenId Screen where the window is located
     */
    void notifyWindowFocused(const QString& windowId, const QString& screenId);

    /**
     * @brief Every screen's current tab-indicator model
     *
     * Replay half of scrollTabStripsChanged, for an effect that loads after
     * the strips were last laid out. Screens with no tabbed column are
     * absent from the map.
     *
     * @return screenId → tab-strip JSON string
     */
    QVariantMap scrollTabStrips() const;

    /**
     * @brief Every screen's current tab-indicator paint overrides
     *
     * Replay half of scrollTabPaintOverridesChanged, for an effect that loads
     * after the overrides were last resolved. Screens with no override are
     * absent from the map.
     *
     * @return screenId → QVariantMap of paint/colour key to value
     */
    QVariantMap scrollTabPaintOverrides() const;

    /**
     * @brief Per-window tab colours resolved from the window rules
     *
     * Delegates to WindowTrackingAdaptor::tabColorRuleParams (which owns the
     * resolve and its memo). Empty map when no WTA is wired or no rule sets a
     * tab colour for the window.
     *
     * @param windowId Window ID to resolve colours for
     * @return activeColor / inactiveColor / urgentColor → colour string
     */
    QVariantMap scrollTabColors(const QString& windowId);

    // floatWindow, unfloatWindow, toggleFocusedWindowFloat, toggleWindowFloat removed:
    // all float operations are now routed through the unified methods —
    // org.plasmazones.Snap.toggleFloatForWindow (SnapAdaptor) for toggle,
    // org.plasmazones.WindowTracking.setWindowFloatingForScreen for directional.

Q_SIGNALS:
    /**
     * @brief Emitted when the pipeline enabled state changes
     * @param enabled True while any pipeline engine is enabled
     */
    void enabledChanged(bool enabled);

    /**
     * @brief Emitted when the set of engine-managed screens changes
     *
     * Also re-emitted with an UNCHANGED set on a desktop/activity switch
     * between contexts with identical NON-EMPTY managed sets
     * (isDesktopSwitch=true, discussion #219) — the effect's catch-scan keys
     * on that wakeup; an empty identical set skips the re-emit.
     *
     * @param screenIds List of screen IDs currently engine-managed
     * @param isDesktopSwitch True if the change is due to desktop/activity switch
     */
    void managedScreensChanged(const QStringList& screenIds, bool isDesktopSwitch);

    /**
     * @brief Emitted when any screen's rules-visible active layout changes
     *
     * The KWin effect listens to this to stamp Field::ActiveLayout onto its
     * window-domain rule queries and to invalidate its rule-verdict caches.
     *
     * @param activeLayouts screenId → rules-visible active layout id string
     */
    void activeLayoutsChanged(const QVariantMap& activeLayouts);

    /**
     * @brief Emitted when tiling layout changes for a screen
     * @param screenId Screen that was retiled
     */
    void tilingChanged(const QString& screenId);

    /**
     * @brief Emitted when windows should be moved to new geometries (batch)
     *
     * The KWin effect listens to this signal and applies geometries to all
     * windows in a single slot invocation, avoiding race conditions when
     * many windows are retiled (e.g. rotate).
     *
     * @param tileRequests Typed list of TileRequestEntry structs, wire shape
     *        a(siiiissbbbbssiiibsb): (windowId, x, y, width, height, zoneId,
     *        screenId, monocle, floating, windowedFullscreen, columnMaximized,
     *        stacking, scrollEdge, viewDelta, visualX, visualY, hasVisualPos,
     *        tabFrom, viewImmediate)
     */
    void windowsTileRequested(const PhosphorProtocol::TileRequestList& tileRequests);

    /**
     * @brief Emitted when a window should be focused
     *
     * The KWin effect listens to this signal and activates the specified window.
     *
     * @param windowId Window ID to focus
     */
    void focusWindowRequested(const QString& windowId);

    /**
     * @brief Emitted when windows are released from engine management
     *
     * Fired when screens leave an engine-managed mode (e.g., switching to
     * manual snapping). NOTE: no effect-side subscriber exists — the
     * restore actually runs through the daemon-internal in-process
     * `windowsReleased` wiring plus the resnap path; this D-Bus signal is
     * external contract surface only.
     *
     * NOTE: This is the D-Bus-facing signal and carries only @p windowIds —
     * the in-process `PlacementEngineBase::windowsReleased` signal
     * additionally carries a `QSet<QString>` of released screen IDs for
     * internal daemon wiring. QSet is not marshallable over D-Bus, so the
     * relay strips that second argument before forwarding.
     *
     * @param windowIds Window IDs no longer under engine control
     */
    void windowsReleasedFromTiling(const QStringList& windowIds);

    /**
     * @brief Emitted when a screen's tab-indicator model changes
     *
     * The KWin effect listens to this to paint tab pills on tabbed scrolling
     * columns. See the XML DocString for the JSON shape; "[]" retracts the
     * screen's indicators.
     *
     * @param screenId Effective screen id the model belongs to
     * @param stripsJson JSON array of tabbed-column entries
     */
    void scrollTabStripsChanged(const QString& screenId, const QString& stripsJson);

    /**
     * @brief A screen's context-rule tab-indicator paint overrides changed
     *
     * @param screenId Effective screen id
     * @param overrides Paint/colour key to value; empty clears the screen
     */
    void scrollTabPaintOverridesChanged(const QString& screenId, const QVariantMap& overrides);

    /**
     * @brief Emitted when rule-resolved tab colours may have changed
     *
     * Two shapes. The BROADCAST (relayScrollTabColorsChanged) carries two
     * empty arguments and means "every window's verdict may have moved" — the
     * receiver re-queries scrollTabColors for the windows it paints. The
     * TARGETED form (relayScrollTabColorsForWindow) names one window and
     * carries its resolved map, so no query is needed.
     *
     * @param windowId Window whose colours changed, empty for the broadcast
     * @param colors Resolved colours; empty on the broadcast, and also on a
     * targeted relay for a window with no tab-colour rule (windowId alone is
     * the discriminator)
     */
    void scrollTabColorsChanged(const QString& windowId, const QVariantMap& colors);

    /**
     * @brief Emitted when a window's floating state changes in an engine
     *
     * The KWin effect listens to this signal to restore pre-tile geometry
     * when floating, and to update its floating cache.
     *
     * @param windowId Window ID whose floating state changed
     * @param isFloating New floating state
     * @param screenId Screen where the window is located
     */
    void windowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId);

private Q_SLOTS:
    /**
     * @brief Flush any windowOpened events that were deferred waiting for panel geometry
     *
     * Connected to PhosphorScreens::ScreenManager::panelGeometryReady. See the rationale comment on
     * windowsOpenedBatch() for why deferral is needed.
     */
    void flushPendingWindowOpens();

private:
    /**
     * @brief Check that the pipeline has engines, logging warning if not
     * @param methodName Name of the calling method for logging
     * @return true if at least one pipeline engine is set
     */
    bool ensurePipeline(const char* methodName) const;

    /**
     * @brief Forward a single window-opened notification to its owning engine
     *
     * Shared tail between windowOpened(), windowsOpenedBatch(), and
     * flushPendingWindowOpens() so all three paths validate arguments and
     * invoke the engine identically.
     */
    void dispatchWindowOpened(const PhosphorProtocol::WindowOpenedEntry& entry);

    /**
     * @brief Decide whether an incoming windowOpened must be deferred
     *
     * Returns true if panel geometry is not yet ready (the caller should queue the
     * entry and return). Lazily connects flushPendingWindowOpens() on first deferral.
     * Exposed as a helper so windowOpened() and windowsOpenedBatch() share the gate.
     *
     * @param incomingCount Entries the caller is about to queue — returns false
     *        (process now, unreserved geometry) rather than growing the queue
     *        past @c kMaxPendingOpens, draining the backlog first so replay
     *        order still holds. See the overflow rationale in the body.
     */
    bool deferUntilPanelReady(qsizetype incomingCount);

    /// Ceiling on the panel-geometry deferral queue. Sized well above any
    /// plausible startup window count so the valve only trips when
    /// panelGeometryReady never arrives.
    static constexpr qsizetype kMaxPendingOpens = 512;

    /// Merge every pipeline engine's active screen set for the D-Bus
    /// screen-set surface (property read + change signal payload).
    QStringList combinedManagedScreens() const;
    /// The pipeline engine whose live set claims @p screenId, else the
    /// primary (first) engine — screen-keyed dispatch for open/focus.
    PhosphorEngine::IPlacementEngine* engineOwningScreen(const QString& screenId) const;
    /// The pipeline engine tracking @p windowId, else the primary —
    /// window-keyed dispatch for close/min-size (the window may have left
    /// its opening screen).
    PhosphorEngine::IPlacementEngine* engineOwningWindow(const QString& windowId) const;

    /// Engines sharing the lifecycle pipeline, primary first (see
    /// setLifecycleEngines). Interface-only borrows.
    QVector<PhosphorEngine::IPlacementEngine*> m_lifecycleEngines;
    /// Last floating state broadcast per window (the dedup gate's memory).
    QHash<QString, bool> m_lastFloatBroadcast;
    /// Last per-window tab-colour map relayed by relayScrollTabColorsForWindow
    /// (its change gate's memory): a retitle that moves no verdict — the
    /// common case, a window with no tab-colour rule retitling per command —
    /// must not put a signal on the bus. Evicted beside m_lastFloatBroadcast
    /// on close / destroy / prune, and dropped by clearEngine.
    QHash<QString, QVariantMap> m_lastScrollTabColorsRelay;
    /// Announce generation: bumped by clearEngine so a queued coalesced
    /// announce from the previous session drops instead of pairing with a
    /// restart's fresh announce (a double emit for one logical change).
    quint64 m_announceGeneration = 0;
    /// Coalescing state for notifyEngineScreensChanged (see its doc).
    bool m_screensAnnouncePending = false;
    bool m_pendingIsDesktopSwitch = false;
    /// Last enabled value broadcast (unset until the first emission).
    std::optional<bool> m_lastEnabledBroadcast;
    /// Last tab-strip payload per screen, the scrollTabStrips replay cache.
    /// Retracted screens are removed, never stored as an empty array.
    QHash<QString, QString> m_lastScrollTabStrips;
    /// Last context-rule tab paint overrides per screen, the
    /// scrollTabPaintOverrides replay cache. Cleared screens are removed.
    QHash<QString, QVariantMap> m_scrollTabPaintOverrides;
    /// Per-screen rules-visible active layout map (see setActiveLayouts).
    QVariantMap m_activeLayouts;
    PhosphorScreens::ScreenManager* m_screenManager = nullptr;
    /// Borrowed; outlives adaptor. Wired post-construction by the daemon so the
    /// tiling open path can resolve RouteToScreen / RouteToDesktop rules
    /// (the rule store + evaluator live on the WTA). Null in tests / when unset —
    /// routing is then skipped and windows open on their spawn screen.
    WindowTrackingAdaptor* m_windowTrackingAdaptor = nullptr;

    // Window-opened events received before the first panel D-Bus query completed.
    // Processing them immediately would compute zones against the unreserved screen rect
    // (the s_availableGeometryCache in PhosphorScreens::ScreenManager is still empty), so we queue them
    // until panelGeometryReady fires. Non-blocking — no nested event loops, no reentrancy.
    PhosphorProtocol::WindowOpenedList m_pendingOpens;
    bool m_pendingOpensListenerInstalled = false;
    /// Opens that arrived while NO pipeline engine claimed their screen AND
    /// a screens announce was pending (the mid-flip window). An
    /// autotile↔scrolling flip keeps the UNION unchanged, so the effect's
    /// managedScreensChanged batch re-add never fires for it — these are
    /// retried ONCE from the coalesced screens announce (the flip has
    /// settled by then; still-unclaimed entries are dropped, never
    /// re-parked). Order-preserving: replay order decides strip column
    /// order and master assignment. Rule routing is baked into the parked
    /// entry's screenId so its side effects run once. Entries are dropped
    /// on close and on clearEngine.
    /// A parked open carries its reclaim eligibility with it: routing is
    /// baked into the parked entry's screenId and is not re-run on retry, so
    /// without the flag a rule-routed entry took the retry path's default
    /// and was offered to the cross-screen reclaim after all — the rule won
    /// on the first dispatch and lost on the retry.
    struct ParkedOpen
    {
        PhosphorProtocol::WindowOpenedEntry entry;
        bool allowCrossScreenClaim = true;
    };
    QList<ParkedOpen> m_unclaimedOpens;
    void dispatchOpenToClaimingEngine(const PhosphorProtocol::WindowOpenedEntry& entry, bool allowPark,
                                      bool allowCrossScreenClaim = true);
    void removeUnclaimedOpen(const QString& windowId);
    /// The panel-geometry deferral queue's twin of removeUnclaimedOpen —
    /// every close path calls both, so a window that opens and closes inside
    /// the startup panel-query window is never dispatched post-mortem at
    /// panelGeometryReady.
    void removePendingOpen(const QString& windowId);

public:
    /**
     * @brief Clear the engine list during shutdown
     *
     * Called by Daemon::stop() before the engines' unique_ptrs are reset,
     * so that any late D-Bus calls (arriving between engine destruction and
     * adaptor destruction) hit ensurePipeline()'s empty check instead of a
     * dangling pointer.
     */
    void clearEngine();

    /**
     * @brief Unconditional per-window teardown, driven by the in-process
     * WindowTrackingAdaptor::windowClosedNotification signal.
     *
     * The D-Bus windowClosed relay is gated effect-side on the close screen
     * still being engine-managed, so a window floated on a managed screen
     * whose screen later left the managed set never reaches windowClosed here
     * and its m_lastFloatBroadcast and m_lastScrollTabColorsRelay entries
     * (and any parked open) leaked for the process — suppressing the first
     * genuine float broadcast or tab-colour relay of a reused id.
     * Plain method, NOT a slot: it must not become a D-Bus surface. It runs
     * AFTER the WTS teardown (the signal's contract), so it uses only the raw
     * id — shadowWindowId may no longer resolve a mutated-class canonical
     * form, which the prune sweep below catches instead.
     */
    void onTrackedWindowDestroyed(const QString& windowId);

    /**
     * @brief Alive-set sweep of the float-broadcast and tab-colour-relay
     * dedup caches, called from WindowTrackingAdaptor::pruneStaleWindows via
     * the daemon wiring.
     *
     * Swept in the INSTANCE-id key space: the maps hold both raw and
     * canonical composites (see windowClosed's dual removal), and only the
     * instance component survives a class rename. Plain method, not a slot.
     * List payload matching the stalePruned signal's marshallable shape.
     */
    void pruneStaleFloatBroadcasts(const QStringList& aliveInstances);
};

} // namespace PlasmaZones
