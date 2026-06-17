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
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorWindowRule/RuleAction.h>
#include <PhosphorWindowRule/RuleEvaluator.h>
#include <PhosphorWindowRule/WindowQuery.h>
#include <PhosphorWindowRule/WindowRuleStore.h>

namespace PlasmaZones {

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
    // Drop the common float-restore geometry relay from BOTH outgoing engines
    // before reassigning, so a re-wire (mode toggle / daemon teardown) can't
    // accumulate duplicate connections.
    if (m_snapEngine) {
        disconnect(m_snapEngine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested, this, nullptr);
    }
    if (m_autotileEngine) {
        disconnect(m_autotileEngine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested, this, nullptr);
    }

    // Detach the snap restore predicate from the outgoing engine before dropping
    // the reference. The predicate captures `this`; clearing it honours the engine
    // header's detach contract ("clear via setShouldRestorePredicate({}) before
    // destroying the captured object") instead of relying on daemon destruction
    // order.
    if (m_cachedSnapEngine) {
        m_cachedSnapEngine->setShouldRestorePredicate({});
        m_cachedSnapEngine->setRestorePositionPredicate({});
        m_cachedSnapEngine->setFloatPredicate({});
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
    // When clearing (nullptr, nullptr), we also clear stale cross-references
    // to prevent dangling pointer access.
    // ═══════════════════════════════════════════════════════════════════════════
    if (auto* snap = qobject_cast<PhosphorSnapEngine::SnapEngine*>(snapEngine)) {
        snap->setZoneAdjacencyResolver(m_zoneDetectionAdaptor);
        if (auto* autotile = qobject_cast<PhosphorTileEngine::AutotileEngine*>(autotileEngine)) {
            snap->setAutotileEngine(autotile);
        }

        // Disabled-context gate for snap auto-restore (discussion #461 item 7).
        // The persist-on-close gate (setShouldTrackPredicate above) blocks NEW
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
            return !isPersistedContextDisabled(screenId, currentDesktop());
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

        // Open-floating gate (snap). A matched "Float this app" rule opens the
        // window floating instead of auto-snapping it. Purely rule-driven (no
        // global default), so the same resolver serves both engines.
        snap->setFloatPredicate([this](const QString& windowId) -> bool {
            return shouldFloatByRule(windowId);
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

void WindowTrackingAdaptor::setWindowRuleStore(PhosphorWindowRule::WindowRuleStore* store)
{
    if (m_windowRuleStore == store) {
        return;
    }
    m_windowRuleStore = store;
    // Drop the evaluator bound to the previous set; it rebuilds lazily against
    // the new one on the next per-window resolve.
    m_windowRuleEvaluator.reset();
}

namespace {
// Build a per-window rule query from the registry metadata, or nullopt when no
// metadata is tracked (the caller falls back to its own default). Shared by the
// RestorePosition and Float resolvers so the metadata→query derivation lives in
// one place. windowClass is not tracked daemon-side (the compositor reports
// appId, which is class-derived), so rules match on appId / title / role / type
// / desktop / pid plus the recorded desktop / activity context; screenId stays
// empty (a window-domain rule does not pin a screen).
std::optional<PhosphorWindowRule::WindowQuery>
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
    PhosphorWindowRule::WindowQuery query;
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
    if (!m_windowRuleStore) {
        return globalDefault;
    }
    const std::optional<PhosphorWindowRule::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return globalDefault;
    }

    if (!m_windowRuleEvaluator) {
        m_windowRuleEvaluator = std::make_unique<PhosphorWindowRule::RuleEvaluator>(m_windowRuleStore->ruleSet());
    }
    const PhosphorWindowRule::ResolvedActions resolved = m_windowRuleEvaluator->resolveCached(windowId, *query);
    if (const std::optional<PhosphorWindowRule::RuleAction> action =
            resolved.slot(QString(PhosphorWindowRule::ActionSlot::RestorePosition))) {
        // A matched RestorePosition rule overrides the global setting.
        return action->params.value(QString(PhosphorWindowRule::ActionParam::Value)).toBool();
    }
    return globalDefault;
}

bool WindowTrackingAdaptor::shouldFloatByRule(const QString& windowId)
{
    // Float is purely rule-driven: there is no global "float on open" setting, so
    // absent a matching rule the answer is "do not float".
    if (!m_windowRuleStore) {
        return false;
    }
    const std::optional<PhosphorWindowRule::WindowQuery> query = buildRuleQueryForWindow(m_windowRegistry, windowId);
    if (!query) {
        return false;
    }

    if (!m_windowRuleEvaluator) {
        m_windowRuleEvaluator = std::make_unique<PhosphorWindowRule::RuleEvaluator>(m_windowRuleStore->ruleSet());
    }
    const PhosphorWindowRule::ResolvedActions resolved = m_windowRuleEvaluator->resolveCached(windowId, *query);
    // The Float action carries free-form params (no Value key), so the verdict is
    // the PRESENCE of the filled slot, not a bool payload.
    return resolved.slot(QString(PhosphorWindowRule::ActionSlot::Float)).has_value();
}

} // namespace PlasmaZones
