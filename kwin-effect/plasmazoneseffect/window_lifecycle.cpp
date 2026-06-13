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

    // window.open shader transition: fires once for every newly-mapped
    // normal top-level window we handle. Gate so the shader DOESN'T fire
    // on the child surfaces an app spawns alongside its main window (popup
    // menus, dropdowns, tooltips, dialogs, transient utility windows).
    // Without this gate, a single app open like Discord triggers a
    // fly-in on every sub-surface — main window sliding in slowly while
    // sidebar / popup surfaces slide in moments behind it from t=0,
    // producing the "main slow + copies fast" visual artifact users
    // describe as "multiple ghosted copies of the animation". KWin's
    // stock fade-in still handles the cosmetic motion for the filtered-
    // out surfaces; we just skip the user's surface-extent shader leg.
    // Same predicate gates both the user-assigned open shader and the
    // first-frame suppression decision below. Compute once.
    const bool tileableAppWindow = shouldHandleWindow(w) && isTileableWindow(w) && !w->isMinimized();

    // Whether this window is a snap-restore candidate — it may be
    // teleported into a saved zone moments after opening (instantly from
    // cache, or after an async daemon resolve). Stricter than
    // tileableAppWindow: also excludes multi-instance siblings.
    const bool canSnapRestore = tileableAppWindow && !hasOtherWindowOfClassWithDifferentPid(w);
    if (tileableAppWindow) {
        // holdAddedGrab=true: take KWin::WindowAddedGrabRole so KWin's
        // stock window-open built-ins (fade / scale / slide / glide)
        // skip this window. Without it, KWin's stock fade-in renders
        // the window at its natural position concurrently with our
        // shader's UV-shifted animation, producing the visible
        // multi-copy ghost trail.
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
    // takes absolute compositor coordinates, so applySnapGeometry moves the
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
                applySnapGeometry(w, cached->geometry, false, /*skipAnimation=*/true);
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

    // Clear floating state — floating is runtime-only and resets on window close.
    // The daemon clears its side in windowClosed().
    m_navigationHandler->setWindowFloating(getWindowId(w), false);

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

    const QString closedWindowId = getWindowId(w);
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
    // the window is being destroyed, so no setNoBorder/removeWindowBorder is
    // needed here (the border item is removed just below and the title bar
    // dies with the window).
    m_snapHandler->onWindowClosed(closedWindowId);
    // Drop the window's decoration ownership state (mode owners, rule
    // overrides, vetoes). forgetWindow makes zero compositor calls — the
    // decoration dies with the window.
    m_decorationManager->forgetWindow(closedWindowId);

    // Remove the window's border item (parent WindowItem is being destroyed anyway,
    // but clean up our tracking hash to avoid stale entries).
    removeWindowBorder(closedWindowId);

    // Notify general daemon for cleanup
    notifyWindowClosed(w);

    // Clean up caches AFTER all consumers that call getWindowId(w).
    // The windowDeleted handler does final cleanup, but removing here
    // prevents re-insertion by any late calls.
    m_windowIdCache.remove(w);
    m_windowIdReverse.remove(closedWindowId);
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
        // when it gains or loses focus. updateAllBorders() below repaints any
        // window that carries a border item, but an opacity-only (borderless)
        // window has nothing to recreate — so without an explicit repaint its
        // re-resolved opacity would not reach the screen until some unrelated
        // damage happened to repaint it. Force a repaint of both the window
        // gaining focus (w) and the one losing it (m_lastActivatedWindow).
        // Gated on hasOpacityRules() because pure border-colour changes are
        // already covered by updateAllBorders()'s item recreate.
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

    // Recreate all borders so the active window gets the active color
    // and inactive windows get the inactive color.  A full recreate is
    // used instead of in-place setOutline() because the latter may not
    // trigger a scene-graph repaint in all KWin versions.
    updateAllBorders();
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

                // Release autotile's decoration ownership before removing from
                // tiling — onWindowClosed only clears tracking since it's also
                // used for truly closing windows. The manager restores the
                // title bar unless another owner still claims it.
                m_decorationManager->releaseKind(windowId, DecorationManager::OwnerKind::Autotile);
                m_autotileHandler->onWindowClosed(windowId, screenId);
                removeWindowBorder(windowId);
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
        // (shared/virtualscreenid.h) — the same predicate used by autotilehandler/tiling.cpp.
        connect(safeW, &KWin::EffectWindow::windowFrameGeometryChanged, this, [this, safeW]() {
            if (!safeW || safeW->isDeleted() || m_virtualScreenDefs.isEmpty() || !m_virtualScreensReady) {
                return;
            }
            // Suppress crossing detection while the daemon is moving this window in response
            // to a VS swap/rotate or resnap. The cached m_virtualScreenDefs may still hold
            // pre-rotation regions when the geometry change fires synchronously from
            // applySnapGeometry, so getWindowScreenId would resolve the new position against
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
        connect(kw, &KWin::Window::captionChanged, this, pushLatest);
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
        // Latch interactive-resize identity for the finish handler (see below):
        // KWin clears isUserResize() before windowFinishUserMovedResized fires, so
        // the move-vs-resize discriminator must be captured here, at the start.
        m_resizingWindow = (window && window->isUserResize()) ? window : nullptr;
        // window.move / window.resize shader transitions: KWin's interactive
        // move/resize is its own animation system (Window::moveResize via
        // pointer drag), but we layer an effect-side shader for visual
        // feedback. windowStartUserMovedResized doesn't disambiguate the
        // two; w->isUserResize() does — interactive resize sets it, plain
        // move leaves it false. Each direction can take its own shader
        // assignment. tryBeginShaderForEvent silently no-ops if the user
        // didn't assign a shader to the path.
        if (window) {
            tryBeginShaderForEvent(window,
                                   window->isUserResize() ? PhosphorAnimation::ProfilePaths::WindowResize
                                                          : PhosphorAnimation::ProfilePaths::WindowMove,
                                   animationDurationMs());
        }
    });
    connect(w, &KWin::EffectWindow::windowFinishUserMovedResized, this, [this](KWin::EffectWindow* window) {
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
                // other geometry-capture paths round too.
                const QRect geom = window->frameGeometry().toRect();
                if (geom.width() > 0 && geom.height() > 0) {
                    PhosphorProtocol::ClientHelpers::fireAndForget(
                        this, PhosphorProtocol::Service::Interface::WindowTracking,
                        QStringLiteral("storePreTileGeometry"),
                        {windowId, geom.x(), geom.y(), geom.width(), geom.height(), getWindowScreenId(window),
                         /*overwrite=*/true},
                        QStringLiteral("storePreTileGeometry - float resize"));
                }
            }
        }
        m_dragTracker->handleWindowFinishMoveResize(window);
    });

    // Track when user manually unmaximizes a monocle-maximized window
    connect(w, &KWin::EffectWindow::windowMaximizedStateChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowMaximizedStateChanged);

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
                // Going-to-maximized is "appear" (forward 0→1);
                // returning to floating is "disappear" (reverse 1→0).
                tryBeginShaderForEvent(window, PhosphorAnimation::ProfilePaths::WindowMaximize, animationDurationMs(),
                                       /*reverse=*/!fullyMaximized);
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
    // geometry leaving the spawn point once applySnapGeometry has
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
    qCInfo(lcEffect) << "Notifying daemon: windowClosed" << windowId << "kind=" << kindInt;
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("windowClosed"), {windowId, kindInt});
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
