// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The float terminals' size-only half, split out of lifecycle.cpp by concern:
// the free-size restore a float verdict gives a window that stays where the
// compositor put it (#1106), and the reclaim-declined float default that is
// one of its callers.

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include "snapenginelogging.h"

#include <algorithm>

namespace PhosphorSnapEngine {

// restoreFreeSizeForUnplaced — the size-only half of a float verdict (#1106).
//
// A window this engine leaves floating sits where KWin placed it, at the size
// the client asked for. KDE apps save their window size to their own config on
// every resize, and a snap is a resize, so an app with a snapped window opens
// its next window at the ZONE's size. Nothing else undoes that: the position
// restore is gated on the unsnapped-position opt-in and moves only a window
// with its own screen-local record, and a fresh second instance has no record
// at all. So the size comes from, in order:
//   1. the window's OWN record — a reopen whose snapped record was declined
//      (managed gate, disabled context, the #1104 layout gate, zone gone) or
//      whose floated record restored without a move; it was re-bound to the
//      live id before the gates ran, free geometry included;
//   2. a LIVE SIBLING's record, the earliest-recorded one with a usable rect.
// A rect is usable only when it lies on @p screenId (screen-local, like the
// floated restore's move) and its size is not that of a zone of the layout
// the screen runs, nor of a span a live window is snapped across: a sibling
// the auto-snap chain placed at open carries the zone-sized spawn frame as
// its own "free" rect, which is the very size this restore exists to undo,
// and bucket order alone cannot tell such a sibling from the first instance.
// A span under a layout switched since, or a size the client rounded by more
// than a couple of pixels, is not caught; the restore is best effort.
// The size is then clamped to the screen's available area, since a rect
// captured at a larger resolution still overlaps.
//
// Gated on @p reason: only a first placement may resize. Open is the fresh
// window, PendingSweep is the open that the readiness gate refused and that
// the sweep now performs, DesktopArrival is the continuation of an open the
// effect parked while its desktop was not showing. The unminimize and
// daemon-restart re-resolves act on a window the user is looking at and may
// have sized, and must leave it alone.
//
// Emits sizeRestoreRequested; the adaptor relays it as a size-only
// applyGeometryRequested and the effect applies it as a teleport under
// first-frame suppression. Reached once per float: the terminals sit behind
// the already-floating guard, and the floated-record branch, which runs
// before that guard, checks the floating state itself before calling this.
void SnapEngine::restoreFreeSizeForUnplaced(const QString& windowId, const QString& screenId,
                                            PhosphorEngine::RestoreReason reason)
{
    if (!m_windowTracker || windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }
    if (reason == PhosphorEngine::RestoreReason::Unminimize
        || reason == PhosphorEngine::RestoreReason::DaemonRestartSweep) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine)
            << "restoreFreeSizeForUnplaced:" << windowId << "re-resolve of a visible window, size left alone";
        return;
    }
    const auto& store = m_windowTracker->placementStore();
    // The sizes a snapped window on this screen can have, resolved once: every
    // zone of the layout the screen runs, plus every multi-zone span a live
    // window is snapped across. A free rect of such a size is a snapped
    // window's spawn frame, not a free life (see the banner).
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
    // Within a couple of pixels, not exact: a fractional-scale round-trip or
    // a size-increment client (terminal cells) lands the frame a pixel or two
    // off the requested rect. A false match only skips the resize, which is
    // the harmless direction.
    const auto isSnappedSize = [&](const QSize& size) {
        return std::any_of(snappedSizes.cbegin(), snappedSizes.cend(), [&](const QSize& s) {
            return qAbs(s.width() - size.width()) <= 2 && qAbs(s.height() - size.height()) <= 2;
        });
    };
    const auto usableOn = [&](const QRect& rect) {
        return rect.isValid() && m_windowTracker->geometryBelongsToScreen(rect, screenId)
            && !isSnappedSize(rect.size());
    };
    QRect freeGeo;
    QString sourceWindowId;
    const auto own = store.peekExact(windowId);
    const QRect ownRect = own ? own->freeGeometryFor(screenId) : QRect();
    if (usableOn(ownRect)) {
        freeGeo = ownRect;
        sourceWindowId = own->windowId;
    } else {
        const QString appId = m_windowTracker->currentAppIdFor(windowId);
        const auto sibling = store.peekLiveSibling(windowId, appId, [&](const PhosphorEngine::WindowPlacement& p) {
            return usableOn(p.freeGeometryFor(screenId));
        });
        if (sibling) {
            freeGeo = sibling->freeGeometryFor(screenId);
            sourceWindowId = sibling->windowId;
        }
    }
    if (!freeGeo.isValid()) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine)
            << "restoreFreeSizeForUnplaced:" << windowId << "no free size on record for" << screenId;
        return;
    }
    QSize size = freeGeo.size();
    if (auto* mgr = m_windowTracker->screenManager()) {
        const QRect avail = mgr->screenAvailableGeometry(screenId);
        if (avail.isValid()) {
            size = size.boundedTo(avail.size());
        }
    }
    qCInfo(PhosphorSnapEngine::lcSnapEngine)
        << "restoreFreeSizeForUnplaced:" << windowId << "->" << size << "from" << sourceWindowId;
    Q_EMIT sizeRestoreRequested(windowId, size, screenId);
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
