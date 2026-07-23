// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QHash>
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

namespace PhosphorScreens {
class PlasmaPanelSource;
class DBusScreenAdaptor;
}

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/QtQuickClockManager.h>
#include <PhosphorConfig/IBackend.h>

namespace PhosphorAnimation {
class CurveLoader;
class ProfileLoader;
}

namespace PhosphorAnimationShaders {
class AnimationShaderRegistry;
}

namespace PhosphorSurfaceShaders {
class SurfaceShaderRegistry;
}

namespace PhosphorEngine {
class WindowRegistry;
}

namespace PhosphorWorkspaces {
class ActivityManager;
class VirtualDesktopManager;
}

namespace PhosphorServiceIdle {
class IdleService;
}

// PhosphorRules::RuleSet is held as a value member below
// (m_excludeRuleSet) — needs a complete type, so include the header
// rather than forward-declare. RuleStore stays in the header by
// pointer only; including RuleSet.h leaves the store forward
// declared here.
#include <PhosphorRules/RuleSet.h>

namespace PhosphorRules {
class RuleStore;
}

namespace PhosphorZones {
class Layout;
class LayoutComputeService;
class LayoutRegistry;
class ZoneDetector;
} // namespace PhosphorZones

// `AssignmentEntry::Mode` appears in member-function signatures below, so
// the full struct definition must be visible here (a forward declaration
// can't surface a nested enum). The header is LGPL-LGPL safe (PhosphorZones
// to daemon header is the standard direction).
#include <PhosphorZones/AssignmentEntry.h>

namespace PlasmaZones {

enum class DisabledReason;
class Settings;
class OverlayService;

class ShortcutManager;
class LayoutAdaptor;
class SettingsAdaptor;
class ShaderAdaptor;
class ControlAdaptor;
class CompositorBridgeAdaptor;
class OverlayAdaptor;
class ZoneDetectionAdaptor;
class WindowTrackingAdaptor;
class WindowDragAdaptor;
class RuleAdaptor;
class ZoneSelectorController;
class UnifiedLayoutController;
class AutotileAdaptor;
class ScreenModeRouter;
class CrossSurfaceResolver;
class DaemonScreenModeAdapter;
class DaemonSettingsGateAdapter;
class DaemonWorkspaceStateAdapter;

} // namespace PlasmaZones

namespace PhosphorContext {
class ContextResolver;
} // namespace PhosphorContext

namespace PlasmaZones {
class SettingsConfigStore;
class SnapAdaptor;
class ShaderRegistry;
} // namespace PlasmaZones

namespace PhosphorTiles {
class AlgorithmRegistry;
class ScriptedAlgorithmLoader;
}

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
     * navigation / OSD paths so the cascade
     * `(modeFor → currentDesktop → currentActivity → isContextDisabled
     * → isContextLocked)` resolves through one call instead of being
     * hand-stitched at each site. OverlayService is NOT yet a consumer —
     * its disabled-context gates still call the legacy
     * `isContextDisabled(m_settings, ...)` directly; migrating it is
     * follow-up work. The pointer is non-null after `init()` and stays
     * non-null until `stop()` runs to completion (which calls
     * `m_contextResolver.reset()` in the teardown order documented at
     * @ref m_contextResolver). Note that `stop()` returns early when
     * `m_running` is false — the init-without-start teardown path
     * therefore skips the explicit reset, and the resolver is destroyed
     * later by `~Daemon` via the unique_ptr member dtor. See
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
    void showLockedOsd(const QString& screenId);
    void showLockedPreviewOsd(const QString& screenId);
    void showContextDisabledOsd(const QString& screenId, int desktop, const QString& activity, DisabledReason reason);
    /// OSD shown when a context has no active layout because its default
    /// assignment is suppressed (global setting or per-context rule) — the
    /// "not assigned" counterpart to @ref showContextDisabledOsd. Tells the user
    /// the mode is selected but nothing is assigned, instead of silently showing
    /// no OSD.
    void showNotAssignedOsd(const QString& screenId);

    // Shortcut cheatsheet overlay (impls in daemon/osd.cpp).
    /// Toggle the cheatsheet on the cursor's screen. Show path resolves the
    /// screen's tiling mode + the shortcut catalog and pushes both into the
    /// overlay (daemon-mediated push), dismisses any other Escape-consuming
    /// modal first (picker / snap assist — at most one Escape grab consumer
    /// at a time), then binds the sheet's dedicated Escape ad-hoc grab.
    void toggleCheatsheet();
    /// Re-push catalog + mode into a visible cheatsheet — live refilter on
    /// mode switches, rebinds, and the autotile feature gate. No-op when
    /// hidden. Mode is re-resolved for the screen the sheet is BOUND to
    /// (not the cursor's current screen).
    void refreshCheatsheetIfVisible();
    /// Release the cheatsheet's Escape ad-hoc grab. Connected to
    /// OverlayService::cheatsheetDismissed in start.cpp.
    void onCheatsheetDismissed();

private:
    /// Show path for the toggle shortcut: resolve cursor screen, catalog,
    /// per-screen mode; dismiss sibling Escape-consuming modals; show and
    /// bind the Escape grab. Only called from toggleCheatsheet().
    void showCheatsheetOnCursorScreen();
    /**
     * @brief Show layout OSD for an autotile algorithm (visual zone preview)
     *
     * Renders the OSD unconditionally — gating on user OSD toggles
     * (showOsdOnLayoutSwitch / showOsdOnDesktopSwitch) is the caller's
     * responsibility. The osdStyle setting controls visual style.
     */
    void showLayoutOsdForAlgorithm(const QString& algorithmId, const QString& displayName, const QString& screenId);
    void clearHighlight();

    /**
     * @brief Bridge Settings::animationProfile into `PhosphorProfileRegistry`
     *        so QML `PhosphorMotionAnimation { profile: "<path>" }` resolves
     *        to the user's active animation settings and live-updates on edit.
     *
     * Phase 4 sub-commit 7. Scans the XDG `plasmazones/curves` and
     * `plasmazones/profiles` directories for user-authored definitions
     * (per decision U's consumer-namespace pattern) and installs
     * live-reload watchers. Registers the daemon's active animation
     * Profile under every well-known `ProfilePaths` shell path that
     * maps to PlasmaZones's single-Profile settings surface so QML
     * consumers can reference specific paths (`widget.zoneHighlight`,
     * `osd.show`, etc.) without the daemon carrying per-event
     * sub-profiles — future sub-commits can diverge paths when
     * per-event customisation is actually exposed to users.
     *
     * Reconnects to `Settings::animationProfileChanged` for live
     * updates; each emit re-registers the active Profile against the
     * same path set, firing `PhosphorProfileRegistry::profileChanged`
     * on each — bound `PhosphorMotionAnimation` consumers re-resolve
     * transparently.
     */
    void setupAnimationProfiles();
    void setupAnimationShaderEffects();
    void setupSurfaceShaderEffects();

    // init() phase methods, run in order from the thin init() (daemon.cpp); the
    // order is load-bearing. Defined across daemon/init_*.cpp + shader_warmup.cpp.
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
    /// under the shell's well-known paths. Called from
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
     * @brief Mode-explicit sibling of isFocusedContextGated for autotile-only
     * shortcuts.
     *
     * Same fail-closed null-resolver semantics, but queries the resolver
     * with an explicit mode (skipping the router-driven mode lookup).
     * Used by handleRetile / handleIncreaseMasterRatio /
     * handleDecreaseMasterRatio / HANDLE_AUTOTILE_ONLY — paths that
     * must gate against the Autotile disable list specifically rather
     * than the live mode for the screen.
     */
    bool isFocusedContextGatedForMode(const QString& screenId, PhosphorZones::AssignmentEntry::Mode mode) const;

    void handleRotate(bool clockwise);
    void handleFloat();
    void handleMove(NavigationDirection direction);
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
    void handleSwapVirtualScreen(NavigationDirection direction);
    void handleRotateVirtualScreens(bool clockwise);

    /** @brief Check if screen is locked for layout change in its current mode */
    bool isScreenLockedForLayoutChange(const QString& screenId);

    /** @brief Handle cycle-layout shortcut (previous or next) */
    void handleCycleLayout(const QString& screenId, bool forward);

    // Start-up sub-methods (definitions split across start.cpp and signals.cpp).
    void connectScreenSignals();
    void connectDesktopActivity();
    void connectShortcutSignals();
    void initializeAutotile();
    void initializeUnifiedController();
    void connectLayoutSignals();
    void connectOverlaySignals();
    void finalizeStartup();
    /** @brief Migrate window screen assignments from physical to virtual IDs after startup */
    void migrateStartupScreenAssignments();

    /**
     * @brief Pre-seed autotile engine with zone-ordered windows for one screen
     *
     * Builds the zone-ordered window list from WTS and passes it to the autotile
     * engine's setInitialWindowOrder(). Used by both per-screen toggle and global
     * snapping→autotile transition.
     *
     * @param screenId Screen identifier
     */
    void seedAutotileOrderForScreen(const QString& screenId);

    /**
     * @brief Flip every autotile assignment to Snapping; restore each screen's
     *        saved snap layout; reset autotile-floating state. Caller is
     *        responsible for the post-conditioning calls
     *        (updateAutotileScreens, updateLayoutFilter, snap resnap).
     */
    void handleAutotileDisabled();

    /**
     * @brief Activate autotile on every screen NOT already on an autotile
     *        assignment. Idempotent for mixed-mode setups: screens already
     *        running autotile keep their per-screen algorithm customisation.
     */
    void handleSnappingToAutotile();

    /**
     * @brief Pre-save snap-mode floating state before entering autotile
     *
     * Saves non-autotile-floated floating windows to WTS's savedSnapFloating set.
     * When screenId is provided, only saves windows on that screen. When empty,
     * saves all floating windows (used for global autotile enable).
     * Idempotent (QSet::insert).
     */
    void presaveSnapFloats(const QString& screenId = QString());

    /**
     * @brief Capture autotile window order for all autotile screens
     *
     * Must be called BEFORE any mode switch that destroys PhosphorTiles::TilingState
     * (e.g. applyLayoutById, handleAutotileDisabled, updateAutotileScreens).
     *
     * @return Map of (screen, desktop, activity) -> ordered window IDs (master first)
     */
    QHash<TilingStateKey, QStringList> captureAutotileOrders() const;

    /**
     * @brief Build pre-tile geometry restore entries for autotile-only windows.
     *
     * Iterates m_lastAutotileOrders and produces a `ZoneAssignmentEntry` per
     * autotile-only window (no zone assignment, never manually snapped).
     * Returns the batch so the caller can feed it to
     * `SnapEngine::emitBatchedResnap` — one batched signal per autotile
     * toggle instead of per-window D-Bus chatter. Zone-snapped windows are
     * already handled by `SnapAdaptor::resnapCurrentAssignments`.
     */
    QVector<ZoneAssignmentEntry> buildAutotileRestoreEntries(const QSet<QString>& excludeWindows = {}, int desktop = -1,
                                                             const QString& activity = QString());

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
     * @brief Recompute which screens use autotile from layout assignments
     *
     * Reads all screen assignments via assignmentIdForScreen(), computes
     * which screens have autotile IDs, calls setActiveScreens() on engine.
     */
    void updateAutotileScreens();

    /**
     * @brief React to a rule change that may have altered active assignments.
     *
     * The unified rule store emits rulesChanged on any rule edit, but only a
     * change to the ACTIVE context's resolved assignment needs windows moved.
     * Diffs each screen's resolved assignment id against the snapshot; for the
     * screens that changed, retiles autotile screens (updateAutotileScreens
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
     * Skips migrate/prune/updateAutotileScreens (all no-ops for regions-only)
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
     * buffer path cannot cover; emit them as a separate batch. The snap-ZONE
     * restores in the buffer reference the OLD layout and are intentionally
     * left to resnapToNewLayout (which re-snaps to the NEW layout's zones).
     * Consumes (clears) m_pendingSnapFloatRestores.
     */
    void emitPendingSnapFloatRestoresForResnapBuffer();

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
    // Filtered slice of m_ruleStore — only rules with a terminal `Exclude`
    // action, built via `PhosphorRules::ExclusionRules::excludeRulesFrom` and
    // kept in lockstep with the store via the rulesChanged subscription wired
    // in init(). SnapEngine borrows a pointer into this set for isAppIdExcluded;
    // the WindowTrackingAdaptor's pruneExcludedPendingRestores receives the
    // AppId patterns extracted from this same slice at refilter time. Held as a
    // member (stable address) so the bound RuleEvaluator's per-revision cache
    // stays valid across back-to-back resolves.
    PhosphorRules::RuleSet m_excludeRuleSet;
    std::unique_ptr<PhosphorZones::LayoutRegistry> m_layoutManager;
    // Daemon-owned tile-algorithm registry. Replaces the old
    // AlgorithmRegistry::instance() singleton — per-process ownership is
    // the only shape that works once PlasmaZones becomes a plugin-based
    // compositor/WM/shell (plugins can't share process-global state
    // safely).
    //
    // ─── DECLARATION ORDER INVARIANT ─────────────────────────────────
    // Every FactoryContext service the bundle borrows (m_layoutManager
    // → IZoneLayoutRegistry, m_algorithmRegistry → ITileAlgorithmRegistry)
    // MUST be declared before m_layoutSources so reverse-order member
    // destruction tears the bundle (and its ZonesLayoutSource /
    // AutotileLayoutSource children) down BEFORE the registries those
    // children borrow. The LayoutSourceBundle contract
    // (libs/phosphor-layout-api/.../LayoutSourceBundle.h) is explicit
    // about this — violating the order produces dangling pointers in
    // every source's destructor, hidden today only by Qt's signal
    // auto-disconnect. Do not reorder these three lines without revisiting
    // every source's destructor.
    //
    // Also declared before ScriptedAlgorithmLoader + AutotileEngine
    // because both take a borrowed pointer to it in their constructor.
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
    /// Per-daemon curve registry. Replaces the prior per-process
    /// `CurveRegistry::instance()` singleton — composition roots own
    /// their own.
    ///
    /// DECLARATION ORDER INVARIANT: must be declared BEFORE `m_settings`
    /// (which takes a borrowed pointer to it in its constructor) and
    /// BEFORE `m_curveLoader` / `m_profileLoader` (which also borrow).
    /// Reverse-order destruction then tears every consumer down before
    /// the registry itself, guaranteeing no UAF on the Settings /
    /// loader teardown paths. Also reset from `PhosphorCurve::s_registry`
    /// in `~Daemon` before teardown so the QML static helper doesn't
    /// dangle into freed memory on process shutdown or successive
    /// Daemon constructions in tests.
    PhosphorAnimation::CurveRegistry m_curveRegistry;
    /// Per-daemon profile registry. Replaces the prior process-global
    /// `PhosphorProfileRegistry::instance()` singleton — composition
    /// roots own their own and publish via `setDefaultRegistry` so QML
    /// callsites resolve through the same instance the daemon
    /// populates from Settings + ProfileLoader.
    ///
    /// DECLARATION ORDER INVARIANT: must be declared BEFORE
    /// `m_overlayService` (which takes a reference into its
    /// SurfaceAnimator) and BEFORE `m_profileLoader` (which holds a
    /// reference). Reverse-order destruction then tears the consumers
    /// down before the registry itself, guaranteeing no UAF on the
    /// service / loader teardown paths. The `setDefaultRegistry(nullptr)`
    /// call in `stop()` clears the QML static handle before teardown.
    PhosphorAnimation::PhosphorProfileRegistry m_profileRegistry;
    /// Per-daemon QtQuickClock manager — replaces the prior process-
    /// global `QtQuickClockManager::instance()` singleton. Published via
    /// `setDefaultManager` so any `PhosphorAnimatedValueBase`-derived
    /// QML type in the overlay shell resolves the same per-window
    /// clocks as the C++ side. Owned over the daemon lifetime; the
    /// destroy-time `setDefaultManager(nullptr)` call in `stop()` drops
    /// the published handle before the unique_ptr destructs.
    std::unique_ptr<PhosphorAnimation::QtQuickClockManager> m_clockManager;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<PhosphorZones::ZoneDetector> m_zoneDetector;
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
    /// The effect pauses decoration-chain animation while idle. That is the only
    /// lever that lets the GPU leave its top performance state: an animated pack
    /// repaints every window carrying it on every vsync, and it is the EXISTENCE of
    /// per-frame work, not its size, that holds the clocks up.
    std::unique_ptr<PhosphorServiceIdle::IdleService> m_idleService;

    /// Coalesces idle-ladder rebuilds. Rearming is not free and not silent — it
    /// destroys and recreates the compositor's ext-idle-notify-v1 object, and while
    /// the session is idle it announces a resume, which wakes every decorated window.
    /// The "Idle after" slider writes on every step of a drag, so the rebuild is
    /// deferred to one net reconfigure once the value settles.
    QTimer m_idleStagesRefreshTimer;
    static constexpr int kIdleStagesRefreshDebounceMs = 250;

    /// Retry budget for an idle ladder that would not arm. The failure this covers is a
    /// login race (the seat's input devices are not advertised yet), so it resolves in well
    /// under a second or it is not going to resolve at all — a handful of tries a second
    /// apart is generous. When the budget runs out the feature degrades to off, which is
    /// what it silently did before anyone was checking.
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
    /// initLayoutAndSettingsWiring() connects layoutAssigned from the ctor — and
    /// a triple-wide disconnect would silently delete those too. Qt::UniqueConnection
    /// is not an option here: it does not apply to lambda/functor connections.
    QList<QMetaObject::Connection> m_restartScopedConnections;

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
    /// Single source of truth for "which engine owns screen X". Used by
    /// WindowTrackingAdaptor and the daemon's navigation handlers (via
    /// `navigatorForShortcut` in navigation.cpp). Owns no state of its
    /// own — just delegates to the layout manager and engine pointers it
    /// was constructed with.
    std::unique_ptr<ScreenModeRouter> m_screenModeRouter;
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
    /// `requestAnimationProfilePublish`. Single-shot, parented to the
    /// daemon so destruction is automatic; only its `pending` flag is
    /// used (the timeout slot fires at 0 ms regardless of when the
    /// trampoline was first armed during the current event-loop tick).
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
     * @brief Batch-update daemon-side float state for overflow-floated windows
     *
     * Updates WTS state directly without emitting per-window D-Bus signals
     * (the effect already processed the float from the windowsTileRequested batch).
     */
    void syncAutotileBatchFloatState(const QStringList& windowIds, const QString& screenId);

    /** @brief Prune m_lastAutotileOrders for stale desktops */
    void pruneContextMapsForDesktop(int maxDesktop);
    /** @brief Prune context maps for removed activities */
    void pruneContextMapsForActivities(const QSet<QString>& validActivities);
    /** @brief Prune m_lastAutotileOrders for old virtual screen IDs that no longer exist */
    void pruneAutotileOrdersForRemovedScreens(const QString& physicalScreenId);
    /**
     * @brief Drop a closed window from every saved autotile order.
     *
     * Without this, a window that closes while the screen is in manual mode
     * stays in m_lastAutotileOrders. On the next manual→autotile toggle,
     * seedAutotileOrderForScreen feeds the stale id back through
     * setInitialWindowOrder; setActiveScreens replays it into the TilingState
     * and recalculateLayout tiles a phantom window. Match by instance id —
     * saved entries are canonical "appId|instanceId" composites.
     */
    void pruneAutotileOrdersForWindow(const QString& instanceId);

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
    // Shared debounce for VS swap/rotate. Each fire commits a config change
    // through Settings and kicks a refresh → resnap cascade — cheap per call
    // but pile-up-prone under keyboard auto-repeat, same rationale as
    // m_rotateDebounce above. One timer for both ops: rapid alternation
    // between swap and rotate is not a user pattern.
    QElapsedTimer m_virtualScreenDebounce;

    // Last autotile window order per (screen, desktop, activity), captured when
    // leaving autotile. Used to re-seed the autotile engine with the same order
    // on re-entry, producing deterministic arrangements across mode toggles.
    // Keyed by TilingStateKey (not plain screen name) so cross-desktop toggles
    // don't overwrite each other's ordering.
    QHash<TilingStateKey, QStringList> m_lastAutotileOrders;

    // Last-applied active assignment id per effective screen (resolved for that
    // screen's current desktop/activity). Diffed on rulesChanged to find the
    // screens a rule edit actually moved; refreshed on context switches and
    // after any apply so a later edit doesn't falsely re-resnap. See
    // reconcileActiveAssignments / diffActiveAssignments.
    QHash<QString, QString> m_activeAssignmentByScreen;

    // Last observed tiled-window count per screen, tracked so the engine's
    // placementChanged stream only re-resolves the per-screen tiling algorithm
    // when the count actually changes (a Field::TiledWindowCount rule keys on
    // it). Without this gate every retile (drag, resize) would re-walk the
    // assignment cascade.
    QHash<QString, int> m_lastTiledCountByScreen;

    // Snap-float restore entries collected during windowsReleasedFromTiling.
    // Consumed by the toggle handler to batch geometry restores into the resnap signal.
    QVector<ZoneAssignmentEntry> m_pendingSnapFloatRestores;

    // State tracking for settingsChanged delta detection (replaces individual signal handlers)
    // Initialized from m_settings in init() before settingsChanged is connected.
    // Header defaults are safe no-ops: both false means "no prior state" so the
    // first settingsChanged won't detect a spurious toggle.
    bool m_prevSnappingEnabled = false;
    bool m_prevAutotileEnabled = false;

    QTimer m_previewNotifyTimer;
    PhosphorTiles::AlgorithmPreviewParams m_preRetilePreviewParams;

    // Single-threaded pool for shader baking — QShaderBaker/glslang is not
    // thread-safe for concurrent compilation (SIGSEGV in QSpirvCompiler).
    QThreadPool m_shaderBakePool;

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
