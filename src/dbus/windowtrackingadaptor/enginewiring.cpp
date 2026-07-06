// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// WindowTrackingAdaptor — engine cross-wiring
//
// Split out of windowtrackingadaptor.cpp to keep that translation unit under the
// 800-line limit. Holds setEngines(), which wires cross-engine references, the
// disabled-context restore predicates, and the shared OSD navigation path.
// ═══════════════════════════════════════════════════════════════════════════════

#include "../windowtrackingadaptor.h"

#include "../zonedetectionadaptor.h"
#include "core/isettings.h"
#include "core/logging.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/RuleEvaluator.h>
#include <PhosphorRules/WindowQuery.h>
#include <PhosphorRules/RuleStore.h>

#include <QJsonArray>

namespace PlasmaZones {

namespace {
// Defined further down in this TU's anonymous namespace; forward-declared here
// so setEngines() (above that definition) can reference it from the snap
// engine's exclusion-query-provider closure.
std::optional<PhosphorRules::WindowQuery>
buildRuleQueryForWindow(const QPointer<PhosphorEngine::WindowRegistry>& registry, const QString& windowId);
} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Engine wiring — cross-references and shared navigation feedback
//
// Signal relay is handled by dedicated adaptors (SnapAdaptor, AutotileAdaptor).
// This method only wires cross-engine references and the shared OSD path.
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingAdaptor::setEngines(PhosphorEngine::PlacementEngineBase* snapEngine,
                                       PhosphorEngine::PlacementEngineBase* autotileEngine)
{
    // Disconnect previous autotile engine nav feedback (the only signal connected here)
    if (m_autotileEngine) {
        disconnect(m_autotileEngine, &PhosphorEngine::PlacementEngineBase::navigationFeedback, this, nullptr);
    }
    // Drop the cross-desktop move relay from BOTH outgoing engines before
    // reassigning (same anti-duplicate-connection reason as the float relay).
    if (m_snapEngine) {
        disconnect(m_snapEngine, &PhosphorEngine::PlacementEngineBase::windowDesktopMoveRequested, this, nullptr);
    }
    if (m_autotileEngine) {
        disconnect(m_autotileEngine, &PhosphorEngine::PlacementEngineBase::windowDesktopMoveRequested, this, nullptr);
        // The cross-output expected-move relay is autotile-only (snap never
        // does a daemon-side cross-output tiling migration); drop it on the
        // same anti-duplicate-connection rewire.
        disconnect(m_autotileEngine, &PhosphorEngine::PlacementEngineBase::windowOutputMoveExpected, this, nullptr);
    }
    // Drop the common float-restore geometry relay from BOTH outgoing engines
    // before reassigning, so a re-wire (mode toggle / daemon teardown) can't
    // accumulate duplicate connections.
    if (m_snapEngine) {
        disconnect(m_snapEngine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested, this, nullptr);
    }
    if (m_autotileEngine) {
        disconnect(m_autotileEngine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested, this, nullptr);
    }
    // Drop the cross-mode handoff slots (move + swap) from BOTH outgoing engines —
    // same anti-duplicate-connection rule. Without this, a rewire with the same
    // engine pointers would double-connect and fire handleCrossModeMove/Swap twice
    // per signal (double windowOutputMoveExpected, a second handoffReceive on
    // already-moved state).
    if (m_snapEngine) {
        disconnect(m_snapEngine, &PhosphorEngine::PlacementEngineBase::crossModeMoveRequested, this, nullptr);
        disconnect(m_snapEngine, &PhosphorEngine::PlacementEngineBase::crossModeSwapRequested, this, nullptr);
    }
    if (m_autotileEngine) {
        disconnect(m_autotileEngine, &PhosphorEngine::PlacementEngineBase::crossModeMoveRequested, this, nullptr);
        disconnect(m_autotileEngine, &PhosphorEngine::PlacementEngineBase::crossModeSwapRequested, this, nullptr);
    }
    // Drop the snap-specific state signals (snap-mode-only types, connected below
    // on the typed engine) from the outgoing snap engine — same rule. Uses the
    // cached typed pointer, still valid here (reassigned further down).
    if (m_cachedSnapEngine) {
        disconnect(m_cachedSnapEngine, &PhosphorSnapEngine::SnapEngine::windowSnapStateChanged, this, nullptr);
        disconnect(m_cachedSnapEngine, &PhosphorSnapEngine::SnapEngine::windowFloatingClearedForSnap, this, nullptr);
    }

    // Detach the snap restore predicate from the outgoing engine before dropping
    // the reference. The predicate captures `this`; clearing it honours the engine
    // header's detach contract ("clear via setShouldRestorePredicate({}) before
    // destroying the captured object") instead of relying on daemon destruction
    // order.
    if (m_cachedSnapEngine) {
        m_cachedSnapEngine->setShouldRestorePredicate({});
        m_cachedSnapEngine->setRestorePositionPredicate({});
        m_cachedSnapEngine->setManagedRestorePredicate({});
        m_cachedSnapEngine->setExclusionQueryProvider({});
        m_cachedSnapEngine->setFloatPredicate({});
        m_cachedSnapEngine->setPlacementZonesResolver({});
    }
    if (m_cachedAutotileEngine) {
        m_cachedAutotileEngine->setRestorePositionPredicate({});
        m_cachedAutotileEngine->setFloatPredicate({});
    }

    m_snapEngine = snapEngine;
    m_autotileEngine = autotileEngine;
    m_cachedSnapEngine = qobject_cast<PhosphorSnapEngine::SnapEngine*>(snapEngine);
    m_cachedAutotileEngine = qobject_cast<PhosphorTileEngine::AutotileEngine*>(autotileEngine);

    // ═══════════════════════════════════════════════════════════════════════════
    // Cross-engine references — SnapEngine needs AutotileEngine for
    // isActiveOnScreen() routing and ZoneDetectionAdaptor for adjacency queries.
    // These are wired only when a live SnapEngine is supplied; a teardown call
    // (nullptr, nullptr) has no SnapEngine to clear them on, so the stale borrows
    // are released by the engines' own destruction (Daemon::stop resets both
    // engines immediately after this call), not here.
    // ═══════════════════════════════════════════════════════════════════════════
    if (auto* snap = qobject_cast<PhosphorSnapEngine::SnapEngine*>(snapEngine)) {
        snap->setZoneAdjacencyResolver(m_zoneDetectionAdaptor);
        if (auto* autotile = qobject_cast<PhosphorTileEngine::AutotileEngine*>(autotileEngine)) {
            snap->setAutotileEngine(autotile);
        }

        // Disabled-context gate for snap auto-restore (discussion #461 item 7).
        // The persist-on-close gate (setShouldTrackPredicate, installed in the
        // WindowTrackingAdaptor constructor) blocks NEW
        // PendingRestore entries on disabled contexts, but pre-existing
        // in-memory entries (recorded before the user toggled the disable, or
        // before the disable propagated through settingsChanged) would still
        // restore the window when it reopens. This gate fires on the open
        // path, so the same isPersistedContextDisabled rule that filters reads
        // and writes also covers the window-arrives-during-running-session
        // case. Resolves the user-visible "windows tracked even on disabled
        // monitor — restarting the service fixes it" symptom: a restart
        // re-loaded from disk, where the read-time filter dropped the same
        // entries; this gate makes the running session match.
        //
        // Activity is left unset — SnapState carries no per-window activity
        // tag, mirroring isPersistedContextDisabled's snap-side default.
        //
        // Desktop: resolveWindowRestore carries no per-restore virtual desktop,
        // so the CURRENT desktop is used. This is exact for the common case (a
        // window opens on the current desktop) and is the only desktop the
        // predicate signature can observe; a cross-virtual-desktop session
        // restore onto a desktop other than the current one is therefore gated
        // by the current desktop's disable state rather than the target's.
        snap->setShouldRestorePredicate([this](const QString& screenId) -> bool {
            return !isPersistedContextDisabled(screenId, currentDesktopForScreen(screenId));
        });

        // Floated-position restore gate (snap-floated windows). On open the engine
        // asks whether THIS window should return to its recorded global position —
        // which, being in compositor-global coords, brings it back to its original
        // monitor even when KWin's session restore reopened it on another output.
        // shouldRestoreFloatedPosition resolves the per-window RestorePosition rule
        // when one matches, otherwise the per-engine
        // `snappingRestoreFloatedWindowsOnLogin` setting (Mode::Snapping here).
        snap->setRestorePositionPredicate([this](const QString& windowId) -> bool {
            return shouldRestoreFloatedPosition(windowId, PhosphorZones::AssignmentEntry::Mode::Snapping);
        });

        // Managed (snapped-to-zone) restore gate. The snapped-to-zone analogue of
        // the floated-position predicate above: when the user disables
        // `restoreWindowsToZonesOnLogin`, a window that was snapped at logout is
        // not auto-restored to its recorded zone on reopen. A matched
        // SetRestoreToZoneOnLogin rule overrides that per window; otherwise the
        // global setting decides. Engine stays settings-agnostic.
        snap->setManagedRestorePredicate([this](const QString& windowId) -> bool {
            return shouldRestoreToZoneOnLogin(windowId);
        });

        // Full-query exclusion provider. The snap engine owns the Exclude rule
        // set + evaluator but, without this, could only build an appId-only
        // query — so Exclude rules keyed on window class / title / size, and the
        // minimum-window-size thresholds, were silently ignored on snap (the
        // autotile engine, which sees the live window in the effect, honoured
        // them). Supplying the same full WindowQuery the float / restore
        // predicates use brings snapping to parity.
        snap->setExclusionQueryProvider([this](const QString& windowId) -> std::optional<PhosphorRules::WindowQuery> {
            return buildRuleQueryForWindow(m_windowRegistry, windowId);
        });

        // Open-floating gate (snap). A matched "Float this app" rule opens the
        // window floating instead of auto-snapping it. Purely rule-driven (no
        // global default), so the same resolver serves both engines.
        snap->setFloatPredicate([this](const QString& windowId) -> bool {
            return shouldFloatByRule(windowId);
        });

        // Open-placement resolver (snap). A matched "Snap this app to zone(s)"
        // rule snaps the opening window into the resolved zone ordinals (the
        // highest-priority restore-chain level), superseding the retired
        // per-layout Layout::appRules. Snap-only — autotile owns placement on
        // its screens, so no resolver is wired into the autotile engine.
        snap->setPlacementZonesResolver(
            [this](const QString& windowId, const QString& screenId) -> PhosphorSnapEngine::PlacementDirective {
                return placementZonesByRule(windowId, screenId);
            });

        // Snap-specific signal: carries PhosphorProtocol::WindowStateEntry which is snap-mode-only.
        // Connected via qobject_cast since the member type is PlacementEngineBase.
        connect(snap, &PhosphorSnapEngine::SnapEngine::windowSnapStateChanged, this,
                &WindowTrackingAdaptor::windowStateChanged);
        connect(snap, &PhosphorSnapEngine::SnapEngine::windowFloatingClearedForSnap, this,
                [this](const QString& windowId, const QString& screenId) {
                    Q_EMIT windowFloatingChanged(windowId, false, screenId);
                });
        // Unified model: keep the live placement record current on every snap
        // state change (commit / uncommit) so the persisted state always matches
        // the window's actual state — including for daemon-restart-window-open.
        connect(snap, &PhosphorSnapEngine::SnapEngine::windowSnapStateChanged, this,
                [this](const QString& windowId, const PhosphorProtocol::WindowStateEntry&) {
                    captureWindowPlacement(windowId);
                });
    } else if (snapEngine) {
        // Snap-mode window state signals are critical for WTS correctness.
        // A non-SnapEngine in the snap slot means state notifications are
        // lost — silently in release builds if we only Q_ASSERT here. The
        // construction-time pattern in snapadaptor.cpp uses qFatal for the
        // same class of misconfiguration; mirror it so debug AND release
        // both halt loudly instead of corrupting state propagation.
        qFatal(
            "WindowTrackingAdaptor::setEngines: snapEngine (%p) is not a SnapEngine — "
            "snap-specific signal wiring would silently drop window-state notifications",
            static_cast<void*>(snapEngine));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // AutotileEngine navigation feedback — shared OSD path
    //
    // Both engines' navigation feedback routes through this adaptor's
    // navigationFeedback signal because the KWin effect listens for OSD
    // data on org.plasmazones.WindowTracking regardless of engine mode.
    //
    // SnapEngine's navigation feedback is connected by SnapAdaptor (mirrors
    // AutotileAdaptor's constructor pattern). This single connection is the
    // only autotile signal that routes through WTA — all other autotile
    // signals go through AutotileAdaptor on org.plasmazones.Autotile.
    //
    // NOTE: AutotileEngine::windowFloatingChanged is NOT relayed here.
    // The daemon intercepts it with a lambda (signals.cpp) for cross-mode
    // state management (autotile-float markers, snap-float preservation,
    // geometry application). See ADR docs/adr-snapengine-migration.md.
    // ═══════════════════════════════════════════════════════════════════════════
    if (m_autotileEngine) {
        connect(m_autotileEngine, &PhosphorEngine::PlacementEngineBase::navigationFeedback, this,
                &WindowTrackingAdaptor::navigationFeedback);
        // Autotile's disabled-context gate is applied centrally at save time by the
        // WindowPlacementStore serialize keep-predicate (isPersistedContextDisabled),
        // so no engine-side disabled-context predicate injection is needed.
        //
        // Floated-position restore IS gated per-engine, mirroring snap: an
        // autotile-floated (untiled) window only returns to its recorded position
        // when the autotile `restoreFloatedWindowsOnLogin` setting (or a per-window
        // RestorePosition rule) opts it in. Same closure shape as the snap wiring
        // above, with Mode::Autotile selecting the autotile default.
        if (m_cachedAutotileEngine) {
            m_cachedAutotileEngine->setRestorePositionPredicate([this](const QString& windowId) -> bool {
                return shouldRestoreFloatedPosition(windowId, PhosphorZones::AssignmentEntry::Mode::Autotile);
            });
            // Open-floating gate (autotile). Same rule-driven resolver as snap; the
            // window is inserted then marked floating so it stays managed.
            m_cachedAutotileEngine->setFloatPredicate([this](const QString& windowId) -> bool {
                return shouldFloatByRule(windowId);
            });
        }
    }

    // Cross-desktop directional move: both engines emit windowDesktopMoveRequested
    // when they re-key a window onto another virtual desktop; relay it to the
    // KWin effect (which performs the real windowToDesktops) over the
    // mode-agnostic WindowTracking interface.
    if (m_autotileEngine) {
        connect(m_autotileEngine, &PhosphorEngine::PlacementEngineBase::windowDesktopMoveRequested, this,
                &WindowTrackingAdaptor::windowDesktopMoveRequested);
    }
    if (m_snapEngine) {
        connect(m_snapEngine, &PhosphorEngine::PlacementEngineBase::windowDesktopMoveRequested, this,
                &WindowTrackingAdaptor::windowDesktopMoveRequested);
    }

    // Daemon-initiated cross-output directional move: the autotile engine
    // migrates its own tiling state and reflows both outputs, then asks the
    // effect to treat the window's resulting outputChanged as expected (skip
    // the reactive close/open re-issue). Snap mode never performs a daemon-side
    // cross-output tiling migration, so only the autotile engine is wired.
    if (m_autotileEngine) {
        connect(m_autotileEngine, &PhosphorEngine::PlacementEngineBase::windowOutputMoveExpected, this,
                &WindowTrackingAdaptor::windowOutputMoveExpected);
    }

    // Cross-MODE directional move: a source engine reached a boundary whose
    // target context is a different tiling mode and cannot place the window
    // itself. Both engines defer here; handleCrossModeMove relinquishes from the
    // source and hands the window to the target engine (autotile insert / snap
    // zone). DirectConnection so the handoff completes synchronously within the
    // navigation call (the engine returns true expecting the window has moved).
    if (m_autotileEngine) {
        connect(m_autotileEngine, &PhosphorEngine::PlacementEngineBase::crossModeMoveRequested, this,
                &WindowTrackingAdaptor::handleCrossModeMove, Qt::DirectConnection);
    }
    if (m_snapEngine) {
        connect(m_snapEngine, &PhosphorEngine::PlacementEngineBase::crossModeMoveRequested, this,
                &WindowTrackingAdaptor::handleCrossModeMove, Qt::DirectConnection);
    }

    // Cross-MODE directional swap: like the move above, but a two-way exchange —
    // handleCrossModeSwap also relinquishes the target's entry-edge partner and
    // sends it back to the focused window's vacated slot. DirectConnection for the
    // same synchronous-handoff reason.
    if (m_autotileEngine) {
        connect(m_autotileEngine, &PhosphorEngine::PlacementEngineBase::crossModeSwapRequested, this,
                &WindowTrackingAdaptor::handleCrossModeSwap, Qt::DirectConnection);
    }
    if (m_snapEngine) {
        connect(m_snapEngine, &PhosphorEngine::PlacementEngineBase::crossModeSwapRequested, this,
                &WindowTrackingAdaptor::handleCrossModeSwap, Qt::DirectConnection);
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Common float-restore geometry channel
    //
    // Both engines emit the base-class geometryRestoreRequested when they
    // restore a floated window (consuming a floated WindowPlacement from the
    // unified store on reopen). Relay it to applyGeometryRequested
    // with an EMPTY zoneId: the effect places the window and treats it as
    // unmanaged (empty zoneId → clearWindowSnapped, no snap border). The engine
    // separately emits windowFloatingChanged(true) so the effect marks it
    // floating. This single relay gives every engine — current and future —
    // float-restore geometry application for free.
    // ═══════════════════════════════════════════════════════════════════════════
    const auto floatRestoreRelay = [this](const QString& windowId, const QRect& geometry, const QString& screenId) {
        if (!geometry.isValid()) {
            return;
        }
        Q_EMIT applyGeometryRequested(windowId, geometry.x(), geometry.y(), geometry.width(), geometry.height(),
                                      QString(), screenId, false);
    };
    if (snapEngine) {
        connect(snapEngine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested, this, floatRestoreRelay);
    }
    if (autotileEngine) {
        connect(autotileEngine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested, this,
                floatRestoreRelay);
    }
}

void WindowTrackingAdaptor::setRuleStore(PhosphorRules::RuleStore* store)
{
    if (m_ruleStore == store) {
        return;
    }
    m_ruleStore = store;
    // Drop the evaluator bound to the previous set; it rebuilds lazily against
    // the new one on the next per-window resolve.
    m_ruleEvaluator.reset();
}

namespace {
// Build a per-window rule query from the registry metadata, or nullopt when no
// metadata is tracked (the caller falls back to its own default). Shared by the
// RestorePosition and Float resolvers so the metadata→query derivation lives in
// one place. windowClass is not tracked daemon-side (the compositor reports
// appId, which is class-derived), so rules match on appId / title / role / type
// / desktop / pid plus the recorded desktop / activity context; screenId stays
// empty (a window-domain rule does not pin a screen). The extended window
// properties (state flags, geometry, accessory flags, captionNormal) are carried
// straight through from the effect's snapshot (setWindowMetadata's a{sv}), so a
// Float / RestorePosition rule keyed on e.g. IsModal or Width matches the same
// values the effect path resolves live. Placement state (IsFloating / IsSnapped /
// Zone) is deliberately absent: these resolvers run at window-open, before any
// placement exists, so a predicate over them must stay inert.
std::optional<PhosphorRules::WindowQuery>
buildRuleQueryForWindow(const QPointer<PhosphorEngine::WindowRegistry>& registry, const QString& windowId)
{
    if (registry.isNull()) {
        return std::nullopt;
    }
    // WindowRegistry is keyed by the BARE instance id; the engine hands us the
    // composite `appId|instanceId`. Extract first — every other registry reader
    // (currentAppIdFor, windowClosed, AutotileEngine) does the same. Looking up by
    // the composite id always misses, which would silently make the rule inert.
    const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(windowId);
    const std::optional<PhosphorEngine::WindowMetadata> meta = registry->metadata(instanceId);
    if (!meta) {
        return std::nullopt;
    }
    PhosphorRules::WindowQuery query;
    query.appId = meta->appId;
    if (!meta->title.isEmpty()) {
        query.title = meta->title;
    }
    if (!meta->windowRole.isEmpty()) {
        query.windowRole = meta->windowRole;
    }
    if (!meta->desktopFile.isEmpty()) {
        query.desktopFile = meta->desktopFile;
    }
    if (meta->pid > 0) {
        query.pid = meta->pid;
    }
    query.windowType = meta->windowType;
    query.virtualDesktop = meta->virtualDesktop;
    query.activity = meta->activity;
    // Screen-derived context fields (ScreenId, Mode, ScreenOrientation, ActiveLayout)
    // are intentionally NOT stamped here: the window metadata does not carry the
    // window's screen geometry / active layout, and this daemon-side query feeds the
    // open-path Float / Restore / placement resolvers only. The effect's live
    // per-window query (ruleQueryFor) stamps ScreenId / Mode / ScreenOrientation, so
    // a rule pairing one of those with a window property resolves there but not on
    // this path. ActiveLayout is populated only by the windowless context cascade
    // (never by either per-window query), so it is context-scoped in practice —
    // which is the primary use of all four of these fields anyway.
    // Extended properties — optional→optional copy preserves engagement exactly,
    // so a field the effect could not observe stays disengaged and inert here too.
    query.isMinimized = meta->isMinimized;
    query.isFullscreen = meta->isFullscreen;
    query.isSticky = meta->isSticky;
    query.isMaximized = meta->isMaximized;
    query.isFocused = meta->isFocused;
    query.isTransient = meta->isTransient;
    query.isNotification = meta->isNotification;
    query.keepAbove = meta->keepAbove;
    query.keepBelow = meta->keepBelow;
    query.skipTaskbar = meta->skipTaskbar;
    query.skipPager = meta->skipPager;
    query.skipSwitcher = meta->skipSwitcher;
    query.isModal = meta->isModal;
    query.hasDecoration = meta->hasDecoration;
    query.isResizable = meta->isResizable;
    query.isMovable = meta->isMovable;
    query.isMaximizable = meta->isMaximizable;
    query.width = meta->width;
    query.height = meta->height;
    query.positionX = meta->positionX;
    query.positionY = meta->positionY;
    query.captionNormal = meta->captionNormal;
    return query;
}
} // namespace

bool WindowTrackingAdaptor::shouldRestoreFloatedPosition(const QString& windowId,
                                                         PhosphorZones::AssignmentEntry::Mode mode)
{
    // m_settings is a hard ctor dependency (qFatal on null), so it is non-null
    // here — deref unguarded like every other method in this class. The global
    // default is per-engine (snap-floated vs autotile-floated); the RestorePosition
    // rule override below is engine-neutral.
    const bool globalDefault = mode == PhosphorZones::AssignmentEntry::Mode::Autotile
        ? m_settings->autotileRestoreFloatedWindowsOnLogin()
        : m_settings->snappingRestoreFloatedWindowsOnLogin();

    // No rule store / metadata → the global setting is the whole policy.
    if (!m_ruleStore) {
        return globalDefault;
    }
    const std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return globalDefault;
    }

    if (!m_ruleEvaluator) {
        m_ruleEvaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());
    }
    // Shares m_ruleEvaluator with shouldFloatByRule; resolveCached is keyed on
    // (windowId, ruleSet revision) and ignores the query on a hit. Safe because both
    // are open-path (resolved once per window lifetime — see shouldFloatByRule) and
    // the effect pushes the window's full metadata before the engine's open-path
    // resolve, so the first (and only) resolve for a window sees complete metadata.
    const PhosphorRules::ResolvedActions resolved = m_ruleEvaluator->resolveCached(windowId, *query);
    if (const std::optional<PhosphorRules::RuleAction> action =
            resolved.slot(QString(PhosphorRules::ActionSlot::RestorePosition))) {
        // A matched RestorePosition rule overrides the global setting.
        return action->params.value(QString(PhosphorRules::ActionParam::Value)).toBool();
    }
    return globalDefault;
}

bool WindowTrackingAdaptor::shouldRestoreToZoneOnLogin(const QString& windowId)
{
    // Mirror shouldRestoreFloatedPosition for the snapped-to-zone policy: a matched
    // SetRestoreToZoneOnLogin rule wins, otherwise the global setting decides.
    const bool globalDefault = m_settings->restoreWindowsToZonesOnLogin();
    if (!m_ruleStore) {
        return globalDefault;
    }
    const std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return globalDefault;
    }
    if (!m_ruleEvaluator) {
        m_ruleEvaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());
    }
    const PhosphorRules::ResolvedActions resolved = m_ruleEvaluator->resolveCached(windowId, *query);
    if (const std::optional<PhosphorRules::RuleAction> action =
            resolved.slot(QString(PhosphorRules::ActionSlot::RestoreToZoneOnLogin))) {
        return action->params.value(QString(PhosphorRules::ActionParam::Value)).toBool();
    }
    return globalDefault;
}

bool WindowTrackingAdaptor::shouldRestoreSizeOnUnsnap(const QString& windowId)
{
    // A matched SetRestoreSizeOnUnsnap rule wins, otherwise the global setting decides.
    const bool globalDefault = m_settings->restoreOriginalSizeOnUnsnap();
    if (!m_ruleStore) {
        return globalDefault;
    }
    const std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return globalDefault;
    }
    if (!m_ruleEvaluator) {
        m_ruleEvaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());
    }
    // Unlike the open-path resolvers above, this fires MID-SESSION on every unsnap
    // (drag-out / drop / cursor-left-zones), long after the window opened. A fresh
    // uncached resolve is required: resolveCached is keyed on (windowId, ruleSet
    // revision) and returns the OPEN-TIME verdict on a hit, so a rule whose WHEN
    // references a property the registry refreshes mid-session (VirtualDesktop /
    // Activity, re-pushed on desktopsChanged / activitiesChanged) would resolve
    // stale. resolve() honours the freshly built query and does not pollute the
    // open-path cache. (Properties the effect does not re-push on a dedicated
    // maximize / geometry change — e.g. IsMaximized / width — are only as fresh as
    // the registry's last extended push, so resolve() reads that same value either
    // way: neutral, not stale, for those; a strict improvement for the refreshed
    // ones.)
    const PhosphorRules::ResolvedActions resolved = m_ruleEvaluator->resolve(*query);
    if (const std::optional<PhosphorRules::RuleAction> action =
            resolved.slot(QString(PhosphorRules::ActionSlot::RestoreSizeOnUnsnap))) {
        return action->params.value(QString(PhosphorRules::ActionParam::Value)).toBool();
    }
    return globalDefault;
}

bool WindowTrackingAdaptor::shouldFloatByRule(const QString& windowId)
{
    // Float is purely rule-driven: there is no global "float on open" setting, so
    // absent a matching rule the answer is "do not float".
    if (!m_ruleStore) {
        return false;
    }
    const std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return false;
    }

    if (!m_ruleEvaluator) {
        m_ruleEvaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());
    }
    // resolveCached is keyed on (windowId, ruleSet revision); on a cache hit the
    // freshly built `query` is ignored. That is safe because windowId is both
    // lifetime-stable AND unique: a reopened window gets a fresh instanceId (new
    // key → miss) and a mid-session appId rename changes the composite key too, so
    // a cached verdict can never outlive the metadata it was built from. Both the
    // float and restore predicates are open-path (resolved once per lifetime).
    const PhosphorRules::ResolvedActions resolved = m_ruleEvaluator->resolveCached(windowId, *query);
    // The Float action carries free-form params (no Value key), so the verdict is
    // the PRESENCE of the filled slot, not a bool payload.
    return resolved.slot(QString(PhosphorRules::ActionSlot::Float)).has_value();
}

PhosphorSnapEngine::PlacementDirective WindowTrackingAdaptor::placementZonesByRule(const QString& windowId,
                                                                                   const QString& screenId)
{
    // Placement is purely rule-driven: absent a matching SnapToZone / RouteToScreen
    // rule there is nothing to snap or route, so the answer is empty.
    if (!m_ruleStore) {
        return {};
    }
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return {};
    }
    // Pin the query to the window's opening screen so a user-authored SnapToZone
    // rule carrying a `ScreenId` match (the settings screen-picker stores the
    // canonical id form the runtime reports) resolves against the screen the
    // window is actually opening on. buildRuleQueryForWindow leaves screenId empty
    // (the sibling Float / RestorePosition resolvers do not have the screen), so
    // the placement path is the one consumer that pins it.
    //
    // The shared evaluator cache is keyed on windowId only, so the FIRST resolver
    // to touch a window seeds the verdict the others reuse. On the open path that
    // is this placement resolve: SnapEngine::resolveWindowRestore calls
    // calculateSnapToPlacementRule up front, before it consults the float /
    // restore predicates — so the screen-pinned query populates the cache first
    // and a screen-constrained rule resolves correctly.
    query->screenId = screenId;

    if (!m_ruleEvaluator) {
        m_ruleEvaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());
    }
    // Shares m_ruleEvaluator with shouldFloatByRule / shouldRestoreFloatedPosition;
    // resolveCached is keyed on (windowId, ruleSet revision) and returns every matched
    // slot, so reading the Placement slot off the same verdict is free. Same open-path
    // lifetime guarantee (resolved once per window lifetime) as the sibling predicates.
    const PhosphorRules::ResolvedActions resolved = m_ruleEvaluator->resolveCached(windowId, *query);

    PhosphorSnapEngine::PlacementDirective directive;

    // RouteToScreen target (optional, independent of SnapToZone): pin the placement
    // to a specific monitor. A non-empty target moves the window there and resolves
    // its zone on that screen. The id is the canonical screen-id form the picker and
    // the runtime both use; the snap engine declines the route if the target is not
    // currently a snapping-mode screen, so an absent / autotile / disabled target is
    // safe here.
    if (const auto route = resolved.slot(QString(PhosphorRules::ActionSlot::RouteScreen))) {
        directive.targetScreenId = route->params.value(QString(PhosphorRules::ActionParam::TargetScreenId)).toString();
    }

    // RouteToDesktop target (optional): when set, the zones resolve on this
    // desktop's layout and the snap commits in this desktop's context, so a
    // combined SnapToZone + RouteToDesktop rule lands the window in the right zone
    // of the destination desktop. The desktop MOVE itself is emitted separately by
    // applyOpenDesktopRouting (engine-neutral); this only steers the snap placement.
    if (const auto route = resolved.slot(QString(PhosphorRules::ActionSlot::RouteDesktop))) {
        const int desktop = route->params.value(QString(PhosphorRules::ActionParam::TargetDesktop)).toInt(0);
        if (desktop >= 1) {
            directive.targetDesktop = desktop;
        }
    }

    const std::optional<PhosphorRules::RuleAction> action =
        resolved.slot(QString(PhosphorRules::ActionSlot::Placement));
    if (!action) {
        // No SnapToZone: return the (possibly route-only) directive. With no ordinals
        // the snap engine treats it as "nothing to snap", so a RouteToScreen WITHOUT
        // an accompanying SnapToZone produces no snap here. The bare "move to monitor
        // X" is performed by applyOpenScreenRouting on the snap open-path facade (it
        // runs only when nothing snapped the window), not in this directive builder.
        return directive;
    }
    // The descriptor validator already guaranteed a non-empty array of in-range
    // 1-based ordinals at load; re-validate defensively against the SAME bound
    // (1..MaxZoneOrdinal) so a future loader change can never feed a bad ordinal
    // into zone resolution.
    const QJsonArray arr = action->params.value(QString(PhosphorRules::ActionParam::Zones)).toArray();
    directive.zoneOrdinals.reserve(arr.size());
    for (const QJsonValue& v : arr) {
        const int n = v.toInt(0);
        if (n >= 1 && n <= PhosphorRules::MaxZoneOrdinal) {
            directive.zoneOrdinals.append(n);
        }
    }
    return directive;
}

void WindowTrackingAdaptor::emitRouteToDesktopIfMatched(const PhosphorRules::ResolvedActions& resolved,
                                                        const QString& windowId)
{
    const std::optional<PhosphorRules::RuleAction> route =
        resolved.slot(QString(PhosphorRules::ActionSlot::RouteDesktop));
    if (!route) {
        return;
    }
    // The descriptor validator already guaranteed a 1-based desktop in range; the
    // effect-side slot re-guards (rejects < 1, out-of-range, and sticky windows),
    // so moving to the desktop the window already occupies is a harmless no-op.
    const int desktop = route->params.value(QString(PhosphorRules::ActionParam::TargetDesktop)).toInt(0);
    if (desktop >= 1) {
        qCInfo(lcDbusWindow) << "open-routing: routing" << windowId << "to virtual desktop" << desktop;
        Q_EMIT windowDesktopMoveRequested(windowId, desktop);
    }
}

void WindowTrackingAdaptor::applyOpenDesktopRouting(const QString& windowId, const QString& screenId)
{
    // Engine-neutral RouteToDesktop: when a matched rule pins the opening window
    // to a virtual desktop, ask the compositor to move it there. Independent of
    // snapping/tiling — the desktop move composes with the window's placement.
    // Called from the snap open-path facade (the autotile path uses
    // applyOpenRoutingForAutotile, which also handles the screen redirect).
    if (!m_ruleStore) {
        return;
    }
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return;
    }
    // Pin the screen so a ScreenId-scoped rule resolves, mirroring placementZonesByRule.
    // resolveCached is keyed on windowId (+ rule-set revision), so on the snap open path
    // this reuses the verdict placementZonesByRule already seeded — no second evaluation.
    query->screenId = screenId;
    if (!m_ruleEvaluator) {
        m_ruleEvaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());
    }
    emitRouteToDesktopIfMatched(m_ruleEvaluator->resolveCached(windowId, *query), windowId);
}

void WindowTrackingAdaptor::applyOpenScreenRouting(const QString& windowId, const QString& screenId)
{
    if (!m_ruleStore) {
        return;
    }
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return;
    }
    // Pin the screen so a ScreenId-scoped rule resolves, mirroring placementZonesByRule.
    query->screenId = screenId;
    if (!m_ruleEvaluator) {
        m_ruleEvaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());
    }
    const PhosphorRules::ResolvedActions resolved = m_ruleEvaluator->resolveCached(windowId, *query);

    // Bare RouteToScreen only. A rule that ALSO carries SnapToZone routes AND snaps
    // via the placement directive (calculateSnapToPlacementRule resolves the zones
    // ON the target screen and returns shouldSnap, so the facade never reaches the
    // no-snap branch that calls this); moving here too would double-place the window.
    if (resolved.slot(QString(PhosphorRules::ActionSlot::Placement))) {
        return;
    }
    const std::optional<PhosphorRules::RuleAction> route =
        resolved.slot(QString(PhosphorRules::ActionSlot::RouteScreen));
    if (!route) {
        return;
    }
    const QString target = route->params.value(QString(PhosphorRules::ActionParam::TargetScreenId)).toString();
    if (target.isEmpty() || target == screenId) {
        return;
    }
    // m_service is non-null post-construction (class invariant); screenManager()
    // itself may still be null (e.g. an unconfigured test fixture), so guard that.
    PhosphorScreens::ScreenManager* screens = m_service->screenManager();
    if (!screens) {
        return;
    }
    const QRect dstAvail = screens->screenAvailableGeometry(target);
    if (!dstAvail.isValid()) {
        // Target monitor is not currently connected — leave the window on its spawn
        // screen (the rule fires again when that monitor returns).
        qCDebug(lcDbusWindow) << "applyOpenScreenRouting: route target" << target
                              << "is not currently connected — not moving" << windowId;
        return;
    }
    const QRect cur = frameGeometry(windowId);
    if (!cur.isValid()) {
        // No geometry pushed yet — nothing to translate onto the target screen.
        return;
    }

    // Map the window's position relative to its current screen's available area onto
    // the target screen's, preserving size, then clamp so the whole frame fits.
    // Preserves "the same spot on the other monitor" across differing resolutions; an
    // unknown / degenerate source area falls back to the target's top-left.
    const QRect srcAvail = screens->screenAvailableGeometry(screenId);
    const int w = qMin(cur.width(), dstAvail.width());
    const int h = qMin(cur.height(), dstAvail.height());
    int x = dstAvail.x();
    int y = dstAvail.y();
    if (srcAvail.isValid() && srcAvail.width() > 0 && srcAvail.height() > 0) {
        const double relX = static_cast<double>(cur.x() - srcAvail.x()) / srcAvail.width();
        const double relY = static_cast<double>(cur.y() - srcAvail.y()) / srcAvail.height();
        x = dstAvail.x() + qRound(relX * dstAvail.width());
        y = dstAvail.y() + qRound(relY * dstAvail.height());
    }
    // Clamp the frame fully inside the target available area.
    x = qBound(dstAvail.left(), x, dstAvail.right() - w + 1);
    y = qBound(dstAvail.top(), y, dstAvail.bottom() - h + 1);

    qCInfo(lcDbusWindow) << "applyOpenScreenRouting: routing" << windowId << "to monitor" << target << "at"
                         << QRect(x, y, w, h);
    // Emit the marker first so the effect treats the resulting outputChanged as an
    // expected daemon-driven move (bookkeeping + decoration only, no reopen), then
    // the free placement (empty zone id ⇒ no snap chrome).
    Q_EMIT windowOutputMoveExpected(windowId, target);
    Q_EMIT applyGeometryRequested(windowId, x, y, w, h, QString(), target, false);
}

QString WindowTrackingAdaptor::applyOpenRoutingForAutotile(const QString& windowId, const QString& screenId)
{
    if (!m_ruleStore) {
        return QString();
    }
    std::optional<PhosphorRules::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return QString();
    }
    query->screenId = screenId;
    if (!m_ruleEvaluator) {
        m_ruleEvaluator = std::make_unique<PhosphorRules::RuleEvaluator>(m_ruleStore->ruleSet());
    }
    const PhosphorRules::ResolvedActions resolved = m_ruleEvaluator->resolveCached(windowId, *query);

    // RouteToDesktop is engine-neutral — emit it for autotile windows too.
    emitRouteToDesktopIfMatched(resolved, windowId);

    // RouteToScreen: redirect the window onto a different AUTOTILE monitor. The
    // snap open path handles snap-mode targets itself (the placement directive),
    // so here we only honour a target that is currently in autotile mode — a snap
    // or disabled target is left to the window's spawn screen (cross-engine
    // routing is out of scope). Returning the target tells the caller to insert
    // the window into that screen's tiling state; the output-move marker stops the
    // effect from re-processing the resulting outputChanged as a fresh open.
    const std::optional<PhosphorRules::RuleAction> route =
        resolved.slot(QString(PhosphorRules::ActionSlot::RouteScreen));
    if (!route) {
        return QString();
    }
    const QString target = route->params.value(QString(PhosphorRules::ActionParam::TargetScreenId)).toString();
    if (target.isEmpty() || target == screenId || !m_layoutManager) {
        return QString();
    }
    // When the same rule also pins a target desktop (RouteToDesktop), the window
    // lands on THAT desktop of the target screen, so gate the autotile-mode check
    // against the destination desktop — not the target's current desktop. Mirrors
    // the snap path (calculateSnapToPlacementRule), which gates modeForScreen on the
    // routed desktop. Absent / 0 ⇒ the target screen's current desktop.
    int destDesktop = currentDesktopForScreen(target);
    if (const auto desktopRoute = resolved.slot(QString(PhosphorRules::ActionSlot::RouteDesktop))) {
        const int d = desktopRoute->params.value(QString(PhosphorRules::ActionParam::TargetDesktop)).toInt(0);
        if (d >= 1) {
            destDesktop = d;
        }
    }
    if (m_layoutManager->modeForScreen(target, destDesktop, m_layoutManager->currentActivity())
        != PhosphorZones::AssignmentEntry::Mode::Autotile) {
        qCDebug(lcDbusWindow) << "applyOpenRoutingForAutotile: RouteToScreen target" << target
                              << "is not in autotile mode — not redirecting" << windowId;
        return QString();
    }
    qCInfo(lcDbusWindow) << "applyOpenRoutingForAutotile: routing" << windowId << "to autotile screen" << target;
    Q_EMIT windowOutputMoveExpected(windowId, target);
    return target;
}

void WindowTrackingAdaptor::handleCrossModeMove(const QString& windowId, const QString& targetScreenId,
                                                int targetDesktop, const QString& direction)
{
    auto* sourceEngine = qobject_cast<PhosphorEngine::PlacementEngineBase*>(sender());
    if (!sourceEngine || windowId.isEmpty() || targetScreenId.isEmpty() || !m_layoutManager) {
        return;
    }

    // Target mode at the DESTINATION context: a cross-desktop crossing targets a
    // (possibly different) mode on the same screen's target desktop; a monitor
    // crossing targets the neighbour screen on the current desktop (targetDesktop
    // == 0).
    const int effectiveDesktop = targetDesktop > 0 ? targetDesktop : currentDesktopForScreen(targetScreenId);
    const QString activity = m_layoutManager->currentActivity();
    const bool targetIsAutotile = m_layoutManager->modeForScreen(targetScreenId, effectiveDesktop, activity)
        == PhosphorZones::AssignmentEntry::Autotile;
    PhosphorEngine::PlacementEngineBase* targetEngine =
        targetIsAutotile ? m_autotileEngine.data() : m_snapEngine.data();
    if (!targetEngine || targetEngine == sourceEngine) {
        return; // target engine unavailable, or not actually cross-mode
    }

    // Where the window currently lives — for the source reflow.
    const QString sourceScreen = sourceEngine->screenForTrackedWindow(windowId);

    // For a SNAP target, resolve the landing zone BEFORE relinquishing the source
    // (snap→snap cross-desktop maps the source's slot; everything else enters the
    // neighbour's edge zone).
    QStringList landingZoneIds;
    if (!targetIsAutotile) {
        auto* snapTarget = qobject_cast<PhosphorSnapEngine::SnapEngine*>(targetEngine);
        QString zoneId;
        if (snapTarget) {
            if (targetDesktop > 0) {
                if (auto* snapSource = qobject_cast<PhosphorSnapEngine::SnapEngine*>(sourceEngine)) {
                    const QString srcZone = snapSource->zoneForWindow(windowId);
                    if (!srcZone.isEmpty()) {
                        zoneId = snapTarget->resolveCrossDesktopZone(srcZone, targetScreenId, targetDesktop).first;
                    }
                }
            }
            if (zoneId.isEmpty()) {
                zoneId = snapTarget->entryZoneForCrossing(direction, targetScreenId);
            }
        }
        if (!zoneId.isEmpty()) {
            landingZoneIds = QStringList{zoneId};
        }
    }

    // Relinquish from the source (tracking-only). An autotile source must reflow
    // — the remaining tiles expand into the vacated slot; handoffRelease does not
    // retile. A snap source just vacates a zone (no reflow).
    sourceEngine->handoffRelease(windowId);
    if (!sourceScreen.isEmpty() && sourceEngine == m_autotileEngine.data()) {
        sourceEngine->retile(sourceScreen);
    }

    // A MONITOR crossing physically relocates the window to a different output.
    // Tell the compositor the imminent output change is daemon-owned BEFORE the
    // placement geometry triggers it — otherwise the effect's reactive
    // outputChanged handler re-issues windowClosed/windowOpened and tears down
    // the placement we're about to make (the exact tear-down NavigationController's
    // in-mode cross-output move guards against). A cross-DESKTOP crossing keeps the
    // window on the same screen (targetScreenId == sourceScreen), so no marker —
    // arming a one-shot for an output change that never comes would swallow the
    // next genuine outputChanged for this window.
    if (!sourceScreen.isEmpty() && targetScreenId != sourceScreen) {
        Q_EMIT windowOutputMoveExpected(windowId, targetScreenId);
    }

    // Place on the target. A cross-DESKTOP move onto an AUTOTILE desktop uses the
    // existing reactive path: the window changes desktops below and the autotile
    // effect catch-scan tiles it (honouring insertion-order) when that desktop
    // becomes current — handoffReceive would mis-place it on the *current*
    // desktop's state. Every other case places immediately:
    //   - monitor crossing (current desktop): handoffReceive tiles / snaps it now;
    //   - cross-desktop onto a SNAP desktop: snap handoffReceive honours toDesktop
    //     (assigns the zone on the target desktop + off-desktop geometry).
    const bool reactiveAutotileDesktopArrival = targetIsAutotile && targetDesktop > 0;
    if (!reactiveAutotileDesktopArrival) {
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = targetScreenId;
        ctx.toDesktop = targetDesktop;
        ctx.fromEngineId = sourceEngine->engineId();
        ctx.sourceZoneIds = landingZoneIds;
        ctx.wasFloating = false; // an explicit move always places, never floats
        targetEngine->handoffReceive(ctx);
    }

    // Physical relocation for a cross-desktop crossing: ask the compositor to
    // move the real window to the target desktop. A monitor crossing needs none —
    // the target engine's placement geometry already relocated the window.
    if (targetDesktop > 0) {
        Q_EMIT windowDesktopMoveRequested(windowId, targetDesktop);
    }
}

void WindowTrackingAdaptor::handleCrossModeSwap(const QString& windowId, const QString& targetScreenId,
                                                int targetDesktop, const QString& direction)
{
    auto* sourceEngine = qobject_cast<PhosphorEngine::PlacementEngineBase*>(sender());
    if (!sourceEngine || windowId.isEmpty() || targetScreenId.isEmpty() || !m_layoutManager) {
        return;
    }

    // Swap is never extended across virtual desktops (exchanging with a window on
    // a desktop you can't see is meaningless — move owns cross-desktop relocation).
    // No engine emits a cross-desktop crossModeSwapRequested; this guard is
    // defensive so a stray desktop-targeted swap is a clean no-op, never a move.
    if (targetDesktop > 0) {
        return;
    }

    const QString activity = m_layoutManager->currentActivity();
    const bool targetIsAutotile =
        m_layoutManager->modeForScreen(targetScreenId, currentDesktopForScreen(targetScreenId), activity)
        == PhosphorZones::AssignmentEntry::Autotile;
    PhosphorEngine::PlacementEngineBase* targetEngine =
        targetIsAutotile ? m_autotileEngine.data() : m_snapEngine.data();
    if (!targetEngine || targetEngine == sourceEngine) {
        return; // target engine unavailable, or not actually cross-mode
    }
    const QString sourceScreen = sourceEngine->screenForTrackedWindow(windowId);
    if (sourceScreen.isEmpty()) {
        return;
    }

    // ── Resolve the swap partner: the target surface's entry-edge window facing
    //    the source, and the slot the focused window will land in (the partner's
    //    slot). ──
    QString partner;
    QStringList focusedLandingZones; // F's landing on a SNAP target (the entry zone)
    int focusedLandingIndex = -1; // F's landing on an AUTOTILE target (partner's index)
    if (targetIsAutotile) {
        if (auto* autotileTarget = qobject_cast<PhosphorTileEngine::AutotileEngine*>(targetEngine)) {
            partner = autotileTarget->entryWindowForCrossing(targetScreenId, direction);
            if (!partner.isEmpty()) {
                focusedLandingIndex = autotileTarget->windowOrderIndexForWindow(targetScreenId, partner);
            }
        }
    } else if (auto* snapTarget = qobject_cast<PhosphorSnapEngine::SnapEngine*>(targetEngine)) {
        const QString entryZone = snapTarget->entryZoneForCrossing(direction, targetScreenId);
        if (!entryZone.isEmpty()) {
            focusedLandingZones = QStringList{entryZone};
            partner = snapTarget->windowInZoneOnScreen(entryZone, targetScreenId);
        }
    }

    // No partner (empty entry slot) → a plain one-way cross-mode move.
    if (partner.isEmpty()) {
        handleCrossModeMove(windowId, targetScreenId, targetDesktop, direction);
        return;
    }

    // ── Capture the partner's landing on the SOURCE: the focused window's vacated
    //    slot. Captured BEFORE any relinquish so the indices/zones are still live. ──
    QStringList partnerLandingZones; // partner's landing on a SNAP source (F's zone)
    int partnerLandingIndex = -1; // partner's landing on an AUTOTILE source (F's index)
    if (sourceEngine == m_autotileEngine.data()) {
        if (auto* autotileSource = qobject_cast<PhosphorTileEngine::AutotileEngine*>(sourceEngine)) {
            partnerLandingIndex = autotileSource->windowOrderIndexForWindow(sourceScreen, windowId);
        }
    } else if (auto* snapSource = qobject_cast<PhosphorSnapEngine::SnapEngine*>(sourceEngine)) {
        const QString fZone = snapSource->zoneForWindow(windowId);
        if (!fZone.isEmpty()) {
            partnerLandingZones = QStringList{fZone};
        }
    }

    // ── A monitor crossing physically relocates BOTH windows to a different
    //    output (F → target, partner → source). Arm the daemon-owned-move marker
    //    for each — but only when that window actually changes output, mirroring
    //    handleCrossModeMove's guard: arming a one-shot for an output change that
    //    never comes would swallow the window's next genuine outputChanged. ──
    if (targetScreenId != sourceScreen) {
        Q_EMIT windowOutputMoveExpected(windowId, targetScreenId);
        Q_EMIT windowOutputMoveExpected(partner, sourceScreen);
    }

    // ── Relinquish both windows from their current engines (tracking-only). ──
    sourceEngine->handoffRelease(windowId);
    targetEngine->handoffRelease(partner);

    // ── Place the focused window on the target, in the partner's slot. ──
    {
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = targetScreenId;
        ctx.fromEngineId = sourceEngine->engineId();
        ctx.sourceZoneIds = focusedLandingZones;
        ctx.insertIndex = focusedLandingIndex;
        ctx.wasFloating = false;
        targetEngine->handoffReceive(ctx);
    }
    // ── Place the partner on the source, in the focused window's vacated slot. ──
    {
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = partner;
        ctx.toScreenId = sourceScreen;
        ctx.fromEngineId = targetEngine->engineId();
        ctx.sourceZoneIds = partnerLandingZones;
        ctx.insertIndex = partnerLandingIndex;
        ctx.wasFloating = false;
        sourceEngine->handoffReceive(ctx);
    }
}

} // namespace PlasmaZones
