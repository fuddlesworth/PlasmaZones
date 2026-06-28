// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include "../autotilehandler.h"
#include "../dragtracker.h"
#include "../navigationhandler.h"
#include "../snapassisthandler.h"
#include "../snaphandler.h"

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

} // namespace PlasmaZones
