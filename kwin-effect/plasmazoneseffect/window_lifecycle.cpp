// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "shader_internal.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>
#include <virtualdesktops.h>
#include <window.h>

#include <QLoggingCategory>
#include <QPointer>
#include <QScopeGuard>

#include "autotilehandler/autotilehandler.h"
#include "handlers/snaphandler.h"
#include "handlers/dragtracker.h"
#include "handlers/navigationhandler.h"
#include "handlers/screenchangehandler.h"
#include "compositor/windowanimator.h"

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
    // Apply any SetWindowLayer rule to the new window right away (persistent
    // window state, so NOT desktop-gated — matching updateAllDecorations'
    // title-bar/layer handling). This eager add-time apply is a layer-only
    // extra trigger on top of the shared reconcile paths (the title-bar
    // reconcile has no window-added call). Placement-scoped layer rules
    // re-reconcile when the async float/zone syncs land, via the
    // placement-state flush.
    reconcileRuleWindowLayer(windowId, w);

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
    // Drop the window's pre-rule layer snapshot the same way — no restore, the
    // keepAbove/keepBelow flags die with the window, and a reused windowId
    // must not inherit a stale snapshot.
    m_ruleWindowLayerSnapshots.remove(closedWindowId);

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
    // updateAllDecorations re-resolve below sees the new focus state. Gated
    // on a non-empty rule set so the no-rules case pays nothing. Opacity
    // needs no separate handling: it is layer-backed, so a focus-scoped
    // SetOpacity re-folds through the same decoration rebuild (each
    // updateWindowDecoration repaints its window), and a window without the
    // opacity-tint layer carries no rule opacity at all.
    if (!m_shaderManager.animationRuleSet().isEmpty()) {
        m_shaderManager.animationRuleEvaluator().clearCache();
    }

    // Re-resolve every window's border against the new focus state so the
    // active window picks up the active colour and the rest the inactive one.
    // updateAllDecorations tears down and re-applies the per-window border shader
    // (reconcileDecorationShader) for each tracked window.
    updateAllDecorations();
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
    // dedups, so outside a desktop switch this is a no-op. windowOutput
    // resolves by window position (Discussion #724: w->screen() can disagree
    // with the daemon on identical-model outputs).
    if (KWin::LogicalOutput* output = windowOutput(w)) {
        if (auto* vd = KWin::effects->currentDesktop(output)) {
            reportScreenDesktop(outputScreenId(output), static_cast<int>(vd->x11DesktopNumber()));
        }
    }

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

KWin::EffectWindow* PlasmaZonesEffect::findWindowByIdExact(const QString& windowId) const
{
    if (windowId.isEmpty()) {
        return nullptr;
    }
    const auto it = m_windowIdReverse.constFind(windowId);
    if (it != m_windowIdReverse.constEnd() && it.value() && !it.value()->isDeleted()) {
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
