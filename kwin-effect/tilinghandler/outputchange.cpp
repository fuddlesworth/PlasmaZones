// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Cross-output transfer: everything that happens when a tracked window's
// KWin output changes — daemon-driven handoff echoes, the autotile↔autotile
// re-add, and the autotile→snapping size restore.

#include "tilinghandler.h"

#include "plasmazoneseffect/plasmazoneseffect.h"
#include "handlers/snaphandler.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <workspace.h>

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QPointer>
#include <QScopeGuard>

#include <algorithm>
#include <memory>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

void TilingHandler::handleWindowOutputChanged(KWin::EffectWindow* w)
{
    if (!w || w->isDeleted() || m_inOutputChanged) {
        return;
    }
    // Re-entrancy guard: geometry changes from onWindowClosed/notifyWindowAdded
    // could trigger outputChanged again if tiling moves the window across screens.
    m_inOutputChanged = true;
    const auto guard = qScopeGuard([this] {
        m_inOutputChanged = false;
    });

    const QString windowId = m_effect->getWindowId(w);
    const QString newScreenId = m_effect->getWindowScreenId(w);

    if (!m_notifiedWindows.contains(windowId)) {
        // A cross-mode SWAP arms windowOutputMoveExpected for the snap
        // partner migrating onto this source screen. That partner is
        // untracked effect-side, so its outputChanged lands here — its
        // arrival IS the marker's expected echo, so consume the one-shot
        // UNCONDITIONALLY (an ineligible arrival — minimized, off-desktop,
        // unmanaged screen — still IS the echo; leaving the marker armed
        // would swallow the window's next genuine outputChanged).
        m_expectedOutputMove.remove(windowId);
        // Window not tracked — but if it moved TO an autotile screen, add
        // it. The daemon already placed it via handoffReceive; the
        // notifyWindowAdded below establishes the effect-side tracking the
        // daemon does not touch, and is a no-op daemon-side (insertWindow
        // rejects an already-tracked window).
        if (m_managedScreens.contains(newScreenId) && m_effect->shouldHandleWindow(w) && !w->isMinimized()
            && w->isOnCurrentDesktop() && w->isOnCurrentActivity()) {
            // knownFreeFloating only when the border state does NOT already
            // track the window as tiled: the handoffReceive that placed it has
            // its frame sitting in the destination zone rect, and passing true
            // would push that rect as the daemon's free/float-back geometry.
            notifyWindowAdded(w, /*knownFreeFloating=*/!TilingStateHelpers::isTiledWindow(m_border, windowId));
            m_effect->updateAllDecorations();
        }
        return;
    }

    const QString oldScreenId = m_notifiedWindowScreens.value(windowId);

    // Daemon-driven handoff FIRST, and by POSITION. For a window tiled on a
    // scrolling screen, getWindowScreenId answers from the very map
    // oldScreenId was read from (the engine-authoritative override), so the
    // old-vs-new diff below is an identity for scroll windows and can never
    // route a scroll handoff — the marker is the only reliable signal.
    // Positional match alone is NOT sufficient: on a two-output layout a
    // strip parking a column onto the neighbour lands its centre exactly
    // where an armed handoff points. The second conjunct — the window must
    // no longer be scroll-TILED on the source — separates the two: a real
    // handoff's release-driven tile update (FIFO-ordered ahead of the
    // geometry echo) has already emptied the source bucket, while a
    // parking hop's window is still tiled there, so the marker survives
    // for the genuine echo and the hop falls through to the same-screen
    // return below. The source screen comes from the marker, not from
    // oldScreenId: the move's tile requests land before this echo and
    // pre-seed m_notifiedWindowScreens with the DESTINATION, so oldScreenId
    // here would name the destination bucket and the test would always say
    // "still tiled". The daemon puts the source on the wire for the arm
    // sites that run after the handoff, which are the ones where even the
    // marker's own arm-time snapshot would have been the destination.
    if (const auto expIt = m_expectedOutputMove.constFind(windowId); expIt != m_expectedOutputMove.constEnd()) {
        const QPointF cf = w->frameGeometry().center();
        const QString positional =
            m_effect->resolveEffectiveScreenId(QPoint(qRound(cf.x()), qRound(cf.y())), m_effect->windowOutput(w));
        const QString sourceScreenId = expIt.value().sourceScreenId;
        const bool stillTiledOnSource = m_scrollingScreens.contains(sourceScreenId)
            && m_border.tiledWindowsByScreen.value(sourceScreenId).contains(windowId);
        if (expIt.value().targetScreenId == positional && !stillTiledOnSource) {
            m_expectedOutputMove.erase(expIt);
            if (m_managedScreens.contains(positional)) {
                m_notifiedWindowScreens[windowId] = positional;
            } else if (m_managedScreens.contains(oldScreenId)) {
                // Cross-MODE move: window left autotile. Drop effect-side
                // autotile tracking (daemon already relinquished via
                // handoffRelease) — else it lingers phantom.
                cleanupAutotileTracking(windowId, oldScreenId);
            } else {
                // scroll↔snap / scroll↔scroll: keep the record current so
                // later diffs measure from the true screen.
                m_notifiedWindowScreens[windowId] = positional;
            }
            m_effect->updateAllDecorations();
            return;
        }
    }

    if (oldScreenId.isEmpty() || oldScreenId == newScreenId) {
        // Belt-and-braces drains for a marker whose move never
        // materialised (refused receive, window closed mid-move):
        //  - record already equals the marked destination → no echo left;
        //  - window is not a parked strip tile → a same-screen settle
        //    means the marked move to a THIRD screen never happened (a
        //    scroll window is exempt: its parking hops legitimately pass
        //    through here while the marker waits for the real echo).
        // Either way, drop the one-shot rather than let it swallow a later
        // genuine move's notification.
        if (const auto expIt = m_expectedOutputMove.constFind(windowId); expIt != m_expectedOutputMove.constEnd()) {
            if (expIt.value().targetScreenId == newScreenId || scrollTrackedScreenFor(windowId).isEmpty()) {
                m_expectedOutputMove.erase(expIt);
            }
        }
        return; // Same screen or unknown — no transfer needed
    }

    const bool oldIsAutotile = m_managedScreens.contains(oldScreenId);
    const bool newIsAutotile = m_managedScreens.contains(newScreenId);

    if (!oldIsAutotile && !newIsAutotile) {
        // Neither screen is autotiled — snapping unsnap is handled by the
        // effect's outputChanged. Still refresh the notified-screen record:
        // a window that left autotile via the desktop-switch float skip
        // stays tracked, and a stale screen here would make every later
        // frameGeometryChanged re-detect a phantom VS crossing.
        if (m_notifiedWindowScreens.contains(windowId)) {
            m_notifiedWindowScreens[windowId] = newScreenId;
        }
        return;
    }

    // A marker surviving to this point is SUPERSEDED: the marker-first
    // block above already consumed every daemon-owned echo (for non-scroll
    // windows newScreenId is byte-identical to its positional resolve, so
    // a matching destination cannot reach here), which leaves only a
    // marker for a move that a later genuine user move overtook. Drop it
    // and run the normal transfer.
    m_expectedOutputMove.remove(windowId);

    qCInfo(lcEffect) << "Window moved between monitors:" << windowId << oldScreenId << "->" << newScreenId;

    // Snapshot the pre-autotile geometry BEFORE onWindowClosed clears it
    // (the close-cleanup sweeps the geometry out of EVERY screen bucket).
    QRectF savedPreAutotileGeo;
    QString savedPreAutotileBucket;
    if (oldIsAutotile) {
        savedPreAutotileGeo = findPreTileGeometry(windowId, &savedPreAutotileBucket);
    }

    // The predicate mirrors the re-add condition below.
    const bool willReAdd = newIsAutotile && !w->isMinimized() && w->isOnCurrentDesktop() && w->isOnCurrentActivity();

    // Minimize-float ownership must SURVIVE the transfer: onWindowClosed's
    // cleanup wipes it wholesale, the window is alive and still floated
    // daemon-side, and willReAdd is false for a minimized window — without
    // re-establishing the claim (or handing it to the snap handler for a
    // snap destination) nobody ever unfloats the window and it stays
    // floating until the next mode toggle. Reachable in bulk on monitor
    // hotplug and VS reconfigure.
    // isMinimizeFloated covers the in-flight unfloat interval too — a window
    // crossing screens mid-unfloat is still owned, and letting the cleanup
    // below drop it without re-establishing the claim strands it floating.
    const bool ownedMinimizeFloat = isMinimizeFloated(windowId);
    const bool wasUntiledMinimizeFloat = m_untiledMinimizeFloats.contains(windowId);
    // The in-flight half of that ownership, snapshotted separately: a window
    // mid-unfloat has already been unminimized, so isMinimized() is FALSE for
    // it and the re-establish below would skip exactly the case the comment
    // claims to cover, leaving the daemon's floating echo without an owner.
    const bool wasUnfloatInFlight = hasUnfloatInFlight(windowId);
    // Snapshot the retry budget too: cleanupAutotileTracking erases it, and
    // the header documents that the budget must survive the ownership hop
    // (a persistently-refused unfloat must not reset to a fresh budget on
    // every cross-screen bounce).
    const int savedUnfloatBudget = unfloatRetryBudgetUsed(windowId);

    // Remove from old screen's autotile state
    onWindowClosed(windowId, oldScreenId);

    if (ownedMinimizeFloat && (w->isMinimized() || wasUnfloatInFlight)) {
        if (newIsAutotile) {
            m_minimizeFloatedWindows.insert(windowId);
            if (wasUntiledMinimizeFloat) {
                m_untiledMinimizeFloats.insert(windowId);
            }
            seedUnfloatRetryBudget(windowId, savedUnfloatBudget);
            if (wasUnfloatInFlight) {
                // The in-flight request named the OLD screen, and its watcher
                // was orphaned by the cleanup above. Re-issue against the new
                // screen: the fresh generation this takes retires the old
                // watcher through its own generation guard, and the window
                // lands back in m_unfloatInFlight rather than being demoted to
                // a plain marker its completion path can no longer find.
                dispatchUnminimizeUnfloat(windowId, newScreenId);
            }
            qCInfo(lcEffect) << "Autotile: minimize-float ownership carried across screens:" << windowId << "->"
                             << newScreenId;
        } else if (SnapHandler* snap = m_effect->snapHandler()) {
            // Snap destination: hand the record over so the unminimize edge
            // on that screen finds an owner (mirrors the deferred-commit
            // transfer in the unfloat grace path). The untiled marker is
            // deliberately NOT carried: snap has no untiled fast path, so an
            // adopted window whose rect belongs to the prior mode takes
            // snap's normal deferred grace and may visibly hop once at the
            // unminimize — accepted over teaching snap a second commit path
            // for this rare cross-screen-while-minimized case.
            // An in-flight unfloat is deliberately DEMOTED to a plain marker
            // here: snap's adopt API takes ownership of the marker only, and
            // the abandoned watcher's generation guard is this handler's, so
            // snap re-drives the unfloat from its own unminimize edge.
            snap->adoptMinimizeFloated(windowId);
            snap->seedUnfloatRetryBudget(windowId, savedUnfloatBudget);
            qCInfo(lcEffect) << "Autotile: minimize-float ownership handed to snap on screen change:" << windowId
                             << "->" << newScreenId;
        }
    }

    if (willReAdd) {
        // Re-add on new autotile screen, carrying over pre-autotile geometry.
        // Cancel any pending cross-screen restore — the window is back in autotile.
        auto pendingIt = m_pendingCrossScreenRestore.find(windowId);
        if (pendingIt != m_pendingCrossScreenRestore.end()) {
            QObject::disconnect(pendingIt.value());
            m_pendingCrossScreenRestore.erase(pendingIt);
        }
        if (savedPreAutotileGeo.isValid()) {
            // Re-filed under the CAPTURE screen's key, not the destination's.
            // The bucket key names the rect's coordinate space (see
            // savePreTileForDesktopMove), and this rect was measured on the old
            // monitor — keying it to the new one would make a later cross-screen
            // desktop move accept a rect it exists to decline. findPreTileGeometry
            // scans every bucket, so the restore path finds it either way.
            m_preTileGeometries[savedPreAutotileBucket][windowId] = savedPreAutotileGeo;
        }
        // RE-ADD: a tiled window's current frame is its zone rect on the old
        // screen — knownFreeFloating=false lets the floating guard reject it
        // (a genuinely floating window passes the guard and keeps its float
        // geometry captured). Without this the tiled rect would be pushed with
        // overwrite=true and destroy the daemon's persisted free-back.
        // Re-add on the new autotile screen. Eligibility drift (e.g. the tiled
        // frame now sits below the user's min-size threshold) or a pending
        // close can filter it locally — a no-op then, the daemon's tile path
        // owns any follow-up.
        notifyWindowAdded(w, /*knownFreeFloating=*/false);
    } else if (oldIsAutotile && !newIsAutotile) {
        // Autotile → snapping: restore the window's original (pre-snap/pre-tile)
        // SIZE after the drag ends.  The effect-side m_preTileGeometries may
        // hold the snap zone geometry (if the window was snapped before entering
        // autotile), so we ask the daemon for the true pre-tile geometry instead.
        // If unavailable, fall back to the effect-side cache.
        QPointer<KWin::EffectWindow> safeW = w;
        const QString wid = windowId;

        // Cancel any prior pending restore for this window (rapid back-and-forth)
        auto oldIt = m_pendingCrossScreenRestore.find(windowId);
        if (oldIt != m_pendingCrossScreenRestore.end()) {
            QObject::disconnect(oldIt.value());
            m_pendingCrossScreenRestore.erase(oldIt);
        }

        // Fetch the daemon's pre-tile geometry (original size before any snapping).
        // The effect-side savedPreAutotileGeo is kept as fallback.
        const int fallbackW = savedPreAutotileGeo.isValid() ? std::max(0, qRound(savedPreAutotileGeo.width())) : 0;
        const int fallbackH = savedPreAutotileGeo.isValid() ? std::max(0, qRound(savedPreAutotileGeo.height())) : 0;

        auto* watcher = new QDBusPendingCallWatcher(
            PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("getValidatedPreTileGeometry"), {windowId}),
            m_effect);
        // Context is `this` (the handler), not m_effect: the lambda captures and
        // dereferences TilingHandler members (m_managedScreens,
        // m_pendingCrossScreenRestore), and the handler is a unique_ptr member of
        // the effect destroyed before its sibling watcher. A `this` context auto-
        // disconnects the callback when the handler dies, matching the sibling
        // watchers above.
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, safeW, wid, fallbackW, fallbackH](QDBusPendingCallWatcher* pw) {
                    pw->deleteLater();

                    int restoreW = fallbackW;
                    int restoreH = fallbackH;
                    QDBusPendingReply<bool, int, int, int, int> reply = *pw;
                    // No arity term: count() is compile-time constant 5 (see
                    // requestDaemonPreTileRestore) — isValid + the success
                    // flag + the positive-extent checks below are the guard.
                    if (reply.isValid() && reply.argumentAt<0>()) {
                        int dw = reply.argumentAt<3>();
                        int dh = reply.argumentAt<4>();
                        if (dw > 0 && dh > 0) {
                            restoreW = dw;
                            restoreH = dh;
                        }
                    }

                    if (restoreW <= 0 || restoreH <= 0 || !safeW || safeW->isDeleted()) {
                        return;
                    }

                    // If the window bounced back to an autotile screen during the
                    // D-Bus round-trip, skip the restore — it's being tiled again.
                    const QString currentScreen = m_effect->getWindowScreenId(safeW);
                    if (m_managedScreens.contains(currentScreen)) {
                        return;
                    }

                    // If the drag already ended, apply immediately — unless the
                    // window was snapped to a zone by dragStopped during this D-Bus
                    // round-trip. In that case, zone geometry is already correct.
                    if (!safeW->isUserMove() && !safeW->isUserResize()) {
                        if (!m_effect->isWindowFloating(wid)) {
                            return; // Snapped to zone — don't clobber zone geometry
                        }
                        const QRectF frame = safeW->frameGeometry();
                        const QRect geo(qRound(frame.x()), qRound(frame.y()), restoreW, restoreH);
                        // Snap-out: the window is leaving tile-managed sizing.
                        m_effect->applyWindowGeometry(safeW, geo, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                                                      PhosphorAnimation::ProfilePaths::WindowSnapOut);
                        return;
                    }

                    // Still dragging — wait for drop, then restore size once.
                    // Use a shared connection so the lambda can disconnect itself
                    // after firing once — preventing subsequent user resizes from
                    // snapping the window back to the pre-autotile size.
                    auto sharedConn = std::make_shared<QMetaObject::Connection>();
                    // Context is `this` (the handler), not m_effect: the lambda
                    // mutates m_pendingCrossScreenRestore, so the connection must
                    // die with the handler.
                    *sharedConn = connect(safeW.data(), &KWin::EffectWindow::windowFinishUserMovedResized, this,
                                          [this, safeW, wid, restoreW, restoreH, sharedConn](KWin::EffectWindow*) {
                                              // Disconnect immediately so this only fires once (the drop),
                                              // not on every subsequent user resize.
                                              QObject::disconnect(*sharedConn);
                                              m_pendingCrossScreenRestore.remove(wid);
                                              if (!safeW || safeW->isDeleted()) {
                                                  return;
                                              }
                                              // Guard: window may have bounced back to autotile during drag.
                                              const QString dropScreen = m_effect->getWindowScreenId(safeW);
                                              if (m_managedScreens.contains(dropScreen)) {
                                                  return;
                                              }
                                              // Guard: window may have been snapped to a zone by dragStopped.
                                              if (!m_effect->isWindowFloating(wid)) {
                                                  return;
                                              }
                                              const QRectF frame = safeW->frameGeometry();
                                              const QRect geo(qRound(frame.x()), qRound(frame.y()), restoreW, restoreH);
                                              // Snap-out: leaving tile-managed sizing.
                                              m_effect->applyWindowGeometry(
                                                  safeW, geo, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                                                  PhosphorAnimation::ProfilePaths::WindowSnapOut);
                                          });
                    m_pendingCrossScreenRestore[wid] = *sharedConn;
                });

        // NOTE: Do NOT call windowUnsnapped here. The drag-drop handler
        // (WindowDragAdaptor::dragStopped) manages the zone assignment transition.
        // Firing windowUnsnapped now would race with the snap-to-zone D-Bus call
        // from the drop handler, destroying the zone assignment that was just created.
        // The drop handler's cross-screen path already clears stale state.

        // Raise above existing windows so it doesn't end up buried behind
        // snapped windows on the target screen.
        KWin::Window* kw = w->window();
        if (kw) {
            auto* ws = KWin::Workspace::self();
            if (ws) {
                ws->raiseWindow(kw);
            }
        }
    }

    m_effect->updateAllDecorations();
}

} // namespace PlasmaZones
