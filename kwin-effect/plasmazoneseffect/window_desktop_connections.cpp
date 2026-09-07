// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "compositor/effectlogging.h"
#include "desktopvisibility.h"

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
        // Corpse gate FIRST, ahead of everything. getWindowId on a deleted
        // window pollutes the id cache, and both updateWindowStickyState and
        // the stamp below reach it. Nothing downstream wants a corpse's
        // answer either: its row in m_trackedDesktopsPerWindow is erased by
        // the windowDeleted cleanup whether or not this edit stamped it, and
        // its sticky value has no consumer.
        if (!window || window->isDeleted()) {
            return;
        }
        updateWindowStickyState(window);
        // Re-stamp SECOND, ahead of every early return below, and read the
        // previous value out here: this signal reports any edit to the desktop
        // set, and each arm below returns from a different point. A stamp
        // written on only some paths would leave the next edit diffing against
        // a set two edits old.
        QSet<QString> previousDesktops;
        bool hadPreviousDesktops = false;
        const auto prevIt = m_trackedDesktopsPerWindow.constFind(window);
        if (prevIt != m_trackedDesktopsPerWindow.constEnd()) {
            previousDesktops = *prevIt;
            hadPreviousDesktops = true;
        }
        const QSet<QString> currentDesktops = desktopIdsOf(window);
        m_trackedDesktopsPerWindow[window] = currentDesktops;
        // Remember where the window lived on the way INTO sticky, because the
        // sticky stamp itself is empty and the un-stick arm below needs to know
        // whether the desktop it lands on is the one the engines keyed it
        // under. Only the transition writes it, so a second edit while sticky
        // cannot overwrite the answer with an empty set.
        if (currentDesktops.isEmpty() && hadPreviousDesktops && !previousDesktops.isEmpty()) {
            m_preStickyDesktopsPerWindow[window] = previousDesktops;
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
        //
        // Measured against the desktop the window's OWN output is showing, not
        // the session-wide current one — see desktopvisibility.h. On the global
        // reading this arm fired for a window that had just become visible on
        // its own output, stripping its tracking and decoration while the user
        // looked at it.
        if (!isOnOwnOutputCurrentDesktop(window) && !window->isOnAllDesktops()) {
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
        if (window->isOnAllDesktops()) {
            return;
        }
        // A MOVE, not any other edit to the desktop set. KWin reports all of
        // them through this one signal, and only a move makes the window newly
        // present on the desktop the user is looking at:
        //
        //   set GREW   (desktop 1 → desktops 1 and 2, while 1 is in view) — the
        //              window was already here and is already placed,
        //   set SHRANK (desktops 1 and 2 → desktop 1, while 1 is in view) — same.
        //
        // Placing on either of those re-places a window that never went
        // anywhere: on a snapping screen with an auto-assign layout it would
        // pull an already-snapped window out of its zone into the first empty
        // one, or re-snap one the user deliberately floated. The tiling arm
        // below is additionally covered by its own isTrackedWindow guard, but
        // this is the discriminator both arms actually want, and it is the only
        // one that reaches the snapping arm at all (snapping keeps no
        // membership set to consult).
        //
        // UN-STUCK (all desktops → desktop 1) is the third shape and it is NOT
        // one of those, which is what stickyFallThrough below is for. A sticky
        // window is on every desktop, so it was visibly here already — but the
        // engines adopt it under ONE concrete desktop key, whichever was in
        // view at the time, and nothing re-keys it across a later switch. Un-
        // stick it from a different desktop and the daemon's reconcile
        // correctly releases it from that stale key, so declining to re-add
        // here left it visible, untracked and unreachable: the id stays in
        // m_notifiedWindows, which is the gate both the catch-scan and
        // notifyWindowAdded consult, so nothing healed it until the window was
        // moved again or reopened.
        //
        // An unseeded stamp reads as "already here": the seed runs for every
        // wired window, so this is unreachable in practice, and declining to
        // place is the conservative answer to a first observation. A null
        // desktop is unclassifiable and takes the same answer. hadPreviousDesktops
        // is what separates the two empty cases — desktopIdsOf returns empty
        // only for KWin's on-all-desktops, so a RECORDED empty stamp means the
        // window was sticky, while no stamp at all means unseeded.
        const KWin::VirtualDesktop* desktopInView = desktopShownOn(window);
        if (!desktopInView || !hadPreviousDesktops) {
            return;
        }
        // An un-stick: the recorded stamp was empty. It only needs re-homing
        // when the window is landing somewhere OTHER than the desktop it was
        // adopted under, because that is the case the daemon's reconcile has
        // just released it from. Coming back to where it started, its key is
        // still correct and a release-and-re-add would only cost it its slot.
        bool stickyFallThrough = false;
        if (previousDesktops.isEmpty()) {
            const auto preStickyIt = m_preStickyDesktopsPerWindow.constFind(window);
            const bool landedWhereItWasAdopted =
                preStickyIt != m_preStickyDesktopsPerWindow.constEnd() && preStickyIt->contains(desktopInView->id());
            m_preStickyDesktopsPerWindow.remove(window);
            if (landedWhereItWasAdopted) {
                return;
            }
            stickyFallThrough = true;
        } else if (previousDesktops.contains(desktopInView->id())) {
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
        //
        // The sticky fall-through takes NEITHER unmanaged branch. Its window
        // never moved and is visible right now, so stashing-and-wiping it (the
        // branch just below) would strip a decoration the user is looking at,
        // and offering it to snapToEmptyZone (the branch after the drain) would
        // pull it into a zone it was never in. It wants only the managed arm's
        // release-and-re-add, which is what re-homes it onto the desktop it is
        // actually on.
        const bool destinationManaged = m_tilingHandler->isManagedScreen(screenId);
        if (!destinationManaged && !stickyFallThrough) {
            // The desktop in view runs no tiling, but the desktop the window
            // came from may well have, and the window was tiled there: this
            // is the EFFECT-side half of that departure, mirroring the
            // departure arm above for a window that left a desktop the user
            // was not looking at. The engine-side half — dropping the window
            // from the source desktop's state, which the desktop in view can
            // say nothing about — is the daemon's: TilingAdaptor's
            // desktop-membership reconcile releases it off the registry
            // desktop set the sibling KWin::Window::desktopsChanged →
            // pushLatest connection carries (#1076). Here: stash the
            // pre-autotile rect before the tracking wipe, exactly as the
            // departure arm does, so a later move back onto a tiled desktop
            // folds it in before its re-add and a float-back there returns to
            // the free position rather than the source desktop's tiled frame;
            // drop the effect's tracking entries and re-resolve the decoration.
            // releaseWindowTracking's daemon relay is gated on this screen
            // being managed, so on this branch it is effect-side only.
            m_tilingHandler->savePreTileForDesktopMove(windowId);
            m_tilingHandler->releaseWindowTracking(windowId, screenId);
            // reconcileDecorationOnPlacementFlip, not removeWindowDecoration.
            // This window is on the desktop in view, so a bare removal takes
            // away a rule- or snap-driven border with nothing to put it back:
            // the departure arm can remove because its window is off-desktop
            // and updateAllDecorations rebuilds it on return, but no desktop
            // switch happens here. The shared funnel re-resolves update-or-
            // remove under the window's new placement state in the same turn,
            // and it must run AFTER the release so it sees the untiled state.
            reconcileDecorationOnPlacementFlip(windowId);
        }
        // Runs on both branches, and before the placement arms below, so a
        // window that was parked for a desktop-arrival restore is restored by
        // the park rather than re-placed from scratch. On a managed
        // destination it can only spend the park and answer false.
        if (m_snapHandler && m_snapHandler->drainDesktopArrivalFor(windowId, window)) {
            return;
        }
        if (!destinationManaged && !stickyFallThrough) {
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
        //
        // NOT for the sticky fall-through, which is the one case where this
        // window is still in m_notifiedWindows and must be re-added anyway.
        // The daemon released it from a stale desktop key and the effect never
        // ran an arm for it, so its id is exactly as tracked as before — this
        // guard would send it away with nothing done. The set-grew case the
        // guard is written for is already excluded by the discriminator above.
        //
        // Past this point everything is the MANAGED arm. The sticky
        // fall-through skipped both unmanaged branches above, so it arrives
        // here on a snapping destination too, and the adopt below is not for
        // it: notifyWindowAdded gates on the same managed set and would
        // decline, leaving the release that precedes it as a scrub with no
        // re-add.
        //
        // It still owes the unmanaged side its own cleanup, which is the same
        // work the branch above does for an ordinary arrival. The daemon has
        // released this window from the desktop key it was adopted under, so
        // the effect's tiling bookkeeping has to go with it — leaving the id in
        // m_notifiedWindows would make every later announce decline, which is
        // the untracked-and-unreachable shape this whole arm exists to close.
        if (!destinationManaged) {
            m_tilingHandler->savePreTileForDesktopMove(windowId);
            m_tilingHandler->releaseWindowTracking(windowId, screenId);
            reconcileDecorationOnPlacementFlip(windowId);
            return;
        }
        if (!stickyFallThrough && m_tilingHandler->isTrackedWindow(windowId)) {
            return;
        }
        if (stickyFallThrough) {
            // The bucket has to be stashed before the release wipes it. On
            // every other path into this arm the departure arm already stashed
            // it, but this window never took one. The departure arm is only
            // reached when the window is NOT on the desktop its own output
            // shows, and the sticky arm only when it is. So without this the
            // release below drops the window's only record of its free
            // geometry and the restore two lines down has nothing to fold
            // back. The window would then be re-added with no free geometry at
            // all, and notifyWindowAdded's knownFreeFloating=false capture
            // would measure it against its current tiled frame.
            m_tilingHandler->savePreTileForDesktopMove(windowId);
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
