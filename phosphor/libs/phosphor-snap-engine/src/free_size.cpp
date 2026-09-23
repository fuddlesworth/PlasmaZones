// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The float terminals' size-only half, split out of lifecycle.cpp by concern:
// the snap arm of the shared free-size restore a float verdict gives a window
// that stays where the compositor put it (#1106), the managed-size list it
// and the floated-record move share, the lineage snapshot the restore gates
// on, and the reclaim-declined float default that is one of its callers.

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include "snapenginelogging.h"

namespace PhosphorSnapEngine {

QList<QSize> SnapEngine::managedSizesOnScreen(const QString& screenId, int desktop) const
{
    QList<QSize> sizes;
    if (!m_windowTracker || screenId.isEmpty()) {
        return sizes;
    }
    // The layout of the context the window is placed INTO, which a session
    // restore onto a background desktop or a RouteToDesktop rule makes
    // differ from the one the screen shows.
    if (m_layoutManager) {
        const int layoutDesktop = desktop >= 1 ? desktop : currentVirtualDesktopForScreen(screenId);
        if (PhosphorZones::Layout* layout =
                m_layoutManager->layoutForScreen(screenId, layoutDesktop, currentActivity())) {
            for (PhosphorZones::Zone* zone : layout->zones()) {
                // zoneGeometry is keyed by the id string, so the braced
                // QUuid spelling is what the tracker parses back; the
                // round-trip is per zone per open, not per frame.
                const QRect geo = m_windowTracker->zoneGeometry(zone->id().toString(), screenId);
                if (geo.isValid()) {
                    sizes.append(geo.size());
                }
            }
        }
    }
    // Every live snapped window on this screen, whatever context it sits
    // in: its resolved rect is a size a managed frame has, including one
    // from another desktop's layout (zoneGeometry finds a zone in any loaded
    // layout) and a multi-zone span. A window floated FROM a zone keeps its
    // assignment for the resnap path but no live frame has that size, so it
    // is skipped. Scoped to this screen's stores, since a store is keyed by
    // the screen it belongs to.
    for (auto it = m_states.states().cbegin(); it != m_states.states().cend(); ++it) {
        const SnapState* state = it.value();
        if (!state || it.key().screenId != screenId) {
            continue;
        }
        for (const QString& snapped : state->snappedWindows()) {
            if (state->isFloating(snapped) || state->screenForWindow(snapped) != screenId) {
                continue;
            }
            const QRect rect = m_windowTracker->resolveZoneGeometry(state->zonesForWindow(snapped), screenId);
            if (rect.isValid()) {
                sizes.append(rect.size());
            }
        }
    }
    return sizes;
}

// The snap arm of PlacementEngineBase::restoreFreeSizeWhereItStands, which
// carries the contract. This supplies what only this engine knows: the sizes
// a snapped window on @p screenId can have in the context of @p desktop.
void SnapEngine::restoreFreeSizeForUnplaced(const QString& windowId, const QString& screenId, int desktop,
                                            PhosphorEngine::RestoreReason reason, bool placedBefore)
{
    if (!m_windowTracker || windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }
    restoreFreeSizeWhereItStands(m_windowTracker, windowId, screenId, reason, placedBefore,
                                 managedSizesOnScreen(screenId, desktop));
}

bool SnapEngine::wasPlacedByPreviousLineage(const QString& windowId) const
{
    return m_windowTracker && placedByPreviousLineage(m_windowTracker->placementStore(), windowId);
}

void SnapEngine::applyNoMatchFloatDefault(const QString& windowId, const QString& screenId,
                                          PhosphorEngine::RestoreReason reason, bool placedBefore)
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
    // A deferredToTilingEngine verdict implies the placement rule was
    // admissible for this window (the defer gate requires !deferredByMode),
    // so the routed open desktop resolves here exactly as it did in the
    // resolve that deferred.
    //
    // The lineage snapshot is the CALLER'S, not one taken here: a declined
    // claim does not always leave the store as it found it. An autotile or
    // scroll claim calls windowOpened on the recorded home BEFORE it verifies
    // membership, and that open reaches takeForReopen, which re-binds the
    // consumed record under this uuid with the claiming engine's slot. When
    // the membership check then refuses the adoption, the claim returns false
    // with that slot-bearing record still standing, and a snapshot taken at
    // this point reads "already placed" for a window no engine ever placed.
    const int routed = routedOpenDesktop(windowId, screenId);
    const int openDesktop = routed >= 1 ? routed : currentVirtualDesktopForScreen(screenId);
    stateForWindowOnScreen(windowId, screenId, openDesktop)->setFloatingOnScreen(windowId, screenId, openDesktop);
    restoreFreeSizeForUnplaced(windowId, screenId, openDesktop, reason, placedBefore);
    Q_EMIT windowFloatingChanged(windowId, true, screenId);
    qCInfo(PhosphorSnapEngine::lcSnapEngine)
        << "applyNoMatchFloatDefault:" << windowId << "reclaim declined — defaulting to floated on" << screenId;
}

} // namespace PhosphorSnapEngine
