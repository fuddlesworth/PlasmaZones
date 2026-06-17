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
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>
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
    }
    if (m_cachedAutotileEngine) {
        m_cachedAutotileEngine->setRestorePositionPredicate({});
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

void WindowTrackingAdaptor::setWindowRuleStore(PhosphorWindowRule::WindowRuleStore* store)
{
    if (m_windowRuleStore == store) {
        return;
    }
    m_windowRuleStore = store;
    // Drop the evaluator bound to the previous set; it rebuilds lazily against
    // the new one on the next shouldRestoreFloatedPosition call.
    m_restorePositionEvaluator.reset();
}

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
    if (!m_windowRuleStore || m_windowRegistry.isNull()) {
        return globalDefault;
    }
    // WindowRegistry is keyed by the BARE instance id; the engine hands us the
    // composite `appId|instanceId`. Extract first — every other registry reader
    // (currentAppIdFor, windowClosed, AutotileEngine) does the same. Looking up by
    // the composite id always misses, which would silently make RestorePosition
    // rules inert and collapse the feature to the global setting.
    const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(windowId);
    const std::optional<PhosphorEngine::WindowMetadata> meta = m_windowRegistry->metadata(instanceId);
    if (!meta) {
        return globalDefault;
    }

    // Build a per-window query from the registry metadata. windowClass is not
    // tracked daemon-side (the compositor reports appId, which is class-derived),
    // so RestorePosition rules match on appId / title / role / type — the common
    // per-app case. Context fields come from the window's recorded desktop /
    // activity; screenId stays empty (a window-domain rule does not pin a screen).
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

    if (!m_restorePositionEvaluator) {
        m_restorePositionEvaluator = std::make_unique<PhosphorWindowRule::RuleEvaluator>(m_windowRuleStore->ruleSet());
    }
    const PhosphorWindowRule::ResolvedActions resolved = m_restorePositionEvaluator->resolveCached(windowId, query);
    if (const std::optional<PhosphorWindowRule::RuleAction> action =
            resolved.slot(QString(PhosphorWindowRule::ActionSlot::RestorePosition))) {
        // A matched RestorePosition rule overrides the global setting.
        return action->params.value(QString(PhosphorWindowRule::ActionParam::Value)).toBool();
    }
    return globalDefault;
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
    const int effectiveDesktop = targetDesktop > 0 ? targetDesktop : currentDesktop();
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
                    const QString srcZone =
                        snapSource->snapState() ? snapSource->snapState()->zoneForWindow(windowId) : QString();
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
    const bool targetIsAutotile = m_layoutManager->modeForScreen(targetScreenId, currentDesktop(), activity)
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
        const QString fZone = snapSource->snapState() ? snapSource->snapState()->zoneForWindow(windowId) : QString();
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
