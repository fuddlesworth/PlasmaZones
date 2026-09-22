// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The float terminals' size-only half, split out of lifecycle.cpp by concern:
// the snap arm of the shared free-size restore a float verdict gives a window
// that stays where the compositor put it (#1106), and the reclaim-declined
// float default that is one of its callers.

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include "snapenginelogging.h"

namespace PhosphorSnapEngine {

// The snap arm of PlacementEngineBase::restoreFreeSizeWhereItStands, which
// carries the contract. This supplies what only this engine knows: the sizes
// a snapped window on @p screenId can have (every zone of the layout the
// screen runs, plus every multi-zone span a live window is snapped across),
// and the screen's available area for the clamp. A span under a layout
// switched since is not caught; the restore is best effort.
void SnapEngine::restoreFreeSizeForUnplaced(const QString& windowId, const QString& screenId,
                                            PhosphorEngine::RestoreReason reason)
{
    if (!m_windowTracker || windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }
    QList<QSize> snappedSizes;
    if (m_layoutManager) {
        if (PhosphorZones::Layout* layout = m_layoutManager->layoutForScreen(
                screenId, currentVirtualDesktopForScreen(screenId), currentActivity())) {
            for (PhosphorZones::Zone* zone : layout->zones()) {
                const QRect geo = m_windowTracker->zoneGeometry(zone->id().toString(), screenId);
                if (geo.isValid()) {
                    snappedSizes.append(geo.size());
                }
            }
        }
    }
    for (SnapState* state : m_states.states()) {
        for (const QString& snapped : state->snappedWindows()) {
            const QStringList zones = state->zonesForWindow(snapped);
            if (zones.size() > 1 && state->screenForWindow(snapped) == screenId) {
                const QRect span = m_windowTracker->resolveZoneGeometry(zones, screenId);
                if (span.isValid()) {
                    snappedSizes.append(span.size());
                }
            }
        }
    }
    QSize available;
    if (auto* mgr = m_windowTracker->screenManager()) {
        const QRect avail = mgr->screenAvailableGeometry(screenId);
        if (avail.isValid()) {
            available = avail.size();
        }
    }
    restoreFreeSizeWhereItStands(m_windowTracker, windowId, screenId, reason, snappedSizes, available);
}

void SnapEngine::applyNoMatchFloatDefault(const QString& windowId, const QString& screenId,
                                          PhosphorEngine::RestoreReason reason)
{
    // The default-float terminal, callable by the SnapAdaptor when a
    // tile-defer verdict (SnapResult::deferredToTilingEngine) was returned
    // and the offered reclaim then DECLINED — the deferring side and the
    // claiming side ask slightly different questions (the claims add live
    // sets, context equality and tileability), so a defer-then-decline is
    // reachable, and without this fallback the window ended the open with
    // no state in any engine: not floated, not tiled, invisible to the
    // float toggle and the save sweep. Guarded so a window that meanwhile
    // gained a definite state is left alone.
    //
    // Deliberately NOT a full mirror of that terminal's preconditions. It
    // skips the disabled-context predicate, on the floated-restore branch's
    // reasoning: a floated window is not being SNAPPED into a context, so a
    // context with snapping disabled has no say in whether it has a float
    // state. It also does not re-check screen mode, which is sound only
    // because a deferredToTilingEngine verdict implies a snapping-mode
    // opening screen (the tile gate requires !deferredByMode) — a second
    // caller would have to establish that itself.
    if (windowId.isEmpty() || screenId.isEmpty() || !isEnabled()) {
        return;
    }
    if (isFloating(windowId)) {
        return;
    }
    if (const SnapState* snappedState = stateForWindow(windowId);
        snappedState && snappedState->isWindowSnapped(windowId)) {
        return;
    }
    stateForWindowOnScreen(windowId, screenId)
        ->setFloatingOnScreen(windowId, screenId, currentVirtualDesktopForScreen(screenId));
    Q_EMIT windowFloatingChanged(windowId, true, screenId);
    restoreFreeSizeForUnplaced(windowId, screenId, reason);
    qCInfo(PhosphorSnapEngine::lcSnapEngine)
        << "applyNoMatchFloatDefault:" << windowId << "reclaim declined — defaulting to floated on" << screenId;
}

} // namespace PhosphorSnapEngine
