// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "shader_internal.h"
#include "compositor/effectlogging.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>
#include <virtualdesktops.h>
#include <window.h>

#include <QLoggingCategory>
#include <QPointer>

#include "tilinghandler/tilinghandler.h"
#include "handlers/snaphandler.h"
#include "handlers/dragtracker.h"
#include "handlers/navigationhandler.h"
#include "handlers/screenchangehandler.h"
#include "compositor/stripviewanimator.h"
#include "compositor/windowanimator.h"

namespace PlasmaZones {

namespace {
// Hard upper bound on first-frame open suppression. A window we expect to
// reposition is held invisible until its configure lands; if it never
// does (window floats, all zones full, daemon unreachable) the deadline
// releases it so a window can never be lost behind a stuck suppression.
// Sized to comfortably cover a daemon resolve + a Wayland configure
// round-trip, while a mis-suppressed floating window only ever waits this
// long — within the envelope of KWin's own window-open fade.
constexpr qint64 kRestoreSuppressDeadlineMs = 250;
} // namespace

// Withhold a freshly-opened window from compositing until its snap-restore
// / autotile reposition lands. See RestoreSuppression in types.h for why
// this is necessary (KWin paints the window at its centred placement
// before the effect can move it).
void PlasmaZonesEffect::beginRestoreSuppression(KWin::EffectWindow* window)
{
    if (!window) {
        return;
    }
    RestoreSuppression sup;
    sup.spawnGeometry = window->frameGeometry();
    sup.deadlineMs = ShaderInternal::shaderClockNowMs() + kRestoreSuppressDeadlineMs;
    m_restoreSuppress.insert(window, sup);
}

// Release a window from first-frame suppression and repaint it so the now-
// settled window (and any open-shader transition) becomes visible. No-op
// if the window was not suppressed.
void PlasmaZonesEffect::endRestoreSuppression(KWin::EffectWindow* window)
{
    if (!window || m_restoreSuppress.remove(window) == 0) {
        return;
    }
    if (!window->isDeleted()) {
        window->addRepaintFull();
    }
}

bool PlasmaZonesEffect::tryInstantSnapRestore(KWin::EffectWindow* w, const QString& windowId, bool canSnapRestore)
{
    if (!canSnapRestore || !w || w->isDeleted() || m_snapHandler->restoreCacheEmpty()) {
        return false;
    }
    const QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
    // Single-shot semantics: takeRestore erases the entry on lookup, so any
    // entry seen here is consumed regardless of which branch below runs (the
    // entry has been considered for routing whether or not it was applied,
    // so the next open of the same appId won't re-evaluate a dead entry).
    // Sole caller is the deferred-routing dispatch
    // (completeDeferredWindowRoutes) — every tileable window routes through
    // the settle defer now, and the dispatch consumes the cache so a
    // deferred window cannot leave its entry alive for a later same-app
    // sibling to claim.
    const std::optional<CachedSnapRestore> cached = m_snapHandler->takeRestore(appId);
    if (!cached) {
        return false;
    }
    const bool savedScreenNowAutotile =
        !cached->screenId.isEmpty() && m_tilingHandler->isManagedScreen(cached->screenId);
    if (cached->geometry.isValid() && !savedScreenNowAutotile) {
        qCInfo(lcEffect) << "Instant snap restore for" << appId << "to:" << cached->geometry
                         << "screen:" << cached->screenId;
        // skipAnimation=true: teleport straight into the zone.
        // First-frame open suppression (beginRestoreSuppression in
        // slotWindowAdded) withholds the window from
        // compositing until KWin's async moveResize commits, so
        // by the time paintWindow runs the live frameGeometry()
        // already reports the resolved zone — the surface-extent
        // open shader (bounce, fly-in) plays into the zone from
        // the first painted frame without any anchor pinning.
        // A client that maps itself maximized (a browser restoring session
        // state) and is instant-restored into a zone would otherwise keep
        // KWin's maximize bit fighting the zone rect from its first frame.
        m_tilingHandler->demoteMaximizeForSnapPlacement(w, cached->geometry);
        applyWindowGeometry(w, cached->geometry, false, /*skipAnimation=*/true,
                            PhosphorAnimation::ProfilePaths::WindowSnapIn, QRectF(), QRectF(),
                            /*demoteMaximizeOnDeferredReplay=*/true);
        return true;
    }
    if (savedScreenNowAutotile) {
        qCDebug(lcEffect) << "Skipping instant snap restore for" << appId
                          << "- saved screen now autotile:" << cached->screenId;
    } else {
        // Cached geometry is invalid (corrupt / zero-size persisted
        // rect on a snap-mode screen).
        qCDebug(lcEffect) << "Discarding instant snap restore entry for" << appId
                          << "- geometry invalid:" << cached->geometry << "screen:" << cached->screenId;
    }
    return false;
}

void PlasmaZonesEffect::refreshRestoreSuppressionDeadline(KWin::EffectWindow* window)
{
    // Deadline-only re-arm for an ALREADY-suppressed window; never suppresses
    // a visible one (that would read as a flash to invisible). Used by the
    // deferred-routing dispatch: the defer-time suppression is armed with the
    // standard deadline, but the screen query it waits on can outlast it, and
    // an expired deadline returns the window to compositing at its centred
    // spawn placement mid-route. spawnGeometry is left untouched so the
    // settle detection keeps its original reference.
    if (!window) {
        // Null-guard symmetry with begin/endRestoreSuppression.
        return;
    }
    const auto it = m_restoreSuppress.find(window);
    if (it != m_restoreSuppress.end()) {
        it->deadlineMs = ShaderInternal::shaderClockNowMs() + kRestoreSuppressDeadlineMs;
    }
}

void PlasmaZonesEffect::slotWindowAdded(KWin::EffectWindow* w)
{
    if (!w) {
        // Null-guard symmetry with every helper in this file. The slot derefs
        // `w` unguarded from here on (isMinimized, isOnCurrentDesktop), so the
        // guard belongs at the entry rather than at each use.
        return;
    }

    // Full property + filter-verdict dump for every window as it opens. Silent
    // unless the opt-in plasmazones.effect.diag category is enabled (see
    // logWindowDiagnostics) — gives the data needed to fix apps KWin
    // mis-classifies (Steam / CEF child surfaces) without journal noise.
    logWindowDiagnostics(w, "windowAdded");

    // Wired exactly once per window: this slot is the only caller that sees
    // windows opened after the effect loaded, and setupWindowConnections
    // carries an m_wiredWindows guard behind that. See the ordering invariant on the
    // constructor's existing-window sweep (lifecycle_wiring_daemon.cpp) for
    // why that sweep and this slot can never both wire the same window.
    setupWindowConnections(w);
    updateWindowStickyState(w);

    // Tileable-app predicate: a normal top-level window we both handle and can
    // tile, that didn't open minimized. Drives the snap-restore candidacy and
    // the first-frame suppression decision below. It does NOT gate the open
    // shader — that gates on the animation filter (see the window.open block),
    // so the user's "exclude transient windows" animation setting stays
    // authoritative for which windows animate on open.
    // Not const: applyRuleOpenFullscreen below can flip the window's
    // fullscreen state, which both predicates read — see the re-derive there.
    bool tileableWindow = shouldHandleWindow(w) && isTileableWindow(w);
    bool tileableAppWindow = tileableWindow && !w->isMinimized();

    // Whether this window is a snap-restore candidate — it may be
    // teleported into a saved zone moments after opening (instantly from
    // cache, or after an async daemon resolve). Stricter than
    // tileableAppWindow: also excludes multi-instance siblings.
    bool canSnapRestore = tileableAppWindow && !hasOtherWindowOfClassWithDifferentPid(w);
    // window.open shader transition. Gate on the animation filter
    // (shouldAnimateWindow, enforced inside tryBeginShaderForEvent) — NOT on
    // tiling eligibility. isTileableWindow() rejects every transient / dialog /
    // popup / menu, so gating the open shader on tileableAppWindow suppressed it
    // for those windows regardless of the user's "exclude transient windows"
    // animation setting, while slotWindowClosed (window.close) gates only on
    // shouldAnimateWindow. That asymmetry made transients animate on close but
    // never on open. Mirroring the close path makes the setting authoritative
    // for both: with exclude-transients off (the default) a transient gets its
    // open shader; with it on, shouldAnimateWindow drops the child surfaces,
    // preserving the ghost-trail suppression the old tiling gate provided for
    // apps that spawn popups/dropdowns alongside their main window.
    //
    // holdAddedGrab=true: take KWin::WindowAddedGrabRole so KWin's stock
    // window-open built-ins (fade / scale / slide / glide) skip this window;
    // without it KWin's stock fade-in renders concurrently with our shader,
    // producing the visible multi-copy ghost trail. beginShaderTransition takes the
    // grab only once a pack has actually resolved and installed, with symmetric
    // rollback if it does not, so it is never held for a window we don't animate.
    // Note the gate is NOT shouldAnimateWindow for every leg: a plasma-shell surface
    // skips that filter entirely and is admitted by animationEventPathFor naming it
    // a `shell.*` leg instead.
    //
    // Runs BEFORE applyRuleOpenFullscreen below, so the animation filter sees
    // the window's pre-flip fullscreen state. Deliberate: the transition must
    // install before KWin composites the first frame or the stock built-ins
    // race it (the grab above), while the rule flip commits a client
    // round-trip later on Wayland anyway — an after-the-flip verdict would
    // read the same pre-commit state.
    if (!w->isMinimized()) {
        tryBeginShaderForEvent(w, PhosphorAnimation::ProfilePaths::WindowOpen, animationDurationMs(),
                               /*reverse=*/false, /*holdCloseGrab=*/false, /*holdAddedGrab=*/true);
    }

    // Populate the daemon's WindowRegistry with this window's initial metadata.
    // Runs before any other daemon notification so consumers querying the
    // registry from their windowOpened handlers see a record (sessions 2+).
    pushWindowMetadata(w);

    // Sync floating state for this window from daemon
    // This ensures windows that were floating when closed remain floating when reopened
    // Use full windowId so daemon can do per-instance lookup with appId fallback
    QString windowId = getWindowId(w);
    m_navigationHandler->syncFloatingStateForWindow(windowId);

    // Decorate the new window immediately, open transition or not. The old
    // slot-fight hazard is gone: reconcileDecorationShader defers the
    // redirect/shader slot to any live transition (it only marks the entry,
    // shaderApplied=false), and renderSurfaceChain re-evaluates the border
    // entry per frame, compositing the decoration UNDER the open animation via
    // uSurfaceLayer — so the border flies in WITH the window instead of
    // popping in at transition end. updateWindowDecoration self-gates and is
    // idempotent (snap/autotile re-running it later is harmless).
    // Current-desktop only, matching updateAllDecorations.
    if (w->isOnCurrentDesktop()) {
        updateWindowDecoration(windowId, w);
    }
    // Apply any SetWindowLayer rule to the new window right away (persistent
    // window state, so NOT desktop-gated — matching updateAllDecorations'
    // title-bar/layer handling). This eager add-time apply is a layer-only
    // extra trigger on top of the shared reconcile paths (the title-bar
    // reconcile has no window-added call). Placement-scoped layer rules
    // re-reconcile when the async float/zone syncs land, via the
    // placement-state flush.
    reconcileRuleWindowLayer(windowId, w);
    // Fullscreen-at-open rule (OpenFullscreen), applied BEFORE the routing /
    // announce blocks below so eligibility checks and the placement engines
    // see the window's final fullscreen state.
    applyRuleOpenFullscreen(windowId, w);
    // The three predicates above were computed BEFORE that flip, and
    // shouldHandleWindow rejects a fullscreen window structurally — so a true
    // verdict leaves them stale-true and the routing block below arms
    // first-frame suppression for a window the engines are about to refuse.
    // Re-derive against the current state. Note what this does and does not
    // reach: shouldHandleWindow reads the COMMITTED fullscreen bit, so the
    // re-derive corrects the synchronous (XWayland) case, while on Wayland the
    // commit lands a client round-trip later and the predicates stay true here.
    // The announce path is not left to this: isEligibleForTilingNotify rejects
    // on requested-OR-committed fullscreen, which is what keeps the daemon
    // from being told about the window on either platform. Gated on rule
    // presence so a session with no OpenFullscreen rule pays nothing (the
    // predicates walk ~30 KWin accessors between them).
    if (m_shaderManager.hasOpenFullscreenRules()) {
        tileableWindow = shouldHandleWindow(w) && isTileableWindow(w);
        tileableAppWindow = tileableWindow && !w->isMinimized();
        canSnapRestore = tileableAppWindow && !hasOtherWindowOfClassWithDifferentPid(w);
    }

    // One-tick settle defer: EVERY tileable window routes through the
    // deferred dispatch (completeDeferredWindowRoutes), not just the
    // pending-screen-query case that machinery was built for. A client can
    // map its surface with window-management flags still queued in the same
    // request burst — Yakuake maps first, then sets keep-above and
    // skip-switcher — so at windowAdded time it reads as a plain tileable
    // window, and the inline routing used to insert it, steal focus onto it
    // and column-size it before the flags landed. Deferring by one
    // event-loop turn lets the already-queued requests be processed first;
    // the dispatch re-runs the eligibility filters against the settled
    // window. The turn is spent inside the first-frame suppression armed
    // below, so nothing paints early and placement latency is unchanged in
    // any human-visible sense. Flags that arrive later than the tick are
    // the eviction arm's job (reevaluateWindowEligibility).
    if (tileableWindow) {
        if (tileableAppWindow) {
            // DELIBERATELY broader than the dispatch-side gate
            // (canSnapRestore || onManagedScreen): the screen's mode may be
            // mid-query and cannot discriminate here. The dispatch releases
            // non-repositioned windows promptly and re-arms the deadline for
            // the rest (refreshRestoreSuppressionDeadline), so the
            // over-suppression costs one event-loop turn, or the query
            // latency when one is in flight.
            beginRestoreSuppression(w);
        }
        m_tilingHandler->deferWindowRouting(w, canSnapRestore);
        return;
    }

    // Non-tileable windows only from here on (every tileable window returned
    // through the defer above, and canSnapRestore implies tileable). The
    // snap-restore, instant-restore and suppression arms that used to live
    // inline here run in completeDeferredWindowRoutes now. One announce is
    // still live for THIS path: isEligibleForTilingNotify carries a
    // fullscreen exemption for scrolling screens that shouldHandleWindow
    // (and therefore tileableWindow) does not, so a windowed-fullscreen
    // strip member re-announced at effect bring-up arrives here and must
    // still reach the daemon — see the re-adoption contract in
    // floatcleanup.cpp.
    m_tilingHandler->notifyWindowAdded(w, /*knownFreeFloating=*/true);
}

void PlasmaZonesEffect::slotWindowClosed(KWin::EffectWindow* w)
{
    if (!w) {
        // Same entry-guard rule as slotWindowAdded: getWindowId, frameGeometry
        // and w->window() below all deref unguarded.
        return;
    }

    // Release keyboard grab if the dragged window was closed
    if (m_keyboardGrabbed && m_dragTracker->draggedWindow() == w) {
        KWin::effects->ungrabKeyboard();
        m_keyboardGrabbed = false;
    }

    // Delegate to helpers
    m_dragTracker->handleWindowClosed(w);

    // Clear floating and snap-zone state — both are runtime-only and reset on
    // window close. The daemon clears its side in windowClosed(). Done here while
    // getWindowId(w) still resolves (before the windowId cache drops the entry),
    // so a reused id can't inherit a stale zone.
    const QString closingWindowId = getWindowId(w);
    m_navigationHandler->setWindowFloating(closingWindowId, false);
    m_navigationHandler->clearWindowZone(closingWindowId);

    // Tear down any in-flight window.movement.* shader transition first — this window
    // is going away and we don't want a half-faded zone shader fighting the
    // fresh window.close shader. Then layer the close shader on top of
    // whatever fade-out KWin applies as part of the close animation.
    endShaderTransition(w);
    // Close is the reverse of open: same user-assigned shader plays
    // 1→0 so an `appear` shader doubles as a `disappear` shader.
    //
    // holdCloseGrab=true: request KWin::WindowClosedGrabRole so KWin
    // keeps the window alive past its normal unmap-and-delete sequence
    // for the duration of our close shader. Without the grab, KWin
    // proceeds with final destruction as soon as this slot returns;
    // OffscreenEffect's `redirect` is auto-released on deletion (per
    // /usr/include/kwin/effect/offscreeneffect.h:53), so paintWindow
    // never gets a frame to run the close shader on. The grab is
    // released by `endShaderTransition` when the timer-driven teardown
    // fires.
    //
    // Skipped for a PARKED scrolling column. Its committed rect sits below the
    // union of every output, so a surface-extent pack — which is nearly all of
    // them — would anchor against a frame that intersects no screen while its
    // texture is the output's, putting the anchor remap outside [0,1]. What it
    // then draws is off-viewport either way, so the transition buys nothing
    // and costs a full-output repaint every frame for its duration. The
    // relocation that normally draws a parked column where it really sits is
    // dropped just below (and again in the untrack funnel), so nothing would
    // move this back on screen.
    //
    // An EMPTY frame counts as parked, not as on-screen. It has no centre worth
    // hit-testing, so the screenAt probe below cannot classify it, and the arm
    // that treats it as unparked is the worst of the three outcomes: the close
    // shader installs, then the m_scrollVisualDelta removal a few lines down
    // pulls the relocation out from under it, and the corpse plays its whole
    // transition at the park rect, off every output.
    const QRectF closingFrame = w->frameGeometry();
    const bool parkedOffAllOutputs = m_scrollVisualDelta.contains(closingWindowId)
        && (closingFrame.isEmpty() || !KWin::effects->screenAt(closingFrame.center().toPoint()));
    if (!parkedOffAllOutputs) {
        tryBeginShaderForEvent(w, PhosphorAnimation::ProfilePaths::WindowClose, animationDurationMs(),
                               /*reverse=*/true, /*holdCloseGrab=*/true);
        // Freeze the corpse's strip displacement BEFORE the removals below
        // and before onWindowClosed's untrack funnel scrubs the tracked
        // screen. The paint path cannot re-derive either term for a deleted
        // window (scrollManagedOutputFor's isDeleted gate), so without this
        // the delta removal below pulled the relocation out from under a
        // still-painting corpse and it snapped to its raw committed rect —
        // for a panned strip's parked-committed tile, the park row at the
        // bottom of the screen. Applies with or without our close shader:
        // KWin's own close animation paints the corpse either way. The
        // corpse's frame is frozen at death, so a one-shot constant is
        // exact; a mid-leg view offset freezes at its close-instant value,
        // which just means a dying window stops riding the strip.
        //
        // Two accepted approximations. (1) The freeze derives from the
        // TRACKED screen alone, while the live draw's predicate additionally
        // exempts user-move/resize and floats from displacement — a tracked
        // window closed mid-drag mid-leg would inherit an offset it was not
        // drawn with. The pre-death exemption state is not recoverable from
        // a Deleted window, so it cannot be honoured here. (2) Damage for
        // the displaced draw region: our close shader's grab pump repaints
        // the output per frame; under a purely FOREIGN close animation the
        // foreign effect's own damage is what keeps the corpse fresh, which
        // in practice the reflow and neighbour legs also cover.
        QPointF frozen = scrollVisualTranslationFor(closingWindowId, closingFrame);
        const QString corpseScreen = m_tilingHandler->scrollTrackedScreenFor(closingWindowId);
        if (!corpseScreen.isEmpty()) {
            if (KWin::LogicalOutput* corpseOutput = outputForScreenId(corpseScreen)) {
                frozen += m_stripViewAnimator->offsetFor(corpseOutput);
            }
        }
        if (!frozen.isNull()) {
            m_scrollCorpseFreeze.insert(w, frozen);
        }
    }
    m_windowAnimator->removeAnimation(w);
    // Pairs with damage like every other remover. Note this is not the only
    // remover on the close path: TilingHandler::onWindowClosed further down
    // this slot funnels into cleanupAutotileTracking, which drops the entry
    // unconditionally, so a conditional hold here would be defeated there.
    if (m_scrollVisualDelta.remove(closingWindowId) > 0 && KWin::effects) {
        KWin::effects->addRepaintFull();
    }
    // A dying window needs no setFullScreen(false) — just the membership.
    // The keep-flag snapshot is RESTORED, not discarded: with the close
    // shader's holdCloseGrab the EffectWindow keeps painting for the
    // transition, and a bare drop left the corpse playing its close leg at
    // keepBelow=true — stacked below its strip neighbours instead of where
    // it visually was. The restore helper erases before its setters and
    // null-guards, so it is safe on a dying window (and on the
    // no-close-shader path it degrades to the plain removal).
    m_windowedFullscreenWindows.remove(closingWindowId);
    m_tilingHandler->restoreWindowedFullscreenLayerDemotion(closingWindowId, w->window());
    m_lastReportedMinSize.remove(closingWindowId);
    m_scrollCommandedRects.remove(closingWindowId);
    m_scrollOfferedColumn.remove(closingWindowId);

    // Same value as closingWindowId above: the windowId cache isn't dropped
    // until later in this slot (m_idCaches.windowIdCache.remove near the end), so a
    // second getWindowId(w) would just re-hit the cache. Reuse the local.
    const QString& closedWindowId = closingWindowId;
    const QString closedScreenId = getWindowScreenId(w);

    // Clean up snap-mode minimize tracking
    m_snapHandler->removeMinimizeFloated(closedWindowId);
    m_dragActivation.floatedWindowIds.remove(closedWindowId);

    // Notify autotile handler for cleanup (tracking sets + autotile D-Bus).
    m_tilingHandler->onWindowClosed(closedWindowId, closedScreenId);
    m_tilingHandler->clearDesktopMoveStash(closedWindowId);

    // Mirror that cleanup for snapping's own border set. Pure bookkeeping —
    // the window is being destroyed, so no setNoBorder/removeWindowDecoration is
    // needed here (the border entry / shader redirect is dropped just below and
    // the title bar dies with the window).
    m_snapHandler->onWindowClosed(closedWindowId);
    // Drop the window's decoration ownership state (the Rule owner and any
    // force-show veto). forgetWindow makes zero compositor calls — the
    // decoration dies with the window.
    m_decorationManager->forgetWindow(closedWindowId);
    // Drop the window's pre-rule layer snapshot the same way — no restore, the
    // keepAbove/keepBelow flags die with the window, and a reused windowId
    // must not inherit a stale snapshot.
    m_ruleWindowLayerSnapshots.remove(closedWindowId);

    // Does this window keep painting after this slot returns, so that its
    // decoration has to ride the close out rather than vanish at frame 1?
    //
    // Two ways it can, and OUR close transition is only one of them. The other
    // is a FOREIGN close animation: any other effect may reference the window
    // in its own windowClosed handler (that is what the signal documents the
    // handler for), and KWin ships several that do it to animate a window
    // out — including the slide the Plasma panel's popups play on dismissal.
    // The corpse is then composited for the whole of that animation with our
    // decoration entry already gone. That is the case a decorated Plasma
    // applet popup hits whenever it has no shader of its own: shouldDecorateWindow
    // admits the panel / applet-popup kinds, while a transition reaches such a
    // surface ONLY through the two `shell.*` legs animationEventPathFor names, and
    // those legs are unset until the user engages a pack on the Shell page (the
    // subtree is isolated, so nothing cascades in to engage them). With none
    // engaged — the default — the decoration is torn down here and the popup slides
    // away bare. An app window with animations disabled (or excluded from them) hits
    // it the same way. BOTH terms below are therefore live for a shell surface: the
    // decoration term carries the un-animated case, the transition term the animated
    // one. Do not drop either as dead.
    //
    // We cannot ask KWin who else holds a grab — the refcount is not exposed,
    // and the other effect's handler may not even have run yet. So the answer
    // is the one fact we own: this window IS decorated, therefore keep the
    // decoration, the multipass composite and the frozen id mapping alive and
    // let the windowDeleted backstop (lifecycle_wiring.cpp) reap them. That
    // signal means "not referenced any more", so with no foreign grab it
    // follows immediately and this costs one signal of latency; with one, the
    // decoration lives exactly as long as the window paints.
    //
    // The paint side already supports this — drawWindow presents EVERY
    // decorated window through its rest composite precisely so a window
    // without a live transition can carry its decoration through a close
    // animation, the fold serves the frozen capture (a corpse produces no
    // client damage to invalidate it), and the present blit modulates by
    // KWin's live data.opacity(), which is the foreign animation's own fade.
    const bool ridesCloseAnimation = m_shaderManager.hasTransition(w) || m_windowDecorations.contains(closedWindowId);

    // Drop the window's border entry and release its border-shader redirect —
    // UNLESS the decoration has to ride the close out (see above):
    // renderSurfaceChain re-evaluates the entry per frame and composites the
    // decoration UNDER the close animation (the border rides the closing
    // window out instead of vanishing at frame 1). endShaderTransition removes
    // the entry on teardown, and the windowDeleted handler is the backstop for
    // a window destroyed mid-animation.
    if (!ridesCloseAnimation) {
        // Pass the window pointer: the id no longer resolves via
        // findWindowById at this point, and the GL release must run (see
        // removeWindowDecoration) or the redirect paints opaque black.
        removeWindowDecoration(closedWindowId, w);
        // And the multipass targets, for the case removeWindowDecoration skips:
        // its no-border early return leaves a multipass entry behind, and such
        // an entry can exist without a decoration entry (the surface-layer and
        // backdrop paths create it on demand). The windowDeleted handler carries
        // a belt for exactly this, but that belt lives inside the
        // windowIdCache guard, and the branch a few lines below scrubs that
        // cache on precisely this path — no transition — so the belt is
        // unreachable here and the full-canvas textures and framebuffers would
        // leak for the session. releaseSurfaceState, not a raw erase: it
        // refuses while a transition owns the window, and in that case the
        // cache IS retained and the delete-path belt does the freeing.
        //
        // COUPLING, load-bearing: when the closing window is a live tab-swap
        // leg's snapshotSource, this frees ITS multipass composite while the
        // swap on the OTHER window keeps animating. That is safe only because
        // the foreign paint-time capture deliberately skips the composite
        // seed (paint_capture.cpp reads m_surfaceMultipass only for
        // src == window) and the swap's oldSnapshot is an independently-owned
        // texture. Letting a foreign source read the composite at paint time
        // would turn this release into a use-after-free.
        releaseSurfaceState(closedWindowId, w);
    }

    // Notify general daemon for cleanup. Pass the screen resolved at the
    // top of this slot: the tiling teardown above erased the scroll
    // tracking override, so re-deriving inside would report a
    // position-based screen (wrong for a parked scroll column).
    notifyWindowClosed(w, closedScreenId);

    // Clean up caches AFTER all consumers that call getWindowId(w).
    // The windowDeleted handler does final cleanup, but removing here
    // prevents re-insertion by any late calls.
    //
    // EXCEPT while a close transition is in flight: the border /
    // multipass-composite entries kept alive above are keyed by the FROZEN
    // windowId, and every close-frame fold lookup re-derives its key via
    // getWindowId(w). Scrubbing the cache here would make that re-derive
    // recompute from the now-Deleted window (empty window() / mutated
    // class), yielding a different or empty id — every lookup misses, the
    // fold never binds uSurfaceLayer, and the decoration vanishes at close
    // frame 1 even though its entries were deliberately preserved. Keep the
    // frozen mapping for the animation's lifetime; the windowDeleted backstop
    // (lifecycle_wiring.cpp) is the re-scrub — windowDeleted always follows a
    // close-grabbed window, so the mapping cannot outlive the animation.
    //
    // Same for a decoration riding a FOREIGN close animation: its entries are
    // keyed by this frozen id too, and every close-frame fold lookup re-derives
    // the key from the now-Deleted window.
    if (!ridesCloseAnimation) {
        m_idCaches.windowIdCache.remove(w);
        m_idCaches.windowIdReverse.remove(closedWindowId);
    }
    m_trackedScreenPerWindow.remove(w);
    m_restoreSuppress.remove(w);
    // Drop any pending-but-not-yet-flushed frame geometry for the
    // closing window. The windowDeleted lambda in lifecycle_wiring.cpp
    // does the same removal as belt-and-suspenders against a
    // windowFrameGeometryChanged emission re-inserting between this
    // slot and windowDeleted (possible for windows held alive via
    // WindowClosedGrabRole). Daemon would discard a stale
    // setFrameGeometry call for a no-longer-tracked windowId anyway,
    // so the leak was wasted D-Bus rather than incorrect — but the
    // cleanup keeps the pending-batch in lockstep with the live
    // window set.
    m_pendingFrameGeometry.remove(closedWindowId);
    m_focusFade.remove(closedWindowId);
    // Symmetric with the `windowDeleted` lambda in `lifecycle_wiring.cpp`
    // (which removes the same key from `m_frameOpacityCache` after the
    // close-grab unref). Close shaders held via `holdCloseGrab=true`
    // keep the EffectWindow alive past slotWindowClosed and the
    // close-path paints can still touch the opacity cache; clearing
    // here ensures the next windowDeleted has nothing to clean up if
    // the close shader runs zero frames.
    m_shaderManager.m_frameOpacityCache.remove(w);
}

void PlasmaZonesEffect::slotWindowActivated(KWin::EffectWindow* w)
{
    // No entry null-guard here, unlike slotWindowAdded / slotWindowClosed, and
    // that asymmetry is deliberate: `w == nullptr` is KWin telling us NOTHING is
    // focused any more, which is a real focus edge. isFocused is resolved as
    // `w == KWin::effects->activeWindow()` (window_query.cpp), so every window's
    // verdict changes on that edge and the invalidation + re-resolve below MUST
    // run. The slot derefs `w` nowhere itself; notifyWindowActivated carries the
    // null check for the only path that needs one.
    //
    // Filtering (e.g. shouldHandleWindow) is done inside notifyWindowActivated.
    // Its bool return is deliberately discarded for the same reason: a false
    // verdict means the ACTIVATED surface is not a daemon-reportable window (own
    // overlay, plasmashell popup, portal dialog), not that focus stayed put — the
    // window that was focused a moment ago has still lost it, and its
    // focus-scoped border has to revert. Skipping the block below on a rejection
    // would strand the focused appearance on the previous window for as long as
    // the popup owns the focus.
    notifyWindowActivated(w);

    // Focus is a window-rule match input (Field::IsFocused), and both the
    // border-appearance and opacity resolvers go through the evaluator's
    // per-window match cache (resolveCached), which is keyed on
    // (windowId, ruleSet revision) — neither of which moves on a focus
    // change. Without dropping the cache, a window keeps the actions it
    // resolved at its FIRST focus state forever (a `WHEN focused` border
    // colour would never revert when the window loses focus). Mirror the
    // windowClass / desktopFile invalidation: drop the whole cache so the
    // updateAllDecorations re-resolve below sees the new focus state. Gated
    // on a non-empty rule set so the no-rules case pays nothing. Opacity
    // needs no separate handling: it is layer-backed, so a focus-scoped
    // SetOpacity re-folds through the same decoration rebuild (each
    // updateWindowDecoration repaints its window), and a window without the
    // opacity-tint layer carries no rule opacity at all.
    if (!m_shaderManager.animationRuleSet().isEmpty()) {
        m_shaderManager.animationRuleEvaluator().clearCache();
    }
    // IsFocused is matchable in a verdict rule too (`ScrollFactor WHEN
    // focused`), and its cache is independent of the one above, so it takes
    // the same focus-edge clear.
    if (!m_shaderManager.effectVerdictRuleSet().isEmpty()) {
        m_shaderManager.effectVerdictRuleEvaluator().clearCache();
    }
    // The exclusion verdicts are cached the same way and IsFocused is just as
    // matchable in an exclusion rule (`ExcludeDecorations WHEN focused`), so
    // drop both exclusion caches on the same edge — without this, the verdict
    // computed at the window's first consult pins for the session and the
    // decoration never flips on focus change. Same per-slice gating as the
    // class-swap invalidation in window_connections.cpp.
    if (!m_snappingExclusionRuleSet.isEmpty()) {
        m_snappingExclusionEvaluator.clearCache();
    }
    if (!m_decorationExclusionRuleSet.isEmpty()) {
        m_decorationExclusionEvaluator.clearCache();
    }

    // Re-resolve every window's border against the new focus state so the
    // active window picks up the active colour and the rest the inactive one.
    // updateAllDecorations tears down and re-applies the per-window border shader
    // (reconcileDecorationShader) for each tracked window.
    updateAllDecorations();
}

void PlasmaZonesEffect::notifyWindowClosed(KWin::EffectWindow* w, const QString& preTeardownScreenId)
{
    if (!w) {
        return;
    }

    const QString windowId = getWindowId(w);

    if (!isDaemonReady("notify windowClosed")) {
        return;
    }

    const int kindInt = static_cast<int>(classifyWindowKind(w));
    // Pass the caller-resolved screen (captured before the tiling teardown
    // dropped the scroll override). The daemon uses it as the
    // final-placement screen when a cross-screen move has left the window
    // untracked by both engines at close — otherwise its float-back records
    // the stale source screen and it reopens on the wrong monitor.
    const QString closeScreenId = preTeardownScreenId.isEmpty() ? getWindowScreenId(w) : preTeardownScreenId;
    qCInfo(lcEffect) << "Notifying daemon: windowClosed" << windowId << "kind=" << kindInt
                     << "screen=" << closeScreenId;
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("windowClosed"), {windowId, kindInt, closeScreenId});
}

void PlasmaZonesEffect::notifyWindowResized(KWin::EffectWindow* w, const QRect& oldGeometry)
{
    if (!w) {
        return;
    }
    if (!isDaemonReady("notify windowResized")) {
        return;
    }

    const QString windowId = getWindowId(w);
    if (windowId.isEmpty()) {
        return;
    }

    const QRect newGeometry = w->frameGeometry().toRect();
    if (!oldGeometry.isValid() || newGeometry.width() <= 0 || newGeometry.height() <= 0) {
        return;
    }
    // Cancelled / no-op resize (Escape, or a same-size finish): nothing moved.
    if (newGeometry == oldGeometry) {
        return;
    }

    qCInfo(lcEffect) << "Notifying daemon: windowResized" << windowId << oldGeometry << "->" << newGeometry;
    PhosphorProtocol::ClientHelpers::fireAndForget(
        this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("notifyWindowResized"),
        {windowId, oldGeometry.x(), oldGeometry.y(), oldGeometry.width(), oldGeometry.height(), newGeometry.x(),
         newGeometry.y(), newGeometry.width(), newGeometry.height()});
}

bool PlasmaZonesEffect::notifyWindowActivated(KWin::EffectWindow* w)
{
    if (!w) {
        return false;
    }

    // Skip non-manageable window types but NOT user-excluded apps — the daemon
    // must always know which window is active so that keyboard shortcuts can
    // correctly skip excluded windows instead of operating on a stale
    // m_lastActiveWindowId.
    const QString windowClass = w->windowClass();
    if (isOwnOverlayClass(windowClass) || isXdgDesktopPortalSurface(windowClass)) {
        return false;
    }
    // Plasma shell surfaces — independent filter chain from shouldHandleWindow()
    // because notifyWindowActivated() intentionally skips user-exclusion lists
    // (the daemon still needs focus updates for excluded apps). The plasmashell
    // rejection must apply in both chains; see isPlasmaShellSurface().
    if (isPlasmaShellSurface(windowClass)) {
        return false;
    }
    // Reject structurally unmanageable window types via the predicate shared
    // verbatim with shouldHandleWindow() — see isStructurallyUnmanageableWindowType().
    // If a window type can never legitimately be a snap/autotile target,
    // reporting it as the active window pollutes the daemon's focus tracking:
    // m_lastActiveWindowId / m_lastActiveScreenId get pinned to a popup, and
    // downstream paths (moveNewWindowsToLastZone, shortcut screen resolution,
    // snap fallbacks) then route real windows to the popup's zone. Discussion
    // #461 item 11: Steam image popups (Electron child surfaces with
    // transient_for set but isPopupWindow false) leaked through an older
    // hand-maintained copy of this list — the shared predicate makes that
    // drift impossible.
    // Fullscreen-on-a-scrolling-screen exception, mirroring the eligibility
    // exemption: the strip keeps tiling a window through real fullscreen, so
    // the daemon must keep hearing its focus — otherwise the scrolling verbs
    // (windowed fullscreen's own toggle first among them) act on whatever
    // window was reported active BEFORE the game went fullscreen. Seen
    // live: the toggle pressed over a fullscreen Proton game landed on the
    // neighbouring terminal. Scoped HERE rather than in the shared
    // predicate so focus-follows-mouse and the other consumers keep
    // treating a genuinely fullscreen window as an occluder. The exemption
    // waives the fullscreen term AND the bare transientFor() term (Wine and
    // Proton toplevels carry transient_for on the real game window — the
    // original live bug); every EXPLICIT type term stays authoritative, so
    // a fullscreen dialog/splash/popup still cannot pin the daemon's focus
    // tracking. The residual accepted leak class is "fullscreen, no
    // explicit type, has a transient parent" — the intended target.
    const bool fullscreenOnScrollingScreen =
        w->isFullScreen() && m_tilingHandler->isScrollingScreen(getWindowScreenId(w));
    if (isStructurallyUnmanageableWindowType(w, nullptr, /*exemptFullscreen=*/fullscreenOnScrollingScreen)) {
        return false;
    }

    // window.focus shader transition. Fires after the rejection-filter cascade
    // so we don't shader plasmashell surfaces, dialogs, etc. — only "real"
    // app windows the user expects to see focus feedback on. Independent of
    // daemon-readiness gating below; the shader runs locally.
    //
    // Gate on a same-window check because KWin's windowActivated also fires
    // on virtual-desktop and activity switches, on re-stacking, and on
    // Wayland focus-stealing arbitration even when the focused window didn't
    // actually change. Without this gate the shader spams every desktop /
    // activity switch. m_shaderManager.m_lastFocusShaderWindow is a QPointer that auto-nulls
    // on window destroy, so a fresh window reusing the address can't
    // false-match.
    //
    // The stamp is written ONLY on this accepted path, and that is the point:
    // it survives every arm that returned above (nullptr focus loss, own
    // overlays, plasmashell popups, portal dialogs, structurally unmanageable
    // types) untouched. So A → popup → A replays no shader. That is the gate
    // working, not a hole in it — those surfaces appear OVER the app window
    // rather than replacing it in the user's attention, and a transient
    // deactivation is exactly the "focused window didn't really change" case
    // this exists to swallow. Our own drag overlay is the sharpest example: a
    // focus shader firing every time a snap drag ends would be pure noise. The
    // accepted cost is that a genuine A → nothing → A refocus is silent too.
    //
    // Suppressed while a scrolling TAB SWAP is running on this window. Picking
    // a tab activates its window, so this handler fires for the very window
    // the tile batch just installed the swap on, and whichever of the two
    // lands second owns the slot. The swap is the one that should: it is the
    // whole visual account of what happened, while the focus leg would replay
    // a generic focus flourish over a window that has not moved — and it would
    // strand the swap's captured snapshot of the tab being replaced.
    //
    // The suppression skips the INSTALL, never the stamp: once this path is
    // reached the window IS the focused one, which is the fact the stamp
    // records, and withholding it would leave the next activation of this same
    // window reading as a change and firing a late focus leg into the middle
    // of the swap.
    if (m_shaderManager.m_lastFocusShaderWindow.data() != w) {
        m_shaderManager.m_lastFocusShaderWindow = w;
        const ShaderTransition* const live = m_shaderManager.findTransition(w);
        if (!live || !live->tabSwap) {
            tryBeginShaderForEvent(w, PhosphorAnimation::ProfilePaths::WindowFocus, animationDurationMs());
        }
    }

    if (!isDaemonReady("notify windowActivated")) {
        // True on purpose: the window IS an acceptable activation target;
        // only the transient daemon gate stopped the report (see the header
        // doc — a bring-up fallback to another window would be wrong here).
        return true;
    }

    QString windowId = getWindowId(w);
    QString screenId = getWindowScreenId(w);

    // Push the output's current desktop BEFORE the activation notifies. On a
    // virtual-desktop switch KWin activates the destination desktop's
    // last-focused window before EffectsHandler::desktopChanged fires, so the
    // daemon would otherwise process this focus event while its per-screen
    // desktop context still points at the desktop the user just left — the
    // autotile engine then sees a same-screen context mismatch and migrates
    // the activated window into the WRONG desktop's tiling state (discussion
    // #728: cross-desktop tile leak on rapid switching). By the time KWin
    // activates the window its current desktop is already updated, and
    // fire-and-forget calls share one ordered D-Bus connection, so reporting
    // here guarantees the daemon switches context first. reportScreenDesktop
    // dedups, so outside a desktop switch this is a no-op.
    //
    // Resolve the output from the id we ALREADY resolved above, not from the
    // window's position. getWindowScreenId is engine-authoritative for a
    // tiled window, and scrolling parks off-screen columns entirely outside
    // their own screen rect, so a position-derived lookup returns the
    // NEIGHBOURING output for a parked or hidden-tab scroll window — and
    // activation routinely lands before the async geometry apply. Reporting
    // that neighbour's desktop instead of the activated window's own screen
    // silently reinstates the discussion-#728 leak for every scrolling screen.
    // windowOutput stays the fallback for an unresolvable id (Discussion #724:
    // w->screen() can disagree with the daemon on identical-model outputs, so
    // the position lookup is still the better of the two remaining options),
    // mirroring the id-first-then-centre order ruleQuery uses for
    // screenOrientation.
    KWin::LogicalOutput* activatedOutput = outputForScreenId(screenId);
    if (!activatedOutput) {
        activatedOutput = windowOutput(w);
    }
    if (KWin::LogicalOutput* output = activatedOutput) {
        if (auto* vd = KWin::effects->currentDesktop(output)) {
            reportScreenDesktop(outputScreenId(output), static_cast<int>(vd->x11DesktopNumber()));
        }
    }

    qCDebug(lcEffect) << "Notifying daemon: windowActivated" << windowId << "on screen" << screenId;
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("windowActivated"), {windowId, screenId});

    // Notify the placement engines of the focus change so m_windowToScreen is
    // updated. NOT gated on isManagedScreen: the managed set tracks the
    // CURRENT desktop and is rebuilt only by the daemon's asynchronous
    // announce, while a cross-desktop activation (taskbar click, alt-tab into
    // another desktop) fires before either the desktopChanged signal or that
    // announce lands — so when the desktop being LEFT runs no placement mode
    // the gate read an empty set and dropped the one report that centers the
    // destination strip on the clicked window. The daemon routes safely on
    // its own: reportScreenDesktop above already switched its context to the
    // destination desktop on the same ordered connection, engineOwningScreen
    // falls back for exactly this desktop-switch window, and every engine's
    // windowFocused self-guards on window tracking, so an activation on a
    // genuinely unmanaged screen is a routed no-op rather than a lost report.
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Tiling,
                                                   QStringLiteral("notifyWindowFocused"), {windowId, screenId},
                                                   QStringLiteral("notifyWindowFocused"));
    return true;
}

KWin::EffectWindow* PlasmaZonesEffect::findWindowByIdExact(const QString& windowId) const
{
    if (windowId.isEmpty()) {
        return nullptr;
    }
    const auto it = m_idCaches.windowIdReverse.constFind(windowId);
    if (it != m_idCaches.windowIdReverse.constEnd() && it.value() && !it.value()->isDeleted()) {
        return it.value();
    }
    return nullptr;
}

KWin::EffectWindow* PlasmaZonesEffect::findWindowById(const QString& windowId) const
{
    if (windowId.isEmpty()) {
        return nullptr;
    }

    // O(1) exact match via reverse cache
    if (KWin::EffectWindow* const exact = findWindowByIdExact(windowId)) {
        return exact;
    }

    // Fallback: appId-based fuzzy match (for cross-session restore where
    // the UUID portion changed but the appId is the same)
    const QString targetAppId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
    KWin::EffectWindow* appMatch = nullptr;
    int matchCount = 0;

    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        // Skip dying windows: close-shader grabs (WindowClosedGrabRole) keep
        // deleted windows in the stacking order between windowClosed and
        // windowDeleted, and matching one would both resolve a dead window
        // AND re-insert its just-scrubbed id into the caches via getWindowId.
        // The exact-match path above enforces the same !isDeleted().
        if (!w || w->isDeleted()) {
            continue;
        }
        const QString wId = getWindowId(w);
        if (::PhosphorIdentity::WindowId::extractAppId(wId) == targetAppId) {
            appMatch = w;
            ++matchCount;
        }
    }
    // Only return the fuzzy match if it's unambiguous — two Firefox windows
    // with different UUIDs would otherwise pick an arbitrary one and silently
    // misroute daemon requests.
    return matchCount == 1 ? appMatch : nullptr;
}

QVector<KWin::EffectWindow*> PlasmaZonesEffect::findAllWindowsById(const QString& windowId) const
{
    // Two cases:
    //   1. Exact-instance match (`wId == windowId`): returns a single-
    //      element vector with just that window — discards any appId
    //      matches accumulated earlier in the stacking-order walk
    //      because the instance id is the strictly stronger identifier.
    //   2. Fuzzy appId match (no exact instance found): accumulates
    //      every window that shares the composite's appId. Used by
    //      autotile to disambiguate when multiple windows share an
    //      appId (e.g. two Firefox instances) — see the header doc on
    //      `plasmazoneseffect.h::findAllWindowsById`.
    QVector<KWin::EffectWindow*> out;
    if (windowId.isEmpty()) {
        return out;
    }
    const QString targetAppId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        // Skip dying windows — same rationale as findWindowById's fuzzy walk
        // (close-grabbed deleted windows linger in the stacking order, and
        // getWindowId would re-pollute the just-scrubbed caches).
        if (!w || w->isDeleted()) {
            continue;
        }
        const QString wId = getWindowId(w);
        if (wId == windowId) {
            // Exact match — discard any appId matches accumulated from earlier
            // windows in the stacking order. Without this clear, a second instance
            // of the same app (same appId) triggers the disambiguation path in
            // slotWindowsTileRequested, which can assign the wrong EffectWindow to
            // the tile entry — leaving the new window untiled.
            return {w};
        }
        if (::PhosphorIdentity::WindowId::extractAppId(wId) == targetAppId) {
            out.append(w);
        }
    }
    return out;
}

} // namespace PlasmaZones
