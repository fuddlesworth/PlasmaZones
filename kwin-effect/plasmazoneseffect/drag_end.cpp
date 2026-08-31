// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include "tilinghandler/tilinghandler.h"
#include "handlers/dragtracker.h"
#include "handlers/navigationhandler.h"
#include "handlers/snapassisthandler.h"
#include "handlers/snaphandler.h"
#include "compositor/effectlogging.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/DragMarshalling.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>
#include <window.h>

#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>

#include <memory>
#include <optional>

namespace PlasmaZones {

namespace {
// Upper bound on how long the effect waits for the daemon's endDrag reply.
// If the daemon is blocked (layout recompute, overlay teardown, heavy
// handler), exceeding this budget means the compositor would otherwise
// stall waiting on a reply that may never come. On expiry the window is
// left at its release position and a warning is logged.
constexpr int EndDragTimeoutMs = 500;
} // namespace

void PlasmaZonesEffect::callEndDrag(KWin::EffectWindow* window, const QString& windowId, bool cancelled)
{
    // Single entry point for drag-end dispatch.
    // Sends endDrag, receives a PhosphorProtocol::DragOutcome, and applies exactly the
    // action the daemon decided. Replaces callDragStopped (whose reply
    // shape was a 9-tuple of out-params) with a typed struct.
    QPointF cursorAtRelease = m_dragTracker->lastCursorPos();

    // Snapshot the drag-start floating state NOW, synchronously at dispatch.
    // The ApplyFloat branch in the async reply lambda below consults it, but
    // the member is overwritten by the next DragTracker::dragStarted — if a
    // successive drag starts before this endDrag reply lands (the daemon may
    // take up to EndDragTimeoutMs), reading the member there would see the
    // wrong drag's state. Capturing a local is the same staleness guard the
    // beginDrag reply gets from m_dragActivation.generation.
    const bool startedFloating = m_dragActivation.startedFloating;

    // Identity of the interactive move/resize this drag belongs to, captured
    // synchronously at dispatch. The three rescue sites in the reply lambda
    // below (ApplyFloat's end, ApplySnap's and RestoreSize's cancel) all read
    // isUserMove() at reply time, up to EndDragTimeoutMs later. By then the
    // user may have released the remaining button and begun a NEW gesture on
    // the same window (a Meta+RMB resize, say) — the predicate cannot tell the
    // two apart, so the rescue would kill a move the user just started. KWin
    // bumps this counter for every interactive move/resize (it is the same
    // handle PlacementTracker uses to tell "the user has interacted since my
    // snapshot"), so requiring it to still match scopes each rescue to the
    // gesture the drag actually owned.
    //
    // Empty unless a move was ALREADY live at dispatch, and that condition is
    // what makes the compare sound without depending on whether KWin bumps the
    // counter at the start or the end of a gesture: captured mid-move, any
    // later gesture reads a different value under either convention. Captured
    // while no move was running, an end-of-gesture bump would leave the next
    // move reading the same value and the guard would pass wrongly.
    const std::optional<uint32_t> dragMoveGeneration = [window]() -> std::optional<uint32_t> {
        if (!window || !window->isUserMove()) {
            return std::nullopt;
        }
        KWin::Window* kw = window->window();
        if (!kw) {
            return std::nullopt;
        }
        return kw->interactiveMoveResizeCount();
    }();

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
    // QPointer: the `handled` handshake already prevents a double-delete, but
    // a raw watcher capture would still dangle if that invariant ever slips.
    connect(timeoutTimer, &QTimer::timeout, this,
            [this, windowId, handled, watcherGuard = QPointer<QDBusPendingCallWatcher>(watcher), timeoutTimer]() {
                if (*handled) {
                    return;
                }
                *handled = true;
                qCWarning(lcEffect) << "endDrag timed out after" << EndDragTimeoutMs
                                    << "ms; daemon unresponsive. Leaving window" << windowId << "at release position.";
                // The window still sits wherever the user dropped it, on whatever
                // screen that is, so a crossing the handlers deferred during the
                // drag has to be re-resolved even though no outcome ever arrived.
                drainDragSuppressedRuleInvalidations();
                if (watcherGuard) {
                    watcherGuard->deleteLater();
                }
                timeoutTimer->deleteLater();
            });
    timeoutTimer->start(EndDragTimeoutMs);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeWindow, windowId, handled, timeoutTimer, startedFloating,
             dragMoveGeneration](QDBusPendingCallWatcher* w) {
                // True only while THIS drag's interactive move is still the one
                // KWin is running, and only when the left button is already up
                // (the case the rescues exist for: KWin waits for the last
                // button, so the move outlives the drop). Any gesture the user
                // began after dispatch reads a different generation and is left
                // alone. See dragMoveGeneration's capture above.
                auto rescuableMove = [&safeWindow, &dragMoveGeneration, this]() -> KWin::Window* {
                    if (!dragMoveGeneration || !safeWindow || !safeWindow->isUserMove()) {
                        return nullptr;
                    }
                    if (m_currentMouseButtons & Qt::LeftButton) {
                        return nullptr;
                    }
                    KWin::Window* kw = safeWindow->window();
                    if (!kw || kw->interactiveMoveResizeCount() != *dragMoveGeneration) {
                        return nullptr;
                    }
                    return kw;
                };
                w->deleteLater();
                if (*handled) {
                    // Timeout already fired; this is a late reply — discard.
                    return;
                }
                *handled = true;
                timeoutTimer->stop();
                timeoutTimer->deleteLater();

                // Both failure paths below leave the window at its release
                // position on its release screen, so the deferred cross-screen
                // invalidations are still owed even though no action is applied.
                QDBusPendingReply<PhosphorProtocol::DragOutcome> reply = *w;
                if (reply.isError()) {
                    qCWarning(lcEffect) << "endDrag call failed:" << reply.error().message();
                    drainDragSuppressedRuleInvalidations();
                    return;
                }
                const PhosphorProtocol::DragOutcome outcome = reply.value();
                if (const QString err = outcome.validationError(); !err.isEmpty()) {
                    // Garbled outcome — refuse to apply any window transform.
                    // Better to leave the window where it is than to float/snap
                    // based on a corrupted payload.
                    qCWarning(lcEffect) << "endDrag outcome rejected:" << err
                                        << "— dropping without applying any action for" << windowId;
                    drainDragSuppressedRuleInvalidations();
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
                    //
                    // Deliberately NO interactive-move rescue here, and none in
                    // NotifyDragOutUnsnap either. Only ApplyFloat, ApplySnap and
                    // RestoreSize carry one, because only those three write
                    // geometry, which a live KWin move would fight — the move
                    // has to end first. These write none: if a non-left button is still down
                    // the user is simply still dragging, and KWin keeping the
                    // move alive until the last button comes up is the correct
                    // behaviour for any window. Ending it here would cut a
                    // gesture short mid-drag.
                    break;

                case PhosphorProtocol::DragOutcome::NotifyDragOutUnsnap:
                    // Window was dragged out of its zone — no longer snap-managed.
                    m_snapHandler->clearWindowSnapped(windowId);
                    // Snapped → unsnapped flips the Mode / IsSnapped rule fields;
                    // re-resolve now (symmetric with the snap-commit path below).
                    invalidateRuleCacheForStateChange(windowId);
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
                    // Same rescue as ApplySnap / RestoreSize, but END rather
                    // than cancel: a float drop keeps the window where it was
                    // dropped, and cancelInteractiveMoveResize would revert it
                    // to the drag-start rect. Without this, a drop where only
                    // a non-left button is still held leaves KWin's move live,
                    // and the window then follows every desktop switch until
                    // the last button comes up somewhere KWin's filter can see.
                    if (KWin::Window* kw = rescuableMove()) {
                        kw->endInteractiveMoveResize();
                    }
                    // Only run the float transition (which restores the
                    // pre-autotile size) when the window was TILED at drag
                    // start. A window that was already floating is merely being
                    // moved — handleDragToFloat would re-apply the stale
                    // pre-autotile rect and clobber any resize the user made
                    // while it was floating. The float-screen reassignment
                    // (setWindowFloatingForScreen) below still runs so a
                    // cross-screen move updates the daemon's float tracking.
                    if (!startedFloating) {
                        m_tilingHandler->handleDragToFloat(safeWindow, windowId);
                    } else {
                        // Already floating at drag start, but the SCROLL
                        // tiled set can still hold the window (a daemon
                        // float that raced the drag start): clear it, or
                        // the tracked-screen override below pins the drop
                        // to the source strip. Idempotent, geometry
                        // untouched.
                        m_tilingHandler->clearWindowTiledAllScreens(windowId);
                    }
                    // Window is now floating — drop it from snapping's set.
                    m_snapHandler->clearWindowSnapped(windowId);
                    // The window is floating now — the Mode / IsSnapped /
                    // IsFloating rule fields have already flipped, so
                    // re-resolve before anything can bail out below. Waiting
                    // until after the drop-screen resolve left the cache
                    // holding the pre-float answer whenever the screen came
                    // back empty.
                    invalidateRuleCacheForStateChange(windowId);
                    // Resolve the drop screen only AFTER the float cleanup
                    // above cleared tiled membership: while the window was
                    // still scroll-tiled, getWindowScreenId answers from the
                    // engine override and would pin a cross-monitor drag-out
                    // to the SOURCE strip's screen instead of where the user
                    // actually dropped it.
                    //
                    // Fall back to the daemon's own release screen when the
                    // compositor-side resolve comes back empty. Everything above
                    // has already committed the float effect-side (tiled
                    // membership cleared, snap entry dropped, rules
                    // re-resolved); bailing here left the daemon's per-screen
                    // float slot unwritten, so its float readers still answered
                    // "not floating" for a window this side had floated.
                    QString dropScreenId = getWindowScreenId(safeWindow);
                    if (dropScreenId.isEmpty()) {
                        dropScreenId = outcome.targetScreenId;
                    }
                    if (dropScreenId.isEmpty()) {
                        qCWarning(lcEffect) << "endDrag ApplyFloat: no drop screen resolved for" << windowId
                                            << "— float committed in the effect but not recorded in the daemon.";
                        break;
                    }
                    // Keep the tiling handler's notified-screen record on the
                    // screen the window was actually dropped on. Neither float
                    // path above touches it, so after a cross-screen float drag
                    // it still names the source and the next outputChanged
                    // would diff against a screen the window already left.
                    // No-op for a window the handler does not track.
                    m_tilingHandler->updateNotifiedScreen(windowId, dropScreenId);
                    // Note: m_dragActivation.floatedWindowIds is intentionally NOT re-set here.
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
                    if (KWin::Window* kw = rescuableMove()) {
                        kw->cancelInteractiveMoveResize();
                    }
                    applyWindowGeometry(safeWindow, snapGeometry);
                    // Drag-drop snap committed — record in snapping's border set,
                    // but only for a resolved snap-mode screen. An empty
                    // (unresolved) or autotile-managed screen is owned by
                    // TilingHandler, so recording it here would double-track the
                    // window — same discriminator as the other snap-commit paths.
                    if (const QString scr =
                            !outcome.targetScreenId.isEmpty() ? outcome.targetScreenId : getWindowScreenId(safeWindow);
                        !scr.isEmpty() && !m_tilingHandler->isManagedScreen(scr)) {
                        // Defensively clear any stale local float flag before
                        // recording the snap — a surviving flag poisons the
                        // next pre-tile capture and wrongly exempts the window
                        // from the drain-time restore veto (same rationale as
                        // the single-window and batch apply paths). Idempotent
                        // when the daemon's windowFloatingChanged(false)
                        // broadcast already landed.
                        m_navigationHandler->setWindowFloating(windowId, false);
                        m_snapHandler->markWindowSnapped(windowId, scr);
                        // Floating → snapped changes the Mode / IsSnapped rule
                        // match fields. Invalidate the per-window match cache so a
                        // placement-scoped border / opacity rule re-resolves now,
                        // rather than waiting for the daemon's windowStateChanged
                        // broadcast (self-contained, mirrors the autotile path).
                        invalidateRuleCacheForStateChange(windowId);
                    } else {
                        // Unresolved or autotile-owned screen: this commit is
                        // not snap-tracked — drop any stale snap entry +
                        // decoration claim instead of merely skipping, same
                        // discriminator epilogue as the single-window and
                        // batch apply paths.
                        m_snapHandler->clearWindowSnapped(windowId);
                        // Symmetric with the snap-tracked branch above: the
                        // window's snap state changed, so re-resolve its rules.
                        invalidateRuleCacheForStateChange(windowId);
                    }
                    break;
                }

                case PhosphorProtocol::DragOutcome::RestoreSize: {
                    if (!safeWindow || safeWindow->isDeleted() || safeWindow->isFullScreen()) {
                        break;
                    }
                    // Drag-to-unsnap: apply pre-snap width/height at current
                    // position. The GEOMETRY is skipped if
                    // slotRestoreSizeDuringDrag already applied it during the
                    // drag (size within 1px) — but only the geometry. That slot
                    // resizes and nothing else (drag_snap.cpp: a single
                    // applyWindowGeometry), and the daemon emits only
                    // restoreSizeDuringDragChanged alongside it, so nothing has
                    // cleared the snap tracking or the rule cache by the time we
                    // get here. Breaking out early left the window in the snap
                    // handler's border set with its rules still resolved as
                    // snapped, on exactly the common path where the mid-drag
                    // restore landed first. Both calls below are idempotent, so
                    // they run whether or not the resize was still owed.
                    QRectF frame = safeWindow->frameGeometry();
                    const bool sizeAlreadyRestored =
                        qAbs(frame.width() - outcome.width) <= 1 && qAbs(frame.height() - outcome.height) <= 1;
                    if (sizeAlreadyRestored) {
                        qCDebug(lcEffect) << "endDrag RestoreSize: already at correct size, skipping the resize";
                    } else {
                        // qRound, not truncation: fractional-scale outputs leave
                        // sub-pixel residue in frameGeometry() (same convention as
                        // the toRect() sites).
                        const QRect geo(qRound(frame.x()), qRound(frame.y()), outcome.width, outcome.height);
                        if (KWin::Window* kw = rescuableMove()) {
                            kw->cancelInteractiveMoveResize();
                        }
                        // Drag-to-unsnap: window leaves zone-managed sizing, restore pre-snap dimensions.
                        applyWindowGeometry(safeWindow, geo, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                                            PhosphorAnimation::ProfilePaths::WindowSnapOut);
                    }
                    // Drag-to-unsnap: window left zone-managed sizing.
                    m_snapHandler->clearWindowSnapped(windowId);
                    // Unsnapped — flips the Mode / IsSnapped rule fields; re-resolve.
                    invalidateRuleCacheForStateChange(windowId);
                    break;
                }
                }

                // A drop that changed the window's screen without changing its
                // snap state (NoOp / CancelSnap) still stales the per-screen
                // match inputs (ScreenId, ScreenOrientation, ActiveLayout). The
                // outputChanged / VS-crossing handlers skip their own
                // invalidation while a drag is in flight and record the window id
                // in m_dragSuppressedRuleInvalidations instead, so draining that
                // set here is what covers those two outcomes. It cannot be
                // rediscovered by comparing screens at this point: both handlers
                // stamp m_trackedScreenPerWindow unconditionally, ahead of their
                // drag gate, so the tracked screen already equals the live one.
                // Idempotent with the per-branch calls above (the flush coalesces
                // the turn), so it runs for every outcome rather than only the two.
                drainDragSuppressedRuleInvalidations();

                // Auto-fill: if window was dropped without snapping to a
                // zone and wasn't floated, try the first empty zone on the
                // release screen. Daemon-provided targetScreenId wins over
                // window's current screen (cross-screen drags).
                // RestoreSize and NotifyDragOutUnsnap are counted here too, and
                // not because they placed the window: they are the daemon
                // saying the user deliberately dragged it OUT of its zone. The
                // daemon stamps targetScreenId before the shouldApply split
                // (drag_protocol.cpp), so it is populated for those outcomes as
                // well, and on an auto-assign layout the fill below would find
                // the zone the drag just vacated empty and snap the window
                // straight back into it. Drag-out would be impossible there.
                const bool autoFillSuppressed = outcome.action == PhosphorProtocol::DragOutcome::ApplySnap
                    || outcome.action == PhosphorProtocol::DragOutcome::ApplyFloat
                    || outcome.action == PhosphorProtocol::DragOutcome::RestoreSize
                    || outcome.action == PhosphorProtocol::DragOutcome::NotifyDragOutUnsnap;
                // isDeleted: don't auto-fill a zone for a close-grabbed dying
                // window — the daemon would commit an assignment for a dead id.
                if (!autoFillSuppressed && safeWindow && !safeWindow->isDeleted() && !outcome.targetScreenId.isEmpty()
                    && isDaemonReady("auto-fill on drop")) {
                    const bool sticky = isWindowSticky(safeWindow);
                    auto onSnapSuccess = [this](const QString&, const QString& snappedScreenId) {
                        m_snapAssistHandler->showContinuationIfNeeded(snappedScreenId);
                    };
                    tryAsyncSnapCall(PhosphorProtocol::Service::Interface::Snap, QStringLiteral("snapToEmptyZone"),
                                     {windowId, outcome.targetScreenId, sticky}, safeWindow, windowId,
                                     /*storePreSnap=*/true, /*fallback=*/nullptr, onSnapSuccess);
                }

                // Snap Assist: show the window picker if the daemon requested
                // it. asyncShow is non-blocking. This fires alongside an
                // ApplySnap outcome (autoFillSuppressed==true) BY DESIGN: the daemon only
                // sets requestSnapAssist when the window actually snapped
                // (drop.cpp: `actuallySnapped && ...`) — snap-assist's purpose
                // is to offer filling the REMAINING empty zones after a snap,
                // so it must not be gated on !autoFillSuppressed.
                if (outcome.requestSnapAssist && !outcome.emptyZones.isEmpty() && !outcome.targetScreenId.isEmpty()) {
                    m_snapAssistHandler->asyncShow(windowId, outcome.targetScreenId, outcome.emptyZones);
                }
            });
}

} // namespace PlasmaZones
