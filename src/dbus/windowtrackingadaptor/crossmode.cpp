// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// WindowTrackingAdaptor — cross-mode directional handoff
//
// handleCrossModeMove / handleCrossModeSwap: relinquish a window from the source
// engine and place (or exchange) it on the target engine when a directional
// navigation crosses into a different tiling mode.
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

namespace PlasmaZones {

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
        // An autotile target's handoffReceive also announces the arrival's
        // tiled (non-floating) state on the passive float-sync channel —
        // intended: the arrival IS tiled, and the relay's last-broadcast
        // gate dedups when the bit already agrees.
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
    // (Both placements below: an autotile receiver's handoffReceive also
    // announces the arrival's tiled state on the passive float-sync channel —
    // intended; the relay's last-broadcast gate dedups an agreeing bit.)
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
