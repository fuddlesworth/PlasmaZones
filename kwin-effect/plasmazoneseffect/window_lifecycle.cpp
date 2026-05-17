// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

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
#include "../dragtracker.h"
#include "../navigationhandler.h"
#include "../screenchangehandler.h"
#include "../scrollhandler.h"
#include "../windowanimator.h"

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

void PlasmaZonesEffect::slotWindowAdded(KWin::EffectWindow* w)
{
    setupWindowConnections(w);
    updateWindowStickyState(w);

    // window.open shader transition: fires once for every newly-mapped
    // window we handle. KWin's stock fade-in handles the *motion* (alpha
    // fade-in over a few frames), so the shader leg layers a transition
    // effect on top. tryBeginShaderForEvent silently no-ops when the user
    // hasn't assigned a shader to window.open in their tree.
    tryBeginShaderForEvent(w, PhosphorAnimation::ProfilePaths::WindowOpen, animationDurationMs());

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

    // Check if this window is a candidate for snap restore
    // Use stricter filter - only normal application windows, NOT dialogs/utilities
    bool canSnapRestore =
        shouldHandleWindow(w) && isTileableWindow(w) && !w->isMinimized() && !hasOtherWindowOfClassWithDifferentPid(w);

    // Instant snap restore: if we have a cached zone geometry for this app,
    // teleport the window immediately — no D-Bus round-trip, no visible flash.
    // The async callResolveWindowRestore still runs to register the zone assignment
    // in the daemon; this just eliminates the visual lag.
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
    if (canSnapRestore && !m_snapRestoreCache.isEmpty()) {
        QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
        auto cacheIt = m_snapRestoreCache.find(appId);
        if (cacheIt != m_snapRestoreCache.end()) {
            const CachedSnapRestore& cached = cacheIt.value();
            const bool savedScreenNowAutotile =
                !cached.screenId.isEmpty() && m_autotileHandler->isAutotileScreen(cached.screenId);
            if (cached.geometry.isValid() && !savedScreenNowAutotile) {
                qCInfo(lcEffect) << "Instant snap restore for" << appId << "to:" << cached.geometry
                                 << "screen:" << cached.screenId;
                applySnapGeometry(w, cached.geometry, false, /*skipAnimation=*/true);
                m_snapRestoreCache.erase(cacheIt);
                // Re-evaluate screen after teleport — cross-VS/cross-monitor
                // moveResize updates KWin's output assignment, so the window
                // may no longer be on an autotile screen.
                onAutotileScreen = m_autotileHandler->isAutotileScreen(getWindowScreenId(w));
            } else if (savedScreenNowAutotile) {
                qCDebug(lcEffect) << "Skipping instant snap restore for" << appId
                                  << "- saved screen now autotile:" << cached.screenId;
                m_snapRestoreCache.erase(cacheIt);
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
        callResolveWindowRestore(w, [this, safeW]() {
            if (!safeW || safeW->isDeleted()) {
                return;
            }
            // Snap restore either moved the window to a snap screen (no-op for
            // autotile) or didn't apply (window genuinely belongs on autotile).
            m_autotileHandler->notifyWindowAdded(safeW);
        });
        return;
    }

    // Standard path: notify autotile first, then try snap restore
    m_autotileHandler->notifyWindowAdded(w);
    // Scroll mode reports independently — it acts only on scroll-mode
    // screens, which are disjoint from autotile screens.
    m_scrollHandler->notifyWindowAdded(w);

    if (!onAutotileScreen && canSnapRestore) {
        callResolveWindowRestore(w);
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
    m_minimizeFloatedWindows.remove(closedWindowId);
    m_dragFloatedWindowIds.remove(closedWindowId);

    // Notify autotile handler for cleanup (tracking sets + autotile D-Bus)
    m_autotileHandler->onWindowClosed(closedWindowId, closedScreenId);
    // Scroll handler does the same for scroll-mode screens.
    m_scrollHandler->onWindowClosed(closedWindowId, closedScreenId);

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
}

void PlasmaZonesEffect::slotWindowActivated(KWin::EffectWindow* w)
{
    // Filtering (e.g. shouldHandleWindow) is done inside notifyWindowActivated
    notifyWindowActivated(w);

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
                m_autotileHandler->savePreAutotileForDesktopMove(windowId, screenId);

                // Restore title bar before removing from tiling — onWindowClosed
                // only clears tracking, it doesn't call setNoBorder(false) since
                // it's also used for truly closing windows.
                if (m_autotileHandler->isBorderlessWindow(windowId)) {
                    KWin::Window* kw = window->window();
                    if (kw) {
                        kw->setNoBorder(false);
                    }
                }
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

            // Delegate autotile handling (autotile→autotile, autotile→snapping, etc.)
            // This must run even during drag so the autotile engine removes the
            // window from the old screen's tiling state immediately.
            m_autotileHandler->handleWindowOutputChanged(safeW);

            // For snapping→snapping cross-screen moves: notify the daemon which
            // decides whether to unsnap based on its own state. If the daemon just
            // assigned this window to the new screen (restore/resnap/snap assist),
            // the stored screen matches and no unsnap occurs. If the user moved
            // the window via "Move to Screen" shortcut, the stored screen differs
            // and the daemon unsnaps.
            // Skip during drag: the drag system owns snap state transitions
            // (float, unsnap, size restore, pre-tile cleanup) and handles them
            // in dragStopped() with richer context.
            // Skip when the old screen disappeared (monitor standby/disconnect):
            // KWin reassigns orphaned windows to remaining outputs, firing
            // outputChanged even though the window didn't actually move. The
            // ScreenChangeHandler will resnap windows after the debounce settles.
            // Also skip during an active screen geometry change (debounce in flight).
            bool oldScreenStillConnected = false;
            for (const auto* output : KWin::effects->screens()) {
                if (outputScreenId(output) == oldScreenId) {
                    oldScreenStillConnected = true;
                    break;
                }
            }
            if (!oldScreenId.isEmpty() && oldScreenId != newScreenId
                && !m_autotileHandler->isAutotileScreen(oldScreenId)
                && !m_autotileHandler->isAutotileScreen(newScreenId) && !m_dragTracker->isDragging()
                && oldScreenStillConnected && !m_screenChangeHandler->isScreenChangeInProgress()) {
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
            // Autotile drag handles VS transfers in dragStopped (line 262-285).
            // Snapping drag handles cross-screen unsnap in dragStopped via daemon.
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
        connect(kw, &KWin::Window::windowClassChanged, this, pushLatest);
        connect(kw, &KWin::Window::desktopFileNameChanged, this, pushLatest);
        connect(kw, &KWin::Window::captionChanged, this, pushLatest);
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

    // Frame-geometry shadow: push the latest geometry to the daemon so
    // daemon-local shortcut handlers (float toggle, etc.) can read fresh
    // geometry without round-tripping. Debounced at ~50ms per window via
    // m_frameGeometryFlushTimer so rapid move/resize sequences collapse
    // into at most one D-Bus push.
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this,
            [this, safeW = QPointer<KWin::EffectWindow>(w)]() {
                if (!safeW || !shouldHandleWindow(safeW)) {
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

    qCInfo(lcEffect) << "Notifying daemon: windowClosed" << windowId;
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("windowClosed"), {windowId});
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
    if (windowClass.contains(QLatin1String("plasmazonesd"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("plasmazones-editor"), Qt::CaseInsensitive)) {
        return;
    }
    if (windowClass.contains(QLatin1String("xdg-desktop-portal"), Qt::CaseInsensitive)) {
        return;
    }
    // Plasma shell surfaces — independent filter chain from shouldHandleWindow()
    // because notifyWindowActivated() intentionally skips user-exclusion lists
    // (the daemon still needs focus updates for excluded apps). The plasmashell
    // rejection must apply in both chains; see isPlasmaShellSurface().
    if (isPlasmaShellSurface(windowClass)) {
        return;
    }
    if (w->isSpecialWindow() || w->isDesktop() || w->isDock() || w->isFullScreen() || w->isSkipSwitcher()
        || w->isDialog() || w->isUtility() || w->isSplash() || w->isNotification() || w->isOnScreenDisplay()
        || w->isModal() || w->isPopupWindow()) {
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

    // Notify the placement engine of the focus change. Autotile needs it to
    // update m_windowToScreen; scroll needs it to track the focused column.
    // The screen sets are disjoint, so at most one branch fires.
    if (m_autotileHandler->isAutotileScreen(screenId)) {
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Autotile,
                                                       QStringLiteral("notifyWindowFocused"), {windowId, screenId},
                                                       QStringLiteral("notifyWindowFocused"));
    } else {
        m_scrollHandler->notifyWindowFocused(windowId, screenId);
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
    // Instance ids are unique — "all windows for a given id" is at most one
    // window. findAllWindowsById exists as an API seam for the (historical)
    // case where callers wanted every instance of an app class matching a
    // given composite; that semantic now lives on the daemon's
    // WindowRegistry::instancesWithAppId() + per-instance lookups. The
    // single-instance behavior here is the only case that remains.
    QVector<KWin::EffectWindow*> out;
    if (windowId.isEmpty()) {
        return out;
    }
    const QString targetAppId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
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
