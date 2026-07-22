// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// WindowTrackingAdaptor — engine cross-wiring
//
// Holds setEngines(), which wires cross-engine references, the disabled-context
// restore predicates, and the shared OSD navigation path.
// ═══════════════════════════════════════════════════════════════════════════════

#include "windowtrackingadaptor.h"

#include "dbus/zonedetectionadaptor.h"
#include "core/interfaces/isettings.h"
#include "core/platform/logging.h"
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
#include "internal.h"

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
                    // Through the relay chokepoint so m_broadcastFloating
                    // tracks this emission too (see relayWindowFloatingChanged).
                    relayWindowFloatingChanged(windowId, false, screenId);
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

} // namespace PlasmaZones
