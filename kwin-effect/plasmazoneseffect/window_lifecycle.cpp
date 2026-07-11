// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"
#include "shader_internal.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>
#include <window.h>

#include <QLoggingCategory>
#include <QPointer>

#include "../autotilehandler.h"
#include "../snaphandler.h"
#include "../dragtracker.h"
#include "../navigationhandler.h"
#include "../screenchangehandler.h"
#include "../windowanimator.h"

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace {
// Hard upper bound on first-frame open suppression. A window we expect to
// reposition is held invisible until its configure lands; if it never
// does (window floats, all zones full, daemon unreachable) the deadline
// releases it so a window can never be lost behind a stuck suppression.
// Sized to comfortably cover a daemon resolve + a Wayland configure
// round-trip, while a mis-suppressed floating window only ever waits this
// long — within the envelope of KWin's own window-open fade.
constexpr qint64 kRestoreSuppressDeadlineMs = 250;

// Maximize-morph landing discriminator: has the frame SIZE actually moved
// away from the departure rect? A maximize/restore always changes the frame
// size, while a position-only change can apply early server-side, so the
// size delta (with a 1px tolerance for fractional-scale residue) is what
// distinguishes "the jump landed" from "still waiting on the client's
// commit". Shared by the state-changed arming site and the deferred
// windowFrameGeometryChanged completion so the threshold has one home.
bool maximizeSizeLanded(const QRectF& frame, const QRectF& departureFrame)
{
    return qAbs(frame.width() - departureFrame.width()) > 1.0 || qAbs(frame.height() - departureFrame.height()) > 1.0;
}
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

void PlasmaZonesEffect::slotWindowAdded(KWin::EffectWindow* w)
{
    // Full property + filter-verdict dump for every window as it opens. Silent
    // unless the opt-in plasmazones.effect.diag category is enabled (see
    // logWindowDiagnostics) — gives the data needed to fix apps KWin
    // mis-classifies (Steam / CEF child surfaces) without journal noise.
    logWindowDiagnostics(w, "windowAdded");

    setupWindowConnections(w);
    updateWindowStickyState(w);

    // Tileable-app predicate: a normal top-level window we both handle and can
    // tile, that didn't open minimized. Drives the snap-restore candidacy and
    // the first-frame suppression decision below. It does NOT gate the open
    // shader — that gates on the animation filter (see the window.open block),
    // so the user's "exclude transient windows" animation setting stays
    // authoritative for which windows animate on open.
    const bool tileableAppWindow = shouldHandleWindow(w) && isTileableWindow(w) && !w->isMinimized();

    // Whether this window is a snap-restore candidate — it may be
    // teleported into a saved zone moments after opening (instantly from
    // cache, or after an async daemon resolve). Stricter than
    // tileableAppWindow: also excludes multi-instance siblings.
    const bool canSnapRestore = tileableAppWindow && !hasOtherWindowOfClassWithDifferentPid(w);
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
    // producing the visible multi-copy ghost trail. tryBeginShaderForEvent
    // takes the grab only after shouldAnimateWindow passes, so it is never held
    // for a window we don't animate.
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

    bool onAutotileScreen = m_autotileHandler->isAutotileScreen(getWindowScreenId(w));

    // First-frame suppression: KWin places a new window at its centred
    // placement geometry and composites it there before this handler can
    // move it into a zone / tile. Withhold the window from compositing
    // until its repositioning configure lands (see RestoreSuppression) so
    // it never flashes at the screen centre. Applies to every window we
    // are about to reposition on open: snap-restore candidates (which
    // always run resolveWindowRestore below) and tileable windows on an
    // autotile screen (which the autotile engine tiles). The window is
    // released by endRestoreSuppression on geometry-settle, on a negative
    // resolve, or on the hard deadline.
    if (canSnapRestore || (tileableAppWindow && onAutotileScreen)) {
        beginRestoreSuppression(w);
    }

    // Instant snap restore: if we have a cached zone geometry for this app,
    // restore the window immediately — no D-Bus round-trip — so it animates
    // into its zone without waiting for the async resolve. The async
    // callResolveWindowRestore still runs to register the zone assignment in
    // the daemon; this just eliminates the visual lag.
    //
    // The cache is authoritative about the target SCREEN, not the window's current
    // placement. Each entry carries the screenId of the saved zone; the daemon
    // populates the cache only for pending restores whose saved screen is in snap
    // mode, so an entry being present means "this app wants to land on a
    // snap-mode zone". Cross-VS/cross-monitor teleport works because moveResize
    // takes absolute compositor coordinates, so applyWindowGeometry moves the
    // window to whichever screen the cached rect lives on. After teleport,
    // re-evaluate onAutotileScreen because KWin updates the window's output
    // assignment.
    //
    // Rare race: the saved screen may have flipped from snap→autotile between
    // when the cache was populated and when the window opens. Re-check the
    // entry's screen mode via the autotile handler before applying.
    if (canSnapRestore && !m_snapHandler->restoreCacheEmpty()) {
        const QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
        // Single-shot semantics: takeRestore erases the entry on lookup, so any
        // entry seen here is consumed regardless of which branch below runs (the
        // entry has been considered for routing whether or not it was applied,
        // so the next open of the same appId won't re-evaluate a dead entry).
        if (const std::optional<CachedSnapRestore> cached = m_snapHandler->takeRestore(appId)) {
            const bool savedScreenNowAutotile =
                !cached->screenId.isEmpty() && m_autotileHandler->isAutotileScreen(cached->screenId);
            if (cached->geometry.isValid() && !savedScreenNowAutotile) {
                qCInfo(lcEffect) << "Instant snap restore for" << appId << "to:" << cached->geometry
                                 << "screen:" << cached->screenId;
                // skipAnimation=true: teleport straight into the zone.
                // First-frame open suppression (beginRestoreSuppression
                // above in slotWindowAdded) withholds the window from
                // compositing until KWin's async moveResize commits, so
                // by the time paintWindow runs the live frameGeometry()
                // already reports the resolved zone — the surface-extent
                // open shader (bounce, fly-in) plays into the zone from
                // the first painted frame without any anchor pinning.
                applyWindowGeometry(w, cached->geometry, false, /*skipAnimation=*/true);
                // Re-evaluate screen after teleport — cross-VS/cross-monitor
                // moveResize updates KWin's output assignment, so the window
                // may no longer be on an autotile screen.
                onAutotileScreen = m_autotileHandler->isAutotileScreen(getWindowScreenId(w));
            } else if (savedScreenNowAutotile) {
                qCDebug(lcEffect) << "Skipping instant snap restore for" << appId
                                  << "- saved screen now autotile:" << cached->screenId;
            } else {
                // Cached geometry is invalid (corrupt / zero-size persisted
                // rect on a snap-mode screen).
                qCDebug(lcEffect) << "Discarding instant snap restore entry for" << appId
                                  << "- geometry invalid:" << cached->geometry << "screen:" << cached->screenId;
            }
        }
    }

    if (onAutotileScreen && canSnapRestore) {
        // Window landed on an autotile screen, but may have a pending snap restore
        // to a non-autotile screen. KWin's session restore places windows at their
        // saved geometry, which may be a pre-snap floating position in the autotile
        // screen's area — even though the window was snapped in the snap screen
        // before logout. Try snap restore FIRST: if it moves the window off the
        // autotile screen, we avoid the autotile add→float→remove→resnap dance
        // that causes visible flickering and repeated resizing.
        QPointer<KWin::EffectWindow> safeW = w;
        // releaseSuppressionOnMiss=false: if snap-restore finds no zone, the
        // onComplete autotile path may still reposition the window — keep it
        // suppressed until that reposition's geometry settles. If autotile
        // ALSO declines to act (notifyWindowAdded returns false: ineligible
        // window, daemon decided not to tile, already-notified), nothing
        // further will move the window, so release suppression immediately
        // rather than waiting for the 250ms deadline.
        m_snapHandler->callResolveWindowRestore(
            w,
            [this, safeW]() {
                if (!safeW || safeW->isDeleted()) {
                    return;
                }
                // Snap restore either moved the window to a snap screen (no-op for
                // autotile) or didn't apply (window genuinely belongs on autotile).
                if (!m_autotileHandler->notifyWindowAdded(safeW)) {
                    endRestoreSuppression(safeW.data());
                }
            },
            /*releaseSuppressionOnMiss=*/false);
        return;
    }

    // Standard path: notify autotile first, then try snap restore. If
    // autotile is on this screen but doesn't actually act (daemon-side
    // filter, already-notified, etc.), and snap-restore won't run either
    // (the !onAutotileScreen guard below), nothing will move the window —
    // release suppression so it doesn't wait out the deadline.
    const bool autotileTookOver = m_autotileHandler->notifyWindowAdded(w);
    if (!autotileTookOver && onAutotileScreen) {
        endRestoreSuppression(w);
    }

    if (!onAutotileScreen && canSnapRestore) {
        // Always run the daemon round-trip — INCLUDING after an instant
        // restore. Instant restore only teleports the window to the cached
        // zone geometry; it does NOT register the window in the daemon's
        // SnapState. Zone registration happens exclusively via the daemon's
        // resolveWindowRestore → commitSnap path, so skipping this call left
        // every instant-restored window a "ghost": visually in its zone but
        // absent from SnapState::zoneAssignments. buildOccupiedZoneSet() then
        // reported the occupied zone as free, so getEmptyZones / snap-assist
        // / empty-zone auto-placement all treated it as empty. On login the
        // instant-restore cache serves nearly every window at once, so almost
        // nothing got registered (zones=1 of 7 in the field logs).
        //
        // The earlier skip of this call after an instant restore assumed the
        // daemon "would just answer no zone" once the snap restore cache
        // entry was consumed. That is false: the snap restore cache is an
        // effect-side latency cache, separate from the daemon's pending-restore queue.
        // pendingRestoreGeometries() (which populates the cache) is a const
        // read and does NOT consume the daemon queue, so resolveWindowRestore
        // still matches the pending entry, consumes it, and commits. When
        // instant restore already placed the window the daemon returns the
        // same zone rect, so the re-apply is a no-op moveResize to the
        // current geometry — the price of correct registration.
        m_snapHandler->callResolveWindowRestore(w);
    }
}

void PlasmaZonesEffect::slotWindowClosed(KWin::EffectWindow* w)
{
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

    // Tear down any in-flight zone.* shader transition first — this window
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
    tryBeginShaderForEvent(w, PhosphorAnimation::ProfilePaths::WindowClose, animationDurationMs(),
                           /*reverse=*/true, /*holdCloseGrab=*/true);
    m_windowAnimator->removeAnimation(w);

    // Same value as closingWindowId above: the windowId cache isn't dropped
    // until later in this slot (m_windowIdCache.remove near the end), so a
    // second getWindowId(w) would just re-hit the cache. Reuse the local.
    const QString& closedWindowId = closingWindowId;
    const QString closedScreenId = getWindowScreenId(w);

    // Clean up snap-mode minimize tracking
    m_snapHandler->removeMinimizeFloated(closedWindowId);
    m_dragFloatedWindowIds.remove(closedWindowId);

    // Notify autotile handler for cleanup (tracking sets + autotile D-Bus).
    // Genuine destruction also drops any desktop-move geometry stash —
    // onWindowClosed itself must not (the desktop-move path creates the
    // stash immediately before calling it).
    m_autotileHandler->onWindowClosed(closedWindowId, closedScreenId, /*windowDestroyed=*/true);
    m_autotileHandler->clearDesktopMoveStash(closedWindowId);

    // Mirror that cleanup for snapping's own border set. Pure bookkeeping —
    // the window is being destroyed, so no setNoBorder/removeWindowDecoration is
    // needed here (the border entry / shader redirect is dropped just below and
    // the title bar dies with the window).
    m_snapHandler->onWindowClosed(closedWindowId);
    // Drop the window's decoration ownership state (the Rule owner and any
    // force-show veto). forgetWindow makes zero compositor calls — the
    // decoration dies with the window.
    m_decorationManager->forgetWindow(closedWindowId);

    // Drop the window's border entry and release its border-shader redirect —
    // UNLESS a close transition was just installed above: renderSurfaceChain
    // re-evaluates the entry per frame and composites the decoration UNDER the
    // close animation (the border rides the closing window out instead of
    // vanishing at frame 1). endShaderTransition removes the entry on
    // teardown, and the windowDeleted handler is the backstop for a window
    // destroyed mid-animation.
    if (!m_shaderManager.hasTransition(w)) {
        // Pass the window pointer: the id no longer resolves via
        // findWindowById at this point, and the GL release must run (see
        // removeWindowDecoration) or the redirect paints opaque black.
        removeWindowDecoration(closedWindowId, w);
    }

    // Notify general daemon for cleanup
    notifyWindowClosed(w);

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
    // frozen mapping for the animation's lifetime; endShaderTransition and
    // the windowDeleted backstop both re-scrub on teardown.
    if (!m_shaderManager.hasTransition(w)) {
        m_windowIdCache.remove(w);
        m_windowIdReverse.remove(closedWindowId);
    }
    m_trackedScreenPerWindow.remove(w);
    m_restoreSuppress.remove(w);
    // Drop any pending-but-not-yet-flushed frame geometry for the
    // closing window. The windowDeleted lambda in lifecycle.cpp does
    // the same removal as belt-and-suspenders against a
    // windowFrameGeometryChanged emission re-inserting between this
    // slot and windowDeleted (possible for windows held alive via
    // WindowClosedGrabRole). Daemon would discard a stale
    // setFrameGeometry call for a no-longer-tracked windowId anyway,
    // so the leak was wasted D-Bus rather than incorrect — but the
    // cleanup keeps the pending-batch in lockstep with the live
    // window set.
    m_pendingFrameGeometry.remove(closedWindowId);
    m_focusFade.remove(closedWindowId);
    // Symmetric with the `windowDeleted` lambda in `lifecycle.cpp`
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
    // Filtering (e.g. shouldHandleWindow) is done inside notifyWindowActivated
    notifyWindowActivated(w);

    // Focus is a window-rule match input (Field::IsFocused), and both the
    // border-appearance and opacity resolvers go through the evaluator's
    // per-window match cache (resolveCached), which is keyed on
    // (windowId, ruleSet revision) — neither of which moves on a focus
    // change. Without dropping the cache, a window keeps the actions it
    // resolved at its FIRST focus state forever (a `WHEN focused` border
    // colour would never revert when the window loses focus). Mirror the
    // windowClass / desktopFile invalidation: drop the whole cache so the
    // re-resolve below (and the next per-frame opacity resolve) sees the new
    // focus state. Gated on a non-empty rule set so the no-rules case pays
    // nothing. The per-frame opacity cache clears every frame at
    // postPaintScreen, so a focus-scoped opacity re-resolves on the next paint.
    if (!m_shaderManager.animationRuleSet().isEmpty()) {
        m_shaderManager.animationRuleEvaluator().clearCache();

        // A focus-scoped SetOpacity rule changes a window's resolved opacity
        // when it gains or loses focus. updateAllDecorations() below repaints any
        // bordered window (updateWindowDecoration requests a full repaint), but an
        // opacity-only (borderless) window has no border to re-resolve — so
        // without an explicit repaint its re-resolved opacity would not reach
        // the screen until some unrelated damage happened to repaint it. Force a
        // repaint of both the window gaining focus (w) and the one losing it
        // (m_lastActivatedWindow). Gated on hasOpacityRules() because pure
        // border-colour changes are already covered by updateAllDecorations()'s
        // per-window repaint.
        if (m_shaderManager.hasOpacityRules()) {
            if (w) {
                w->addRepaintFull();
            }
            if (m_lastActivatedWindow && m_lastActivatedWindow != w) {
                m_lastActivatedWindow->addRepaintFull();
            }
        }
    }
    // Track the now-active window as the next focus change's "previously
    // active" window. Updated unconditionally (even with no rules yet) so the
    // pointer is correct if opacity rules are added before the next activation.
    m_lastActivatedWindow = w;

    // Re-resolve every window's border against the new focus state so the
    // active window picks up the active colour and the rest the inactive one.
    // updateAllDecorations tears down and re-applies the per-window border shader
    // (reconcileDecorationShader) for each tracked window.
    updateAllDecorations();
}

void PlasmaZonesEffect::setupWindowConnections(KWin::EffectWindow* w)
{
    if (!w)
        return;

    connect(w, &KWin::EffectWindow::windowDesktopsChanged, this, [this](KWin::EffectWindow* window) {
        updateWindowStickyState(window);

        // When a window is moved to a different desktop (e.g., "Move to Desktop 2"),
        // treat it as removed from the current desktop's tiling. The normal desktop-
        // switch flow will pick it up when the user switches to the target desktop.
        if (window && !window->isOnCurrentDesktop() && !window->isOnAllDesktops()) {
            const QString windowId = getWindowId(window);
            const QString screenId = getWindowScreenId(window);
            if (m_autotileHandler->isAutotileScreen(screenId)) {
                // Save pre-autotile geometry before onWindowClosed clears it.
                // When the window is re-added on the target desktop, this preserved
                // geometry is used instead of the current (tiled) frame position.
                m_autotileHandler->savePreAutotileForDesktopMove(windowId);

                // Title-bar state is rule-driven (no autotile decoration claim
                // to release): KWin's off-desktop noBorder reset is corrected on
                // desktop return by updateAllDecorations → resyncWindow for any
                // rule-owned window. onWindowClosed below only clears effect-side
                // tracking (shared with the genuine-close path).
                m_autotileHandler->onWindowClosed(windowId, screenId);
                removeWindowDecoration(windowId);
                qCInfo(lcEffect) << "Window moved off current desktop, removed from autotile:" << windowId;
            }
        }
    });

    // Detect when a window moves between monitors (e.g., "Move to Screen Right").
    // KWin::Window::outputChanged fires once when the window's output property changes.
    // Transfer the window from the old screen's autotile state to the new screen's state,
    // and unsnap any snapped window that crosses screens.
    KWin::Window* kw = w->window();
    if (kw) {
        QPointer<KWin::EffectWindow> safeW = w;
        // Track the window's screen ID so we can detect cross-screen moves for snapping windows
        // (not tracked by the autotile handler's m_notifiedWindowScreens).
        m_trackedScreenPerWindow[w] = getWindowScreenId(w);
        connect(kw, &KWin::Window::outputChanged, this, [this, safeW]() {
            if (!safeW || safeW->isDeleted()) {
                return;
            }
            const QString newScreenId = getWindowScreenId(safeW);
            const QString oldScreenId = m_trackedScreenPerWindow.value(safeW);
            m_trackedScreenPerWindow[safeW] = newScreenId;

            // Detect involuntary moves up front: when a monitor drops out
            // (DPMS standby on Wayland, hotplug-unplug) KWin reassigns the
            // windows that were on it to a remaining output and fires
            // outputChanged for each — even though the user did nothing. Both
            // the autotile and snapping paths below must skip these, because
            // routing them through the normal cross-screen logic would either
            // tile a window from the disabled monitor into the active
            // autotile zone (discussion #527) or fire a spurious unsnap.
            // Recovery is owned by the daemon's virtualScreensReconfigured /
            // ScreenChangeHandler debounce, which resettles assignments once
            // the screen change has stopped chattering.
            bool oldScreenStillConnected = false;
            for (const auto* output : KWin::effects->screens()) {
                if (outputScreenId(output) == oldScreenId) {
                    oldScreenStillConnected = true;
                    break;
                }
            }
            const bool involuntaryMove = !oldScreenId.isEmpty()
                && (!oldScreenStillConnected || m_screenChangeHandler->isScreenChangeInProgress());

            // Delegate autotile handling (autotile→autotile, autotile→snapping, etc.)
            // This must run even during drag so the autotile engine removes the
            // window from the old screen's tiling state immediately. The
            // involuntary-move guard is the symmetric partner of the snapping
            // guard further down — before #527, only the snapping path was
            // protected and KWin's orphan-reassignment got mistaken for the
            // window genuinely entering autotile.
            if (!involuntaryMove) {
                m_autotileHandler->handleWindowOutputChanged(safeW);
            }

            // For snapping→snapping cross-screen moves: notify the daemon which
            // decides whether to unsnap based on its own state. If the daemon just
            // assigned this window to the new screen (restore/resnap/snap assist),
            // the stored screen matches and no unsnap occurs. If the user moved
            // the window via "Move to Screen" shortcut, the stored screen differs
            // and the daemon unsnaps.
            // Skip during drag: the drag system owns snap state transitions
            // (float, unsnap, size restore, pre-tile cleanup) and handles them
            // in dragStopped() with richer context.
            // Skip involuntary moves: see the involuntaryMove computation above.
            if (!oldScreenId.isEmpty() && oldScreenId != newScreenId
                && !m_autotileHandler->isAutotileScreen(oldScreenId)
                && !m_autotileHandler->isAutotileScreen(newScreenId) && !m_dragTracker->isDragging()
                && !involuntaryMove) {
                const QString windowId = getWindowId(safeW);
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("windowScreenChanged"),
                    {windowId, newScreenId}, QStringLiteral("cross-screen move"));
            }
        });
        // Virtual screen boundary detection: KWin's outputChanged only fires when
        // the physical monitor changes. Moving a window between virtual screens on the
        // same physical monitor (e.g., A/vs:0 → A/vs:1) is invisible to outputChanged.
        // Detect these crossings via frameGeometryChanged, using the same trackedScreen
        // state as the outputChanged handler above.
        // (The autotile handler has its own detection in slotWindowFrameGeometryChanged;
        // this covers snapping-mode windows which autotile doesn't track.)
        //
        // VS crossing detection uses PhosphorIdentity::VirtualScreenId::isVirtualScreenCrossing()
        // (<PhosphorIdentity/VirtualScreenId.h>) — the same predicate used by
        // autotilehandler/tiling.cpp.
        connect(safeW, &KWin::EffectWindow::windowFrameGeometryChanged, this, [this, safeW]() {
            if (!safeW || safeW->isDeleted() || m_virtualScreenDefs.isEmpty() || !m_virtualScreensReady) {
                return;
            }
            // Suppress crossing detection while the daemon is moving this window in response
            // to a VS swap/rotate or resnap. The cached m_virtualScreenDefs may still hold
            // pre-rotation regions when the geometry change fires synchronously from
            // applyWindowGeometry, so getWindowScreenId would resolve the new position against
            // stale boundaries and report a phantom crossing.
            if (m_inDaemonGeometryApply) {
                return;
            }
            const QString newScreenId = getWindowScreenId(safeW);
            const QString oldScreenId = m_trackedScreenPerWindow.value(safeW);
            if (!PhosphorIdentity::VirtualScreenId::isVirtualScreenCrossing(oldScreenId, newScreenId)) {
                return;
            }
            m_trackedScreenPerWindow[safeW] = newScreenId;

            // Skip during drag — the drag system owns state transitions.
            // Autotile drag handles VS transfers via the drag-policy-changed path.
            // Snapping drag handles cross-screen unsnap on drag-stop via the daemon.
            if (m_dragTracker->isDragging()) {
                return;
            }

            // Skip VS detection for autotile-tracked windows — the autotile
            // handler's slotWindowFrameGeometryChanged owns VS crossing for
            // windows it already tracks (m_notifiedWindows). Only untracked
            // windows (snapping-mode entering an autotile VS) need delegation.
            const QString windowId = getWindowId(safeW);
            if (m_autotileHandler->isTrackedWindow(windowId)) {
                return;
            }

            // Delegate autotile handling for untracked cross-VS transitions
            // (snapping→autotile). The autotile handler's own detection only
            // covers windows it already tracks.
            m_autotileHandler->handleWindowOutputChanged(safeW);

            // For snapping→snapping cross-VS moves: notify the daemon
            if (!m_autotileHandler->isAutotileScreen(oldScreenId) && !m_autotileHandler->isAutotileScreen(newScreenId)
                && !m_screenChangeHandler->isScreenChangeInProgress()) {
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("windowScreenChanged"),
                    {windowId, newScreenId}, QStringLiteral("virtual screen crossing"));
            }
        });

        // Clean up the tracked screen entry when the window is destroyed
        connect(safeW, &QObject::destroyed, this, [this, safeW]() {
            m_trackedScreenPerWindow.remove(safeW);
        });

        // Metadata mutations: KWin fires these when an app swaps its class or
        // desktop file after the surface is already mapped. Electron/CEF apps
        // (Emby, some Discord forks) do this mid-session and silently break any
        // daemon state keyed to the first-seen class. Push the latest metadata
        // to the WindowRegistry so consumers query the current value.
        //
        // Per feedback_class_change_exclusion.md: the registry only updates its
        // record. It does NOT retroactively unsnap, re-snap, or re-evaluate
        // rules — that would surprise users. Committed state stays committed.
        auto pushLatest = [this, safeW]() {
            if (safeW && !safeW->isDeleted()) {
                pushWindowMetadata(safeW);
            }
        };
        // Caption changes fire every frame for terminals / browsers; the extended
        // property snapshot (geometry / state flags) doesn't change on a title tick,
        // so refresh the registry's core metadata (title) WITHOUT rebuilding and
        // marshalling the ~20-entry a{sv} each frame. The daemon preserves the
        // existing extended fields when none are sent.
        auto pushCaptionOnly = [this, safeW]() {
            if (safeW && !safeW->isDeleted()) {
                pushWindowMetadata(safeW, /*includeExtended=*/false);
            }
        };
        // Class / desktop-file mutations invalidate the animation rule
        // evaluator's per-window match cache. The cache is keyed on the
        // window's frozen composite id but the cascade resolves against
        // the LIVE windowClass — so without invalidation, a SetOpacity /
        // OverrideAnimation* rule for the post-rename class silently
        // never applies (Electron/CEF/Steam family). pushLatest already
        // refreshes the daemon's WindowRegistry record; mirror that
        // refresh on the effect's local resolver cache. Caption /
        // desktops / activities / role changes don't feed the
        // WindowClass matcher so they don't need the cache drop.
        auto invalidateRuleCache = [this]() {
            m_shaderManager.animationRuleEvaluator().clearCache();
        };
        connect(kw, &KWin::Window::windowClassChanged, this, pushLatest);
        connect(kw, &KWin::Window::windowClassChanged, this, invalidateRuleCache);
        connect(kw, &KWin::Window::desktopFileNameChanged, this, pushLatest);
        connect(kw, &KWin::Window::desktopFileNameChanged, this, invalidateRuleCache);
        connect(kw, &KWin::Window::captionChanged, this, pushCaptionOnly);
        // Per-window virtual-desktop / activity / role changes also refresh the
        // registry so context-aware rule resolution sees current values. Same
        // record-only contract: no retroactive re-evaluation of committed state.
        connect(kw, &KWin::Window::desktopsChanged, this, pushLatest);
        connect(kw, &KWin::Window::activitiesChanged, this, pushLatest);
        connect(kw, &KWin::Window::windowRoleChanged, this, pushLatest);

        // Diagnostic dump on identity change — but ONLY for class / desktop-file,
        // never caption. CEF/Electron apps (Steam included) map with a
        // placeholder class and swap in the real one here, so re-dumping on
        // those catches the final classification the filters act on. Caption
        // is deliberately excluded: it feeds no filter, and terminals /
        // browsers (and this very tool's progress spinner) rewrite their title
        // every frame — dumping on captionChanged floods the journal with
        // identical blocks. See logWindowDiagnostics().
        auto logIdentityChange = [this, safeW]() {
            if (safeW && !safeW->isDeleted()) {
                logWindowDiagnostics(safeW, "identityChanged");
            }
        };
        connect(kw, &KWin::Window::windowClassChanged, this, logIdentityChange);
        connect(kw, &KWin::Window::desktopFileNameChanged, this, logIdentityChange);
    }

    // Detect drag start/end via KWin's per-window signals instead of polling.
    // windowStartUserMovedResized fires once when an interactive move (or resize) begins;
    // windowFinishUserMovedResized fires once when it ends (button release, Escape, etc.).
    // This eliminates the poll timer that previously scanned the full stacking order at
    // 32ms intervals during drag — a significant source of compositor-thread overhead.
    //
    // NOTE: windowFrameGeometryChanged / windowStepUserMovedResized are intentionally NOT
    // connected for drag tracking. They fire on every pixel of movement, which would flood
    // D-Bus. Cursor position updates are handled event-driven via slotMouseChanged →
    // DragTracker::updateCursorPosition(), throttled to ~30Hz.
    connect(w, &KWin::EffectWindow::windowStartUserMovedResized, this, [this](KWin::EffectWindow* window) {
        m_dragTracker->handleWindowStartMoveResize(window);
        // Latch interactive-resize identity AND the pre-resize frame for the finish
        // handler (see below): KWin clears isUserResize() before
        // windowFinishUserMovedResized fires, so both the move-vs-resize
        // discriminator and the baseline geometry must be captured here, at the
        // start. The geometry feeds the neighbour-reflow report (GitHub #652);
        // m_resizeStartGeometry is only read at finish when this latch identifies a
        // resize, so a plain move leaves it cleared.
        m_resizingWindow = (window && window->isUserResize()) ? window : nullptr;
        m_resizeStartGeometry = QRect();
        if (m_resizingWindow) {
            m_resizeStartGeometry = window->frameGeometry().toRect();
        }
        // window.movement.move shader transition: KWin's interactive move is
        // its own animation system (Window::moveResize via pointer drag), but
        // we layer an effect-side shader for visual feedback.
        // windowStartUserMovedResized doesn't disambiguate move from resize;
        // w->isUserResize() does — interactive resize sets it, plain move
        // leaves it false. Interactive RESIZE deliberately starts NO shader
        // event: it is a held gesture with no discrete before/after until
        // release (the compositor repaints the re-laid content live the whole
        // time), so a crossfade pack has nothing meaningful to play, and the
        // soft-body sim omits KWin's resize edge-lock logic (mesh_sim.cpp) so
        // the move-physics packs have no real story there either. Discrete
        // resizes are covered by the snapIn / layoutSwitch / maximize events.
        // tryBeginShaderForEvent silently no-ops if the user didn't assign a
        // shader to the path.
        if (window && !window->isUserResize()) {
            tryBeginShaderForEvent(window, PhosphorAnimation::ProfilePaths::WindowMove, animationDurationMs());
            // Genuine old-content capture for cross-fade legs: the drag
            // begins with the window ALIVE and its pre-drag content still
            // current, so a move pack that declares uOldWindow gets a real
            // decorated snapshot to fade FROM — matching the drag-snap morph
            // path — instead of leaning on the iHasOldWindow fallback (which
            // collapses the old side to the live content). The !oldSnapshot
            // guard preserves an existing capture on a retargeted transition,
            // mirroring drag_snap; a failed capture clears needsSnapshot and
            // the shader-side fallback covers it.
            if (auto* st = m_shaderManager.findTransition(window); st && st->cached) {
                if (st->cached->iOldWindowLoc >= 0 && !st->oldSnapshot) {
                    st->needsSnapshot = true;
                }
                // Anchor iFromRect at the grab frame for rect-driven packs.
                // Under the opt-in `move` class, only a pack declaring BOTH
                // move and geometry can reach this leg with iFromRect
                // declared (pure crossfade packs are refused by the
                // resolvedShaderAppliesToEvent gate, and wobble reads no
                // rects), but the anchor keeps such a hybrid correct:
                // unseeded, rect-driven packs derive their drawn rect from
                // iFromRect unconditionally, so the first `durationMs` of the
                // drag would play mix(0-rect, live, t) — the window sweeping
                // in from the screen origin. Seeded at the grab, the ramp is
                // a short catch-up ease toward the live frame and the pinned
                // tail (progress held at 1) draws the live rect exactly as
                // before. The !isValid guard preserves a retargeted
                // transition's original anchor.
                if (st->cached->iFromRectLoc >= 0 && !st->fromGeometry.isValid()) {
                    st->fromGeometry = window->frameGeometry();
                }
                // HELD transition: the drag is open-ended, so the shader
                // stays active (progress clamped at 1) until the release
                // handler below schedules the settle-tail teardown; the
                // duration timer stands down for held transitions. The
                // grab origin anchors iMoveOffset, and the velocity spring
                // integrates from here (see the paint pipeline).
                st->holdUntilRelease = true;
                st->grabOrigin = window->frameGeometry().topLeft();
                st->lastMovePos = st->grabOrigin;
                st->lastMoveSampleMs = -1;
                // Seed the generic soft-body lattice (iMoveMesh) so a
                // mesh-consuming pack (wobble, ...) gets neighbour-coupled
                // physics from the first frame. The grip is the node
                // nearest the cursor at grab; physics constants use KWin's
                // middle preset (per-pack tuning can layer on later).
                if (st->cached->iMoveMeshLoc >= 0 && KWin::effects) {
                    ShaderInternal::initMeshSim(st->meshSim, window->frameGeometry(), KWin::effects->cursorPos(),
                                                st->meshParams);
                }
            }
        }
    });
    connect(w, &KWin::EffectWindow::windowFinishUserMovedResized, this, [this](KWin::EffectWindow* window) {
        // Release a HELD move transition with a settle tail (interactive
        // resize starts no shader transition, see the start handler): the
        // velocity spring decays through zero over the next fraction of a
        // second, letting wobble/tilt shaders relax to rest before the
        // teardown lands. Generation-guarded exactly like the duration
        // timer so an interrupting transition owns its own lifetime.
        if (window) {
            if (auto* st = m_shaderManager.findTransition(window); st && st->holdUntilRelease) {
                QPointer<KWin::EffectWindow> safeWindow(window);
                if (st->meshSim.initialized) {
                    // Soft-body lattice: hand teardown to the settle gate.
                    // Clearing holdUntilRelease drops the transition into
                    // "active while the lattice still has energy" mode (see
                    // the paint pipeline), so the wobble rings out for as
                    // long as it physically takes rather than a fixed tail.
                    // The timer is only a generous SAFETY cap in case the
                    // sim never reaches its settle threshold.
                    //
                    // Fresh epoch for the handoff: the start-scheduled
                    // duration timer in tryBeginShaderForEvent captured the
                    // install generation and only stands down while
                    // holdUntilRelease is set. On a drag SHORTER than the
                    // nominal duration that timer fires after this clear,
                    // sees a matching generation, and would cut the ring-out
                    // off mid-settle — so bump the generation to invalidate
                    // it. The paint pipeline's expiry teardown captures the
                    // live generation at queue time, so the settle gate and
                    // the safety cap below both own the new epoch.
                    st->holdUntilRelease = false;
                    st->generation = ++m_shaderManager.m_shaderTransitionGenerationCounter;
                    const quint64 myGeneration = st->generation;
                    constexpr int kMeshSettleSafetyCapMs = 4000;
                    QTimer::singleShot(kMeshSettleSafetyCapMs, this, [this, safeWindow, myGeneration]() {
                        if (!safeWindow) {
                            return;
                        }
                        if (const auto* live = m_shaderManager.findTransition(safeWindow);
                            live && live->generation == myGeneration) {
                            endShaderTransition(safeWindow);
                        }
                    });
                } else {
                    // Velocity / trail packs: the springLag decays over the
                    // next fraction of a second, so keep the fixed tail.
                    // holdUntilRelease stays SET here, so the start-scheduled
                    // duration timer keeps standing down and this tail timer
                    // (guarded on the install generation) owns the teardown.
                    const quint64 myGeneration = st->generation;
                    QTimer::singleShot(animationDurationMs(), this, [this, safeWindow, myGeneration]() {
                        if (!safeWindow) {
                            return;
                        }
                        if (const auto* live = m_shaderManager.findTransition(safeWindow);
                            live && live->generation == myGeneration) {
                            endShaderTransition(safeWindow);
                        }
                    });
                }
            }
        }
        const bool wasResize = (window && m_resizingWindow == window);
        m_resizingWindow = nullptr;
        // A floating window the user just RESIZED has a new free size. Persist it
        // immediately into the unified record's shared free geometry (overwrite=true)
        // so the float-back is durable right away — recordFreeGeometry marks the
        // placement store dirty, arming the debounced save. The save-time sweep only
        // folds the live frame shadow into the record on the next dirtying event /
        // shutdown, and a bare resize never marks anything dirty, so without this the
        // new size could be lost on an unclean exit. Resizes never snap, so this can
        // never race the drag→snap pipeline (which owns the move case); guarding on
        // isWindowFloating keeps it to genuinely floated windows.
        if (wasResize && shouldHandleWindow(window)) {
            const QString windowId = getWindowId(window);
            if (!windowId.isEmpty() && isWindowFloating(windowId)) {
                // toRect() (rounding) rather than truncation: fractional-scale
                // outputs leave sub-pixel residue in frameGeometry(), and the
                // other geometry-capture paths round too. Correct for
                // maximize/fullscreen (freeGeometryForCapture) so maximizing a
                // floating window does not clobber its free-float size with the
                // full-monitor rect (this store uses overwrite=true).
                const QRect geom = freeGeometryForCapture(window, QRectF(window->frameGeometry())).toRect();
                if (geom.width() > 0 && geom.height() > 0) {
                    PhosphorProtocol::ClientHelpers::fireAndForget(
                        this, PhosphorProtocol::Service::Interface::WindowTracking,
                        QStringLiteral("storePreTileGeometry"),
                        {windowId, geom.x(), geom.y(), geom.width(), geom.height(), getWindowScreenId(window),
                         /*overwrite=*/true},
                        QStringLiteral("storePreTileGeometry - float resize"));
                }
            }
            // Report the committed resize to the daemon so it can reflow tiled
            // neighbours (GitHub #652). The daemon ignores floating / untracked
            // windows, so this is harmless for the float case handled just above.
            // The enclosing shouldHandleWindow(window) is the effect-side gate
            // (excluded windows never reach here); the daemon then additionally
            // re-validates membership before reflowing.
            notifyWindowResized(window, m_resizeStartGeometry);
        }
        m_dragTracker->handleWindowFinishMoveResize(window);
    });

    // Track when user manually unmaximizes a monocle-maximized window
    connect(w, &KWin::EffectWindow::windowMaximizedStateChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowMaximizedStateChanged);

    // Departure-rect capture for the maximize morph wiring below. KWin
    // guarantees windowMaximizedStateAboutToChange fires before the
    // maximize/restore geometry change (effectwindow.h documents the
    // ordering, and the stock maximize script relies on it the same way),
    // so frameGeometry() here is the rect the window is leaving — the only
    // point the old rect can be read. The state-changed edge below may fire
    // with the destination geometry already applied OR still pending the
    // client's commit (see PendingMaximizeMorph); either way this capture
    // is what anchors the morph's departure. Latest-wins per window: an
    // axis-only intermediate flip overwrites the entry, which is correct —
    // the morph should depart from wherever the window actually was just
    // before the edge we act on. Erased on windowDeleted alongside
    // m_lastFullyMaximized.
    connect(w, &KWin::EffectWindow::windowMaximizedStateAboutToChange, this,
            [this](KWin::EffectWindow* window, bool, bool) {
                if (window) {
                    m_shaderManager.m_preMaximizeFrame.insert(window, window->frameGeometry());
                }
            });

    // window.maximize / window.unmaximize shader transition. Sibling lambda
    // to the AutotileHandler hookup above (autotile drives the snap-back
    // logic; we drive the shader leg).
    //
    // KWin emits windowMaximizedStateChanged once per axis flip — a
    // user-driven left-half-snap → fully-maximize sequence fires twice
    // (vertical-only first, then fully-maximized). Without an edge filter
    // we'd start the WindowMaximize shader for the intermediate state,
    // then immediately install WindowMaximize on the next emission, with
    // the timer-driven teardown of the first racing the install of the
    // second. Track the last fully-maximized state per window and only
    // fire on actual edge transitions.
    connect(w, &KWin::EffectWindow::windowMaximizedStateChanged, this,
            [this](KWin::EffectWindow* window, bool horizontal, bool vertical) {
                if (!window) {
                    return;
                }
                const bool fullyMaximized = horizontal && vertical;
                const bool wasFullyMaximized = m_shaderManager.m_lastFullyMaximized.value(window, false);
                if (fullyMaximized == wasFullyMaximized) {
                    return; // intermediate axis-only flip, no shader
                }
                m_shaderManager.m_lastFullyMaximized.insert(window, fullyMaximized);
                // Drag-restore guard: KWin unmaximizes a window mid interactive
                // move when the user grabs the maximized title bar and pulls
                // ("restore on drag"). The drag already owns the visuals — the
                // windowStartUserMovedResized hookup above installed the
                // window.move shader as a HELD transition — and installing
                // WindowMaximize here would supersede it: the move pack dies
                // mid-drag and a full-screen→cursor morph replays over the
                // pointer. The isUserResize branch is skipped for a different
                // reason: an interactive resize starts NO shader (the start
                // handler gates on !isUserResize), but it is still a held
                // gesture with continuous geometry feedback, so a discrete
                // maximize morph replaying under the pointer would be just as
                // wrong. Skip the shader; the edge tracking above still ran,
                // so the next non-interactive flip fires normally.
                if (window->isUserMove() || window->isUserResize()) {
                    m_shaderManager.m_pendingMaximizeMorph.remove(window);
                    return;
                }
                const QRectF newFrame = window->frameGeometry();
                QRectF preFrame = m_shaderManager.m_preMaximizeFrame.value(window);
                if (preFrame.isEmpty()) {
                    // No capture (window managed after the about-to-change
                    // fired, or a degenerate rect). Fall back to the live
                    // frame: the size test below then defers to the geometry
                    // change, which still carries the real jump.
                    preFrame = newFrame;
                }
                // KWin does NOT guarantee the maximize/restore geometry has
                // been applied when this state signal fires — see the
                // PendingMaximizeMorph docstring for the observed decoupling.
                // Only install here when the size has actually changed
                // (maximizeSizeLanded above); otherwise arm the pending entry
                // and let the size-delivering windowFrameGeometryChanged below
                // complete the install at the visible jump.
                if (maximizeSizeLanded(newFrame, preFrame)) {
                    m_shaderManager.m_pendingMaximizeMorph.remove(window);
                    beginMaximizeShaderMorph(window, preFrame);
                } else {
                    m_shaderManager.m_pendingMaximizeMorph.insert(window,
                                                                  {preFrame, ShaderInternal::shaderClockNowMs()});
                }
            });

    // Track when a monocle-maximized window goes fullscreen
    connect(w, &KWin::EffectWindow::windowFullScreenChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowFullScreenChanged);

    // Autotile: center undersized Wayland windows as soon as they commit constrained size
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowFrameGeometryChanged);

    // Single windowFrameGeometryChanged lambda combining the effect-side
    // per-tick work — first-frame open suppression release AND debounced
    // daemon push — into one connection. Keeping these as two separate
    // connections (which they were originally) doubled the per-geometry-
    // tick lambda dispatch cost without functional benefit; the bodies
    // are independent so collapsing them just runs one capture+vtable
    // hop per tick instead of two. The autotile-handler connection
    // immediately above is kept separate because it dispatches to a slot
    // on a different receiver (`m_autotileHandler.get()`).
    //
    // Body 1 — first-frame open suppression release: a window withheld
    // from compositing on open (see RestoreSuppression) is released the
    // moment its reposition configure lands — detected as the live
    // geometry leaving the spawn point once applyWindowGeometry has
    // stamped the resolved target. Before the target is known a
    // geometry change is just the client's own initial size negotiation
    // and is ignored. Full-rect compare (not just topLeft) catches
    // size-only configures whose origin coincidentally matches the
    // spawn point.
    //
    // Body 2 — frame-geometry shadow: push the latest geometry to the
    // daemon so daemon-local shortcut handlers (float toggle, etc.) can
    // read fresh geometry without round-tripping. Debounced at ~50 ms
    // per window via m_frameGeometryFlushTimer so rapid move/resize
    // sequences collapse into at most one D-Bus push.
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this,
            [this, safeW = QPointer<KWin::EffectWindow>(w)]() {
                if (!safeW) {
                    return;
                }
                // Body 0 — deferred maximize-morph completion. The maximize
                // state edge above arms this entry when it fires before the
                // client has committed the new size (see PendingMaximizeMorph);
                // the geometry change that actually delivers the size lands
                // here and starts the morph at the visible jump. A
                // position-only step keeps waiting. The deadline discards a
                // stale entry (state flipped but the commit never came — e.g.
                // an occluded client under the lock screen) so a much later
                // unrelated resize cannot fire a bogus maximize animation.
                if (const auto pendingIt = m_shaderManager.m_pendingMaximizeMorph.constFind(safeW.data());
                    pendingIt != m_shaderManager.m_pendingMaximizeMorph.constEnd()) {
                    const auto pending = pendingIt.value();
                    if (maximizeSizeLanded(safeW->frameGeometry(), pending.departureFrame)) {
                        m_shaderManager.m_pendingMaximizeMorph.remove(safeW.data());
                        constexpr qint64 kPendingMaximizeMorphDeadlineMs = 1000;
                        const bool stale =
                            ShaderInternal::shaderClockNowMs() - pending.armedAtMs > kPendingMaximizeMorphDeadlineMs;
                        // Same interactive guard as the arming site: a drag
                        // that started while the entry was pending owns the
                        // visuals through the window.move shader.
                        if (!stale && !safeW->isUserMove() && !safeW->isUserResize()) {
                            beginMaximizeShaderMorph(safeW.data(), pending.departureFrame);
                        }
                    }
                }
                // Body 1 — suppression release
                if (auto it = m_restoreSuppress.find(safeW.data()); it != m_restoreSuppress.end()
                    && it->targetGeometry.isValid() && safeW->frameGeometry() != it->spawnGeometry) {
                    endRestoreSuppression(safeW.data());
                }
                // Body 2 — debounced daemon shadow
                if (!shouldHandleWindow(safeW)) {
                    return;
                }
                const QString windowId = getWindowId(safeW);
                if (windowId.isEmpty()) {
                    return;
                }
                // Self-heal a noBorder reset KWin issues asynchronously after a
                // cross-OUTPUT move. For a rule-owned (title-bar-hidden) window
                // the manager already believes it hidden, so the synchronous
                // resync in updateAllDecorations bails ("still suppressed") when it
                // runs before KWin re-evaluates the decoration. KWin grows the
                // frame by the title-bar height when it re-decorates, firing this
                // very signal: resyncWindow re-hides exactly the windows the
                // manager owns and believes hidden whose decoration drifted back,
                // and is a self-guarding no-op otherwise.
                m_decorationManager->resyncWindow(windowId);
                const QRect geo = safeW->frameGeometry().toRect();
                if (geo.width() <= 0 || geo.height() <= 0) {
                    return;
                }
                m_pendingFrameGeometry[windowId] = geo;
                if (!m_frameGeometryFlushTimer->isActive()) {
                    m_frameGeometryFlushTimer->start();
                }
            });

    // Autotile: track minimize/unminimize to remove/re-add windows from tiling
    connect(w, &KWin::EffectWindow::minimizedChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowMinimizedChanged);

    // Snap mode: track minimize/unminimize to float/unfloat snapped windows
    connect(w, &KWin::EffectWindow::minimizedChanged, this, &PlasmaZonesEffect::slotWindowMinimizedChanged);
}

void PlasmaZonesEffect::beginMaximizeShaderMorph(KWin::EffectWindow* window, const QRectF& departureFrame)
{
    if (!window) {
        return;
    }
    // ALWAYS a forward leg. Geometry packs encode direction in the rects,
    // not the timeline: the zone-snap path (drag_snap.cpp) never reverses
    // either — a shrink into a small zone is a forward morph with a small
    // iToRect. Reversing here split the two geometry-shader families:
    // fragment morphs read raw iTime (flipped → played maximized→restored),
    // but the vertex-grid packs (fold / stretch / flow / ripple-snap) run
    // their motion through legProgress(), which un-flips iTime back to a
    // forward 0→1 — with swapped rects they animated restored→MAXIMIZED
    // while the real window sat restored, then popped at teardown ("gets
    // sized down, then plays an animation"). Forward + natural rects
    // satisfies both families with the same values, and keeps the grid
    // anchoring contract intact (apply() builds the deform grid on
    // iToRect == the live frame).
    tryBeginShaderForEvent(window, PhosphorAnimation::ProfilePaths::WindowMaximize, animationDurationMs(),
                           /*reverse=*/false);
    // Geometry-morph endpoints — sibling of the drag-snap wiring in
    // drag_snap.cpp. window.maximize is a geometry-contract event, so every
    // assignable pack derives its drawn rect from iFromRect/iToRect; leaving
    // them default-invalid pushes zero vec4s and a morph pack masks every
    // fragment outside a 0×0 rect at the origin — the window paints fully
    // transparent for the whole leg and pops in on teardown.
    auto* st = m_shaderManager.findTransition(window);
    if (!st || !st->cached || st->cached->iFromRectLoc < 0) {
        return;
    }
    const QRectF newFrame = window->frameGeometry();
    QRectF preFrame = departureFrame;
    if (preFrame.isEmpty()) {
        // Degenerate departure rect: degrade to a static morph at the live
        // frame — visible, just motionless — rather than the transparent
        // zero-rect.
        preFrame = newFrame;
    }
    // Always retarget the destination; anchor the departure + snapshot only
    // on a fresh morph. A rapid maximize→unmaximize toggle with the same
    // shader lands here while the first leg is still live (same effect,
    // same direction, same timing mode → beginShaderTransition's
    // same-effect short-circuit keeps the prior transition), and the
    // captured snapshot already holds the ORIGINAL content — re-anchoring
    // fromGeometry or re-capturing mid-flight would jump the drawn rect and
    // collapse the cross-fade. Mirrors the drag-snap retarget rule.
    st->toGeometry = newFrame;
    if (!st->oldSnapshot) {
        st->fromGeometry = preFrame;
        // Old-content cross-fade: same guard as the move-start hookup. The
        // raw capture happens on the first paint (post-jump, so it degrades
        // to the live content for undecorated windows), but decorated
        // windows seed from the frozen pre-jump composite — see
        // captureOldWindowSnapshot.
        if (st->cached->iOldWindowLoc >= 0) {
            st->needsSnapshot = true;
        }
    }
}

void PlasmaZonesEffect::notifyWindowClosed(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }

    const QString windowId = getWindowId(w);

    if (!isDaemonReady("notify windowClosed")) {
        return;
    }

    const int kindInt = static_cast<int>(classifyWindowKind(w));
    // Pass KWin's authoritative current screen for the window. The daemon uses it
    // as the final-placement screen when a cross-screen move has left the window
    // untracked by both engines at close — otherwise its float-back records the
    // stale source screen and it reopens on the wrong monitor.
    const QString closeScreenId = getWindowScreenId(w);
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

void PlasmaZonesEffect::notifyWindowActivated(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }

    // Skip non-manageable window types but NOT user-excluded apps — the daemon
    // must always know which window is active so that keyboard shortcuts can
    // correctly skip excluded windows instead of operating on a stale
    // m_lastActiveWindowId.
    const QString windowClass = w->windowClass();
    if (isOwnOverlayClass(windowClass) || isXdgDesktopPortalSurface(windowClass)) {
        return;
    }
    // Plasma shell surfaces — independent filter chain from shouldHandleWindow()
    // because notifyWindowActivated() intentionally skips user-exclusion lists
    // (the daemon still needs focus updates for excluded apps). The plasmashell
    // rejection must apply in both chains; see isPlasmaShellSurface().
    if (isPlasmaShellSurface(windowClass)) {
        return;
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
    if (isStructurallyUnmanageableWindowType(w)) {
        return;
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
    if (m_shaderManager.m_lastFocusShaderWindow.data() != w) {
        m_shaderManager.m_lastFocusShaderWindow = w;
        tryBeginShaderForEvent(w, PhosphorAnimation::ProfilePaths::WindowFocus, animationDurationMs());
    }

    if (!isDaemonReady("notify windowActivated")) {
        return;
    }

    QString windowId = getWindowId(w);
    QString screenId = getWindowScreenId(w);

    qCDebug(lcEffect) << "Notifying daemon: windowActivated" << windowId << "on screen" << screenId;
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("windowActivated"), {windowId, screenId});

    // Notify autotile engine of focus change so m_windowToScreen is updated
    if (m_autotileHandler->isAutotileScreen(screenId)) {
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Autotile,
                                                       QStringLiteral("notifyWindowFocused"), {windowId, screenId},
                                                       QStringLiteral("notifyWindowFocused"));
    }
}

KWin::EffectWindow* PlasmaZonesEffect::findWindowById(const QString& windowId) const
{
    if (windowId.isEmpty()) {
        return nullptr;
    }

    // O(1) exact match via reverse cache
    auto it = m_windowIdReverse.constFind(windowId);
    if (it != m_windowIdReverse.constEnd() && it.value() && !it.value()->isDeleted()) {
        return it.value();
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
