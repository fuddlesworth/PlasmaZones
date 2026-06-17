// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include "../autotilehandler.h"
#include "../dragtracker.h"
#include "../snapassisthandler.h"
#include "../snaphandler.h"
#include "../windowanimator.h"
#include "shader_resolve.h"
#include "window_query.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/DragMarshalling.h>

#include <effect/effecthandler.h>
#include <window.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>

#include <memory>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace {
// Upper bound on how long the effect waits for the daemon's endDrag reply.
// If the daemon is blocked (layout recompute, overlay teardown, heavy
// handler), exceeding this budget means the compositor would otherwise
// stall waiting on a reply that may never come. On expiry the window is
// left at its release position and a warning is logged.
constexpr int EndDragTimeoutMs = 500;
} // namespace

bool PlasmaZonesEffect::borderActivated(KWin::ElectricBorder border)
{
    Q_UNUSED(border)
    // We no longer reserve edges, so this callback won't be triggered by our effect.
    // The daemon handles disabling Quick Tile via KWin config.
    return false;
}

// The kwin-effect no longer calls the legacy dragStarted D-Bus method;
// beginDrag sets up snap-path state internally on the daemon side, so
// there's only one code path into the drag state machine.

// The dragMoved lambda sends updateDragCursor directly via
// ClientHelpers::fireAndForget. Single entry point for hot-path cursor updates.

void PlasmaZonesEffect::callEndDrag(KWin::EffectWindow* window, const QString& windowId, bool cancelled)
{
    // Single entry point for drag-end dispatch.
    // Sends endDrag, receives a PhosphorProtocol::DragOutcome, and applies exactly the
    // action the daemon decided. Replaces callDragStopped (whose reply
    // shape was a 9-tuple of out-params) with a typed struct.
    QPointF cursorAtRelease = m_dragTracker->lastCursorPos();

    // qRound the cursor coords (not truncation): the hot-path updateDragCursor
    // stream rounds, so on fractional-scale outputs the release coordinate the
    // daemon resolves the drop zone against must round too, or it can differ by
    // 1px from the last streamed tick at a zone boundary.
    QDBusPendingCall pendingCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("endDrag"),
        {windowId, qRound(cursorAtRelease.x()), qRound(cursorAtRelease.y()), static_cast<int>(m_currentModifiers),
         static_cast<int>(m_currentMouseButtons), cancelled});

    QPointer<KWin::EffectWindow> safeWindow = window;
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    // Pair the watcher with a timeout. If the daemon is blocked (layout
    // recompute, overlay teardown, heavy handler), the compositor would
    // otherwise wait indefinitely for a reply that may never come. The
    // shared `handled` flag guarantees exactly-once handling: whichever
    // fires first (reply or timeout) takes the transition, the other path
    // is a no-op. Deleting the watcher does NOT cancel the underlying
    // QDBusPendingCall — any late reply is silently discarded by Qt.
    auto handled = std::make_shared<bool>(false);
    QTimer* timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, this, [windowId, handled, watcher, timeoutTimer]() {
        if (*handled) {
            return;
        }
        *handled = true;
        qCWarning(lcEffect) << "endDrag timed out after" << EndDragTimeoutMs
                            << "ms; daemon unresponsive. Leaving window" << windowId << "at release position.";
        watcher->deleteLater();
        timeoutTimer->deleteLater();
    });
    timeoutTimer->start(EndDragTimeoutMs);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, windowId, handled, timeoutTimer](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                if (*handled) {
                    // Timeout already fired; this is a late reply — discard.
                    return;
                }
                *handled = true;
                timeoutTimer->stop();
                timeoutTimer->deleteLater();

                QDBusPendingReply<PhosphorProtocol::DragOutcome> reply = *w;
                if (reply.isError()) {
                    qCWarning(lcEffect) << "endDrag call failed:" << reply.error().message();
                    return;
                }
                const PhosphorProtocol::DragOutcome outcome = reply.value();
                if (const QString err = outcome.validationError(); !err.isEmpty()) {
                    // Garbled outcome — refuse to apply any window transform.
                    // Better to leave the window where it is than to float/snap
                    // based on a corrupted payload.
                    qCWarning(lcEffect) << "endDrag outcome rejected:" << err
                                        << "— dropping without applying any action for" << windowId;
                    return;
                }
                qCInfo(lcEffect) << "endDrag outcome:" << windowId << "action=" << outcome.action
                                 << "screen=" << outcome.targetScreenId << "geo=" << outcome.toRect()
                                 << "snapAssist=" << outcome.requestSnapAssist;

                switch (outcome.action) {
                case PhosphorProtocol::DragOutcome::NoOp:
                case PhosphorProtocol::DragOutcome::CancelSnap:
                    // Daemon handled any internal cleanup. CancelSnap returns
                    // the window to its pre-drag state, so its snap-managed
                    // status is unchanged — nothing for the effect to retrack.
                    break;

                case PhosphorProtocol::DragOutcome::NotifyDragOutUnsnap:
                    // Window was dragged out of its zone — no longer snap-managed.
                    m_snapHandler->clearWindowSnapped(windowId);
                    break;

                case PhosphorProtocol::DragOutcome::ApplyFloat: {
                    // Autotile bypass drag ended — float the window at its
                    // current screen. The plugin-side compositor work
                    // (handleDragToFloat, setWindowFloatingForScreen) was
                    // previously inlined in the dragStopped lambda; now it
                    // fires here off the daemon's authoritative answer.
                    //
                    // Cross-VS transitions that happened mid-drag were
                    // applied by slotDragPolicyChanged at the moment of
                    // crossing, so by the time we get here the autotile
                    // handler has the right tracking state.
                    //
                    // isDeleted: same reply-latency hygiene as ApplySnap /
                    // RestoreSize below — floating a dying window would
                    // re-pollute the scrubbed id caches and record a daemon
                    // float for a dead id.
                    if (!safeWindow || safeWindow->isDeleted()) {
                        break;
                    }
                    const QString dropScreenId = getWindowScreenId(safeWindow);
                    if (dropScreenId.isEmpty()) {
                        break;
                    }
                    m_autotileHandler->handleDragToFloat(safeWindow, windowId);
                    // Window is now floating — drop it from snapping's set.
                    m_snapHandler->clearWindowSnapped(windowId);
                    // Note: m_dragFloatedWindowIds is intentionally NOT re-set here.
                    // See dragStopped handler — the marker is cleared at drag end
                    // because the daemon's drag-end float path (setWindowFloat →
                    // windowFloatingStateSynced) never emits applyGeometryForFloat,
                    // so there's nothing for the marker to suppress.
                    PhosphorProtocol::ClientHelpers::fireAndForget(
                        this, PhosphorProtocol::Service::Interface::WindowTracking,
                        QStringLiteral("setWindowFloatingForScreen"), {windowId, dropScreenId, true},
                        QStringLiteral("setWindowFloatingForScreen - endDrag ApplyFloat"));
                    qCInfo(lcEffect) << "endDrag ApplyFloat:" << windowId << "on" << dropScreenId;
                    break;
                }

                case PhosphorProtocol::DragOutcome::ApplySnap: {
                    // isDeleted: close-shader grabs keep dying windows alive
                    // through the D-Bus reply latency (same hygiene as the
                    // batch apply path).
                    if (!safeWindow || safeWindow->isDeleted() || safeWindow->isFullScreen()) {
                        break;
                    }
                    const QRect snapGeometry = outcome.toRect();
                    // If the window is still in user-move state because only
                    // the activation mouse button is held (LMB already
                    // released), cancel KWin's interactive move so we can
                    // snap immediately. Without this, applyWindowGeometry
                    // defers (100ms retry) until ALL buttons are released —
                    // noticeable delay when using a mouse button (RMB) for
                    // zone activation.
                    if (safeWindow->isUserMove() && !(m_currentMouseButtons & Qt::LeftButton)) {
                        if (KWin::Window* kw = safeWindow->window()) {
                            kw->cancelInteractiveMoveResize();
                        }
                    }
                    applyWindowGeometry(safeWindow, snapGeometry);
                    // Drag-drop snap committed — record in snapping's border set,
                    // but only for a resolved snap-mode screen. An empty
                    // (unresolved) or autotile-managed screen is owned by
                    // AutotileHandler, so recording it here would double-track the
                    // window — same discriminator as the other snap-commit paths.
                    if (const QString scr =
                            !outcome.targetScreenId.isEmpty() ? outcome.targetScreenId : getWindowScreenId(safeWindow);
                        !scr.isEmpty() && !m_autotileHandler->isAutotileScreen(scr)) {
                        // Defensively clear any stale local float flag before
                        // recording the snap — a surviving flag poisons the
                        // next pre-tile capture and wrongly exempts the window
                        // from the drain-time restore veto (same rationale as
                        // the single-window and batch apply paths). Idempotent
                        // when the daemon's windowFloatingChanged(false)
                        // broadcast already landed.
                        m_navigationHandler->setWindowFloating(windowId, false);
                        m_snapHandler->markWindowSnapped(windowId, scr);
                    } else {
                        // Unresolved or autotile-owned screen: this commit is
                        // not snap-tracked — drop any stale snap entry +
                        // decoration claim instead of merely skipping, same
                        // discriminator epilogue as the single-window and
                        // batch apply paths.
                        m_snapHandler->clearWindowSnapped(windowId);
                    }
                    break;
                }

                case PhosphorProtocol::DragOutcome::RestoreSize: {
                    if (!safeWindow || safeWindow->isDeleted() || safeWindow->isFullScreen()) {
                        break;
                    }
                    // Drag-to-unsnap: apply pre-snap width/height at current
                    // position. Skip if slotRestoreSizeDuringDrag already
                    // applied during the drag (size within 1px).
                    QRectF frame = safeWindow->frameGeometry();
                    if (qAbs(frame.width() - outcome.width) <= 1 && qAbs(frame.height() - outcome.height) <= 1) {
                        qCDebug(lcEffect) << "endDrag RestoreSize: already at correct size, skipping";
                        break;
                    }
                    // qRound, not truncation: fractional-scale outputs leave
                    // sub-pixel residue in frameGeometry() (same convention as
                    // the toRect() sites).
                    const QRect geo(qRound(frame.x()), qRound(frame.y()), outcome.width, outcome.height);
                    if (safeWindow->isUserMove() && !(m_currentMouseButtons & Qt::LeftButton)) {
                        if (KWin::Window* kw = safeWindow->window()) {
                            kw->cancelInteractiveMoveResize();
                        }
                    }
                    // Drag-to-unsnap: window leaves zone-managed sizing, restore pre-snap dimensions.
                    applyWindowGeometry(safeWindow, geo, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                                        PhosphorAnimation::ProfilePaths::WindowSnapOut);
                    // Drag-to-unsnap: window left zone-managed sizing.
                    m_snapHandler->clearWindowSnapped(windowId);
                    break;
                }
                }

                // Auto-fill: if window was dropped without snapping to a
                // zone and wasn't floated, try the first empty zone on the
                // release screen. Daemon-provided targetScreenId wins over
                // window's current screen (cross-screen drags).
                const bool applied = outcome.action == PhosphorProtocol::DragOutcome::ApplySnap
                    || outcome.action == PhosphorProtocol::DragOutcome::ApplyFloat;
                // isDeleted: don't auto-fill a zone for a close-grabbed dying
                // window — the daemon would commit an assignment for a dead id.
                if (!applied && safeWindow && !safeWindow->isDeleted() && !outcome.targetScreenId.isEmpty()
                    && isDaemonReady("auto-fill on drop")) {
                    const bool sticky = isWindowSticky(safeWindow);
                    auto onSnapSuccess = [this](const QString&, const QString& snappedScreenId) {
                        m_snapAssistHandler->showContinuationIfNeeded(snappedScreenId);
                    };
                    tryAsyncSnapCall(PhosphorProtocol::Service::Interface::Snap, QStringLiteral("snapToEmptyZone"),
                                     {windowId, outcome.targetScreenId, sticky}, safeWindow, windowId, true, nullptr,
                                     onSnapSuccess);
                }

                // Snap Assist: show the window picker if the daemon requested
                // it. asyncShow is non-blocking. This fires alongside an
                // ApplySnap outcome (applied==true) BY DESIGN: the daemon only
                // sets requestSnapAssist when the window actually snapped
                // (drop.cpp: `actuallySnapped && ...`) — snap-assist's purpose
                // is to offer filling the REMAINING empty zones after a snap,
                // so it must not be gated on !applied.
                if (outcome.requestSnapAssist && !outcome.emptyZones.isEmpty() && !outcome.targetScreenId.isEmpty()) {
                    m_snapAssistHandler->asyncShow(windowId, outcome.targetScreenId, outcome.emptyZones);
                }
            });
}

void PlasmaZonesEffect::tryAsyncSnapCall(const QString& interface, const QString& method, const QList<QVariant>& args,
                                         QPointer<KWin::EffectWindow> window, const QString& windowId,
                                         bool storePreSnap, std::function<void()> fallback,
                                         std::function<void(const QString&, const QString&)> onSnapSuccess,
                                         bool skipAnimation, std::function<void()> onComplete)
{
    QDBusPendingCall call = PhosphorProtocol::ClientHelpers::asyncCall(interface, method, args);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, window, windowId, storePreSnap, method, fallback, onSnapSuccess, args, skipAnimation,
             onComplete](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                QDBusPendingReply<int, int, int, int, bool> reply = *w;
                if (reply.isError()) {
                    qCDebug(lcEffect) << method << "error:" << reply.error().message();
                    if (fallback)
                        fallback();
                    if (onComplete)
                        onComplete();
                    return;
                }
                if (reply.argumentAt<4>() && window && !window->isDeleted()) {
                    QRect geo(reply.argumentAt<0>(), reply.argumentAt<1>(), reply.argumentAt<2>(),
                              reply.argumentAt<3>());
                    qCInfo(lcEffect) << method << "snapping" << windowId << "to:" << geo;
                    if (storePreSnap)
                        // `window` is non-null inside this branch (guarded by the
                        // `reply.argumentAt<4>() && window` check above), so the
                        // ternary fall-through to QRectF() is unreachable.
                        m_snapHandler->ensurePreSnapGeometryStored(window, windowId, window->frameGeometry());
                    applyWindowGeometry(window, geo, false, skipAnimation);
                    // Async snap (keyboard / empty-zone / last-zone / auto-fill)
                    // committed — record in snapping's border set, but only for
                    // a resolved snap-mode screen (autotile windows are tracked
                    // by AutotileHandler; an empty screen is left untracked,
                    // mirroring the batch path's discriminator).
                    if (const QString asyncScr = getWindowScreenId(window);
                        !asyncScr.isEmpty() && !m_autotileHandler->isAutotileScreen(asyncScr)) {
                        // Defensive stale-float clear — see the drag-drop
                        // commit path; idempotent vs the daemon broadcast.
                        m_navigationHandler->setWindowFloating(windowId, false);
                        m_snapHandler->markWindowSnapped(windowId, asyncScr);
                    } else {
                        // Same discriminator epilogue as the other commit
                        // paths: drop stale snap tracking instead of skipping.
                        m_snapHandler->clearWindowSnapped(windowId);
                    }
                    // args[1] is screenId (e.g. for snapToEmptyZone, snapToLastZone)
                    if (onSnapSuccess && args.size() >= 2) {
                        onSnapSuccess(windowId, args[1].toString());
                    }
                    if (onComplete)
                        onComplete();
                    return;
                }
                if (fallback)
                    fallback();
                if (onComplete)
                    onComplete();
            });
}

void PlasmaZonesEffect::repaintSnapRegions(KWin::EffectWindow* window, const QRectF& oldFrame, const QRect& newGeo)
{
    window->addRepaintFull();
    // Guard the global compositor repaint requests: this method can run
    // from late D-Bus reply callbacks (callEndDrag → applySnap → here)
    // that may dispatch during compositor teardown, when KWin::effects
    // has been torn down. The window-local addRepaintFull above is
    // safe because the EffectWindow itself is alive (we hold a
    // QPointer-checked reference at the call site).
    if (KWin::effects) {
        if (oldFrame.isValid()) {
            KWin::effects->addRepaint(oldFrame.toAlignedRect());
        }
        KWin::effects->addRepaint(newGeo);
    }
}

void PlasmaZonesEffect::applyWindowGeometry(KWin::EffectWindow* window, const QRect& geometry, bool allowDuringDrag,
                                            bool skipAnimation, const QString& profilePath)
{
    if (!window) {
        qCWarning(lcEffect) << "applyGeometry: window is null";
        return;
    }

    // Normalize so width/height are non-negative; reject invalid rects
    QRect geo = geometry.normalized();
    if (!geo.isValid() || geo.width() <= 0 || geo.height() <= 0) {
        qCWarning(lcEffect) << "applyGeometry: invalid or empty geometry:" << geometry;
        return;
    }

    // Don't call moveResize() on fullscreen windows, it can crash KWin.
    // See KDE bugs #429752, #301529, #489546.
    if (window->isFullScreen()) {
        qCDebug(lcEffect) << "applyGeometry: window is fullscreen, skipping";
        return;
    }

    // For X11/XWayland windows, KWin constrains the frame size to align with
    // WM_SIZE_HINTS (size increments for terminals like Ghostty, Kitty, etc.).
    // Pre-compute the constrained size and center the window in its zone so the
    // gap is distributed evenly instead of all at the bottom-right.
    // This applies to all snap operations (zone snap, autotile, resnap, etc.).
    // Wayland-native clients negotiate size async (constrainFrameSize only
    // checks min/max, not char-cell grid), so they're handled by the deferred
    // check in slotWindowFrameGeometryChanged().
    if (window->isX11Client()) {
        KWin::Window* kw = window->window();
        if (kw) {
            const QSizeF constrained = kw->constrainFrameSize(QSizeF(geo.size()));
            const int cw = qRound(constrained.width());
            const int ch = qRound(constrained.height());
            if (cw < geo.width() || ch < geo.height()) {
                // Clamp to non-negative: if min-size exceeds the zone in one
                // dimension, don't shift the window beyond the zone's edge.
                const int dx = qMax(0, geo.width() - cw) / 2;
                const int dy = qMax(0, geo.height() - ch) / 2;
                geo = QRect(geo.x() + dx, geo.y() + dy, cw, ch);
                qCDebug(lcEffect) << "Pre-centered X11 window with size constraints:"
                                  << "zone=" << geometry.size() << "constrained=" << constrained << "adjusted=" << geo;
            }
        }
    }

    // If this window is held invisible until it is repositioned on open
    // (first-frame suppression — see RestoreSuppression), stamp the
    // resolved rect as its settle target. The windowFrameGeometryChanged
    // handler treats the next geometry change as the real reposition (not
    // the client's own initial sizing) only once this target is set.
    if (auto supIt = m_restoreSuppress.find(window); supIt != m_restoreSuppress.end()) {
        supIt->targetGeometry = geo;
    }

    // Skip no-op: if window is already at the target geometry AND there is
    // no in-flight animation, calling moveResize() is redundant and can have
    // subtle stacking side effects on some KWin versions (e.g. during daemon
    // restart double-processing).
    //
    // When an animation IS in flight, frameGeometry() already reflects the
    // committed target from the previous applyWindowGeometry's moveResize —
    // but the visual position is still mid-transition. A rapid reversal
    // (float → unfloat, rotate → rotate back) legitimately targets the same
    // committed geometry and must NOT be skipped, because the animation needs
    // to play from the current visual position to that target.
    // Compare integer-aligned rects: `frameGeometry()` carries qreal
    // precision and on fractional-scale outputs may keep sub-pixel residue
    // from prior moveResize commits, so a float-bit-exact equality against
    // an integer `geo` would silently miss and run a redundant moveResize.
    if (QRectF(geo) == window->frameGeometry().toRect() && !m_windowAnimator->hasAnimation(window)) {
        qCDebug(lcEffect) << "moveResize: window already at target geometry, skipping:" << geo;
        // Release first-frame open suppression here. The settle-detection
        // hook on windowFrameGeometryChanged would otherwise wait forever
        // for a configure that never fires (the resolved zone equals the
        // spawn position — happens on KWin session restore where the
        // saved geometry already matches a snap zone). Hold-suppression
        // exists only to mask the placement→reposition flash; with no
        // reposition coming, the window must paint immediately.
        endRestoreSuppression(window);
        return;
    }

    // INFO level: a standing record of every resolved window placement.
    // Generally useful operationally, and the resolved pixel rect is the one
    // number a support report needs to diagnose zone-geometry bugs (the zone
    // id is logged elsewhere; the resolved rect previously was not). Mirrors
    // the autotile path, which already logs "Autotile tile request: QRect=".
    qCInfo(lcEffect) << "Setting window geometry from" << window->frameGeometry() << "to" << geo;

    // Capture old frame before moveResize for repaint region
    const QRectF oldFrame = window->frameGeometry();

    // In KWin 6, we use the window's moveResize methods
    // When allowDuringDrag is false: defer if window is in user move/resize (snap on release)
    // When allowDuringDrag is true: apply immediately (snap-on-hover during drag)
    if (!allowDuringDrag && (window->isUserMove() || window->isUserResize())) {
        qCDebug(lcEffect) << "Window in user move/resize, deferring geometry via windowFinishUserMovedResized";
        QPointer<KWin::EffectWindow> safeWindow = window;
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(window, &KWin::EffectWindow::windowFinishUserMovedResized, this,
                        [this, safeWindow, geo, skipAnimation, profilePath, conn](KWin::EffectWindow*) {
                            disconnect(*conn);
                            if (safeWindow && !safeWindow->isDeleted() && !safeWindow->isFullScreen()) {
                                applyWindowGeometry(safeWindow, geo, false, skipAnimation, profilePath);
                            }
                        });
        return;
    }

    // Animation: moveResize to the final geometry immediately, then morph
    // the window visually from its old position/size to the new one using
    // translate + scale in paintWindow(). This follows the standard KDE
    // effect pattern — effects are visual overlays, never per-frame moveResize.
    //
    // `shouldAnimateWindow` adds the user's per-animation Window
    // Filtering gate (transient / min-size / app / class) and lets a
    // WindowRule carrying any OverrideAnimation* action override the
    // filter when the rule's class matcher matches. Falling through to
    // the non-animated path just runs the moveResize without the snap
    // motion / shader.
    //
    // First-placement-on-open carve-out: a window moved into its zone/tile
    // moments after opening still has its window.open shader in flight —
    // uniquely marked by the held WindowAddedGrabRole (addedGrabHeld, set only
    // by the slotWindowAdded open path). Installing the snap/tile morph would
    // supersede it (one transition per window), so the open animation never
    // plays. Skip the morph and fall through to the plain moveResize: the
    // first-frame open suppression keeps the window hidden until the move
    // lands, so the open shader plays into the destination rect. Later moves
    // (no open shader in flight) morph normally.
    const auto* inFlightTransition = m_shaderManager.findTransition(window);
    const bool firstPlacementWithOpenShader = inFlightTransition && inFlightTransition->addedGrabHeld;

    if (!skipAnimation && !allowDuringDrag && !firstPlacementWithOpenShader && m_windowAnimator->isEnabled()
        && shouldAnimateWindow(window)) {
        const QRectF targetFrame(geo);

        // Bail before any work when the in-flight animation already
        // targets this frame — saves both the moveResize signal
        // emission AND the rule resolve on rapid retargets to the same
        // zone. Pre-Pass-2 the moveResize ran first and was redundant
        // here (kwin's moveResize is internally a no-op when geometry
        // already matches, but still pays signal-dispatch cost on the
        // hot path of rapid drag retargets).
        if (m_windowAnimator->hasAnimation(window) && m_windowAnimator->isAnimatingToTarget(window, targetFrame)) {
            return; // Already animating to this target
        }

        // Apply final geometry immediately — client starts re-rendering at new size.
        // Do this before touching the animator so the controller's
        // downstream bounds / padding queries see the updated
        // expandedGeometry for this frame.
        KWin::Window* kw = window->window();
        if (kw) {
            kw->moveResize(targetFrame);
        }

        // Per-window animation motion-cascade: a Timing WindowRule for
        // this (windowClass, eventPath) replaces the global animator
        // profile's curve / duration for THIS animation only. No rule →
        // resolver returns the base profile unchanged and no override is
        // passed, preserving the historical fast-path. Retarget intentionally does
        // not re-apply the cascade — once an animation is in flight, it
        // stays on the curve that started it for visual continuity.
        //
        // Empty-rule-list short-circuit: when the user has no app rules
        // configured (the default-state case for most users), skip both
        // the resolver call AND the deep `Profile::operator!=` (which
        // walks `curve->equals` virtual + 5 std::optional comparisons)
        // — the cost is paid on every animated snap otherwise.
        //
        // Build the full per-window query once and reuse for the shader
        // resolver call below — matches the shape `shouldAnimateWindow`
        // uses for its rule-override gate, so a rule that gates the
        // animation also resolves its curve / timing / shader slots.
        const PhosphorWindowRule::WindowQuery query = windowRuleQuery(window);
        const auto& baseProfile = m_windowAnimator->profile();
        const PhosphorAnimation::Profile* motionOverridePtr = nullptr;
        PhosphorAnimation::Profile motionProfile;
        // Empty-rule-set short-circuit: a no-rules user skips both the
        // resolver call AND the deep `Profile::operator!=`. Resolution routes
        // through the unified RuleEvaluator via the effect-local shim.
        if (!m_shaderManager.animationRuleSet().isEmpty()) {
            motionProfile =
                PlasmaZones::resolveAnimationMotionProfile(m_shaderManager.animationRuleEvaluator(), baseProfile, query,
                                                           profilePath, getWindowId(window), m_curveRegistry);
            if (motionProfile != baseProfile)
                motionOverridePtr = &motionProfile;
        }

        if (m_windowAnimator->hasAnimation(window)) {
            // Capture the displaced animation's endpoints before retarget
            // modifies or deletes the entry. On a rapid reversal where
            // advance() hasn't ticked, m_current still equals m_from
            // (the animation's start point), so retarget(newTarget) sees
            // current ≈ newTarget when the reversal goes back to the
            // original zone — degenerate. Use the displaced animation's
            // TARGET as the visual origin for the replacement: that's
            // where the window was visually heading (and where moveResize
            // just committed to), so animating from there to the new
            // target matches the user's expectation.
            const QRectF displacedTarget = m_windowAnimator->animationFor(window)->to();
            const QRectF visualPos = m_windowAnimator->currentValue(window, QRectF(oldFrame));
            const auto result = m_windowAnimator->retargetWithResult(
                window, targetFrame, PhosphorAnimation::RetargetPolicy::PreserveVelocity);
            if (result == PhosphorAnimation::RetargetResult::DegenerateReap) {
                // Retarget collapsed (current visual ≈ new target).
                // Start a fresh animation from the displaced target
                // (where the window was heading) to the new target.
                // If that's also degenerate (same point), startAnimation
                // returns false and no animation plays — correct, since
                // there's no visual distance to cover.
                const QRectF animFrom = (displacedTarget != targetFrame) ? displacedTarget : visualPos;
                m_windowAnimator->startAnimation(window, animFrom, targetFrame, motionOverridePtr);
            }
        } else {
            m_windowAnimator->startAnimation(window, QRectF(oldFrame), targetFrame, motionOverridePtr);
        }

        if (m_windowAnimator->hasAnimation(window)) {
            // Same cascade as tryBeginShaderForEvent: rule layer wins
            // for matching windows; engaged-empty rule effectId blocks
            // the tree fallthrough. Reuse the `query` local from the
            // motion-cascade above instead of rebuilding the WindowQuery.
            //
            // Route through `resolveAnimationShaderAndDuration` (which
            // uses `evaluator.resolveCached(windowId, query)`). The
            // sister `resolveAnimationMotionProfile` call above already
            // populated the per-window cache slot for this query, so
            // this cached read is a hit. The earlier shape called a
            // standalone uncached shader-profile resolver here, which
            // paid an extra priority-order walk per snap on every
            // non-empty rule set — same regression the shim was
            // introduced to fix for `tryBeginShaderForEvent` (see the
            // historical-pair note in shader_resolve.cpp).
            //
            // The duration field is intentionally discarded: the snap
            // shader path leaves durationMs at zero on purpose —
            // paintWindow rides the WindowAnimator's timeline. The
            // Timing-rule duration override is honoured transitively
            // via `motionProfile` above (driving the animator's
            // duration), so the shader still terminates with the
            // rule-overridden snap motion.
            const auto resolved = PlasmaZones::resolveAnimationShaderAndDuration(
                m_shaderManager.animationRuleEvaluator(), m_shaderManager.profileTree(), getWindowId(window), query,
                profilePath, /*defaultDurationMs=*/0);
            auto shaderProfile = resolved.profile;
            if (!resolved.shaderSlotFromRule && shaderProfile.effectiveEffectId().isEmpty()) {
                // No rule matched and no tree override resolved a shader for
                // this move event — apply the built-in per-event default
                // (window-morph for snap/move/resize) via the shared SSOT,
                // which respects an explicit tree "None". Keeps the default
                // consistent with what the settings UI shows
                // (resolvedShaderProfile uses the same helper) without
                // persisting it into config. Gated on `!shaderSlotFromRule`: a
                // per-app window rule that set "None" (engaged-empty effectId)
                // is a deliberate opt-out and must NOT be overridden here.
                shaderProfile =
                    PhosphorAnimationShaders::resolveShaderWithDefault(m_shaderManager.profileTree(), profilePath);
            }
            if (!shaderProfile.effectiveEffectId().isEmpty()) {
                beginShaderTransition(window, shaderProfile);
                // If the installed shader is a geometry morph (declares
                // iFromRect), hand it the old/new frames and request the
                // old-content snapshot. The morph then owns the visual
                // geometry animation — it interpolates the drawn rect from
                // oldFrame to targetFrame and cross-fades the old snapshot
                // into the live new content — so paintWindow gates off the
                // C++ WindowAnimator translate+scale for this window. The
                // WindowAnimator still runs (durationMs == 0) purely to drive
                // the morph's progress timeline.
                if (auto* mt = m_shaderManager.findTransition(window);
                    mt && mt->cached && mt->cached->iFromRectLoc >= 0) {
                    // Always retarget the morph to the new destination.
                    mt->toGeometry = targetFrame;
                    // On a RETARGET mid-morph, beginShaderTransition short-
                    // circuits (same shader) and keeps the existing transition,
                    // so its captured snapshot already holds the ORIGINAL old
                    // content. Preserve it (and fromGeometry) and only redirect
                    // the destination — re-capturing here would grab the
                    // mid-morph/new content and collapse the cross-fade. Only a
                    // fresh morph (no snapshot yet) anchors fromGeometry and
                    // requests the capture.
                    if (!mt->oldSnapshot) {
                        mt->fromGeometry = QRectF(oldFrame);
                        mt->needsSnapshot = true;
                    }
                }
            }
        }

        repaintSnapRegions(window, oldFrame, geo);
        return;
    }

    // No animation path (disabled, during drag, etc.): apply moveResize directly.
    if (m_windowAnimator->hasAnimation(window)) {
        m_windowAnimator->removeAnimation(window);
    }

    KWin::Window* kwinWindow = window->window();
    if (kwinWindow) {
        // DEBUG: the resolved rect is already logged at INFO above ("Setting
        // window geometry from ... to ..."), which covers both the animated
        // and non-animated paths — keep this one at debug to avoid a
        // duplicate INFO line for the same apply.
        qCDebug(lcEffect) << "moveResize: QRect=" << geo << "-> QRectF=" << QRectF(geo);
        kwinWindow->moveResize(QRectF(geo));

        repaintSnapRegions(window, oldFrame, geo);
    } else {
        qCWarning(lcEffect) << "Cannot get underlying Window from EffectWindow";
    }
}

void PlasmaZonesEffect::slotRestoreSizeDuringDrag(const QString& windowId, int width, int height)
{
    // Restore pre-snap size when cursor leaves zone during drag. The window may have been
    // snapped when the drag started (at zone size); when the user drags out of all zones,
    // we restore to floated state immediately so they see the window return to original size.
    // This complements the release path (dragStopped) which also handles restore.
    if (!m_dragTracker->isDragging() || m_dragTracker->draggedWindowId() != windowId) {
        return;
    }

    KWin::EffectWindow* window = m_dragTracker->draggedWindow();
    if (!window || !shouldHandleWindow(window)) {
        return;
    }

    if (width <= 0 || height <= 0) {
        return;
    }

    // Restore-size-only: keep current position, apply pre-snap width/height
    QRectF frame = window->frameGeometry();
    // qRound, not truncation — fractional-scale sub-pixel residue (see above).
    QRect geometry(qRound(frame.x()), qRound(frame.y()), width, height);

    qCDebug(lcEffect) << "Restoring size during drag:" << windowId << geometry;
    // Live drag-out unsnap: restoring pre-snap dimensions while the user is still dragging.
    // Logically a snap-out (the window is leaving zone-managed sizing).
    applyWindowGeometry(window, geometry, /*allowDuringDrag=*/true, /*skipAnimation=*/false,
                        PhosphorAnimation::ProfilePaths::WindowSnapOut);
}

void PlasmaZonesEffect::slotDragPolicyChanged(const QString& windowId, const PhosphorProtocol::DragPolicy& newPolicy)
{
    // Daemon-owned cross-VS flip. The daemon's updateDragCursor
    // handler computed policy at the current cursor position and found it
    // different from the policy in force — tell us so we can apply the
    // compositor-level transition. Replaces the effect-side cross-VS flip
    // loop in the dragMoved lambda that walked KWin::effects->screens()
    // with a stale m_autotileScreens cache.
    //
    // Guards: this slot only acts if we're actively tracking the drag for
    // this windowId. Stray signals (daemon restart, out-of-order delivery)
    // are ignored.
    if (!m_dragTracker->isDragging() || m_dragTracker->draggedWindowId() != windowId) {
        qCDebug(lcEffect) << "slotDragPolicyChanged: drag no longer active for" << windowId;
        return;
    }

    if (const QString err = newPolicy.validationError(); !err.isEmpty()) {
        // Garbled policy change — keep current state rather than transitioning
        // to a corrupted one. The daemon will re-emit on the next cursor tick
        // if this was transient.
        qCWarning(lcEffect) << "slotDragPolicyChanged rejected:" << err << "for" << windowId;
        return;
    }

    const PhosphorProtocol::DragBypassReason oldReason = m_currentDragPolicy.bypassReason;
    const PhosphorProtocol::DragBypassReason newReason = newPolicy.bypassReason;
    if (oldReason == newReason) {
        // Same reason but different screenId (autotile→autotile cross-VS):
        // update the captured screen so endDrag's ApplyFloat uses the right one.
        m_currentDragPolicy = newPolicy;
        if (newReason == PhosphorProtocol::DragBypassReason::AutotileScreen) {
            m_dragBypassScreenId = newPolicy.screenId;
        }
        return;
    }

    qCInfo(lcEffect) << "slotDragPolicyChanged:" << windowId << oldReason << "->" << newReason
                     << "screen=" << newPolicy.screenId;

    m_currentDragPolicy = newPolicy;

    KWin::EffectWindow* dragW = m_dragTracker->draggedWindow();

    if (newReason == PhosphorProtocol::DragBypassReason::AutotileScreen) {
        // Snap → autotile (or context-disabled → autotile). Cancel any
        // active snap overlay, enter bypass mode. Mirrors the old
        // effect-side flip block's "snap→autotile" branch, but driven by
        // daemon truth rather than an effect-cached screen set.
        if (!m_dragBypassedForAutotile) {
            m_snapHandler->callCancelSnap();
            m_dragBypassedForAutotile = true;
            m_dragBypassScreenId = newPolicy.screenId;
        } else {
            // Already in bypass but on a different autotile screen — just
            // update the captured screen id.
            m_dragBypassScreenId = newPolicy.screenId;
        }
        return;
    }

    if (oldReason == PhosphorProtocol::DragBypassReason::AutotileScreen) {
        // Autotile → snap (or autotile → context-disabled). Drop the
        // bypass flag and initialize snap-drag state as if the drag just
        // started on this snap screen. Remove the window from autotile
        // tracking so slotWindowFrameGeometryChanged doesn't fight the
        // snap geometry on subsequent geometry changes.
        //
        // Do NOT call handleDragToFloat here: the mid-drag schedule would
        // race against the zone snap at drop, making the window jump after
        // the user lets go. onWindowClosed alone clears the tracking state.
        if (dragW) {
            m_autotileHandler->onWindowClosed(windowId, m_dragBypassScreenId);
        }
        m_dragBypassedForAutotile = false;
        m_dragActivationDetected = false;
        if (!m_keyboardGrabbed) {
            KWin::effects->grabKeyboard(this);
            m_keyboardGrabbed = true;
        }
        return;
    }

    // Other transitions (snap ↔ context_disabled / snapping_disabled):
    // no compositor-level work needed. The daemon will return a NoOp at
    // endDrag for disabled paths.
}

} // namespace PlasmaZones
