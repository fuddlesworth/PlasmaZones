// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "compositor/effectlogging.h"

#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>
#include <virtualdesktops.h>
#include <window.h>

#include <QLoggingCategory>

#include "tilinghandler/tilinghandler.h"

namespace PlasmaZones {

namespace {
// The window's VirtualDesktop id set, in the form m_trackedDesktopsPerWindow
// stores it: an EMPTY set is KWin's "on all desktops". VirtualDesktop::id() is
// CONSTANT, unlike x11DesktopNumber(), which renumbers when a desktop is
// removed — a stamp has to survive that to still mean anything on the next
// desktop edit. Null entries are skipped, matching the derivations in
// window_identity.cpp.
QSet<QString> desktopIdsOf(const KWin::EffectWindow* window)
{
    QSet<QString> ids;
    if (!window) {
        return ids;
    }
    const QList<KWin::VirtualDesktop*> desktops = window->desktops();
    for (const KWin::VirtualDesktop* vd : desktops) {
        if (vd) {
            ids.insert(vd->id());
        }
    }
    return ids;
}
} // namespace

// Everything that reacts to a window's virtual-desktop SET changing: the
// departure arm (the window left the desktop in view), the arrival arm (it
// moved onto the desktop in view), and the per-window stamp both arms diff
// against. Its own translation unit because window_connections.cpp is over the
// file-size ceiling and this was its largest single concern.
void PlasmaZonesEffect::wireDesktopChangeHandler(KWin::EffectWindow* w)
{
    // Seed the desktop-set stamp the handler below diffs against, so the
    // window's very first desktop edit is already classifiable. Every wired
    // window is seeded here; the windowDeleted cleanup erases the entry
    // alongside m_trackedScreenPerWindow.
    m_trackedDesktopsPerWindow[w] = desktopIdsOf(w);

    connect(w, &KWin::EffectWindow::windowDesktopsChanged, this, [this](KWin::EffectWindow* window) {
        updateWindowStickyState(window);
        // Re-stamp FIRST, ahead of every early return below, and read the
        // previous value out here: this signal reports any edit to the desktop
        // set, and each arm below returns from a different point. A stamp
        // written on only some paths would leave the next edit diffing against
        // a set two edits old.
        QSet<QString> previousDesktops;
        bool hadPreviousDesktops = false;
        if (window) {
            const auto prevIt = m_trackedDesktopsPerWindow.constFind(window);
            if (prevIt != m_trackedDesktopsPerWindow.constEnd()) {
                previousDesktops = *prevIt;
                hadPreviousDesktops = true;
            }
            m_trackedDesktopsPerWindow[window] = desktopIdsOf(window);
        }
        // No metadata push here: the daemon's float resolver reads the
        // window's own desktop/activity from the registry, but that is kept
        // fresh by the KWin::Window::desktopsChanged → pushLatest connection
        // below (this signal is KWin's EffectWindow relay of the same event,
        // so a push here would build and marshal the extended snapshot twice
        // per desktop move).

        // When a window is moved to a different desktop (e.g., "Move to Desktop 2"),
        // treat it as removed from the current desktop's tiling. The normal desktop-
        // switch flow will pick it up when the user switches to the target desktop.
        // Judged against the window's OWN output, not the active one.
        // EffectWindow::isOnCurrentDesktop() has no output overload and
        // resolves through the active output (VirtualDesktopManager stores
        // only QHash<LogicalOutput*, VirtualDesktop*>, with no global current
        // member), so under per-output virtual desktops it answers for the
        // wrong monitor whenever the window lives on a non-active one. That
        // would release tracking and drop the decoration for a window still
        // fully visible on another screen. Dynamic workspaces makes per-output
        // mode mandatory, so this went from a hand-configured edge to routine.
        KWin::VirtualDesktop* ownDesktop =
            (window && KWin::effects) ? KWin::effects->currentDesktop(windowOutput(window)) : nullptr;
        if (window && ownDesktop && !window->isOnDesktop(ownDesktop) && !window->isOnAllDesktops()) {
            const QString windowId = getWindowId(window);
            const QString screenId = getWindowScreenId(window);
            if (m_tilingHandler->isManagedScreen(screenId)) {
                // Save pre-autotile geometry before onWindowClosed clears it.
                // When the window is re-added on the target desktop, this preserved
                // geometry is used instead of the current (tiled) frame position.
                m_tilingHandler->savePreTileForDesktopMove(windowId);

                // Title-bar state is rule-driven (no autotile decoration claim
                // to release): KWin's off-desktop noBorder reset is corrected on
                // desktop return by updateAllDecorations → resyncWindow for any
                // rule-owned window. releaseWindowTracking, NOT onWindowClosed:
                // the window is alive and merely moving desktops, so the close
                // relay's capture and its ledger append must not fire (the
                // preserved pre-tile geometry above is the state that matters).
                m_tilingHandler->releaseWindowTracking(windowId, screenId);
                removeWindowDecoration(windowId);
                qCInfo(lcEffect) << "Window moved off current desktop, removed from autotile:" << windowId;
            }
            return;
        }

        // The mirror case: the window arrived ON the desktop in view, moved
        // here from another one (a pager / Overview drop, or "Move to
        // Desktop" aimed at the current desktop). Nothing else adopts it.
        // The arm above only fires for a window LEAVING the visible desktop,
        // so the source context never released it, and the desktop-return
        // catch-scan in slotScreensChanged never runs because no desktop
        // switch happened. Without this the window sits over the strip /
        // stack untracked until the user leaves the desktop and comes back.
        if (!window || window->isDeleted() || window->isOnAllDesktops()) {
            return;
        }
        // A MOVE, not any other edit to the desktop set. KWin reports all of
        // them through this one signal, and only a move makes the window newly
        // present on the desktop the user is looking at:
        //
        //   set GREW   (desktop 1 → desktops 1 and 2, while 1 is in view) — the
        //              window was already here and is already placed,
        //   set SHRANK (desktops 1 and 2 → desktop 1, while 1 is in view) — same,
        //   UN-STUCK   (all desktops → desktop 1) — a sticky window is on every
        //              desktop, so it was already here too.
        //
        // Placing on any of those re-places a window that never went anywhere:
        // on a snapping screen with an auto-assign layout it would pull an
        // already-snapped window out of its zone into the first empty one, or
        // re-snap one the user deliberately floated. The tiling arm below is
        // additionally covered by its own isTrackedWindow guard, but this is
        // the discriminator both arms actually want, and it is the only one
        // that reaches the snapping arm at all (snapping keeps no membership
        // set to consult).
        //
        // An unseeded stamp reads as "already here": the seed runs for every
        // wired window, so this is unreachable in practice, and declining to
        // place is the conservative answer to a first observation. A null
        // current desktop is unclassifiable and takes the same answer.
        // Resolved for the window's own output. The no-argument overload
        // answers for the ACTIVE output, so measuring an arrival against it
        // compares the window's desktop set to a different monitor's current
        // desktop under per-output virtual desktops, which this feature makes
        // mandatory.
        const KWin::VirtualDesktop* desktopInView =
            KWin::effects ? KWin::effects->currentDesktop(windowOutput(window)) : nullptr;
        if (!desktopInView || !hadPreviousDesktops || previousDesktops.isEmpty()
            || previousDesktops.contains(desktopInView->id())) {
            return;
        }
        const QString windowId = getWindowId(window);
        const QString screenId = getWindowScreenId(window);
        // A window parked for a desktop-arrival restore can reach its desktop by
        // this route instead of a desktop switch: the user drags it there in the
        // pager, or picks "Move to Desktop" aimed at the one in view. The drain
        // is only wired to the desktop and activity signals, so without this the
        // park would survive, and the NEXT unrelated desktop switch would spend
        // it — re-placing a window the user has since positioned by hand.
        // Draining here restores it at the moment it arrives, which is what the
        // park was for, and spends the entry so nothing fires later.
        //
        // Scoped to THIS window, and returning when it fires, because the arms
        // below place the arriving window themselves (snapToEmptyZone on a
        // snapping screen, the tiling adopt on a managed one). Running both
        // would put two independent placement answers for one window on the wire
        // at once, with the winner decided by D-Bus reply order.
        if (m_snapHandler && m_snapHandler->drainDesktopArrivalFor(windowId, window)) {
            return;
        }
        if (!m_tilingHandler->isManagedScreen(screenId)) {
            // Snapping screen. There is no stack to join and snapping places
            // nothing on its own, so an arrival floats — unless the context's
            // layout auto-assigns, which is the one case with somewhere to put
            // it. Offer it the same auto-fill the drop path runs (drag_end.cpp),
            // and let the daemon decide: snapToEmptyZone gates itself on
            // `layout->autoAssign() || autoAssignAllLayouts()` and answers
            // shouldSnap=false when neither is on, which is exactly the
            // nothing-to-do case. It resolves the empty zone against the
            // screen's CURRENT desktop, so the arrival is measured against the
            // context it landed in, not the one it left.
            //
            // shouldHandleWindow / isOnCurrentActivity are this arm's OWN
            // gates, not belt-and-braces: neither the daemon's snapToEmptyZone
            // slot nor the engine's calculateSnapToEmptyZone re-checks either
            // (the engine documents that it deliberately does not even skip
            // floating windows — its callers gate it). The drop path gets both
            // for free, because an excluded window never reaches drag handling
            // at all and a drop happens on the activity in view; an arrival
            // gets neither, so a user-excluded window, or one that landed here
            // while belonging to another activity, would be snapped into a zone
            // of a layout that is not its context's.
            if (shouldHandleWindow(window) && window->isOnCurrentActivity()
                && isDaemonReady("auto-fill on desktop arrival")) {
                tryAsyncSnapCall(PhosphorProtocol::Service::Interface::Snap, QStringLiteral("snapToEmptyZone"),
                                 // sticky=false, not isWindowSticky(): a sticky
                                 // window returned above, so it is the only
                                 // value that can reach here.
                                 {windowId, screenId, false}, window, windowId,
                                 /*storePreSnap=*/true, /*fallback=*/nullptr);
            }
            return;
        }
        // Already in this desktop's stack: the signal reported a desktop SET
        // that merely grew (desktop 1 → desktops 1 and 2), not a move. Re-adding
        // would append the window to the engine state a second time.
        if (m_tilingHandler->isTrackedWindow(windowId)) {
            return;
        }
        // Release first, unconditionally. The window may still be parked in the
        // SOURCE desktop's engine state (it was demoted, not dropped, when the
        // user switched away from that desktop), and adding it here without
        // releasing leaks it into two contexts at once. The daemon resolves the
        // owning engine by window id, so this reaches the source context even
        // though the current one has already changed; on a window the engine
        // never held it is a no-op.
        m_tilingHandler->releaseWindowTracking(windowId, screenId);
        // Fold the departure arm's preserved pre-autotile rect back in, exactly
        // as the desktop-return catch-scan does before ITS re-add. Ordering is
        // load-bearing in both directions: releaseWindowTracking above wipes
        // this window's pre-tile bucket (cleanupAutotileTracking →
        // cleanupClosedWindowState), so restoring earlier would be undone, and
        // notifyWindowAdded below reads the bucket, so restoring later would be
        // too late. Without it the window keeps no free geometry at all — the
        // stash is stranded (the window is tracked from here on, so the
        // catch-scan's restore branch can never reach it either) and a
        // float-back lands on the source desktop's tiled rect, which is the
        // precise outcome savePreTileForDesktopMove exists to prevent.
        m_tilingHandler->restorePreTileForDesktopMove(windowId, screenId);
        // knownFreeFloating=false, matching the catch-scan: the frame is very
        // likely the SOURCE desktop's tiled rect, and the floating guard has to
        // run and reject it rather than persist it as free-floating geometry.
        m_tilingHandler->notifyWindowAdded(window, /*knownFreeFloating=*/false);
        qCInfo(lcEffect) << "Window moved onto current desktop, added to autotile:" << windowId;
    });
}

} // namespace PlasmaZones
