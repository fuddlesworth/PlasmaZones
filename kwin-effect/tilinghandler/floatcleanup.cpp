// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Tiling eligibility, float shed, and tiled-membership bookkeeping for
// TilingHandler.
//
// Split out of state.cpp by concern. These answer one question between them:
// which windows the engine is allowed to own, and what has to be undone when it
// stops owning one. The eligibility predicate is the gate on the way in; the two
// float paths are the way out, one for the active channel and one for the
// passive WindowTracking broadcast; markWindowTiled and its clears are the
// membership those paths maintain. Membership changes flip the IsTiled rule
// field, so every mutation here is change-gated and invalidates the per-window
// rule cache only on a genuine transition.

#include "tilinghandler.h"
#include "handlers/navigationhandler.h"
#include "handlers/snaphandler.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "compositor/effectlogging.h"

#include <effect/effectwindow.h>
#include <window.h>

#include <QLoggingCategory>
#include <QPointer>
#include <QScopeGuard>
#include <QTimer>

namespace PlasmaZones {

bool TilingHandler::isEligibleForTilingNotify(KWin::EffectWindow* w, bool* rejectedOnlyBecauseMinimized) const
{
    if (rejectedOnlyBecauseMinimized) {
        *rejectedOnlyBecauseMinimized = false;
    }
    // Null first, so the gates below need no per-gate `w &&` prefix.
    if (!w) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (null window)";
        return false;
    }
    // Close-grabbed dying windows survive in the stacking order for the
    // close-animation duration; announcing one as opened would insert an
    // orphan into the tiling tree (shrinking live tiles) until a later
    // retile cleans it up.
    if (w->isDeleted()) {
        return false;
    }
    // Early-out: KWin internal surfaces (overlay QQuickViews, zone overlays, etc.)
    // are never eligible for autotile notification. KWin's InternalWindow::minSize()
    // segfaults when the backing QWindow is null. See discussion #511.
    if (w->window() && w->window()->isInternal()) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (internal window)" << m_effect->getWindowId(w);
        return false;
    }
    // Compute the scrolling windowed-fullscreen exemption BEFORE the
    // handleable check and thread it through: shouldHandleWindow's structural
    // fullscreen reject would otherwise fire here, 30 lines before this
    // function's own fullscreen clause below, and at effect bring-up (empty
    // membership hash) that made the documented re-adoption of a flagged
    // column unreachable — the window was never announced, so no batch could
    // ever restore membership. Mirrors notifyWindowActivated's exemption.
    // REQUESTED or committed. EffectWindow::isFullScreen() is the committed
    // bit, which lags a client round-trip on Wayland — so a window the
    // OpenFullscreen rule just fullscreened (the flip writes the requested bit
    // synchronously at windowAdded, decoration_rules.cpp) read as NOT
    // fullscreen here and was announced, pushing the about-to-be-fullscreen
    // frame to the daemon as free geometry with overwrite=true, which the
    // free-geometry guard's own contract forbids. The union closes that window
    // and costs nothing elsewhere: the exit path re-announces on the committed
    // exit signal, by which point neither bit is set.
    KWin::Window* kwFs = w->window();
    const bool fullScreen = w->isFullScreen() || (kwFs && kwFs->isRequestedFullScreen());
    const bool fullscreenOnScrollingScreen = fullScreen && isScrollingScreen(m_effect->getWindowScreenId(w));
    if (!m_effect->shouldHandleWindow(w, nullptr, /*exemptFullscreen=*/fullscreenOnScrollingScreen)) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (not handleable)" << m_effect->getWindowId(w);
        return false;
    }
    if (!m_effect->isTileableWindow(w)) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (not tileable)" << m_effect->getWindowId(w);
        return false;
    }
    // A window that is fullscreen at first contact (opened fullscreen, or
    // present-fullscreen when autotile is enabled / the daemon restarts):
    // KWin owns its geometry and re-asserts the fullscreen frame, so
    // announcing it would (a) push the fullscreen frame as free geometry
    // with overwrite=true and (b) make the daemon try to tile a window KWin
    // won't let move. The exit-fullscreen slot re-announces it via
    // notifyWindowAdded once it returns to a normal frame.
    //
    // Exempt: a scrolling WINDOWED-FULLSCREEN window. Its fullscreen state
    // is the effect's own doing and its geometry is the column rect the
    // daemon still owns, so the rationale above does not apply — and a
    // re-announce (screen churn, effect bring-up) must not silently drop it.
    //
    // The membership half of that exemption cannot fire at effect BRING-UP:
    // the hash fills from the daemon's batches, and the announce is what
    // triggers the batch. So a SCROLLING screen exempts fullscreen windows
    // wholesale — a restarted effect re-announces a flagged window, the
    // daemon's stash claim re-flags it, and the adopt-on-batch arm restores
    // membership. A genuinely fullscreen window announced this way is the
    // acceptable half of the trade: the strip tiles it behind the
    // fullscreen surface (the daemon never untiles on fullscreen anyway),
    // the geometry apply bails on its requested state, and its exit lands
    // in an already-consistent strip instead of a never-announced limbo.
    // Snapping/autotile screens keep the reject in full.
    // The fullscreen term first so the overwhelmingly common non-fullscreen
    // window pays neither the id lookup nor the screen resolve. Reuses the
    // requested-OR-committed answer computed above — see its comment for why
    // the committed bit alone is not enough — and the scrolling-screen answer
    // already resolved into fullscreenOnScrollingScreen, which is exactly this
    // term inside the `fullScreen &&` short-circuit. Recomputing it here paid
    // getWindowScreenId (a scroll-tracking lookup) twice for every fullscreen
    // window.
    if (fullScreen
        && !(m_effect->m_windowedFullscreenWindows.contains(m_effect->getWindowId(w)) || fullscreenOnScrollingScreen)) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (fullscreen)" << m_effect->getWindowId(w);
        return false;
    }
    if (!w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (wrong desktop/activity)" << m_effect->getWindowId(w);
        return false;
    }
    // Reject windows smaller than the user-configured minimum size.
    // Prevents small utility windows (emoji picker, color picker, etc.)
    // from entering the tiling tree and disrupting the layout.
    const QRectF frame = w->frameGeometry();
    if ((m_effect->m_cachedMinWindowWidth > 0 && frame.width() < m_effect->m_cachedMinWindowWidth)
        || (m_effect->m_cachedMinWindowHeight > 0 && frame.height() < m_effect->m_cachedMinWindowHeight)) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (too small)" << m_effect->getWindowId(w)
                          << "size=" << frame.size() << "threshold=" << m_effect->m_cachedMinWindowWidth << "x"
                          << m_effect->m_cachedMinWindowHeight;
        return false;
    }
    // Checked LAST so the out-flag means "every other gate passed": the batch
    // announce claims such a window as minimize-floated (minimizedChanged
    // never fires for a window that was already minimized when its screen
    // entered autotile, so the runtime minimize→float path cannot cover it).
    // frameGeometry stays at the pre-minimize frame while minimized, so the
    // min-size gate above evaluates real dimensions for this ordering.
    if (w->isMinimized()) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (minimized)" << m_effect->getWindowId(w);
        if (rejectedOnlyBecauseMinimized) {
            *rejectedOnlyBecauseMinimized = true;
        }
        return false;
    }
    qCDebug(lcEffect) << "isEligibleForTilingNotify: accepted" << m_effect->getWindowId(w) << "size=" << frame.size()
                      << "class=" << w->windowClass() << "skipSwitcher=" << w->isSkipSwitcher()
                      << "keepAbove=" << w->keepAbove() << "transient=" << (w->transientFor() != nullptr);
    return true;
}

void TilingHandler::deferWindowRouting(KWin::EffectWindow* window, bool canSnapRestore)
{
    if (!window || window->isDeleted()) {
        return;
    }
    const QString windowId = m_effect->getWindowId(window);
    m_pendingFreshWindows.insert(windowId);
    m_deferredWindowRoutes.insert(windowId, DeferredWindowRoute{QPointer<KWin::EffectWindow>(window), canSnapRestore});
    // Zero-tick dispatch when no screen query is in flight: the defer is a
    // flags-settle turn, not a wait (see the routing block in
    // slotWindowAdded). When a query IS pending its finished handler owns
    // the dispatch — every arm of it calls completeDeferredWindowRoutes —
    // and a tick that was already armed before the query started finds an
    // empty route map and no-ops. The re-check inside the tick covers the
    // opposite race, a query starting between this schedule and the tick
    // firing.
    if (!m_initialScreenQueryPending && !m_deferredRouteDispatchScheduled) {
        m_deferredRouteDispatchScheduled = true;
        QTimer::singleShot(0, this, [this] {
            m_deferredRouteDispatchScheduled = false;
            if (m_initialScreenQueryPending) {
                return;
            }
            completeDeferredWindowRoutes();
        });
    }
}

void TilingHandler::reevaluateWindowEligibility(KWin::EffectWindow* w)
{
    if (!w || w->isDeleted()) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    // Only a window this handler actually announced is a candidate; for
    // everything else a flag flip stays free. Snap-owned windows are
    // deliberately out of scope: their capture window is closed by the
    // routing defer (resolveWindowRestore now runs post-settle), and a LATE
    // keep-above on a snapped window is dominated by the user raising it
    // from the title-bar menu, where an unsnap would fight them.
    if (!m_notifiedWindows.contains(windowId)) {
        return;
    }
    // The STRUCTURAL pair only, NOT isEligibleForTilingNotify: its desktop /
    // activity / minimized arms describe visibility, not eligibility, and
    // testing them here would evict a healthy tile on a plain desktop
    // switch. Both predicates consult the window's OWN keep-above
    // (windowOwnKeepAbove), so a SetWindowLayer-raised window does not
    // evict itself.
    //
    // Same scrolling-fullscreen exemption as isEligibleForTilingNotify: the
    // strip keeps tiling a window through real fullscreen, so a benign flag
    // edge (keep-above cleared, say) on a fullscreen scrolling tile must not
    // read shouldHandleWindow's structural fullscreen reject as an eviction
    // verdict.
    KWin::Window* kwFs = w->window();
    const bool fullScreen = w->isFullScreen() || (kwFs && kwFs->isRequestedFullScreen());
    const bool fullscreenOnScrollingScreen = fullScreen && isScrollingScreen(m_effect->getWindowScreenId(w));
    if (m_effect->shouldHandleWindow(w, nullptr, /*exemptFullscreen=*/fullscreenOnScrollingScreen)
        && m_effect->isTileableWindow(w)) {
        return;
    }
    const QString screenId = m_effect->getWindowScreenId(w);
    // Spawn frame read BEFORE the release: cleanupAutotileTracking (inside
    // releaseWindowTracking) clears the pre-tile bucket with the rest of
    // the tracking.
    const QRectF spawnGeo = preTileRestoreRectFor(windowId, screenId, w->frameGeometry());
    qCInfo(lcEffect) << "Flags-settle eviction:" << windowId << "no longer tileable, releasing from" << screenId;
    releaseWindowTracking(windowId, screenId);
    if (spawnGeo.isValid() && !w->isUserMove() && !w->isUserResize()) {
        // Same VS-crossing suppression bracket as every other effect-made
        // geometry write (save/restore, nesting-safe), and the same
        // tracked-screen re-seed the daemon pre-tile restore does after a
        // suppressed apply.
        const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
        m_effect->m_daemonGate.inGeometryApply = true;
        const auto restoreGate = qScopeGuard([this, prevInApply] {
            m_effect->m_daemonGate.inGeometryApply = prevInApply;
        });
        m_effect->applyWindowGeometry(w, spawnGeo.toRect(), /*allowDuringDrag=*/false, /*skipAnimation=*/true);
        m_effect->m_trackedScreenPerWindow[w] = m_effect->getWindowScreenId(w);
    }
    // The release means no daemon moveResize is coming, and the spawn restore
    // above lands the window back ON its suppression spawnGeometry — so the
    // geometry-settle release can never fire. Release first-frame suppression
    // here (no-op for an unsuppressed window) rather than letting an eviction
    // that raced the daemon's first tile keep the window invisible until the
    // 250 ms deadline.
    m_effect->endRestoreSuppression(w);
}

QSet<QString> TilingHandler::completeDeferredWindowRoutes()
{
    const auto routes = m_deferredWindowRoutes;
    m_deferredWindowRoutes.clear();
    QSet<QString> routedWindowIds;
    routedWindowIds.reserve(routes.size());
    for (auto it = routes.constBegin(); it != routes.constEnd(); ++it) {
        routedWindowIds.insert(it.key());
        KWin::EffectWindow* window = it->window.data();
        if (!window || window->isDeleted()) {
            m_pendingFreshWindows.remove(it.key());
            continue;
        }
        const QString windowId = m_effect->getWindowId(window);
        routedWindowIds.insert(windowId);
        // The pending-fresh entry was keyed by the id at defer time; if the
        // live id diverged, the old key would leak forever (the tail prune
        // below only drops dead/off-screen windows, and this window is
        // neither).
        if (windowId != it.key()) {
            m_pendingFreshWindows.remove(it.key());
        }
        // The defer exists so the eligibility filters run against SETTLED
        // flags, and the snap arms need that re-run as much as the tiling
        // one: it->canSnapRestore was computed at windowAdded time, before a
        // same-burst keep-above / skip-switcher landed, and acting on the
        // stale verdict would instant-restore (or daemon-resolve) a
        // now-excluded window into a zone. Recompute the structural pair the
        // slotWindowAdded predicate used — the tiling arm re-filters on its
        // own through isEligibleForTilingNotify either way.
        const bool canSnapRestore =
            it->canSnapRestore && m_effect->shouldHandleWindow(window) && m_effect->isTileableWindow(window);
        // The defer-time first-frame suppression was armed with the standard
        // deadline, but the screen query this dispatch waited on can outlast
        // it — re-arm (deadline only, no-op for unsuppressed windows) so the
        // window doesn't return to compositing at its centred spawn placement
        // between deadline expiry and the reposition below.
        m_effect->refreshRestoreSuppressionDeadline(window);
        // Consume (and maybe apply) the instant snap-restore cache entry —
        // a deferred window must not leave its entry alive for a later
        // same-app sibling to claim.
        // A teleport can move the window to another screen; re-resolve after.
        QString screenId = m_effect->getWindowScreenId(window);
        if (canSnapRestore && !window->isMinimized()
            && m_effect->tryInstantSnapRestore(window, windowId, /*canSnapRestore=*/true)) {
            screenId = m_effect->getWindowScreenId(window);
        }
        if (m_managedScreens.contains(screenId)) {
            if (window->isMinimized()) {
                // A window that minimized while the screen query was pending
                // is excluded from the follow-up batch (it is in
                // routedWindowIds), so nothing else will claim it — claim it
                // here, release the first-frame suppression (a minimized
                // window paints nothing, and leaving the suppression armed
                // stalls its eventual restore for the 250 ms deadline), and
                // drop the spawn-provenance marker so a later re-add cannot
                // inherit knownFreeFloating=true from a stale entry.
                // Empty filter: passing m_managedScreens duplicated the
                // claim's own internal autotile-screen gate verbatim.
                claimAlreadyMinimizedAsFloated(window, windowId, {}, /*enteringAutotile=*/true);
                m_pendingFreshWindows.remove(windowId);
                m_effect->endRestoreSuppression(window);
                continue;
            }
            if (canSnapRestore && m_effect->snapHandler()) {
                QPointer<KWin::EffectWindow> safeWindow = window;
                m_effect->snapHandler()->callResolveWindowRestore(
                    window,
                    [this, safeWindow, windowId](bool snapApplied) {
                        if (!safeWindow || safeWindow->isDeleted()) {
                            return;
                        }
                        if (!m_managedScreens.contains(m_effect->getWindowScreenId(safeWindow.data()))) {
                            m_pendingFreshWindows.remove(windowId);
                            m_effect->endRestoreSuppression(safeWindow.data());
                            return;
                        }
                        // knownFreeFloating only when the restore did NOT
                        // apply — a zone-placed window's live frame is the
                        // zone rect, not a genuine free frame.
                        if (!notifyWindowAdded(safeWindow.data(), /*knownFreeFloating=*/!snapApplied)
                            && !m_notifiedWindows.contains(windowId)) {
                            m_effect->endRestoreSuppression(safeWindow.data());
                        }
                    },
                    /*releaseSuppressionOnMiss=*/false);
            } else if (!notifyWindowAdded(window, /*knownFreeFloating=*/true)
                       && !m_notifiedWindows.contains(windowId)) {
                m_effect->endRestoreSuppression(window);
            }
            continue;
        }

        m_pendingFreshWindows.remove(it.key());
        m_pendingFreshWindows.remove(windowId);
        if (canSnapRestore && !window->isMinimized() && m_effect->snapHandler()) {
            m_effect->snapHandler()->callResolveWindowRestore(window);
        } else {
            m_effect->endRestoreSuppression(window);
        }
    }

    const auto pendingIds = m_pendingFreshWindows.values();
    for (const QString& windowId : pendingIds) {
        // EXACT resolve: the entry is keyed to a specific instance's id, so a
        // fuzzy hit on a same-app sibling must not keep a dead entry alive —
        // a retained stale entry later flips knownFreeFloating to true and
        // poisons the free-geometry capture.
        KWin::EffectWindow* window = m_effect->findWindowByIdExact(windowId);
        if (!window || window->isDeleted() || !m_managedScreens.contains(m_effect->getWindowScreenId(window))) {
            m_pendingFreshWindows.remove(windowId);
        }
    }
    return routedWindowIds;
}

void TilingHandler::applyFloatCleanup(const QString& windowId)
{
    // Windowed fullscreen dies on float (the engine clears its tile flag by
    // taking the window OUT of the strip, so no batch entry ever arrives to
    // un-flag it here) — drop the client's fullscreen state now or it stays
    // fullscreen-configured while free-floating.
    // Route through the shared release helper: it carries the isDeleted
    // check, the keep-flag restore, AND the inGeometryApply bracket that
    // setFullScreen(false) needs — on X11 it synchronously emits
    // windowFrameGeometryChanged, and no float call site holds the guard,
    // so an unbracketed drop re-enters the VS-crossing detector mid-cleanup.
    // Windowed fullscreen releases HERE rather than in the funnel call below,
    // and the split is deliberate. This one has to land before the geometry
    // work between the two, while the maximize claims have to land after it —
    // the comment at that call spells out why. Collapsing them into a single
    // call would move one compositor write across that work, which is a
    // behaviour change dressed as a cleanup.
    // Membership through forgetWindowedFullscreen like every other site, so
    // the ledger has one writer. The remove's RESULT is still the gate: the
    // compositor call must fire only when this pass is the one that owned the
    // claim, or a second cleanup for the same window would re-issue
    // setFullScreen(false) against a window that has since re-entered
    // fullscreen on its own.
    if (m_effect->m_windowedFullscreenWindows.contains(windowId)) {
        forgetWindowedFullscreen(windowId);
        releaseWindowedFullscreenState(windowId);
    }
    // The clear-in-flight marker dies with the hold, like the untrack
    // funnel and the snap↔snap belt drop it — a marker outliving the strip
    // membership can only refuse a future adopt (usually consumed by the
    // next flag-off entry, but a float means no batch entry ever arrives).
    m_windowedFsClearInFlight.remove(windowId);
    // A floating window is free to move itself — stop countering.
    m_effect->m_scrollCommandedRects.remove(windowId);
    m_effect->m_scrollOfferedColumn.remove(windowId);
    m_effect->m_navigationHandler->setWindowFloating(windowId, true);
    // A floating window is no longer tile-managed on any screen — clear tiled
    // tracking. clearWindowTiledAllScreens re-resolves the window's rules when the
    // tiled status flips, so a baseline border / title-bar rule scoped to tiled
    // windows stops drawing / hiding on the now-floating window (the setWindowFloating
    // above also re-resolves on the IsFloating flip; both coalesce).
    clearWindowTiledAllScreens(windowId);
    // Drop centering/target tracking too — a floated window isn't being
    // tiled anymore so a stale entry here would trigger centering on the
    // next frameGeometryChanged, snapping the floated window back into an
    // old zone rect. slotWindowsTileRequested no longer clears these
    // globally (it can't without wiping sibling-VS state), so the float
    // path has to clean up after itself.
    m_tileTargetZones.remove(windowId);
    m_centeredWaylandZones.remove(windowId);
    // And the parked-column paint hint, for the same reason and with a
    // sharper failure. A floating window is never a parked column, but the
    // float leaves the entry behind: floatWindowInternal takes the window OUT
    // of the strip, so the batch its own relayout emits does not contain it,
    // and the per-entry write in slotWindowsTileRequested never runs to clear
    // it. The orphan is inert only while BOTH halves of scrollManagedOutputFor
    // stay shut, and a later snap on another screen reopens both: the snap
    // adds tiled membership, and scrollTrackedScreenFor falls back to
    // m_notifiedWindowScreens — which this cleanup does not clear — so it
    // answers with the OLD scrolling screen. The paint pass for the window's
    // real output then skips it as belonging elsewhere, and the window is
    // simply not drawn. The removal changes where the paint path draws the
    // window, so it pairs with damage per m_scrollVisualDelta's contract (the
    // float paths have no guaranteed follow-up geometry apply).
    if (m_effect->m_scrollVisualDelta.remove(windowId) > 0 && KWin::effects) {
        KWin::effects->addRepaintFull();
    }
    // Geometry first, then the decoration funnel — the order every sibling
    // exit path uses: the monocle unmaximize is a geometry change, and
    // resolving the chain after it means the resolve sees the window's
    // final shape.
    // Both maximize claims, at THIS position rather than beside the windowed-
    // fullscreen release above: the monocle unmaximize is a geometry change,
    // and the decoration resolve below must see the window's final shape. The
    // windowed-fullscreen half already ran, so this call finds nothing left
    // for it — the scope still names it, because what a path releases should
    // not depend on which of its claims happened to be handled first.
    //
    // Both die with the float for the reason this file documents for the
    // sibling states: floatWindowInternal takes the window OUT of the strip,
    // so the batch its own relayout emits does not contain it and the
    // per-entry Release in the tile batch never runs. Left held, the window
    // stays KWin-maximized as a floater and a later re-tile resolves to None
    // instead of Apply, silently never re-asserting.
    releaseAllClaims(windowId, m_effect->findWindowByIdExact(windowId), ScrollDecisions::ClaimScope::StripExit);
    // Shared placement-flip funnel (update-or-remove in the same turn) —
    // the bare removal here left the float paths WITHOUT a bulk
    // updateAllDecorations follow-up (daemon auto-float past maxWindows)
    // undecorated until an unrelated refresh, the same drag-start blackout
    // the snap engine had. The tiled/floating facts were flipped above, so
    // the funnel resolves the floating-state chain.
    m_effect->reconcileDecorationOnPlacementFlip(windowId);
}

void TilingHandler::applyPassiveFloatShed(const QString& windowId)
{
    // The shed half of applyFloatCleanup, for the WindowTracking interface's
    // float channel (PlasmaZonesEffect::slotWindowFloatingChanged). That slot
    // receives floats from producers that never reach
    // TilingHandler::slotWindowFloatingChanged (the scroll passive channel's
    // windowFloatingStateSynced among them), so applyFloatCleanup never runs
    // for them and every one of these sheds was silently bypassed.
    // Deliberately EXCLUDES applyFloatCleanup's setWindowFloating: the passive
    // slot performs that write itself (daemon_apply.cpp, immediately before
    // this call), so repeating it here would only re-drive an idempotent
    // setter. unmaximizeMonocleWindow is excluded too, but on different
    // grounds — nothing on this channel covers it, and re-driving a maximize
    // restore from a passive float signal has not been shown safe against the
    // monocle batch that owns that membership. The tiled-membership clear IS
    // performed below: no caller on this channel does it, and a floating
    // window left recorded as tiled keeps the tiled appearance scope.
    //
    // Membership-OR-snapshot guard: the membership remove() is consumed by
    // the first call, and every arm of releaseWindowedFullscreenState (the
    // no-window miss included) erases the snapshot — so a SURVIVING snapshot
    // means membership was dropped by a path that never called the release
    // at all and the release is still owed. (cleanupAutotileTracking used to
    // be such a path; it now forgets and releases together, so no CURRENT
    // caller leaves a lone snapshot — the guard stays because the invariant
    // it protects is cheap and the next such path would be silent.) releaseWindowedFullscreenState is idempotent and
    // deliberately does not consult the membership hash, so re-driving it
    // off the snapshot is safe.
    // PassiveFloat is the scope whose blank is a genuine DECISION rather than
    // an accident: monocle is excluded, for the reason spelled out above. The
    // funnel carries the membership-OR-snapshot guard this site argued for,
    // and the clear-in-flight drop that used to sit beside it.
    //
    // The column mirror DOES answer to this scope, unlike monocle. The
    // exclusion reasoning above does not carry to it: monocle is refused
    // because the monocle batch owns the membership and could re-drive it,
    // whereas a float ENDS the strip's claim outright and no batch will ever
    // carry a cleared flag for a window that is no longer in the strip. Left
    // held, the window stays KWin-maximized as a floater for the session —
    // this channel's whole reason for existing is that the active funnel
    // never runs for its producers.
    const ClaimReleaseResult claims =
        releaseAllClaims(windowId, m_effect->findWindowByIdExact(windowId), ScrollDecisions::ClaimScope::PassiveFloat);
    // A floating window is free to move itself — stop countering.
    m_effect->m_scrollCommandedRects.remove(windowId);
    m_effect->m_scrollOfferedColumn.remove(windowId);
    // A floating window is no longer tile-managed on any screen, and this
    // channel has no other writer of that fact (the passive slot's own
    // clearWindowSnapped covers the SNAP facts only). Left standing, IsTiled
    // stays true for a floated window and a tiled-scoped border / title-bar
    // rule keeps drawing or hiding. The helper is change-gated and
    // re-resolves the window's rules itself on the flip, so it costs nothing
    // for a window that was not tiled. Placed before the decoration re-drive
    // below, per reconcileDecorationOnPlacementFlip's flip-facts-first
    // contract.
    clearWindowTiledAllScreens(windowId);
    // Same rationale as applyFloatCleanup for all three: a stale target
    // re-triggers centering on the next frameGeometryChanged, and a stale
    // relocation-delta entry makes a later snap on another screen paint the window
    // at the dead strip position (or not at all).
    m_tileTargetZones.remove(windowId);
    m_centeredWaylandZones.remove(windowId);
    // The removal changes where the paint path draws the window (relocated
    // position → nothing), and unlike the active channel this path has no
    // follow-up geometry apply guaranteed to damage — so pair it.
    if (m_effect->m_scrollVisualDelta.remove(windowId) > 0 && KWin::effects) {
        KWin::effects->addRepaintFull();
    }
    // Decoration re-drive: the sibling exit paths (the self-exit arms, the
    // active channel's applyFloatCleanup) all re-resolve chrome on this
    // transition, and shouldDecorateWindow's fullscreen reject lifts the
    // moment the release above lands — without this the window comes back
    // with no PlasmaZones chrome until an unrelated sweep. The rule-cache
    // invalidation the passive slot performs later early-returns in a
    // default-config session, so it cannot substitute. Gated on the RELEASE
    // having run, not on membership: the caller's clearWindowSnapped
    // reconciled before this shed (fact-flip contract), i.e. while the
    // window was still fullscreen, so a snapshot-only release with no
    // follow-up reconcile here would leave the window undecorated until an
    // unrelated sweep.
    if (claims.windowedFullscreen) {
        m_effect->reconcileDecorationOnPlacementFlip(windowId);
    }
}

void TilingHandler::markWindowTiled(const QString& screenId, const QString& windowId)
{
    const bool wasTiled = isTiledWindow(windowId);
    // Single-owner enforced HERE, not by call-site discipline: a window
    // belongs to exactly one screen bucket, and readers that answer with the
    // first bucket found (screenForTiledWindow, the outputchange scroll
    // guard) are only correct while that holds. A future caller that skipped
    // its own removeFromOtherScreens would otherwise break them silently.
    TilingStateHelpers::removeFromOtherScreens(m_border, windowId, screenId);
    TilingStateHelpers::addTiledOnScreen(m_border, screenId, windowId);
    // Re-resolve only on the false→true transition: a window already tiled on
    // another screen stays tiled, so re-adding it changes no rule outcome.
    if (!wasTiled) {
        m_effect->invalidateRuleCacheForStateChange(windowId);
    }
}

void TilingHandler::clearWindowTiledAllScreens(const QString& windowId)
{
    if (TilingStateHelpers::removeFromAllScreens(m_border, windowId)) {
        // Was tiled on at least one screen and now is not — IsTiled flipped.
        m_effect->invalidateRuleCacheForStateChange(windowId);
    }
}

void TilingHandler::clearWindowTiledOnScreen(const QString& screenId, const QString& windowId)
{
    if (TilingStateHelpers::removeTiledOnScreen(m_border, screenId, windowId) && !isTiledWindow(windowId)) {
        // Removed from this screen and not tiled on any other — IsTiled flipped.
        m_effect->invalidateRuleCacheForStateChange(windowId);
    }
}

} // namespace PlasmaZones
