// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "handlers/snaphandler.h"
#include "tilinghandler/tilinghandler.h"
#include "desktopvisibility.h"
#include "compositor/effectlogging.h"

#include <effect/effecthandler.h>
#include <virtualdesktops.h>
#include <window.h>

#include <QLoggingCategory>
#include <QPointer>

namespace PlasmaZones {

namespace {
// The window's VirtualDesktop id set, in the form the context stamp stores
// it: an EMPTY set is KWin's "on all desktops". VirtualDesktop::id() is
// CONSTANT, unlike x11DesktopNumber(), which renumbers when a desktop is
// removed, so a stamp still means something on the next edit. Null entries
// are skipped, matching the derivations in window_identity.cpp.
WindowContextEdge::IdSet desktopIdsOf(const KWin::EffectWindow* window)
{
    WindowContextEdge::IdSet ids;
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

// The window's activity ids, in the stamp's form: EMPTY is "on all
// activities".
WindowContextEdge::IdSet activityIdsOf(const KWin::EffectWindow* window)
{
    WindowContextEdge::IdSet ids;
    if (window) {
        const QStringList activities = window->activities();
        for (const QString& activity : activities) {
            ids.insert(activity);
        }
    }
    return ids;
}
} // namespace

// Everything that reacts to a window's desktop or activity set changing.
// KWin reports a move, a set that grew or shrank, a stick and an un-stick
// through one signal per axis; each is classified against the window's stamp
// (WindowContextEdge::classify) and handed to applyWindowContextEdit, whose
// arms depend only on that edge and on the window's tracking state, so both
// axes share them. Its own translation unit because window_connections.cpp is
// over the file-size ceiling.
void PlasmaZonesEffect::wireContextChangeHandlers(KWin::EffectWindow* w)
{
    // Seed the stamp so the window's very first edit is classifiable. The
    // windowDeleted cleanup erases it alongside m_trackedScreenPerWindow.
    WindowContextEdge::Stamp& seed = m_contextStampPerWindow[w];
    seed.desktops = desktopIdsOf(w);
    seed.activities = activityIdsOf(w);

    connect(w, &KWin::EffectWindow::windowDesktopsChanged, this, [this](KWin::EffectWindow* window) {
        // Corpse gate FIRST, ahead of everything. getWindowId on a deleted
        // window pollutes the id cache, and updateWindowStickyState, the
        // metadata push and the arms call it (the stamp is a raw-pointer
        // lookup and does not). Nothing downstream wants a corpse's answer
        // either: its stamp is erased by the windowDeleted cleanup whether or
        // not this edit wrote it, and its sticky value has no consumer.
        if (!window || window->isDeleted()) {
            return;
        }
        updateWindowStickyState(window);
        // The registry, and the daemon's per-window membership reconcile it
        // drives (the tiling per-key release and in-view adopt, the snap
        // carry), must see the new desktop set before any notice this handler
        // sends, so the push leads (F293). The sticky report goes first
        // because the reconcile's autotile adopt reads it. This is the only
        // push for a desktop edit.
        pushWindowMetadata(window);
        // The arms below adopt or drain by the exclusion verdict, which must
        // be judged on the desktop the window moved to.
        evictExclusionVerdicts(getWindowId(window));
        // Re-stamp before any arm runs, so the next edit diffs against this one.
        const auto stampIt = m_contextStampPerWindow.find(window);
        const bool hadPrevious = stampIt != m_contextStampPerWindow.end();
        WindowContextEdge::Stamp& stamp = hadPrevious ? *stampIt : m_contextStampPerWindow[window];
        const WindowContextEdge::IdSet previous = stamp.desktops;
        stamp.desktops = desktopIdsOf(window);
        // Measured against the desktop the window's OWN output is showing, not
        // the session-wide current one (desktopvisibility.h).
        const KWin::VirtualDesktop* shown = desktopShownOn(window);
        applyWindowContextEdit(window,
                               WindowContextEdge::classify(previous, hadPrevious, stamp.desktops,
                                                           shown ? shown->id() : QString(),
                                                           window->isOnCurrentActivity()));
    });

    // The activity axis, through the same arms: a move to another activity
    // leaves the old layout and joins the one in view (F549), and a stick
    // into "every activity" settles a snap park (F295). KWin's EffectWindow
    // relays no activity signal, so this hangs off the KWin::Window, ahead of
    // the rule invalidation window_metadata_connections.cpp queues for it.
    if (KWin::Window* const kw = w->window()) {
        const QPointer<KWin::EffectWindow> safeW = w;
        connect(kw, &KWin::Window::activitiesChanged, this, [this, safeW]() {
            KWin::EffectWindow* const window = safeW.data();
            if (!window || window->isDeleted()) {
                return;
            }
            pushWindowMetadata(window);
            evictExclusionVerdicts(getWindowId(window));
            const auto stampIt = m_contextStampPerWindow.find(window);
            const bool hadPrevious = stampIt != m_contextStampPerWindow.end();
            WindowContextEdge::Stamp& stamp = hadPrevious ? *stampIt : m_contextStampPerWindow[window];
            const WindowContextEdge::IdSet previous = stamp.activities;
            stamp.activities = activityIdsOf(window);
            applyWindowContextEdit(
                window,
                WindowContextEdge::classify(previous, hadPrevious, stamp.activities,
                                            KWin::effects ? KWin::effects->currentActivity() : QString(),
                                            isOnOwnOutputCurrentDesktop(window)));
        });
    }
}

// The arms. "tracked" is the effect's own tiling bookkeeping for the window,
// "parked" a window a desktop or activity switch demoted (still held in its
// context's state), and "managed" whether the screen in view tiles. By the time this
// runs the daemon has released and adopted per key off the metadata the
// handler pushed first, so every arm only settles the effect's side, and the
// one release it relays is a genuine departure's.
void PlasmaZonesEffect::applyWindowContextEdit(KWin::EffectWindow* window, const WindowContextEdge::Edge& edge)
{
    using WindowContextEdge::Kind;
    if (edge.kind == Kind::Unclassifiable) {
        return;
    }
    const QString windowId = getWindowId(window);
    const QString screenId = getWindowScreenId(window);
    TilingHandler* const tiling = m_tilingHandler.get();
    const bool tracked = tiling->isTrackedWindow(windowId);
    const bool parked = tiling->isParkedForDesktopReturn(windowId);
    const bool managed = tiling->isManagedScreen(screenId);
    // A window that became present on every desktop is in view now, whatever
    // the edge: an open's continuation is drained, any other snap park is
    // cancelled, since the window never moved (F295).
    if (edge.becameEverywhere && m_snapHandler) {
        m_snapHandler->settleDesktopArrivalOnEverywhere(windowId, window);
    }

    switch (edge.kind) {
    case Kind::Unclassifiable:
        return;
    case Kind::StayedHidden:
        // Between desktops nobody is looking at. A window a desktop switch
        // parked is still held on the desktop it left, and the reconcile has
        // released that key, so the park goes: the catch-scan re-announces it
        // on the destination's first view, where the stash is folded back
        // (F563). A set that only grew keeps every key it held.
        if (parked && edge.leftAnId) {
            tiling->savePreTileForDesktopMove(windowId);
            tiling->cleanupAutotileTracking(windowId);
        }
        return;
    case Kind::Departed:
        if (tracked) {
            // Left the desktop in view. The free geometry is stashed before
            // the tracking wipe so the re-add on the destination folds it back.
            // Only a genuine move relays a release, and after the metadata it
            // finds nothing the reconcile left. Unchecking the desktop in view,
            // or an un-stick onto a hidden desktop, keeps the window's other
            // keys, which a relay would take (F399, F581).
            tiling->savePreTileForDesktopMove(windowId);
            if (edge.genuineMove) {
                tiling->releaseWindowTracking(windowId, screenId);
            } else {
                tiling->cleanupAutotileTracking(windowId);
            }
            // Title-bar state is rule-driven: KWin's off-desktop noBorder reset
            // is corrected on return by updateAllDecorations.
            removeWindowDecoration(windowId);
            qCInfo(lcEffect) << "Window left the context in view, untracked:" << windowId;
        } else if (managed) {
            removeWindowDecoration(windowId);
        }
        return;
    case Kind::Arrived: {
        if (!managed) {
            // Onto a desktop in view that does not tile. A window the daemon
            // held tiled where it came from was released there by the
            // reconcile; this is the effect's half of that departure. The
            // stash is taken before the tracking wipe so a later move back
            // onto a tiled desktop folds it in, the release is effect-only on
            // this screen, and the decoration is re-resolved after it, because
            // no desktop switch will rebuild it. A snapping arrival is placed
            // by the daemon's membership carry (#1121); the drain only spends
            // a park the window carried here. A window that became sticky was
            // settled above and still holds its keys elsewhere (F520).
            if (!edge.becameEverywhere) {
                tiling->savePreTileForDesktopMove(windowId);
                tiling->releaseWindowTracking(windowId, screenId);
                reconcileDecorationOnPlacementFlip(windowId);
                // A move from a tiling desktop is owed its free placement,
                // paid before the drain, whose park the pay skips (F364, F414).
                if (edge.genuineMove) {
                    tiling->payOwedFreePlacement(window, windowId, screenId);
                }
                if (m_snapHandler) {
                    m_snapHandler->drainDesktopArrivalFor(windowId, window);
                }
            }
            return;
        }
        // Onto a tiled desktop in view. A snap park is spent (the drain
        // answers false on a managed screen).
        if (m_snapHandler) {
            m_snapHandler->drainDesktopArrivalFor(windowId, window);
        }
        // The reconcile carries a held tile here without reading any rule. A
        // rule excluding the window on this desktop lets it go (F292).
        if (isExcludedBySnappingRule(window)) {
            tiling->releaseWindowTracking(windowId, screenId);
            reconcileDecorationOnPlacementFlip(windowId);
            qCInfo(lcEffect) << "Window moved into the context in view is excluded there, released:" << windowId;
            return;
        }
        // An arrival the user is taken to: a move onto the desktop in view, or
        // a window shown on every desktop from another one. A set that only
        // grew onto it is placed by the reconcile and not focused.
        const bool focusArrival = edge.genuineMove || edge.becameEverywhere;
        // Load-bearing (F352): the reconcile adopts a held window into the
        // desktop in view, so one the effect still tracks (a switch between
        // two desktops that both tile demotes nothing) needs an announce only
        // to be focused.
        if (tracked && !focusArrival) {
            return;
        }
        // Parked or tracked on the desktop it came from, or held by no tiling
        // engine. The free geometry is stashed before the effect-only teardown
        // wipes it (F933), the tracking goes WITHOUT a relay, which would take
        // back the adoption the reconcile just made (F563, F717), and the
        // window is announced: a held one is focused by "Focus new windows"
        // (F583), a fresh one is inserted. knownFreeFloating=false because the
        // frame is likely the source desktop's tile rect.
        tiling->savePreTileForDesktopMove(windowId);
        tiling->cleanupAutotileTracking(windowId);
        tiling->restorePreTileForDesktopMove(windowId, screenId);
        if (tiling->notifyWindowAdded(window, /*knownFreeFloating=*/false, focusArrival)) {
            qCInfo(lcEffect) << "Window moved into the context in view, added to autotile:" << windowId;
        } else {
            qCDebug(lcEffect) << "Window moved into the context in view, not tiled:" << windowId;
        }
        return;
    }
    case Kind::StayedInView:
        if (edge.becameEverywhere) {
            // Stuck from the desktop in view: still placed where it is.
            return;
        }
        if (edge.leftEverywhere && managed) {
            // Un-stuck onto the desktop in view. The engines hold a sticky
            // window on every desktop it was seen on, so it keeps its key here
            // and its slot (F410). Only a window no engine took, a sticky one
            // under RestoreOnly, is announced now, as the re-placement it is.
            if (!tracked && !parked) {
                tiling->notifyWindowAdded(window, /*knownFreeFloating=*/false, /*focusEligible=*/false);
            }
            return;
        }
        if (!managed && tracked && edge.onlyInView) {
            // Shrunk to the non-tiling desktop in view alone: the reconcile
            // released every tiled key it held, so the effect lets go too
            // (F553).
            tiling->savePreTileForDesktopMove(windowId);
            tiling->cleanupAutotileTracking(windowId);
            reconcileDecorationOnPlacementFlip(windowId);
        }
        // Any other edit in view (a set that grew or shrank) is re-keyed per
        // key by the reconcile.
        return;
    }
}

} // namespace PlasmaZones
