// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// FILE-SIZE EXCEPTION (sanctioned): the Daemon class is the composition root
// — every service, adaptor, engine, and timer member plus the phase-method
// declarations their wiring documentation hangs off. The implementation is
// already split across daemon/*.cpp by phase; splitting the class DECLARATION
// would scatter the ownership/destruction-order contract the header's member
// ordering encodes.

#pragma once

#include <QObject>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QThreadPool>
#include <chrono>
#include <memory>

#include "controllers/shortcutmanager.h"
#include <PhosphorLayoutApi/LayoutSourceBundle.h>
#include "core/types/types.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/Swapper.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorTiles/AlgorithmPreviewParams.h>

#include "daemon/daemon_fwd.h"

namespace PlasmaZones {

/**
 * @brief Main daemon for PlasmaZones
 *
 * Runs in the background managing layouts, zone overlays, KWin D-Bus
 * communication, keyboard shortcuts, and multi-monitor support.
 */
class Daemon : public QObject
{
    Q_OBJECT

public:
    explicit Daemon(QObject* parent = nullptr);
    ~Daemon() override;

    // No singleton - use dependency injection instead

    // Initialization
    bool init();
    void start();
    void stop();

    // Component access
    PhosphorZones::LayoutRegistry* layoutManager() const
    {
        return m_layoutManager.get();
    }
    PhosphorZones::ZoneDetector* zoneDetector() const
    {
        return m_zoneDetector.get();
    }
    Settings* settings() const
    {
        return m_settings.get();
    }

    /**
     * @brief Unified layout-preview source (manual zones + autotile algorithms).
     *
     * Returns a composite that aggregates PhosphorZones::ZonesLayoutSource
     * (over m_layoutManager) and PhosphorTiles::AutotileLayoutSource (over
     * the daemon-owned PhosphorTiles::AlgorithmRegistry instance at
     * m_algorithmRegistry).  Daemon-internal consumers — overlay layout
     * picker, snap-assist preview thumbnails, the layout adaptor's D-Bus
     * surface — see one ILayoutSource* and branch on
     * `LayoutPreview::isAutotile` rather than on which concrete provider
     * produced an entry.
     */
    PhosphorLayout::ILayoutSource* layoutSource() const
    {
        return m_layoutSources.composite();
    }
    OverlayService* overlayService() const
    {
        return m_overlayService.get();
    }
    PhosphorScreens::ScreenManager* screenManager() const
    {
        return m_screenManager.get();
    }
    PhosphorWorkspaces::VirtualDesktopManager* virtualDesktopManager() const
    {
        return m_virtualDesktopManager.get();
    }
    PhosphorWorkspaces::ActivityManager* activityManager() const
    {
        return m_activityManager.get();
    }
    /**
     * @brief Frozen-snapshot per-screen mode + disable/lock cascade façade.
     *
     * Borrowed by the three D-Bus adaptors (SnapAdaptor,
     * WindowTrackingAdaptor, WindowDragAdaptor) and the daemon's own
     * navigation / OSD / overlay paths so the cascade
     * `(modeFor → currentDesktop → currentActivity → isContextDisabled
     * → isContextLocked)` resolves through one call instead of being
     * hand-stitched at each site. The pointer is non-null after `init()` and stays
     * non-null until `stop()` runs. The init-without-start path detaches the
     * overlay's borrow before its early return, while normal running teardown
     * also clears every adaptor borrow before resetting the resolver. See
     * @ref m_contextResolver for the declaration-order invariant.
     */
    PhosphorContext::ContextResolver* contextResolver() const
    {
        return m_contextResolver.get();
    }
    ShortcutManager* shortcutManager() const
    {
        return m_shortcutManager.get();
    }
    PhosphorEngine::WindowRegistry* windowRegistry() const
    {
        return m_windowRegistry.get();
    }

    // Overlay control (delegates to OverlayService)
    Q_INVOKABLE void showOverlay();
    Q_INVOKABLE void hideOverlay();
    Q_INVOKABLE bool isOverlayVisible() const;

    // OSD notifications
    void showLayoutOsd(PhosphorZones::Layout* layout, const QString& screenId = QString());
    /// Announce a native scrolling template (picker/slot/cycle/KCM apply,
    /// or @p locked for the lock-toggle preview): projects the blueprint
    /// into preview zones and hands the payload to the overlay's
    /// template OSD. Impl in daemon/osd.cpp.
    void showScrollingTemplateOsd(const PhosphorZones::ScrollingTemplate& templ, const QString& screenId,
                                  bool locked = false);
    void showLockedOsd(const QString& screenId);
    void showLockedPreviewOsd(const QString& screenId);
    void showContextDisabledOsd(const QString& screenId, int desktop, const QString& activity, DisabledReason reason);
    /// OSD shown when a context has no active layout because its default
    /// assignment is suppressed (global setting or per-context rule) — the
    /// "not assigned" counterpart to @ref showContextDisabledOsd. Tells the user
    /// the mode is selected but nothing is assigned, instead of silently showing
    /// no OSD.
    void showNotAssignedOsd(const QString& screenId);
    /// Which user-facing OSD toggle a caller gated its announcement on.
    /// Carried into the deferred strip-preview dispatch so it re-reads the
    /// SAME setting the caller checked: the two are gated differently, and
    /// the settle window is long enough to turn either off inside it.
    enum class OsdTrigger {
        LayoutSwitch,
        DesktopSwitch,
    };
    /// Whether an empty strip may defer its card by the settle beat
    /// (@ref kScrollingOsdAdoptSettleMs) or must render the sketch now.
    /// Immediate is for the batched multi-screen show, where one deferred
    /// screen would land ~300 ms after the rest.
    enum class StripSettle {
        Defer,
        Immediate,
    };
    /// Mode-switch OSD for a screen entering Scrolling. Preview style shows
    /// the live strip (deferred one beat when the toggle races the strip
    /// adoption, unless @p settle forbids it); Text style shows a text card.
    void showScrollingModeOsd(const QString& screenId, OsdTrigger trigger, StripSettle settle = StripSettle::Defer);
    /// How long an OSD waits for the effect's re-announce batch to land in
    /// the scroll engine before rendering the card. Serves every caller that
    /// can announce Scrolling ahead of adoption: the mode-toggle shortcut,
    /// the KCM assignment apply, and the desktop-switch batch.
    static constexpr int kScrollingOsdAdoptSettleMs = 300;

    // Shortcut cheatsheet overlay (impls in daemon/osd.cpp).
    /// Toggle the cheatsheet on the cursor's screen. Show path resolves the
    /// screen's tiling mode, the two feature gates, the engine layouts
    /// capability and the shortcut catalog, and pushes them all into the
    /// overlay (daemon-mediated push), dismisses any other Escape-consuming
    /// modal first (picker / snap assist — at most one Escape grab consumer
    /// at a time), then binds the sheet's dedicated Escape ad-hoc grab.
    void toggleCheatsheet();
    /// Re-push catalog + mode + gates + layouts capability into a visible
    /// cheatsheet — live refilter on mode switches, rebinds, feature-gate
    /// flips and context switches. No-op when hidden. Everything is
    /// re-resolved for the screen the sheet is BOUND to (not the cursor's
    /// current screen).
    void refreshCheatsheetIfVisible();
    /// Release the cheatsheet's Escape ad-hoc grab. Connected to
    /// OverlayService::cheatsheetDismissed in shortcuts_wiring.cpp
    /// (connectShortcutSignals).
    void onCheatsheetDismissed();

private:
    /// Show path for the toggle shortcut: resolve cursor screen, catalog,
    /// per-screen mode; dismiss sibling Escape-consuming modals; show and
    /// bind the Escape grab. Only called from toggleCheatsheet().
    void showCheatsheetOnCursorScreen();
    /// Everything both cheatsheet push sites hand the overlay, resolved in
    /// one place so show and refresh cannot drift apart.
    struct CheatsheetPushState
    {
        QString modeString;
        bool autotileAvailable = false;
        bool scrollingAvailable = false;
        bool layoutsAvailable = false;
        /// True when the bound screen's engine consumes layouts as sizing
        /// TEMPLATES (LayoutSupport::Templates): the sheet swaps the layouts
        /// rows' tooltips for template wording.
        bool layoutsAreTemplates = false;
    };
    CheatsheetPushState cheatsheetPushStateFor(const QString& screenId) const;
    /**
     * @brief Show layout OSD for an autotile algorithm (visual zone preview)
     *
     * Renders the OSD unconditionally — gating on user OSD toggles
     * (showOsdOnLayoutSwitch / showOsdOnDesktopSwitch) is the caller's
     * responsibility. The osdStyle setting controls visual style.
     */
    void showLayoutOsdForAlgorithm(const QString& algorithmId, const QString& displayName, const QString& screenId);
    /// The scrolling strip preview card itself: live visible-tile rects, or
    /// the representative endless-strip sketch when the strip is empty.
    /// Carries NO gates of its own, so it stays private and reachable only
    /// through showScrollingModeOsd, which applies them (and re-applies them
    /// on the settle dispatch that calls back in here).
    void showScrollingStripPreviewOsd(const QString& screenId);
    /// True when the setting @p trigger names is on.
    bool isOsdTriggerEnabled(OsdTrigger trigger) const;
    /// Stop @p screenId's armed strip-preview settle timer, if any. Called
    /// from every showScrollingModeOsd arm that does NOT arm it, so a toggle
    /// followed by a reconcile cannot land a duplicate card a beat later.
    void stopScrollingOsdSettleTimer(const QString& screenId);
    /// Destroy the per-screen strip-preview settle timers. An empty
    /// @p screenId reaps all of them (stop()); a screen id reaps that
    /// output's, including every virtual sub-screen of it (screenRemoved).
    void reapScrollingOsdSettleTimers(const QString& screenId = QString());
    void clearHighlight();

    /**
     * @brief Bridge Settings::animationProfile into `PhosphorProfileRegistry`
     *        so QML `PhosphorMotionAnimation { profile: "<path>" }` resolves
     *        to the user's active animation settings and live-updates on edit.
     *
     * Scans the XDG `plasmazones/curves` and `plasmazones/profiles`
     * directories for user-authored definitions and installs live-reload
     * watchers (via constructAnimationLoaders); seeds the shell animation
     * family defaults (`seedShellAnimationFamilies`) and installs their
     * owner tag as the registry's low-precedence tag so seed entries never
     * ship in the published motion tree; publishes the three QML statics
     * (`PhosphorCurve::setDefaultRegistry`,
     * `PhosphorProfileRegistry::setDefaultRegistry`,
     * `QtQuickClockManager::setDefaultManager`) that `stop()` clears; and
     * registers the daemon's active animation Profile under the
     * settings-driven path set — `ProfilePaths::Global` today
     * (kSettingsDrivenProfilePaths) — with every other path resolving
     * through inheritance.
     *
     * Live updates route through the coalescing 0 ms trampoline
     * `requestAnimationProfilePublish`: `Settings::animationProfileChanged`,
     * `ProfileLoader::profilesChanged`, and `CurveLoader::curvesChanged` all
     * arm it, and the publish re-registers only when the registry observes a
     * value-or-owner change.
     */
    void setupAnimationProfiles();
    void setupAnimationShaderEffects();
    void setupSurfaceShaderEffects();

    // init() phase methods, run in order from the thin init() (daemon.cpp); the
    // order is load-bearing. Defined across daemon/init_*.cpp, shader_warmup.cpp
    // and animation_profiles.cpp.
    void setupShaderWarmBakes();
    void initLayoutAndSettingsWiring();
    void initCoreAdaptors();
    void initEnginesAndWiring();
    bool registerDBusService();

    /// Watch the session going idle and push it to the KWin effect, which pauses
    /// decoration-chain animation on it. See m_idleService for why the daemon owns this
    /// rather than the effect.
    ///
    /// Called from init(), and again from start() after a stop(). A TIMEOUT change does not
    /// come through here — it re-arms the ladder via refreshIdleStages() — and the
    /// PauseWhenIdle toggle re-arms nothing at all, deliberately (see idle.cpp).
    void setupIdleService();

    /// (Re)arm the idle ladder from the current timeout. A single stage, armed whenever
    /// the compositor supports idle notification — NOT torn down when PauseWhenIdle goes
    /// off (an empty ladder cannot tell us the seat is already idle when the user turns
    /// the feature back on). Called from init() and whenever the timeout moves.
    void refreshIdleStages();

    /// Disconnect every connection setupIdleService made whose sender outlives the idle
    /// service, and forget them. Used by BOTH stop() and a re-entrant setupIdleService, so
    /// a second setup cannot stack duplicates on top of live connections.
    void teardownIdleConnections();

    /// Is the session idle, as far as decoration pausing is concerned?
    ///
    /// The seat being idle is a FACT (the ladder reports it whenever the compositor
    /// supports idle notification). Pausing on it is a CHOICE (the PauseWhenIdle setting).
    /// This is the single place the two are combined, so the toggle cannot be honoured on
    /// one publishing path and forgotten on another.
    [[nodiscard]] bool sessionIdleNow() const;

    /// Announce the session's idle state to the KWin effect, on CHANGE only.
    ///
    /// The idle service can report the same state more than once, and a redundant emit
    /// is a D-Bus broadcast that says nothing (the effect does dedupe it at its own
    /// door, so it costs traffic rather than repaints). @p force overrides the change
    /// check for the one case where our last published value is not the question: a
    /// client that just (re)connected and knows nothing of it.
    void publishSessionIdle(bool idle, bool force = false);
    /// Push the current `Settings::animationProfile()` into the registry
    /// under the settings-driven paths (`ProfilePaths::Global` today,
    /// kSettingsDrivenProfilePaths). Called from
    /// `setupAnimationProfiles()` at startup and from the coalescing
    /// trampoline `requestAnimationProfilePublish` on every
    /// `animationProfileChanged` / `profilesChanged` /
    /// `curvesChanged` signal.
    void publishActiveAnimationProfile();
    /// Schedule a coalesced publish on the next event-loop tick. The
    /// settings-slider drag fires `animationProfileChanged` at ~30 Hz,
    /// and a curve-pack edit can fire `curvesChanged` and
    /// `profilesChanged` back-to-back in the same tick. Funnelling
    /// through a single-shot 0-ms timer collapses every signal in the
    /// current event-loop iteration into one publish call. The
    /// registry's value-equality guard would already make duplicate
    /// publishes free, but the publish itself does a Settings parse
    /// + curve resolve which is not free during a slider drag.
    void requestAnimationProfilePublish();

    // ═══════════════════════════════════════════════════════════════════════════
    // Navigation handlers — single code path per operation (DRY/SOLID)
    // Resolve screen → check mode (autotile vs zones) → delegate → OSD from backend
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief Convenience mode check: routed through m_screenModeRouter.
     *
     * All daemon navigation/signal paths that need to branch on "is this
     * screen in autotile mode?" use this method instead of checking the
     * engine pointer directly. Centralising the lookup behind one call
     * is how the single-source-of-truth invariant is enforced inside the
     * daemon. The router itself (src/core/screenmoderouter.cpp) IS the
     * underlying source and inspects `m_autotileEngine->isActiveOnScreen`
     * directly — every other caller (navigation/signal/start/osd paths)
     * routes through `isAutotileScreen` or `m_screenModeRouter->isAutotileMode`.
     */
    bool isAutotileScreen(const QString& screenId) const;

    /**
     * @brief Resolve the current mode for @p screenId via the router with
     * the same "null router → Snapping" fallback DaemonScreenModeAdapter
     * applies. Single point of truth for daemon-internal mode lookups
     * that don't have the ContextResolver handle in hand (signal
     * handlers, OSD paths). Eliminates the open-coded
     * `m_screenModeRouter ? m_screenModeRouter->modeFor(...) : Snapping`
     * rebuild that used to sit inline.
     */
    PhosphorZones::AssignmentEntry::Mode currentModeFor(const QString& screenId) const;

    using LayoutSupport = PhosphorEngine::IPlacementEngine::LayoutSupport;
    /**
     * @brief How the engine owning @p screenId relates to user-selectable
     * layouts (IPlacementEngine::layoutSupport). Gates the layout picker
     * and the layout-selection shortcuts (cycle, quick slots, layout lock),
     * feeds the OverlayService's injected LayoutSupportResolver (the
     * picker/drag-popup layout lists) and the cheatsheet's layouts-row
     * filter — so no surface assumes snap semantics on a screen whose
     * engine has no layout concept or falls through to the manual layout
     * list. Only a null ROUTER falls back to Placement (the shutdown
     * window; same Snapping fallback as currentModeFor) — engineFor itself
     * never returns null for a routed screen.
     */
    LayoutSupport layoutSupportForScreen(const QString& screenId) const;

    /**
     * @brief Failure OSD for a layout-selection shortcut pressed on a screen
     * whose engine does not provide layouts. Honours the showNavigationOsd
     * setting like the navigationFeedback relay in signals.cpp.
     */
    void showLayoutsUnavailableOsd(const QString& screenId);

    /**
     * @brief Per-context disable cascade gate for navigation shortcuts.
     *
     * Returns true when the handler should silently no-op — either the
     * resolver is null (shutdown window) or the focused (monitor,
     * desktop, activity) is on the user's disable list. Centralises
     * the inline gate every geometry-side-effect handler must carry so
     * the discussion #461 bug class can't recur when a new handler is
     * added. Handlers that only manipulate focus (no geometry side
     * effect) intentionally do NOT use this gate.
     */
    bool isFocusedContextGated(const QString& screenId) const;

    /**
     * @brief Mode-explicit sibling of isFocusedContextGated for
     * single-engine shortcuts.
     *
     * Same fail-closed null-resolver semantics, but queries the resolver
     * with an explicit mode (skipping the router-driven mode lookup).
     * Used by the autotile verbs (handleRetile / master-ratio /
     * HANDLE_AUTOTILE_ONLY, gating against the Autotile disable list) and
     * by the scrolling shortcut resolver (scrolling_init.cpp, gating
     * against the Scrolling disable list).
     */
    bool isFocusedContextGatedForMode(const QString& screenId, PhosphorZones::AssignmentEntry::Mode mode) const;

    void handleRotate(bool clockwise);
    void handleFloat();
    void handleMove(NavigationDirection direction);
    void handleSpan(NavigationDirection direction);
    void handleFocus(NavigationDirection direction);
    void handlePush();
    void handleRestore();
    void handleSwap(NavigationDirection direction);
    void handleSnap(int zoneNumber);
    void handleCycle(bool forward);
    void handleResnap();
    void handleSnapAll();
    void handleFocusMaster();
    void handleSwapWithMaster();
    void handleIncreaseMasterRatio();
    void handleDecreaseMasterRatio();
    void handleIncreaseMasterCount();
    void handleDecreaseMasterCount();
    void handleRetile();
    /// The mode-toggle shortcut's handler (autotile_init.cpp): cycles the
    /// cursor's screen Snapping → Tiling → Scrolling → Snapping, skipping
    /// modes whose master switch is off, and carries the leaving mode's
    /// window order and snap state across the flip.
    void handleTilingModeToggle();
    void handleSwapVirtualScreen(NavigationDirection direction);
    void handleRotateVirtualScreens(bool clockwise);

    /** @brief Check if screen is locked for layout change in its current mode */
    bool isScreenLockedForLayoutChange(const QString& screenId);

    /** @brief Handle cycle-layout shortcut (previous or next) */
    void handleCycleLayout(const QString& screenId, bool forward);

    // Start-up sub-methods. Definitions are split by concern across
    // start.cpp (screen / desktop / activity wiring), signals.cpp (unified
    // controller, layout and overlay wiring), shortcuts_wiring.cpp
    // (connectShortcutSignals), autotile_init.cpp (initializeAutotile),
    // scrolling_init.cpp (connectScrollingShortcuts) and scrolling.cpp
    // (the two per-recompute scrolling helpers).
    void connectScreenSignals();
    void connectDesktopActivity();
    void connectShortcutSignals();
    void initializeAutotile();
    /// Wire the ShortcutManager's scrolling-column signals to the scroll
    /// engine (scrolling_init.cpp).
    void connectScrollingShortcuts();
    /// Push the derived scrolling screen set into the scroll engine —
    /// order seeding, per-context rule params, the TEMPLATE vocabulary
    /// (each screen's resolved template layout extracted into per-screen
    /// preset lists), setActiveScreens (scrolling.cpp). Called from
    /// updateEngineScreens so both engines' sets flip atomically per
    /// context recompute.
    void updateScrollingScreens(const QSet<QString>& scrollingScreens);
    /// Shared capture phase: store leaving-scrolling screens' column order
    /// into m_lastEngineOrders BEFORE either engine seeds (see
    /// updateEngineScreens' capture-all → seed-all ordering).
    void captureScrollingOrders(const QSet<QString>& scrollingScreens);
    /// Parse @p stripsJson, enrich each tab with live title / urgency /
    /// per-window colour, and drive @p screenId's overlay indicator.
    void applyScrollTabStrips(const QString& screenId, const QString& stripsJson);
    /// Re-run the enrichment for every screen holding a cached payload.
    ///
    /// Needed because enrichment reads live window state the ENGINE cannot
    /// see, while the engine's tabStripsChanged is change-gated on the
    /// structural payload alone. A window that starts demanding attention or
    /// retitles moves no rect, so without this its tab would keep the values
    /// it had at the last structural change.
    void refreshScrollTabEnrichment();
    /// Coalescing front door for refreshScrollTabEnrichment. Retitling is a
    /// high-rate signal, so a burst collapses into a single refresh.
    void scheduleScrollTabEnrichmentRefresh();
    void initializeUnifiedController();
    void connectLayoutSignals();
    void connectOverlaySignals();
    void finalizeStartup();
    /** @brief Migrate window screen assignments from physical to virtual IDs after startup */
    void migrateStartupScreenAssignments();

    /**
     * @brief Pre-seed autotile engine with zone-ordered windows for one screen
     *
     * Prefers the SAVED order from the last mode toggle
     * (m_lastEngineOrders, deterministic re-entry) and only falls back to
     * the zone-ordered window list from WTS. filterEngineSeedOrder runs
     * before seeding: float is PER MODE, so a non-minimized window always
     * seeds (a snap-mode float must never make it untileable here), and
     * minimized windows stay as positional placeholders except the
     * user-floated-then-minimized case. See that function's contract — this
     * summary previously claimed the opposite and is exactly what would lead
     * a future fixer to reintroduce the untileable-by-mode-swap bug.
     * The result goes to the autotile engine's setInitialWindowOrder(). Used
     * by both per-screen toggle and global snapping→autotile transition.
     *
     * @param screenId Screen identifier
     */
    void seedAutotileOrderForScreen(const QString& screenId);

    /**
     * @brief Flip every autotile assignment to Snapping; restore each screen's
     *        saved snap layout; reset autotile-floating state. Caller is
     *        responsible for the post-conditioning calls
     *        (updateEngineScreens, updateLayoutFilter, snap resnap).
     */
    void handleAutotileDisabled();

    /**
     * @brief Activate autotile on every screen NOT already on an autotile
     *        assignment. Idempotent for mixed-mode setups: screens already
     *        running autotile keep their per-screen algorithm customisation.
     */
    void handleSnappingToAutotile();

    /**
     * @brief Pre-save snap-mode floating state before entering a tiling mode
     *
     * Captures each floating window's placement into its unified
     * WindowPlacement record (captureWindowPlacement — the record's snap slot
     * plus shared free geometry are the single source of truth; there is no
     * parallel saved-float set), so the snap slot survives the autotile or
     * scrolling session and the release handler can restore it. When screenId
     * is provided, only windows on that screen are captured; empty captures
     * all floating windows (global enable). Idempotent — content-identical
     * captures no-op.
     */
    void presaveSnapFloats(const QString& screenId = QString());
    /// Shared windowsReleased handler for both tiling-family engines:
    /// restores snap float/zone state for windows returning to snapping and
    /// clears the releasing engine's mode-specific float markers. See the
    /// definition in engine_release.cpp.
    void handleEngineWindowsReleased(PhosphorEngine::IPlacementEngine* releasingEngine, const QStringList& windowIds,
                                     const QSet<QString>& releasedScreenIds);

    /**
     * @brief Capture autotile window order for all autotile screens
     *
     * Must be called BEFORE any mode switch that destroys PhosphorTiles::TilingState
     * (e.g. applyLayoutById, handleAutotileDisabled, updateEngineScreens).
     *
     * @return Map of (screen, desktop, activity) -> ordered window IDs (master first)
     */
    QHash<TilingStateKey, QStringList> captureAutotileOrders() const;

    /**
     * @brief Build pre-tile geometry restore entries for autotile-only windows.
     *
     * Iterates m_lastEngineOrders and produces a `ZoneAssignmentEntry` per
     * autotile-only window (no zone assignment, never manually snapped).
     * Returns the batch so the caller can feed it to
     * `SnapEngine::emitBatchedResnap` — one batched signal per autotile
     * toggle instead of per-window D-Bus chatter. Zone-snapped windows are
     * already handled by `SnapAdaptor::resnapCurrentAssignments`.
     *
     * @param excludeWindows Window ids to leave out of the entries (already
     *        handled elsewhere in the calling flow).
     * @param desktop,activity Together restrict to ONE context: when desktop
     *        is -1 BOTH are ignored (any context, activity included); when
     *        desktop >= 0 the saved key must match both exactly, so an empty
     *        activity selects the no-activity context, not every activity.
     * @param onlyScreenId When non-empty, restrict to that screen's saved
     *        orders — the per-screen toggle must not emit restores for
     *        windows still tiled on other screens.
     */
    QVector<ZoneAssignmentEntry> buildAutotileRestoreEntries(const QSet<QString>& excludeWindows = {}, int desktop = -1,
                                                             const QString& activity = QString(),
                                                             const QString& onlyScreenId = QString());

    /** @brief Show layout OSD deferred (avoids blocking on first-time QML compilation) */
    void showLayoutOsdDeferred(const QUuid& layoutId, const QString& screenId);
    /** @brief Show algorithm OSD deferred (avoids blocking on first-time QML compilation) */
    void showAlgorithmOsdDeferred(const QString& algorithmId, const QString& algorithmName, const QString& screenId);

    /**
     * @brief Show OSD for the current desktop's layout/algorithm on desktop or activity switch
     *
     * Resolves the focused screen, reads the per-desktop assignment, and shows
     * the appropriate OSD (layout or algorithm). DRY helper for both
     * currentDesktopChanged and currentActivityChanged handlers.
     *
     * @param activity Current activity ID
     */
    void showDesktopSwitchOsd(const QString& activity);

    /**
     * @brief Per-screen desktop-switch OSD (Plasma 6.7 per-output virtual desktops)
     *
     * Shows the desktop-switch OSD only on @p screenId, using that screen's own
     * current virtual desktop. Driven by the per-screen screenDesktopChanged
     * handler so a single screen's switch doesn't flash every monitor (#648).
     */
    void showDesktopSwitchOsdForScreen(const QString& screenId, const QString& activity);

    /**
     * @brief Show per-screen OSD for all effective screens
     *
     * Iterates effectiveScreenIds, resolves assignment (autotile vs snapping),
     * and calls showLayoutOsdForAlgorithm or showLayoutOsd per screen inside
     * a single deferred event-loop pass so all surfaces show simultaneously.
     * DRY helper shared by showDesktopSwitchOsd and the startup OSD path
     * (finalizeStartup).
     */
    void showOsdForAllScreens(const QString& activity);

    /**
     * @brief Per-screen OSD for an explicit screen set
     *
     * Like showOsdForAllScreens but for the given @p screenIds; each screen uses
     * its OWN current virtual desktop (per-output virtual desktops). Backs both
     * showOsdForAllScreens and showDesktopSwitchOsdForScreen.
     */
    void showOsdForScreens(const QStringList& screenIds, const QString& activity);

    /**
     * @brief Recompute BOTH tiling-family engines' screen sets from the cascade
     *
     * Reads every screen's assignment, derives the autotile and scrolling
     * sets in one walk, and pushes them through the shared
     * capture-all → seed-all → apply-all phase (captureScrollingOrders /
     * updateScrollingScreens run in the same pass so a same-flip
     * autotile↔scrolling transition replays window order deterministically).
     */
    void updateEngineScreens();

    /**
     * @brief React to a rule change that may have altered active assignments.
     *
     * The unified rule store emits rulesChanged on any rule edit, but only a
     * change to the ACTIVE context's resolved assignment needs windows moved.
     * Diffs each screen's resolved assignment id against the snapshot; for the
     * screens that changed, retiles autotile screens (updateEngineScreens
     * self-diffs) and drives the legacy resnap/OSD path via the LayoutAdaptor
     * (markScreensChanged + applyAssignmentChanges). A no-op when nothing
     * assignment-affecting changed (appearance / exclude / lock edits, etc.).
     */
    void reconcileActiveAssignments();

    /**
     * @brief Recompute each effective screen's active assignment id and return
     *        the set whose id differs from @ref m_activeAssignmentByScreen,
     *        updating the snapshot to the new values (dropping removed screens).
     *
     * Called by reconcileActiveAssignments (with apply) and, with the result
     * discarded, to refresh the snapshot after a context switch or a legacy
     * apply so a later rule edit doesn't falsely re-resnap those screens.
     */
    QSet<QString> diffActiveAssignments();

    /**
     * @brief The KCM assignment-apply pass (init_assignment_apply.cpp).
     *
     * Runs on LayoutAdaptor::assignmentChangesApplied, AFTER the KCM's batch
     * save has committed every assignment and setting. Classifies each
     * effective screen by LIVE router mode, resnaps the snapping ones in
     * @p changedScreenIdsList, and announces the outcome per screen (disabled
     * / not-assigned / locked / mode / layout). An empty list means "every
     * screen may have changed".
     */
    void handleAssignmentChangesApplied(const QStringList& changedScreenIdsList);

    /**
     * @brief Respond to a PhosphorScreens::ScreenManager VS cache change for a physical screen
     *
     * Wired to PhosphorScreens::ScreenManager::virtualScreensChanged. Performs the post-change
     * fan-out: clears stale resnap buffer, migrates window assignments to the
     * new VS IDs (when subdivisions exist), prunes stale autotile orders,
     * refreshes the autotile screen set, recalculates affected zone
     * geometries inline, resnaps windows on this physical screen and its
     * virtual children, and schedules the debounced geometry update for
     * downstream consumers.
     */
    void onVirtualScreensReconfigured(const QString& physicalScreenId);

    /**
     * @brief Lightweight handler for regions-only VS config changes.
     *
     * Fires on swap/rotate/boundary-resize where the VS ID set is unchanged.
     * Skips migrate/prune/updateEngineScreens (all no-ops for regions-only)
     * and only recalculates zone geometries and triggers a snap-mode resnap
     * tagged with the vs_reconfigure action so the kwin-effect does not fire
     * snap-assist.
     *
     * The autotile retile is driven by the engine's own handler on
     * virtualScreenRegionsChanged — the Daemon's path does NOT force-retile
     * so there is exactly one retile per change (eliminates the "move then
     * retile" double-pass users observed on VS swap/rotate).
     */
    void onVirtualScreenRegionsChanged(const QString& physicalScreenId);

    /** @brief Resnap windows to current layout zones (only in manual/snap mode) */
    void resnapIfManualMode();

    /**
     * @brief Emit the float-restore half of m_pendingSnapFloatRestores for the
     *        resnap-buffer paths (picker / quick-layout cycle / KCM apply).
     *
     * windowsReleased populates m_pendingSnapFloatRestores whenever a screen
     * leaves the autotile set, but the resnap-buffer paths
     * (populateResnapBufferForAllScreens + resnapToNewLayout) never consume it
     * — only the mode-toggle and autotile-disable paths do. A window
     * snap-FLOATED before passing through autotile would therefore lose its
     * float-back position on a picker/KCM flip. Floating windows are excluded
     * from the resnap buffer, so these float restores are a disjoint set the
     * buffer path cannot cover; emit them as a separate batch.
     *
     * Two consume modes: with @p preserveZoneEntries (the
     * updateEngineScreens tail drain) only the float half is consumed and
     * the snap-ZONE entries stay in m_pendingSnapFloatRestores for the
     * mode-toggle / autotile-disable consumers that run after the
     * recompute. Without it (resnap-buffer consumers, prune-origin drains)
     * the whole buffer is consumed — remaining zone entries are handed to
     * an in-flight resnapToNewLayout when one exists, else dropped.
     */
    void emitPendingSnapFloatRestoresForResnapBuffer(bool preserveZoneEntries = false);

    /**
     * @brief Consume the snap-ZONE half of m_pendingSnapFloatRestores on a
     *        context-switch recompute that has no downstream zone consumer.
     *
     * updateEngineScreens' tail drain preserves the zone half for the
     * mode-toggle and autotile-disable paths, which feed it into
     * preClaimedZoneIds and their batched restore. The context-switch
     * recomputes (per-screen desktop switch, activity switch, virtual-screen
     * reconfigure, the tiled-count gates, the rule reconcile) have no such
     * consumer, so a tiling→snapping demotion there left windows sitting at
     * their tile rects and the entries lingering for an unrelated later
     * consumer to replay. The entries are complete (window, zones, geometry,
     * screen, desktop), so they are emitted directly as their own batch
     * rather than routed through the resnap buffer — that keeps the move
     * scoped to the windows the recompute actually released instead of
     * resnapping every window on the screen.
     *
     * No-op while a recompute is in progress: the batch then belongs to the
     * outer pass, whose own consumer owns the zone half.
     */
    void flushPendingSnapZoneRestores();

    /**
     * @brief Update layout filter on overlay service and unified layout controller
     *
     * Shows both manual and autotile layouts when the feature gate is enabled.
     */
    void updateLayoutFilter();
    /** @brief Update layout filter for a specific screen's mode (for cycle/popup) */
    void updateLayoutFilterForScreen(const QString& focusedScreenId);

    /**
     * @brief Sync UnifiedLayoutController from per-desktop assignments
     *
     * Syncs the current layout, the global active layout, and the layout
     * filter from the actual per-desktop assignment for the focused screen.
     * Must be called on every desktop/activity switch so global state
     * reflects the new context.
     */
    void syncModeFromAssignments();

    std::unique_ptr<PhosphorConfig::IBackend> m_configBackend;
    // Unified Rule store (rules.json). Declared BEFORE
    // m_layoutManager because the LayoutRegistry borrows it for its
    // rule-backed assignment cascade — construction order must build the
    // store first. The RuleAdaptor borrows it too.
    std::unique_ptr<PhosphorRules::RuleStore> m_ruleStore;
    // Filtered slice of m_ruleStore — only rules carrying an `Exclude` or
    // `ExcludePlacement` action (a kept rule may carry other actions too),
    // built via `PhosphorRules::ExclusionRules::excludePlacementRulesFrom` and
    // kept in lockstep with the store via the rulesChanged subscription wired
    // in init(). SnapEngine borrows a pointer into this set for isAppIdExcluded;
    // the WindowTrackingAdaptor's pruneExcludedPendingRestores receives the
    // AppId patterns extracted from this same slice at refilter time. Held as a
    // member (stable address) so the bound RuleEvaluator's per-revision cache
    // stays valid across back-to-back resolves.
    PhosphorRules::RuleSet m_excludeRuleSet;
    /// Native scrolling-template store. Created in the Daemon constructor,
    /// deliberately before the layout-source bundle is built so the template
    /// provider has a store to register against. initServices then injects it
    /// into m_layoutManager for the template-backed assignment cascade, and
    /// stop() clears that injection before this unique_ptr resets.
    ///
    /// Declared BEFORE m_layoutManager for the same reason m_ruleStore is:
    /// the registry borrows it, so reverse-order destruction must tear the
    /// registry down first or the borrow dangles.
    std::unique_ptr<PhosphorZones::ScrollingTemplateStore> m_scrollingTemplateStore;
    std::unique_ptr<PhosphorZones::LayoutRegistry> m_layoutManager;
    // Daemon-owned tile-algorithm registry, replacing the old
    // AlgorithmRegistry::instance() singleton: plugins can't share
    // process-global state safely, so the composition root owns it.
    // DECLARATION ORDER INVARIANT: every FactoryContext service the bundle
    // borrows (m_layoutManager → IZoneLayoutRegistry, m_algorithmRegistry
    // → ITileAlgorithmRegistry, m_scrollingTemplateStore →
    // ScrollingTemplateSource) MUST precede m_layoutSources, so
    // reverse-order destruction tears the bundle and its ZonesLayoutSource
    // / AutotileLayoutSource children down before the registries they
    // borrow. See the LayoutSourceBundle contract
    // (libs/phosphor-layout-api/.../LayoutSourceBundle.h). Violating it
    // dangles every source's destructor, hidden today only by Qt's signal
    // auto-disconnect, so don't reorder these three lines without
    // revisiting them. Must also precede ScriptedAlgorithmLoader and
    // AutotileEngine, which borrow it in their constructors.
    std::unique_ptr<PhosphorTiles::AlgorithmRegistry> m_algorithmRegistry;
    // Manual layouts + autotile algorithms composed behind layoutSource().
    // The bundle owns all three objects so destruction is deterministic
    // (composite first, then the child sources it borrows from). See
    // libs/phosphor-layout-api/.../LayoutSourceBundle.h for the
    // construction contract.
    PhosphorLayout::LayoutSourceBundle m_layoutSources;
    /// Cached pointer to the bundle's autotile source — populated once
    /// after buildFromRegistered in the ctor, then handed to every
    /// consumer that wants the long-lived preview-cache fast path
    /// (overlay service, layout adaptor, unified controller). Avoids
    /// repeating m_layoutSources.source(autotileLayoutSourceName())
    /// at every wiring site (DRY) and removes the temptation to typo
    /// the literal. Borrowed; lifetime tied to m_layoutSources, so it
    /// MUST stay declared immediately adjacent (and below) the bundle
    /// so reverse-order member destruction nulls borrowed-pointer
    /// consumers before the source itself is gone — see
    /// "DECLARATION ORDER INVARIANT" comment above.
    PhosphorLayout::ILayoutSource* m_autotileLayoutSource = nullptr;
    // ─── End of layout-source declaration block ─────────────────────────
    std::unique_ptr<PhosphorZones::LayoutComputeService> m_layoutComputeService;

    /// Raw Global-path profile as the loader registered it, snapshot once per
    /// loader reload and cleared on profilesChanged. The settings-driven
    /// publish merges its fallbacks over THIS rather than over the registry's
    /// current entry, which is the merged result of the previous tick and
    /// would freeze the fallbacks at their first observed value. Keyed by path
    /// even though kSettingsDrivenProfilePaths holds a single entry today, so
    /// adding a second path needs no new plumbing. Borrows nothing, so it sits
    /// outside the declaration-order block below.
    QHash<QString, PhosphorAnimation::Profile> m_rawJsonProfiles;
    /// Per-daemon curve registry, replacing the `CurveRegistry::instance()`
    /// singleton so each composition root owns its own.
    /// DECLARATION ORDER INVARIANT: must precede `m_settings`,
    /// `m_curveLoader` and `m_profileLoader`, all of which borrow it, so
    /// reverse-order destruction tears every consumer down first and no
    /// Settings / loader teardown path can UAF. Also cleared from
    /// `PhosphorCurve::s_registry` in `~Daemon`, so the QML static helper
    /// can't dangle on shutdown or across successive Daemon
    /// constructions in tests.
    PhosphorAnimation::CurveRegistry m_curveRegistry;
    /// Per-daemon profile registry, replacing the
    /// `PhosphorProfileRegistry::instance()` singleton. Published via
    /// `setDefaultRegistry` so QML callsites resolve through the same
    /// instance the daemon populates from Settings + ProfileLoader.
    /// DECLARATION ORDER INVARIANT: must precede `m_overlayService` (which
    /// references it from its SurfaceAnimator) and `m_profileLoader`, so
    /// reverse-order destruction tears the consumers down first and no
    /// service / loader teardown path can UAF. `stop()` calls
    /// `setDefaultRegistry(nullptr)` to clear the QML static handle.
    PhosphorAnimation::PhosphorProfileRegistry m_profileRegistry;
    /// Per-daemon QtQuickClock manager — replaces the prior process-
    /// global `QtQuickClockManager::instance()` singleton. Published via
    /// `setDefaultManager` so any `PhosphorAnimatedValueBase`-derived
    /// QML type in the overlay shell resolves the same per-window
    /// clocks as the C++ side. Owned over the daemon lifetime; the
    /// destroy-time `setDefaultManager(nullptr)` call in `stop()` drops
    /// the published handle before the unique_ptr destructs.
    std::unique_ptr<PhosphorAnimation::QtQuickClockManager> m_clockManager;
    // Declared BEFORE m_settings so reverse-order destruction frees it AFTER
    // m_settings: the ctor's adjacentThresholdChanged lambda (a m_settings->this
    // connection stop() deliberately does not sever) dereferences m_zoneDetector,
    // and if m_zoneDetector died first a signal emitted during m_settings
    // teardown would deref freed memory. Constructed from nullptr, so it has no
    // dependency on m_settings and the order flip is safe.
    std::unique_ptr<PhosphorZones::ZoneDetector> m_zoneDetector;
    std::unique_ptr<Settings> m_settings;
    // Single source of truth for live-window instance identity + metadata.
    // Populated by the kwin-effect bridge. Consumers query appIdFor() etc.
    // instead of parsing composite windowId strings.
    std::unique_ptr<PhosphorEngine::WindowRegistry> m_windowRegistry;
    /// Plasma D-Bus panel-offset source. Declared before m_screenManager
    /// because the manager holds a non-owning IPanelSource* into it.
    std::unique_ptr<PhosphorScreens::PlasmaPanelSource> m_panelSource;
    /// Settings-backed IConfigStore for VS topology. Shared by
    /// m_screenManager (Config::configStore) and m_virtualScreenSwapper
    /// (constructor arg). Declared before both so destruction order
    /// runs swapper → screen-manager → store.
    std::unique_ptr<SettingsConfigStore> m_virtualScreenStore;
    std::unique_ptr<PhosphorScreens::ScreenManager> m_screenManager;
    /// Per-daemon shader registry. Replaces the previous
    /// ShaderRegistry::instance() singleton — per-process ownership is the
    /// plugin-architecture-friendly shape (matches m_algorithmRegistry).
    /// Declared BEFORE m_overlayService so the OverlayService can hold a
    /// borrowed pointer to it; reverse-order destruction tears the service
    /// down before the registry, guaranteeing no UAF on shadersChanged
    /// disconnect during shutdown. Also declared before the D-Bus adaptors
    /// (ShaderAdaptor, SettingsAdaptor) that borrow it.
    std::unique_ptr<ShaderRegistry> m_shaderRegistry;
    /// OverlayService takes ScreenManager* via constructor injection — must
    /// be declared AFTER m_screenManager so the initializer-list construction
    /// order matches.
    std::unique_ptr<OverlayService> m_overlayService;
    /// Session-idle detection for Decorations.Performance.PauseWhenIdle.
    ///
    /// Owned by the DAEMON, not the effect: idleness arrives over
    /// `ext-idle-notify-v1`, which is a Wayland CLIENT protocol. The effect lives
    /// inside the compositor, which SERVES that protocol rather than consuming it,
    /// so it cannot watch for its own session going idle. The daemon is already a
    /// Wayland client, so it watches and pushes the resolved boolean to the effect
    /// over D-Bus (SettingsAdaptor::sessionIdleChanged).
    ///
    /// The effect pauses decoration-chain animation while idle, the only lever
    /// that lets the GPU leave its top performance state. An animated pack
    /// repaints every window carrying it on every vsync, and it is the EXISTENCE
    /// of per-frame work, not its size, that holds the clocks up.
    std::unique_ptr<PhosphorServiceIdle::IdleService> m_idleService;

    /// Coalesces idle-ladder rebuilds. Rearming destroys and recreates the
    /// compositor's ext-idle-notify-v1 object, and while idle it announces a
    /// resume that wakes every decorated window. The "Idle after" slider writes
    /// on every drag step, so rebuilds defer to one net reconfigure.
    QTimer m_idleStagesRefreshTimer;
    static constexpr int kIdleStagesRefreshDebounceMs = 250;

    /// Retry budget for an idle ladder that would not arm. This covers a login
    /// race (the seat's input devices are not advertised yet), which resolves in
    /// well under a second or not at all, so a few tries a second apart is
    /// generous. Exhausting it degrades the feature to off, which is what it
    /// silently did before anyone was checking.
    static constexpr int kIdleArmRetries = 5;
    static constexpr int kIdleArmRetryDelayMs = 1000;
    int m_idleArmRetriesLeft = kIdleArmRetries;

    /// The last idle state we announced. The effect starts up assuming an active
    /// session, so this starts false and the two agree from the outset.
    bool m_publishedSessionIdle = false;

    /// This compositor has no ext-idle-notify-v1, established once. m_idleService is null in
    /// that case, which is indistinguishable from "not built yet" — so start()'s re-arm,
    /// which guards on exactly that, would rebuild and re-probe the service on every
    /// stop()→start() cycle and log the unsupported notice again each time.
    bool m_idleUnsupported = false;

    /// Every connection setupIdleService made whose SENDER outlives m_idleService: the two
    /// settings signals, the debounce timer (a value member), and bridgeRegistered when a
    /// compositor bridge exists (conditional, so three or four). Held so
    /// stop() severs exactly these — not, say, every connection m_settings has to us, most
    /// of which are made in the constructor or init() and would never come back on a
    /// stop()→start() cycle — and so a re-armed service cannot stack duplicates.
    QList<QMetaObject::Connection> m_idleConnections;

    /// Connections installed by connectLayoutSignals() / connectOverlaySignals().
    /// Both functions re-run on every start(), but their senders survive stop(),
    /// so a restart would stack duplicate handlers. We disconnect these exact
    /// handles on re-entry rather than the (sender, signal, receiver) triple:
    /// other call sites install their OWN handlers on the same signals — e.g.
    /// initLayoutAndSettingsWiring() connects layoutAssigned from init() — and
    /// a triple-wide disconnect would silently delete those too. Qt::UniqueConnection
    /// is not an option here: it does not apply to lambda/functor connections.
    QList<QMetaObject::Connection> m_restartScopedConnections;
    /// Handles for the autotile shortcut connections installed by
    /// initializeAutotile(). Separate from m_restartScopedConnections because
    /// that list is cleared in connectLayoutSignals(), which start() calls
    /// AFTER initializeAutotile() — sharing one list would drop these handles
    /// the moment after they were installed.
    QList<QMetaObject::Connection> m_autotileShortcutConnections;
    /// Scrolling twin of the list above, cleared and refilled on the same
    /// schedule and kept separate for the same reason.
    QList<QMetaObject::Connection> m_scrollingShortcutConnections;
    /// Handles for every connection installed by initLayoutAndSettingsWiring().
    /// The senders (m_settings, m_layoutManager, the three value-member
    /// timers) all survive stop(), and init() CAN re-run (stop() -> init() ->
    /// start()), so a bare re-wire would stack duplicate handlers — double
    /// mode-transition passes and double gap resnaps per settings save. Exact
    /// handles, not (sender, signal, receiver) triples, for the same reason
    /// as m_restartScopedConnections.
    QList<QMetaObject::Connection> m_layoutSettingsWiringConnections;

    std::unique_ptr<PhosphorWorkspaces::VirtualDesktopManager> m_virtualDesktopManager;
    std::unique_ptr<PhosphorWorkspaces::ActivityManager> m_activityManager;
    std::unique_ptr<ShortcutManager> m_shortcutManager;

    // Domain-specific D-Bus adaptors
    // D-Bus adaptors need a parent (the adapted object); Qt requires it.
    // So we use raw pointers; Qt parent-child system manages their lifetime
    LayoutAdaptor* m_layoutAdaptor = nullptr;
    SettingsAdaptor* m_settingsAdaptor = nullptr;
    OverlayAdaptor* m_overlayAdaptor = nullptr; // Overlay visibility only
    ZoneDetectionAdaptor* m_zoneDetectionAdaptor = nullptr; // PhosphorZones::Zone detection queries
    WindowTrackingAdaptor* m_windowTrackingAdaptor = nullptr; // Window-zone tracking
    PhosphorScreens::DBusScreenAdaptor* m_screenAdaptor = nullptr;
    WindowDragAdaptor* m_windowDragAdaptor = nullptr; // Window drag handling
    // Held so stop() can invoke detach() before the unique_ptr members
    // those adaptors borrow from are destroyed. ~QObject runs AFTER all
    // unique_ptr member destructors, so without an explicit detach the
    // adaptors would see dangling pointers for a destruction-ordering
    // window (and any queued D-Bus call landing in that window would UAF).
    ShaderAdaptor* m_shaderAdaptor = nullptr;
    ControlAdaptor* m_controlAdaptor = nullptr;
    // Unified Rule store + its D-Bus adaptor. The store owns
    // rules.json (daemon sole writer); the adaptor exposes it on
    // org.plasmazones.Rules. Adaptor is Qt-parented (raw pointer); it
    // borrows the store, so stop() calls detach() before the store unique_ptr
    // is destroyed.
    RuleAdaptor* m_ruleAdaptor = nullptr;
    /// Scrolling-engine wire surface (org.plasmazones.Scrolling) — the
    /// scroll-specific screen set; lifecycle traffic rides the shared
    /// tiling adaptor. Qt-parented; stop() clears its engine pointer
    /// before the engine unique_ptr resets.
    ScrollingAdaptor* m_scrollingAdaptor = nullptr;
    // Compositor bridge adaptor (KWin effect ↔ daemon protocol endpoint).
    // Parented to `this`; holds only plain state, so it needs no detach().
    CompositorBridgeAdaptor* m_compositorBridge = nullptr;

    // Unified layout management
    std::unique_ptr<UnifiedLayoutController> m_unifiedLayoutController;

    // Scripted algorithm loader (file watcher for user-defined Luau algorithms).
    // m_algorithmRegistry is declared up at the top of the member block with
    // m_layoutManager — see the DECLARATION ORDER INVARIANT comment there.
    std::unique_ptr<PhosphorTiles::ScriptedAlgorithmLoader> m_scriptedAlgorithmLoader;

    // Shared neighbour-output / neighbour-desktop resolver injected into both
    // engines. Declared BEFORE the engines so it is destroyed AFTER them (they
    // borrow it), and after m_screenManager / m_virtualDesktopManager (which it
    // borrows) so those outlive it.
    std::unique_ptr<CrossSurfaceResolver> m_crossSurfaceResolver;

    // Window engines (held as base class; concrete types known only in daemon.cpp/enginefactory.cpp)
    std::unique_ptr<PhosphorEngine::PlacementEngineBase> m_autotileEngine;
    std::unique_ptr<PhosphorEngine::PlacementEngineBase> m_snapEngine;
    std::unique_ptr<PhosphorEngine::PlacementEngineBase> m_scrollEngine;
    /// Single source of truth for "which engine owns screen X". Used by
    /// WindowTrackingAdaptor and the daemon's navigation handlers (via
    /// `navigatorForShortcut` in navigation.cpp). Owns no state of its
    /// own — just delegates to the layout manager and engine pointers it
    /// was constructed with.
    std::unique_ptr<ScreenModeRouter> m_screenModeRouter;
    /// The engine screen sets DERIVED by the last updateEngineScreens
    /// recompute (post context-disable exclusion), snapshotted before any
    /// setActiveScreens applies. The shared windowsReleased handler gates
    /// its "headed to the other engine" skip on these — the live sets lag
    /// mid-pass and the raw cascade cannot see the exclusions.
    ///
    /// VALIDITY: windowsReleased also fires OUTSIDE updateEngineScreens
    /// (desktop/activity/removed-screen prunes). The handler prefers the
    /// live isActiveOnScreen answer when the recompute latch is NOT held —
    /// these snapshots are only authoritative mid-pass, where the live sets
    /// are the stale side.
    QSet<QString> m_derivedAutotileScreens;
    QSet<QString> m_derivedScrollingScreens;
    /// Re-entrancy latch + coalesced re-run flag for updateEngineScreens
    /// (see its head comment).
    bool m_updateEngineScreensInProgress = false;
    bool m_updateEngineScreensQueued = false;
    /// PhosphorContext::ContextResolver wiring.
    ///
    /// DECLARATION ORDER INVARIANT: the three adapter members must be
    /// declared (and therefore destroyed) AFTER `m_settings`,
    /// `m_virtualDesktopManager`, `m_activityManager`, and
    /// `m_screenModeRouter` — they hold non-owning pointers to those
    /// services. `m_contextResolver` must be declared AFTER the three
    /// adapters because it holds non-owning pointers to them. Reverse
    /// destruction order is C++'s default, so this declaration order
    /// guarantees the resolver tears down first, then the adapters,
    /// then the underlying services.
    std::unique_ptr<DaemonWorkspaceStateAdapter> m_workspaceStateAdapter;
    std::unique_ptr<DaemonScreenModeAdapter> m_screenModeAdapter;
    std::unique_ptr<DaemonSettingsGateAdapter> m_settingsGateAdapter;
    std::unique_ptr<PhosphorContext::ContextResolver> m_contextResolver;
    /// Stateless facade over m_virtualScreenStore for VS swap/rotate.
    /// Held as a member rather than reconstructed per-call so navigation
    /// handlers don't need to know about its dependencies.
    std::unique_ptr<PhosphorScreens::VirtualScreenSwapper> m_virtualScreenSwapper;
    SnapAdaptor* m_snapAdaptor = nullptr;
    TilingAdaptor* m_tilingAdaptor = nullptr;
    /// Autotile-engine wire surface (org.plasmazones.Autotile) — algorithm
    /// selection, master ops, and autotile config; lifecycle traffic rides
    /// the shared tiling adaptor. Qt-parented; stop() clears its engine
    /// pointer before the engine unique_ptr resets.
    AutotileAdaptor* m_autotileAdaptor = nullptr;

    /// Phase 6: animation shader effect discovery. Scans
    /// `plasmazones/animations` from XDG data dirs and monitors for
    /// user-dropped packs via QFileSystemWatcher. Declared AFTER
    /// m_overlayService — lifetime is managed explicitly in stop():
    /// the overlay service's borrowed registry pointer is nulled
    /// before this registry is reset, preventing dangling-pointer
    /// access during shutdown.
    std::unique_ptr<PhosphorAnimationShaders::AnimationShaderRegistry> m_animationShaderRegistry;

    /// Surface shader effect discovery (window border / rounded corners / glow
    /// — the third shader-pack category beside zone shaders + animation
    /// transitions). Scans `plasmazones/surface` from XDG data dirs and monitors
    /// for user-dropped packs via QFileSystemWatcher, mirroring
    /// m_animationShaderRegistry. The daemon warm-bakes discovered packs so the
    /// first surface paint never blocks on glslang, and lends the registry to
    /// the overlay service (setSurfaceShaderRegistry, Stage d) whose
    /// SurfaceShaderItem hosts render decoration packs on OSD / popup surfaces.
    /// Declared AFTER m_overlayService: stop() nulls the overlay's borrow
    /// before resetting this registry.
    std::unique_ptr<PhosphorSurfaceShaders::SurfaceShaderRegistry> m_surfaceShaderRegistry;

    /// Phase 4 sub-commit 7: user-authored curve / profile scanners.
    /// Scan `plasmazones/curves` and `plasmazones/profiles` from XDG
    /// data dirs and register discovered entries with `CurveRegistry`
    /// / `PhosphorProfileRegistry` with live-reload enabled. Owned by
    /// the daemon for process lifetime; QFileSystemWatcher survives
    /// as long as the loader.
    std::unique_ptr<PhosphorAnimation::CurveLoader> m_curveLoader;
    std::unique_ptr<PhosphorAnimation::ProfileLoader> m_profileLoader;

    /// Coalescing trampoline for the publish path — see
    /// `requestAnimationProfilePublish`. Single-shot, and a VALUE member (no
    /// QObject parent), so destruction is automatic. Paired with the separate
    /// `m_animationPublishPending` flag declared below (the timeout slot
    /// fires at 0 ms regardless of when the trampoline was first armed
    /// during the current event-loop tick).
    QTimer m_animationPublishTimer;
    bool m_animationPublishPending = false;

    // Desktop/activity resolution helpers (DRY — used by multiple handlers)
    int currentDesktop() const;
    /// This screen's current virtual desktop (Plasma 6.7 per-output virtual
    /// desktops, #648), falling back to the global currentDesktop().
    int currentDesktopForScreen(const QString& screenId) const;
    QString currentActivity() const;
    /// True when any effective screen's current-context assignment is an
    /// autotile layout. Reads the per-screen desktop (per-output virtual
    /// desktops, #648) and current activity fresh on every call.
    bool isAnyScreenAutotile() const;
    /// True when @p geometry overlaps at least one currently-connected
    /// effective screen. Used to reject restore targets that resolve onto an
    /// output that has gone away (monitor unplug).
    bool intersectsAnyLiveScreen(const QRect& geometry) const;
    bool isCurrentContextLockedForMode(const QString& screenId, PhosphorZones::AssignmentEntry::Mode mode) const;

    /**
     * @brief Sync daemon-side float state when autotile floats/unfloats a window
     *
     * Propagates floating state to PhosphorPlacement::WindowTrackingService and KWin effect,
     * manages autotile-originated vs snap-mode float bookkeeping, restores
     * pre-tile geometry on float, and shows navigation OSD.
     */
    void syncAutotileFloatState(const QString& windowId, bool floating, const QString& screenId);

    /**
     * @brief Passively sync daemon-side float state without restoring geometry
     *
     * Handler for AutotileEngine::windowFloatingStateSynced. Mirrors the WTS
     * bookkeeping of syncAutotileFloatState (setWindowFloating, autotileFloated
     * marker, pre-float zone housekeeping) but skips applyGeometryForFloat and
     * the navigation OSD — this path is invoked when the engine's internal
     * state diverges from WTS (e.g. a newly-inserted window carrying stale
     * snap-mode float state), not by a user float toggle. The window already
     * has a valid position and must not be teleported.
     */
    void syncAutotileFloatStatePassive(const QString& windowId, bool floating, const QString& screenId);

    /**
     * @brief Scroll twin of syncAutotileFloatStatePassive.
     *
     * Handler for ScrollEngine::windowFloatingStateSynced. The scroll engine
     * emits that signal (rather than windowFloatingChanged) for every float
     * transition it makes on its OWN initiative: the rule/oversize float at
     * open, the record-restore float at open, the cross-context and stale-key
     * migration drops, the unfloat-by-adoption, and the handoffReceive
     * re-float. None of those is a user float action, so none may restore the
     * stored free geometry (it would teleport a window that already has a
     * valid position — discussion #271) or raise a navigation OSD.
     *
     * It also carries the cross-engine eviction the autotile twin performs:
     * scroll adoption must release any stale snap or autotile tracking, or a
     * window re-announced onto a scrolling screen leaves a permanent ghost in
     * the engine that still tracks it.
     */
    void syncScrollFloatStatePassive(const QString& windowId, bool floating, const QString& screenId);

    /**
     * @brief Batch-update daemon-side float state for overflow-floated windows
     *
     * Updates WTS state directly without emitting per-window D-Bus signals
     * (the effect already processed the float from the windowsTileRequested batch).
     */
    void syncAutotileBatchFloatState(const QStringList& windowIds, const QString& screenId);

    /** @brief Prune m_lastEngineOrders for stale desktops */
    void pruneContextMapsForDesktop(int maxDesktop);
    /** @brief Prune context maps for removed activities */
    void pruneContextMapsForActivities(const QSet<QString>& validActivities);
    /** @brief Prune m_lastEngineOrders for old virtual screen IDs that no longer exist */
    void pruneEngineOrdersForRemovedScreens(const QString& physicalScreenId);
    /**
     * @brief Drop a closed window from every saved TILING-FAMILY order
     * (autotile stack orders and scrolling column orders share
     * m_lastEngineOrders).
     *
     * Without this, a window that closes while the screen is in manual mode
     * stays in m_lastEngineOrders. On the next manual→tiling toggle, the
     * order seeding feeds the stale id back through setInitialWindowOrder;
     * setActiveScreens replays it into the engine state and the retile
     * places a phantom window. Match by instance id — saved entries are
     * canonical "appId|instanceId" composites.
     */
    void pruneEngineOrdersForWindow(const QString& instanceId);

    /// Arm OSD suppression for @p count upcoming resnap feedback signals. ADDS
    /// to the running count (never clobbers) so overlapping async resnap streams
    /// accumulate instead of overwriting each other, and (re)starts the watchdog
    /// so a primed feedback that never arrives can't leave the counter stuck. A
    /// non-positive @p count is a no-op. See @ref m_suppressResnapOsd.
    void armResnapOsdSuppression(int count);

    bool m_running = false;
    int m_suppressResnapOsd = 0;
    /// Bounds @ref m_suppressResnapOsd leakage: a resnap that produces zero
    /// moves emits no feedback, so without this the count would stay armed and
    /// suppress the next unrelated OSD. Reset to 0 on timeout; re-armed by
    /// @ref armResnapOsdSuppression.
    QTimer m_suppressResnapOsdWatchdog;

    /// Shutdown flag — set by `aboutToQuit`, `stop()`. Gates `shouldSuppressOsd()`.
    bool m_shuttingDown = false;
    bool m_aboutToQuitConnected = false;

    /// Deadline bumped by `screenRemoved` (start.cpp). ~1 s cooldown prevents
    /// OSD shows during output teardown cascades and monitor hot-unplug.
    std::chrono::steady_clock::time_point m_screensSettlingUntil;

    /// Mirrors `plasma-workspace.target` ActiveState on the user-bus systemd.
    /// `true` = real session, `false` = phantom/inactive. Defaults to `true`
    /// (fail-open for non-systemd / headless). See `queryPlasmaWorkspaceState()`
    /// for the full rationale.
    bool m_plasmaWorkspaceActive = true;

    /// D-Bus object path for `plasma-workspace.target`, resolved by GetUnit.
    QString m_plasmaWorkspaceTargetPath;

    bool shouldSuppressOsd() const;

    /// Async query of systemd's user bus for `plasma-workspace.target` state.
    /// Fail-open on all D-Bus errors. Called once from `start()`.
    void queryPlasmaWorkspaceState();

    /// Continuation of `queryPlasmaWorkspaceState()` — fetches ActiveState
    /// and subscribes to PropertiesChanged after GetUnit resolves.
    void fetchPlasmaWorkspaceActiveState();

private Q_SLOTS:
    void onPlasmaWorkspaceTargetPropertiesChanged(const QString& interfaceName, const QVariantMap& changedProperties,
                                                  const QStringList& invalidatedProperties);

private:
    // Debounce timers for shortcuts that generate expensive work (Vulkan surface
    // creation, geometry batches, OSD churn) when triggered faster than ~100ms
    // by keyboard auto-repeat. Checked at the top of each handler.
    static constexpr int kShortcutDebounceMs = 100;
    QElapsedTimer m_rotateDebounce;
    QElapsedTimer m_floatDebounce;
    QElapsedTimer m_cycleLayoutDebounce;
    // One timer for all four span directions; handleSpan carries the why.
    QElapsedTimer m_spanDebounce;
    // Shared debounce for VS swap/rotate. Each fire commits a config change
    // through Settings and kicks a refresh → resnap cascade — cheap per call
    // but pile-up-prone under keyboard auto-repeat, same rationale as
    // m_rotateDebounce above. One timer for both ops: rapid alternation
    // between swap and rotate is not a user pattern.
    QElapsedTimer m_virtualScreenDebounce;
    /// Per-start connections outside stop()'s per-sender sweep and outside
    /// m_restartScopedConnections: the two m_settings→this cheatsheet
    /// refilters, on autotileEnabled and scrollingEnabled. m_settings is
    /// swept only by teardownIdleConnections, whose ctor/init connections
    /// must survive, so these per-start ones are severed here by handle. The WTA-to-drag-adaptor fan-out is instead
    /// kept unique with Qt::UniqueConnection, and the layout/overlay connections are tracked in
    /// m_restartScopedConnections and cleared at the top of connectLayoutSignals().
    QVector<QMetaObject::Connection> m_perStartConnections;

    // Last TILING-FAMILY window order per (screen, desktop, activity),
    // captured when a screen leaves autotile OR scrolling and shared by
    // both engines' seeding (captureScrollingOrders / updateScrollingScreens
    // and the autotile capture/seed) — a same-pass flip replays one
    // engine's order into the other. Keyed by TilingStateKey (not plain
    // screen name) so cross-desktop toggles don't overwrite each other.
    QHash<TilingStateKey, QStringList> m_lastEngineOrders;
    /// The engine's RAW tab-strip payload per screen, kept so the enrichment
    /// can be re-run without the engine re-emitting (see
    /// refreshScrollTabEnrichment). Written by applyScrollTabStrips keyed on
    /// the PARSED payload, so the engine's literal "[]" clear prunes the entry;
    /// also pruned on virtual-screen reconfigure and when a screen leaves
    /// scrolling. NOT rekeyed by OverlayService::rekeyOverlayState — after a
    /// VS rekey the stale key's pushes are refused downstream and the live key
    /// picks the cache back up on its next structural emit.
    QHash<QString, QString> m_lastScrollTabStripsJson;
    /// Set between a scheduleScrollTabEnrichmentRefresh() and its queued run.
    bool m_scrollTabEnrichmentPending = false;

    /// One screen's last-applied assignment state: the resolved assignment id
    /// plus, for Scrolling contexts, the resolved template layout id (empty
    /// elsewhere — the template resolver is mode-gated). The template rides
    /// the snapshot so the KCM apply path can tell a template-only change
    /// (same sentinel id, different template) from a genuine mode/layout
    /// switch and skip the mode-switch OSD for it; the diff's CHANGED set
    /// stays keyed on assignmentId alone (only an id change moves windows).
    struct ActiveAssignmentSnapshot
    {
        QString assignmentId;
        QString templateId;
        bool operator==(const ActiveAssignmentSnapshot&) const = default;
    };
    // Last-applied active assignment per effective screen (resolved for that
    // screen's current desktop/activity). Diffed on rulesChanged to find the
    // screens a rule edit actually moved; refreshed on context switches and
    // after any apply so a later edit doesn't falsely re-resnap. See
    // reconcileActiveAssignments / diffActiveAssignments.
    QHash<QString, ActiveAssignmentSnapshot> m_activeAssignmentByScreen;

    // Compression latch for the deferred rulesChanged → reconcile pass. The
    // store emits rulesChanged synchronously from inside every mutation, and
    // the daemon's own assignment writes (mode toggle, quick layouts, KCM
    // batch) are stored as rules — reconciling inline re-entered the full
    // assignment-apply path mid-toggle (double OSDs, a resnap racing the
    // engine flip). Deferring to the next event-loop pass lets the write's
    // own layoutAssigned tail re-prime the snapshot first, so self-inflicted
    // edits diff empty and only external rule edits actually apply.
    bool m_reconcileAssignmentsPending = false;

    // Last observed tiled-window count per screen, tracked so the engine's
    // placementChanged stream only re-resolves the per-screen tiling algorithm
    // when the count actually changes (a Field::TiledWindowCount rule keys on
    // it). Without this gate every retile (drag, resize) would re-walk the
    // assignment cascade. The value carries the ENGINE that recorded it: both
    // the autotile and scrolling gates write here, and after a mode flip the
    // incoming engine's first count must not compare against the outgoing
    // engine's cache (an equal count would swallow the re-resolve). The key
    // stays the bare screenId so the physical-id prune in start.cpp keeps
    // matching.
    QHash<QString, QPair<const void*, int>> m_lastTiledCountByScreen;

    // Snap-float restore entries collected by handleEngineWindowsReleased
    // (either tiling engine's windowsReleased). Cleared once per
    // updateEngineScreens recompute; consumed by the toggle handler to
    // batch geometry restores into the resnap signal.
    QVector<ZoneAssignmentEntry> m_pendingSnapFloatRestores;

    // State tracking for settingsChanged delta detection (replaces individual signal handlers)
    // Initialized from m_settings in init() before settingsChanged is connected.
    // Header defaults are safe no-ops: both false means "no prior state" so the
    // first settingsChanged won't detect a spurious toggle.
    bool m_prevSnappingEnabled = false;
    bool m_prevAutotileEnabled = false;
    bool m_prevScrollingEnabled = false;

    QTimer m_previewNotifyTimer;
    PhosphorTiles::AlgorithmPreviewParams m_preRetilePreviewParams;

    // Single-threaded pool for shader baking — QShaderBaker/glslang is not
    // thread-safe for concurrent compilation (SIGSEGV in QSpirvCompiler).
    QThreadPool m_shaderBakePool;
    /// Zone-path shadersChanged → warm-bake wiring, held so a stop() → init()
    /// cycle disconnects the prior handler instead of stacking a second one
    /// (m_shaderRegistry is ctor-owned and survives stop(), unlike the
    /// animation/surface registries which are recreated each init).
    QMetaObject::Connection m_zoneWarmBakeConnection;
    /// Skip-unchanged gate for the warm bakes: "<category>:<id>" → last
    /// scheduled fingerprint (vert path + vert mtime + frag path + frag mtime +
    /// param preamble). The fingerprint is built as a SUPERSET of the real bake
    /// cache key (ShaderNodeRhi::shaderCacheKey, which also folds the file
    /// mtimes in), so an unchanged pack is skipped on a whole-catalog registry
    /// emit while a pack whose .frag/.vert body was edited re-warms — the paths
    /// alone would stay identical across such an edit and wrongly suppress it.
    QHash<QString, QString> m_scheduledBakeFingerprints;

    // Geometry update debouncing to prevent cascade of redundant recalculations
    QTimer m_geometryUpdateTimer;
    bool m_geometryUpdatePending = false;
    void processPendingGeometryUpdates();

    // After geometry updates settle, request KWin effect to re-apply window positions (panel editor fix)
    QTimer m_reapplyGeometriesTimer;

    // Debounced resnap of currently-snapped windows after a gap/padding change
    // (global or per-screen snapping). Lets users see the new spacing applied to
    // already-snapped windows on save instead of having to re-snap each one
    // (discussion #661). Coalesces a batch of per-side edits into one pass.
    QTimer m_gapResnapTimer;

    // Watchdog: if the KWin effect has not registered as a compositor bridge
    // within a grace period after startup, window control is dead (drags and
    // shortcuts do nothing). On timeout the daemon logs a diagnostic warning
    // and raises a desktop notification. Stopped early once the bridge
    // registers. Single-shot.
    QTimer m_bridgeWatchdogTimer;
    void warnCompositorBridgeMissing();

    // Log a "compositor bridge missing" warning and raise a desktop
    // notification. `diagnosis` carries a specific root cause (e.g. an effect
    // plugin built against a different KWin version) when one was identified;
    // an empty string falls back to the generic enable-the-effect guidance.
    void emitBridgeMissingWarning(const QString& diagnosis);
};

} // namespace PlasmaZones
