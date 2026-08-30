// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// WindowTrackingAdaptor — cross-mode directional handoff
//
// handleCrossModeMove / handleCrossModeSwap: relinquish a window from the source
// engine and place (or exchange) it on the target engine when a directional
// navigation crosses into a different tiling mode.
// handleCrossModeFocus: activate the target engine's entry-edge window — no
// window travels and no engine state changes.
// ═══════════════════════════════════════════════════════════════════════════════

#include "windowtrackingadaptor.h"
#include "internal.h"

#include "dbus/zonedetectionadaptor.h"
#include "core/interfaces/isettings.h"
#include "core/platform/logging.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
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

PhosphorEngine::PlacementEngineBase*
WindowTrackingAdaptor::engineForMode(PhosphorZones::AssignmentEntry::Mode mode) const
{
    switch (mode) {
    case PhosphorZones::AssignmentEntry::Autotile:
        return m_autotileEngine.data();
    case PhosphorZones::AssignmentEntry::Scrolling:
        return m_scrollEngine.data();
    case PhosphorZones::AssignmentEntry::Snapping:
        return m_snapEngine.data();
    }
    return nullptr;
}

void WindowTrackingAdaptor::handleCrossModeMove(const QString& windowId, const QString& targetScreenId,
                                                int targetDesktop, const QString& direction)
{
    // sender() is only valid inside this signal dispatch; the impl below
    // never touches it, so every other caller passes its engine explicitly.
    crossModeMoveImpl(qobject_cast<PhosphorEngine::PlacementEngineBase*>(sender()), windowId, targetScreenId,
                      targetDesktop, direction);
}

void WindowTrackingAdaptor::moveWindowToWorkspaceVerb(const QString& rawWindowId, const QString& targetScreenId,
                                                      int targetDesktop, const QString& targetDesktopId,
                                                      const QString& direction, bool moveOutput)
{
    if (rawWindowId.isEmpty() || targetDesktop < 1) {
        return;
    }
    // Canonicalize at the entry. Two workspace-verb emitters hand this slot a
    // BARE compositor instance id rather than a composite: the owner-wins
    // reunion arm and the removal-race re-route (WorkspaceController's census
    // walks the registry by instanceId). Every consumer downstream — the
    // engines' tracking maps and, over the wire, the effect's findWindowById —
    // is keyed on the composite id, so a bare id finds no engine here and
    // resolves no window there, and the move is silently dropped.
    // shadowWindowId() maps instanceId → the registry's canonical composite and
    // is the identity for an id that is already composite.
    const QString windowId = shadowWindowId(rawWindowId);
    // Sticky (on-all-desktops) windows: the effect REFUSES the desktop move
    // outright (slotWindowDesktopMoveRequested drops isOnAllDesktops windows),
    // so running the handoff would re-key the daemon's engine state onto a
    // desktop the real window never moves to. Refuse at the entry instead, so
    // no state diverges. The window is already present on every workspace,
    // which is what the verb was asking for.
    if (m_service && m_service->isWindowSticky(windowId)) {
        qCDebug(lcDbusWindow) << "workspace move: refusing sticky window" << windowId;
        return;
    }
    PhosphorEngine::PlacementEngineBase* sourceEngine = nullptr;
    for (PhosphorEngine::PlacementEngineBase* engine :
         {m_scrollEngine.data(), m_autotileEngine.data(), m_snapEngine.data()}) {
        if (engine && engine->isWindowTracked(windowId)) {
            sourceEngine = engine;
            break;
        }
    }
    if (!sourceEngine) {
        // Untracked (floating / excluded) windows still change desktops; no
        // engine state exists to hand over. The OUTPUT leg matters too: a
        // move-workspace-to-monitor rider keeps its desktop int, so the
        // desktop move alone would be a no-op and the floating rider would
        // never leave the source output. The effect no-ops the output move
        // when the window is already there.
        emitDesktopMove(windowId, targetDesktop, targetDesktopId);
        if (moveOutput && !targetScreenId.isEmpty()) {
            Q_EMIT windowOutputMoveRequested(windowId, targetScreenId);
        }
        return;
    }
    // An empty target screen is reachable on the wire: the reconciler's
    // ownerOf() answers empty for a workspace whose ownership has not settled,
    // and the verbs emit that value through. crossModeMoveImpl requires a
    // target screen (it resolves the destination MODE from it) and would
    // otherwise return silently, leaving the window on its old workspace with
    // no trace. Degrade to the window's OWN screen — an unowned workspace has
    // no other monitor to name, and same-output cross-desktop IS the ordinary
    // shape of this verb — rather than the untracked branch's bare desktop
    // move, which would part a tracked window from its engine state.
    // Desktop-only route (RouteToWorkspace): the window stays on the monitor
    // it is on, so the handoff's destination context is that monitor, not the
    // workspace's owner screen. Resolving it here rather than passing the
    // owner through keeps crossModeMoveImpl's "re-home on the target screen"
    // contract intact instead of quietly re-keying engine state onto an output
    // the window never reaches.
    QString resolvedScreenId = moveOutput ? targetScreenId : QString();
    if (resolvedScreenId.isEmpty()) {
        resolvedScreenId = sourceEngine->screenForTrackedWindow(windowId);
        if (moveOutput) {
            qCWarning(lcDbusWindow) << "workspace move: no owner screen for" << windowId << "- falling back to its own"
                                    << resolvedScreenId;
        }
        if (resolvedScreenId.isEmpty()) {
            return;
        }
    }
    crossModeMoveImpl(sourceEngine, windowId, resolvedScreenId, targetDesktop, direction, /*allowSameEngine=*/true,
                      targetDesktopId);
}

void WindowTrackingAdaptor::emitDesktopMove(const QString& windowId, int targetDesktop, const QString& targetDesktopId)
{
    // Prefer the id. The number is a POSITION, resolved daemon-side and then
    // sent, and the workspace feature renumbers concurrently with its own
    // moves: the move fills the destination, a filled workspace makes the
    // reconciler cut a new trailing empty, and that shift can land before the
    // effect applies the number. Observed live — a route to the workspace at
    // position 2 arrived on the desktop that had just taken position 2.
    // Callers with no id (the directional verbs) genuinely mean the position.
    if (!targetDesktopId.isEmpty()) {
        Q_EMIT windowDesktopMoveByIdRequested(windowId, targetDesktopId);
        return;
    }
    Q_EMIT windowDesktopMoveRequested(windowId, targetDesktop);
}

void WindowTrackingAdaptor::crossModeMoveImpl(PhosphorEngine::PlacementEngineBase* sourceEngine,
                                              const QString& windowId, const QString& targetScreenId, int targetDesktop,
                                              const QString& direction, bool allowSameEngine,
                                              const QString& targetDesktopId)
{
    if (!sourceEngine || windowId.isEmpty() || targetScreenId.isEmpty() || !m_layoutManager) {
        return;
    }

    // Target mode at the DESTINATION context: a cross-desktop crossing targets a
    // (possibly different) mode on the same screen's target desktop; a monitor
    // crossing targets the neighbour screen on the current desktop (targetDesktop
    // == 0).
    const int effectiveDesktop = targetDesktop > 0 ? targetDesktop : currentDesktopForScreen(targetScreenId);
    const QString activity = m_layoutManager->currentActivity();
    const PhosphorZones::AssignmentEntry::Mode targetMode =
        m_layoutManager->modeForScreen(targetScreenId, effectiveDesktop, activity);
    const bool targetIsAutotile = targetMode == PhosphorZones::AssignmentEntry::Autotile;
    PhosphorEngine::PlacementEngineBase* targetEngine = engineForMode(targetMode);
    if (!targetEngine || (targetEngine == sourceEngine && !allowSameEngine)) {
        // Target engine unavailable, or not actually cross-mode. The
        // workspace move verb passes allowSameEngine: a same-engine handoff
        // (release + receive with toDesktop) IS the same-mode cross-desktop
        // re-key, and the reactive autotile branch below is same-engine-safe.
        return;
    }
    // Deliberately NO isActiveOnScreen pre-gate here (or in the swap twin),
    // unlike handleCrossModeFocus: a focus toward a disabled-context screen
    // would walk state the engine never adopted, but the MOVE and SWAP paths
    // hand the window over through refusal-recovering receives
    // (guardedHandoff below, receiveVerified in the swap) that re-home it
    // into the source on a refusal — a logged bounce, not a stranding — and
    // the gate would turn that recoverable bounce into a silent no-op.

    // Where the window currently lives — for the source reflow.
    const QString sourceScreen = sourceEngine->screenForTrackedWindow(windowId);

    // For a SNAP target, resolve the landing zone BEFORE relinquishing the
    // source (snap→snap cross-desktop maps the source's slot; everything else
    // enters the neighbour's edge zone). The branch also runs for a SCROLLING
    // target — the SnapEngine cast fails there and the zone list stays empty,
    // which is what a strip target needs (its landing is insertIndex, below).
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

    // The source is relinquished inside guardedHandoff below (or explicitly
    // on the reactive-arrival branch). An autotile source must reflow — the
    // remaining tiles expand into the vacated slot; handoffRelease does not
    // retile. A snap source just vacates a zone (no reflow). A scroll
    // source's handoffRelease schedules its own coalesced retile, so the
    // strip closes up without an explicit call. The min size is queried
    // BEFORE any release (the release drops the source's tracking, and the
    // receiver seeds from ctx.minSize).
    const QSize windowMinSize = sourceEngine->windowMinimumSize(windowId);

    // Place on the target. A cross-DESKTOP move onto an AUTOTILE desktop uses the
    // existing reactive path: the window changes desktops below and the autotile
    // effect catch-scan tiles it (honouring insertion-order) when that desktop
    // becomes current — handoffReceive would mis-place it on the *current*
    // desktop's state. Every other case places immediately:
    //   - monitor crossing (current desktop): handoffReceive tiles / snaps it now;
    //   - cross-desktop onto a SNAP desktop: snap handoffReceive honours toDesktop
    //     (assigns the zone on the target desktop + off-desktop geometry);
    //   - cross-desktop onto a SCROLLING desktop: scroll handoffReceive honours
    //     toDesktop too (places into that desktop's strip) — the reactive
    //     deferral is autotile-only.
    const bool reactiveAutotileDesktopArrival = targetIsAutotile && targetDesktop > 0;
    bool placedOnTarget = false;
    // Set only on the reactive branch (see its arm below): the compositor-side
    // output hop that branch has no other carrier for.
    bool needsOutputMove = false;
    if (!reactiveAutotileDesktopArrival) {
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = targetScreenId;
        ctx.toDesktop = targetDesktop;
        ctx.fromEngineId = sourceEngine->engineId();
        ctx.sourceZoneIds = landingZoneIds;
        ctx.minSize = windowMinSize;
        ctx.wasFloating = false; // an explicit move always places, never floats
        // Edge-aware entry for a SCROLLING target: moving right crosses into
        // the strip's LEFT edge, so the arrival becomes the FIRST column
        // (index 0); moving left enters from the right and appends
        // (insertIndex -1 → columnCount() in handoffReceive). This mirrors
        // the snap target's entryZoneForCrossing and the scroll engine's own
        // in-strip boundary semantics — without it every crossing appended
        // at the far end, the opposite edge from the one the window entered.
        // A vertical crossing ("up"/"down" between stacked monitors) has no
        // strip edge to enter from; insertIndex stays -1 and the window
        // appends at the right end, same as "left".
        if (targetMode == PhosphorZones::AssignmentEntry::Scrolling && direction == QLatin1String("right")) {
            ctx.insertIndex = 0;
        }
        // An autotile target's handoffReceive also announces the arrival's
        // tiled (non-floating) state on the passive float-sync channel —
        // intended: the arrival IS tiled, and the relay's last-broadcast
        // gate dedups when the bit already agrees.
        //
        // Guarded: handoffReceive can silently refuse, and the source has
        // already released inside the helper — on refusal the helper
        // re-homes into the source so the window is not stranded
        // tracked-by-no-engine. recoverDesktop stays 0 (current): the
        // desktop-move request below is adoption-gated, so on a refusal
        // the real window never left the source desktop.
        placedOnTarget = WindowTrackingInternal::guardedHandoff(sourceEngine, targetEngine, ctx, sourceScreen);
        // A MONITOR crossing physically relocates the window to a different
        // output. Mark the imminent output change as daemon-owned so the
        // effect's reactive outputChanged handler does not re-issue
        // windowClosed/windowOpened and tear down the placement just made.
        // Post-receive and adoption-gated (same contract as the swap path):
        // a refused receive must not arm a one-shot for a move that never
        // happens — it would swallow the next genuine outputChanged. The
        // effect applies geometry through deferred commits, so the marker
        // signal lands before the output change is processed.
        //
        // The source screen travels on the wire because this arm site runs
        // AFTER the handoff: handoffReceive already pushed the destination's
        // tiles, so the effect's own notified-screen record names the
        // destination by now and cannot answer "where did it come from".
        // screensMatch, not a raw compare: connector-name / EDID-id spelling
        // and the "/vs:" suffix make raw inequality unreliable, and a spurious
        // one arms a one-shot for a move that never happens — which then
        // swallows the window's next genuine outputChanged.
        if (placedOnTarget && !sourceScreen.isEmpty()
            && !PhosphorScreens::ScreenIdentity::screensMatch(targetScreenId, sourceScreen)) {
            Q_EMIT windowOutputMoveExpected(windowId, targetScreenId, sourceScreen);
        }
    } else {
        // Reactive autotile desktop arrival: no receive here (the effect
        // catch-scan tiles it on the target desktop), so just release. The
        // desktop move below must still fire — the catch-scan only runs
        // once the window actually lands on the target desktop.
        sourceEngine->handoffRelease(windowId);
        placedOnTarget = true;
        // The OUTPUT leg has no other carrier on this branch. The immediate
        // arm above relocates the window through the target engine's placement
        // geometry, but here nothing is placed — the effect's desktop-return
        // catch-scan adopts the window, and that scan keys each window to the
        // output it is PHYSICALLY on (getWindowScreenId(w) == screenId, in
        // screenschanged.cpp). Left on the source output the window is adopted
        // into the wrong screen's tiling state, or into none at all when that
        // output is not autotiled. Ask the compositor to move it; the effect
        // no-ops a window already on the target output.
        //
        // Deliberately NO windowOutputMoveExpected marker: that one-shot exists
        // to stop the effect re-issuing windowClosed/windowOpened for a move the
        // daemon already carried in its own state. Here the source engine has
        // released and no engine holds the window, so the close/open re-issue is
        // exactly the adoption path this branch relies on.
        //
        // Emitted AFTER the desktop move at the tail, mirroring the untracked
        // branch's desktop-then-output order: the window leaves the visible
        // desktop first, so the output hop is not drawn on the desktop the user
        // is looking at.
        needsOutputMove =
            !sourceScreen.isEmpty() && !PhosphorScreens::ScreenIdentity::screensMatch(targetScreenId, sourceScreen);
    }
    if (!sourceScreen.isEmpty() && sourceEngine == m_autotileEngine.data()) {
        sourceEngine->retile(sourceScreen);
    }

    // Physical relocation for a cross-desktop crossing: ask the compositor to
    // move the real window to the target desktop. A monitor crossing needs
    // none — the target engine's placement geometry already relocated the
    // window. Adoption-gated: after a refused handoff the state was
    // re-homed onto the SOURCE context, and physically relocating the real
    // window anyway would part it from its state.
    if (placedOnTarget && targetDesktop > 0) {
        emitDesktopMove(windowId, targetDesktop, targetDesktopId);
        if (needsOutputMove) {
            Q_EMIT windowOutputMoveRequested(windowId, targetScreenId);
        }
    }
}

void WindowTrackingAdaptor::handleCrossModeFocus(const QString& targetScreenId, const QString& direction, bool* handled)
{
    auto* sourceEngine = qobject_cast<PhosphorEngine::PlacementEngineBase*>(sender());
    if (!sourceEngine || targetScreenId.isEmpty() || !m_layoutManager) {
        return;
    }
    // Monitor crossings only, so the target context is the neighbour screen's
    // CURRENT desktop — the same resolution handleCrossModeMove uses with
    // targetDesktop == 0.
    const QString activity = m_layoutManager->currentActivity();
    const PhosphorZones::AssignmentEntry::Mode targetMode =
        m_layoutManager->modeForScreen(targetScreenId, currentDesktopForScreen(targetScreenId), activity);
    PhosphorEngine::PlacementEngineBase* targetEngine = engineForMode(targetMode);
    if (!targetEngine || targetEngine == sourceEngine) {
        // Either the engine is absent, or the raw mode cascade answered the
        // SOURCE's own mode — which happens for a context-DISABLED neighbour,
        // since modeForScreen consults no resolver while the emitting engine
        // gates on its live (resolver-filtered) screen set. `handled` stays
        // false, so the emitter reports no_target either way.
        return;
    }
    // The resolved engine must actually be ACTIVE on the target screen: the
    // mode cascade alone also names disabled-context screens the engine never
    // adopted, and asking such an engine for an entry window would walk state
    // it does not have.
    if (!targetEngine->isActiveOnScreen(targetScreenId)) {
        return;
    }
    // The entry-edge window facing the source, per the target's own
    // vocabulary — the same per-engine resolution handleCrossModeSwap uses
    // for its partner.
    QString target;
    if (auto* autotileTarget = qobject_cast<PhosphorTileEngine::AutotileEngine*>(targetEngine)) {
        target = autotileTarget->entryWindowForCrossing(targetScreenId, direction);
    } else if (auto* scrollTarget = qobject_cast<PhosphorScrollEngine::ScrollEngine*>(targetEngine)) {
        target = scrollTarget->entryWindowForCrossing(targetScreenId, direction);
    } else if (auto* snapTarget = qobject_cast<PhosphorSnapEngine::SnapEngine*>(targetEngine)) {
        const QString entryZone = snapTarget->entryZoneForCrossing(direction, targetScreenId);
        if (!entryZone.isEmpty()) {
            target = snapTarget->windowInZoneOnScreen(entryZone, targetScreenId);
        }
    }
    if (target.isEmpty()) {
        return; // empty entry edge — handled stays false, emitter reports no_target
    }
    Q_EMIT activateWindowRequested(target);
    if (handled) {
        *handled = true;
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
    const PhosphorZones::AssignmentEntry::Mode targetMode =
        m_layoutManager->modeForScreen(targetScreenId, currentDesktopForScreen(targetScreenId), activity);
    const bool targetIsAutotile = targetMode == PhosphorZones::AssignmentEntry::Autotile;
    PhosphorEngine::PlacementEngineBase* targetEngine = engineForMode(targetMode);
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
    } else if (auto* scrollTarget = qobject_cast<PhosphorScrollEngine::ScrollEngine*>(targetEngine)) {
        partner = scrollTarget->entryWindowForCrossing(targetScreenId, direction);
        if (!partner.isEmpty()) {
            // Scroll landing slots are COLUMN indices; the scroll engine's
            // handoffReceive consumes insertIndex in that unit.
            focusedLandingIndex = scrollTarget->columnIndexForWindow(targetScreenId, partner);
        }
    } else if (auto* snapTarget = qobject_cast<PhosphorSnapEngine::SnapEngine*>(targetEngine)) {
        const QString entryZone = snapTarget->entryZoneForCrossing(direction, targetScreenId);
        if (!entryZone.isEmpty()) {
            focusedLandingZones = QStringList{entryZone};
            partner = snapTarget->windowInZoneOnScreen(entryZone, targetScreenId);
        }
    }

    // No partner (empty entry slot) → a plain one-way cross-mode move. The
    // impl, not the slot: the slot recovers its source from sender(), which
    // only survives here because this is a nested slot invocation — passing
    // the engine explicitly keeps the degrade valid from any context.
    if (partner.isEmpty()) {
        crossModeMoveImpl(sourceEngine, windowId, targetScreenId, targetDesktop, direction);
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
    } else if (auto* scrollSource = qobject_cast<PhosphorScrollEngine::ScrollEngine*>(sourceEngine)) {
        partnerLandingIndex = scrollSource->columnIndexForWindow(sourceScreen, windowId);
    } else if (auto* snapSource = qobject_cast<PhosphorSnapEngine::SnapEngine*>(sourceEngine)) {
        const QString fZone = snapSource->zoneForWindow(windowId);
        if (!fZone.isEmpty()) {
            partnerLandingZones = QStringList{fZone};
        }
    }

    // (The one-shot output-move markers are armed at the TAIL of this
    // function, after the receives, and only for windows the destination
    // genuinely adopted — arming for a refused receive would leave a marker
    // that swallows that window's next genuine outputChanged, the exact
    // hazard handleCrossModeMove documents.)

    // ── Relinquish both windows from their current engines (tracking-only).
    //    Min sizes are queried first: release drops each source's tracking,
    //    and the receivers seed from ctx.minSize. ──
    const QSize focusedMinSize = sourceEngine->windowMinimumSize(windowId);
    const QSize partnerMinSize = targetEngine->windowMinimumSize(partner);
    sourceEngine->handoffRelease(windowId);
    targetEngine->handoffRelease(partner);

    // ── Place both windows, VERIFYING each receive. The paired-release
    //    order above is load-bearing (each arrival lands in the other's
    //    vacated slot), so this cannot route through guardedHandoff's
    //    release-then-receive shape — instead each receive is followed by
    //    the same adoption check and, on refusal, a re-home into the
    //    engine that just released the window (its own slot is free again
    //    because the OTHER window is not there yet / was also refused).
    //    An unverified refusal would strand the window tracked by no
    //    engine — the exact hazard guardedHandoff exists for. ──
    //    The re-home carries the window's OWN original slot (@p fallbackZones /
    //    @p fallbackIndex), not a cleared one. A snap fallback engine needs a
    //    zone: with sourceZoneIds empty its handoffReceive takes the
    //    !wasFloating tail, which only broadcasts not-floating and returns, so
    //    the window ends up tracked by no engine and the swap cannot recover
    //    from a refusal into snap at all. Each window's own slot is already in
    //    scope — F's original zone on the source screen is what the PARTNER was
    //    going to land in, and vice versa.
    const auto receiveVerified = [](PhosphorEngine::IPlacementEngine* dest,
                                    PhosphorEngine::IPlacementEngine* fallbackEngine, const QString& fallbackScreen,
                                    const QStringList& fallbackZones, int fallbackIndex,
                                    PhosphorEngine::IPlacementEngine::HandoffContext ctx) {
        dest->handoffReceive(ctx);
        if (dest->isWindowTracked(ctx.windowId)) {
            return true;
        }
        qCWarning(lcDbusWindow) << "cross-mode swap:" << dest->engineId() << "refused" << ctx.windowId;
        PhosphorEngine::IPlacementEngine::HandoffContext back = ctx;
        back.toScreenId = fallbackScreen;
        back.insertIndex = fallbackIndex;
        back.sourceZoneIds = fallbackZones;
        back.fromEngineId = dest->engineId();
        fallbackEngine->handoffReceive(back);
        if (fallbackEngine->isWindowTracked(ctx.windowId)) {
            qCInfo(lcDbusWindow) << "cross-mode swap: re-homed" << ctx.windowId << "into" << fallbackEngine->engineId()
                                 << "on" << fallbackScreen;
        } else {
            qCWarning(lcDbusWindow) << "cross-mode swap: re-home REFUSED too -" << ctx.windowId
                                    << "is tracked by no engine";
        }
        return false;
    };
    // Whether each receive was actually adopted. The downstream reflow and
    // move-marker gates read these instead of asking isWindowTracked again:
    // the verdict is decided here, and re-deriving it left two sources of
    // truth for one fact.
    // Only the PARTNER's verdict is usable AS THE ADOPTION ANSWER. The focused
    // window's is re-read after both receives (see focusedStillOnTarget
    // below), because the partner's re-home can evict it. focusedAdopted is
    // still needed though: paired with focusedStillOnTarget it is what
    // DISTINGUISHES an eviction (adopted, then displaced, so nothing re-homed
    // it) from a plain refusal (receiveVerified already re-homed it).
    bool focusedAdopted = false;
    bool partnerAdopted = false;
    // (Both placements: an autotile receiver's handoffReceive also announces
    // the arrival's tiled state on the passive float-sync channel —
    // intended; the relay's last-broadcast gate dedups an agreeing bit.)
    {
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = windowId;
        ctx.toScreenId = targetScreenId;
        ctx.fromEngineId = sourceEngine->engineId();
        ctx.sourceZoneIds = focusedLandingZones;
        ctx.minSize = focusedMinSize;
        ctx.insertIndex = focusedLandingIndex;
        ctx.wasFloating = false;
        // Re-home slot for F is its ORIGINAL slot on the source screen, which
        // is the one the partner was going to take.
        focusedAdopted =
            receiveVerified(targetEngine, sourceEngine, sourceScreen, partnerLandingZones, partnerLandingIndex, ctx);
    }
    {
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = partner;
        ctx.toScreenId = sourceScreen;
        ctx.fromEngineId = targetEngine->engineId();
        ctx.sourceZoneIds = partnerLandingZones;
        ctx.minSize = partnerMinSize;
        ctx.insertIndex = partnerLandingIndex;
        ctx.wasFloating = false;
        // Mirror: the partner's re-home slot is its own original slot on the
        // target screen, the one F was going to take.
        //
        // COLLISION NOTE, so this is not re-raised as data loss: when F's
        // receive was refused, receiveVerified re-homed F into the source at
        // (partnerLandingZones, partnerLandingIndex) — the same slot this
        // receive now targets. No engine EVICTS on receive (snap assigns the
        // window into the zone beside any co-occupant, autotile and scroll
        // inserts shift indices), so the worst outcome is a shared zone or a
        // shifted tile order on the source screen: degraded, visible, and
        // user-recoverable, with both windows still tracked. A pre-emptive
        // slot reroute here would need engine-specific vacancy queries for a
        // state the user can fix with one drag.
        partnerAdopted =
            receiveVerified(sourceEngine, targetEngine, targetScreenId, focusedLandingZones, focusedLandingIndex, ctx);
    }

    // Source reflow, same rule as handleCrossModeMove: an autotile source
    // does not retile on handoffRelease, so the slot F vacated stays a hole
    // unless something else retiles that screen. The partner's arrival
    // normally does it, but not on the paths that leave the source without a
    // partner — a receive refused at the destination's screen-set check
    // returns before its own reflow, and the re-home then puts the partner
    // back on the target. A snap source vacates a zone (nothing to reflow)
    // and a scroll source's handoffRelease schedules its own retile.
    if (!sourceScreen.isEmpty() && sourceEngine == m_autotileEngine.data()) {
        sourceEngine->retile(sourceScreen);
    }
    // Mirrored hole on the TARGET side: targetEngine->handoffRelease(partner)
    // does not retile either, and if the focused window's receive was refused
    // (re-homed into the source) while the partner's receive into the source
    // succeeded, nothing ever arrives on the target to close the partner's
    // vacated slot. Same autotile-only rule as above.
    // focusedAdopted is RE-CHECKED here rather than trusted from the first
    // receive. DEFENSIVE: no in-tree engine evicts a co-occupant on receive
    // (snap assigns the arrival into the zone beside any occupant, autotile
    // and scroll inserts shift indices — all three receivers verified), so
    // with today's engines this always re-reads true when focusedAdopted is
    // true. The re-read stands because it is the only check that would catch
    // a future receiver that DOES displace, and the recovery below depends
    // on knowing, not assuming.
    const bool focusedStillOnTarget = targetEngine->isWindowTracked(windowId);
    // No !targetScreenId.isEmpty() term: handleCrossModeSwap already
    // returned on an empty target id, so the engine identity plus the
    // re-read flag are the whole condition.
    if (targetEngine == m_autotileEngine.data() && !focusedStillOnTarget) {
        targetEngine->retile(targetScreenId);
    }
    // Adopted, then EVICTED by the partner's re-home landing in the same slot
    // (unreachable with today's non-evicting receivers — see the re-check
    // note above — kept as the recovery half of the same defence).
    // receiveVerified already returned true for F, so its own refusal path
    // never ran and nothing has re-homed it — F is tracked by no engine at
    // all, silently. Recover it the way receiveVerified would have: back into
    // the source engine on the source screen, at its original slot (which is
    // the one the partner was going to take).
    // Gated on !partnerAdopted too. The landing slot used below is F's ORIGINAL
    // source slot, which is exactly where the partner goes when ITS receive
    // SUCCEEDS — so re-homing there while the partner holds it would displace
    // one of the two windows, and would also falsify the partner's own
    // move-marker gate below. The eviction this recovers from is by definition
    // caused by the partner's RE-HOME, i.e. its receive was refused. F leaving
    // the target while the partner WAS adopted is a different fault; log it
    // rather than guessing.
    if (focusedAdopted && !focusedStillOnTarget && partnerAdopted) {
        qCWarning(lcDbusWindow) << "cross-mode swap:" << windowId << "left" << targetEngine->engineId()
                                << "but the partner was adopted, so its source slot is taken - not re-homing,"
                                << "window is now tracked by no engine";
    }
    if (focusedAdopted && !focusedStillOnTarget && !partnerAdopted) {
        qCWarning(lcDbusWindow) << "cross-mode swap:" << windowId << "was adopted by" << targetEngine->engineId()
                                << "then evicted by the partner's re-home - re-homing into" << sourceEngine->engineId();
        PhosphorEngine::IPlacementEngine::HandoffContext back;
        back.windowId = windowId;
        back.toScreenId = sourceScreen;
        back.fromEngineId = targetEngine->engineId();
        back.sourceZoneIds = partnerLandingZones;
        back.insertIndex = partnerLandingIndex;
        back.minSize = focusedMinSize;
        back.wasFloating = false;
        sourceEngine->handoffReceive(back);
        if (!sourceEngine->isWindowTracked(windowId)) {
            qCWarning(lcDbusWindow) << "cross-mode swap: eviction re-home REFUSED too -" << windowId
                                    << "is tracked by no engine";
        } else {
            // The recovery re-homed F back across outputs (target → source).
            // Arm the daemon-owned-move marker for it like every other
            // tracked-success placement in this file — an unmarked genuine
            // outputChanged would be re-issued as windowClosed/windowOpened
            // and tear down the placement just made. Tracked-success only;
            // the refused branch above arms nothing.
            if (!PhosphorScreens::ScreenIdentity::screensMatch(sourceScreen, targetScreenId)) {
                Q_EMIT windowOutputMoveExpected(windowId, sourceScreen, targetScreenId);
            }
            if (!sourceScreen.isEmpty() && sourceEngine == m_autotileEngine.data()) {
                sourceEngine->retile(sourceScreen);
            }
        }
    }

    // ── Arm the daemon-owned-move markers for the windows that were
    //    genuinely adopted. Placement geometry lands on the coalesced
    //    retile (queued), so post-receive arming is still ahead of the
    //    compositor's outputChanged; a refused receive arms nothing. ──
    //    Both markers carry their source explicitly, same reason as the move
    //    path: the receives above already re-pointed the effect's
    //    notified-screen records at each window's destination.
    //    focusedStillOnTarget, not focusedAdopted: arming the one-shot for a
    //    window the target no longer holds would swallow that window's next
    //    genuine outputChanged. screensMatch rather than a raw compare, for
    //    the connector-name / EDID-id / "/vs:" spelling reasons guardedHandoff
    //    documents — a spurious inequality here arms a move that never happens.
    if (!PhosphorScreens::ScreenIdentity::screensMatch(targetScreenId, sourceScreen)) {
        if (focusedStillOnTarget) {
            Q_EMIT windowOutputMoveExpected(windowId, targetScreenId, sourceScreen);
        }
        // The isWindowTracked term is the partner-side twin of the one
        // deliberate focusedStillOnTarget re-read above, with the same
        // justification: arming the one-shot for a window the source no
        // longer holds would swallow its next genuine outputChanged. With
        // today's non-evicting receivers it always agrees with
        // partnerAdopted; it stands for the same future-receiver reason.
        if (partnerAdopted && sourceEngine->isWindowTracked(partner)) {
            Q_EMIT windowOutputMoveExpected(partner, sourceScreen, targetScreenId);
        }
    }
}

} // namespace PlasmaZones
