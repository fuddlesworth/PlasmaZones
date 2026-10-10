// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include "dragpolicytransition.h"
#include "tilinghandler/tilinghandler.h"
#include "handlers/dragtracker.h"
#include "handlers/navigationhandler.h"
#include "handlers/snaphandler.h"
#include "compositor/effectlogging.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/DragMarshalling.h>

#include <effect/effecthandler.h>

#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QPointer>

namespace PlasmaZones {

bool PlasmaZonesEffect::borderActivated(KWin::ElectricBorder border)
{
    Q_UNUSED(border)
    // We no longer reserve edges, so this callback won't be triggered by our effect.
    // The daemon handles disabling Quick Tile via KWin config.
    return false;
}

// The kwin-effect no longer calls the legacy dragStarted D-Bus method;
// beginDrag sets up snap-path state internally on the daemon side, so
// there's only one code path into the drag state machine. The dragMoved
// lambda sends updateDragCursor directly via ClientHelpers::fireAndForget.
// callEndDrag (the drag-end outcome dispatch) lives in drag_end.cpp.

void PlasmaZonesEffect::tryAsyncSnapCall(const QString& interface, const QString& method, const QList<QVariant>& args,
                                         QPointer<KWin::EffectWindow> window, const QString& windowId,
                                         bool storePreSnap, std::function<void()> fallback,
                                         std::function<void(const QString&, const QString&)> onSnapSuccess,
                                         bool skipAnimation, std::function<void()> onComplete,
                                         std::function<void()> onError)
{
    // The free frame, captured BEFORE the call: the daemon commits the snap
    // while answering it, and a capture after the reply meets a window already
    // in a zone, which the daemon refuses. Overwrite, because this frame is the
    // most recent free spot.
    if (storePreSnap && window && !window->isDeleted()) {
        m_snapHandler->ensurePreSnapGeometryStored(window, windowId, QRectF(window->frameGeometry()),
                                                   /*overwrite=*/true);
    }
    QDBusPendingCall call = PhosphorProtocol::ClientHelpers::asyncCall(interface, method, args);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(
        watcher, &QDBusPendingCallWatcher::finished, this,
        [this, window, windowId, storePreSnap, method, fallback, onSnapSuccess, args, skipAnimation, onComplete,
         onError](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<int, int, int, int, bool> reply = *w;
            if (reply.isError()) {
                qCDebug(lcEffect) << method << "error:" << reply.error().message();
                if (onError)
                    onError();
                else if (fallback)
                    fallback();
                if (onComplete)
                    onComplete();
                return;
            }
            if (reply.argumentAt<4>() && (!window || window->isDeleted())) {
                // The daemon DID resolve/commit — the window just died in
                // flight. This is not a restore miss: onMiss/fallback
                // would drop restart-candidate state a same-app reopen
                // may still need. Nothing to apply; just complete.
                if (onComplete)
                    onComplete();
                return;
            }
            if (reply.argumentAt<4>() && window && !window->isDeleted()) {
                QRect geo(reply.argumentAt<0>(), reply.argumentAt<1>(), reply.argumentAt<2>(), reply.argumentAt<3>());
                qCInfo(lcEffect) << method << "snapping" << windowId << "to:" << geo;
                // The placement statement before the apply. The pre-snap
                // capture ran before the call, so its
                // freeGeometryForCapture already read the maximize state
                // the hand-back consumes. Ungated on isManagedScreen,
                // unlike the daemon_apply sites (their slots also carry
                // float restores): this funnel always commits a zone
                // placement and applies the rect unconditionally below.
                //
                // The purpose follows storePreSnap, which only a user
                // verb sets (the auto-fill on drop); a restore reply
                // re-states a placement the window had.
                const PlacementStatement::Purpose purpose =
                    storePreSnap ? PlacementStatement::Purpose::UserVerb : PlacementStatement::Purpose::Restatement;
                // The screen the reply places the window on, read from the
                // rect, not from the window: an apply that changes its size
                // or output has not landed yet (F117). The tracked screen is
                // pre-seeded with it and the apply bracketed, the pair every
                // other daemon-driven apply carries (F294).
                const QPoint centre = geo.center();
                const QString asyncScr = resolveEffectiveScreenId(centre, KWin::effects->screenAt(centre));
                if (!asyncScr.isEmpty()) {
                    m_trackedScreenPerWindow[window] = asyncScr;
                    m_tilingHandler->updateNotifiedScreen(windowId, asyncScr);
                    reportActiveWindowScreen(window, asyncScr);
                }
                if (const PlacementStatement::Verdict verdict = m_tilingHandler->preparePlacement(window, geo, purpose);
                    verdict.apply) {
                    const auto applyGuard = geometryApplyScope();
                    applyWindowGeometry(window, verdict.applyRect, false, skipAnimation,
                                        PhosphorAnimation::ProfilePaths::WindowPlaceIn, QRectF(), QRectF(), purpose);
                }
                // Async snap (keyboard / empty-zone / last-zone / auto-fill)
                // committed — record in snapping's border set, but only for
                // a resolved snap-mode screen (autotile windows are tracked
                // by TilingHandler; an empty screen is left untracked,
                // mirroring the batch path's discriminator).
                if (!asyncScr.isEmpty() && !m_tilingHandler->isManagedScreen(asyncScr)) {
                    // Defensive stale-float clear — see the drag-drop
                    // commit path; idempotent vs the daemon broadcast.
                    m_navigationHandler->setWindowFloating(windowId, false);
                    m_snapHandler->markWindowSnapped(windowId, asyncScr);
                    // Floating → snapped changes the Mode / IsSnapped rule
                    // match fields. Invalidate the per-window match cache so a
                    // placement-scoped border / opacity rule re-resolves now,
                    // rather than waiting for the daemon's windowStateChanged
                    // broadcast (self-contained, mirrors the autotile path).
                    invalidateRuleCacheForStateChange(windowId);
                } else {
                    // Same discriminator epilogue as the other commit
                    // paths: drop stale snap tracking instead of skipping.
                    m_snapHandler->clearWindowSnapped(windowId);
                    // Symmetric with the snap-tracked branch: re-resolve rules.
                    invalidateRuleCacheForStateChange(windowId);
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
            return;
        });
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

    // Upper bound as well as a lower one. This is an unvalidated D-Bus wire
    // value (the daemon's recorded pre-snap size), and the pair goes straight
    // into a moveResize — the same discipline the scrolling visual-delta pair
    // gets on the way in (tiling.cpp bounds it before it can reach the paint
    // path). The full virtual screen, not this window's output: a window
    // manually resized to span two monitors before it was snapped has a
    // legitimate pre-snap size larger than either one, and clamping to a
    // single output would shrink it on every drag-out. Anything past the whole
    // desktop is garbage by construction, so bound rather than reject: a
    // clamped restore still returns the window to a usable size, where a
    // rejected one leaves it stuck at the zone's.
    const QSize virtualSize = KWin::effects ? KWin::effects->virtualScreenSize() : QSize();
    if (virtualSize.isValid()) {
        width = qMin(width, virtualSize.width());
        height = qMin(height, virtualSize.height());
    }

    // Restore-size-only: keep current position, apply pre-snap width/height
    QRectF frame = window->frameGeometry();
    // qRound, not truncation — fractional-scale sub-pixel residue (see the
    // no-op skip in applyWindowGeometry, window_geometry_apply.cpp).
    QRect geometry(qRound(frame.x()), qRound(frame.y()), width, height);

    qCDebug(lcEffect) << "Restoring size during drag:" << windowId << geometry;
    // Live drag-out unsnap: restoring pre-snap dimensions while the user is
    // still dragging. No animation profile is passed, and that is deliberate
    // rather than an omission: allowDuringDrag skips the whole animated branch
    // in applyWindowGeometry, so a during-drag restore is instant by
    // construction and any profile handed in here would be dead argument. The
    // window is following the pointer — it has to resize under the cursor now,
    // not ease toward the size over the next few frames.
    applyWindowGeometry(window, geometry, /*allowDuringDrag=*/true, /*skipAnimation=*/false);
}

void PlasmaZonesEffect::slotDragPolicyChanged(const QString& windowId, const PhosphorProtocol::DragPolicy& newPolicy)
{
    // Daemon-owned cross-VS flip. The daemon's updateDragCursor
    // handler computed policy at the current cursor position and found it
    // different from the policy in force — tell us so we can apply the
    // compositor-level transition. Replaces the effect-side cross-VS flip
    // loop in the dragMoved lambda that walked KWin::effects->screens()
    // with a stale m_managedScreens cache.
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
    if (oldReason != newPolicy.bypassReason) {
        qCInfo(lcEffect) << "slotDragPolicyChanged:" << windowId << oldReason << "->" << newPolicy.bypassReason
                         << "screen=" << newPolicy.screenId;
    }
    // Every emission is applied, a same-reason one included: a tiling engine to
    // another can change the screen and the keyboard answer, and the LATCH, not
    // the reason, says whether there is a bypass to leave (the drag-start fast
    // path can latch while the policy still holds the conservative default).
    m_currentDragPolicy = newPolicy;
    applyDragPolicyTransition(m_dragTracker->draggedWindow(), windowId, oldReason, DragPolicyTransition::Source::Flip);
}

void PlasmaZonesEffect::applyDragPolicyTransition(KWin::EffectWindow* w, const QString& windowId,
                                                  PhosphorProtocol::DragBypassReason oldReason,
                                                  DragPolicyTransition::Source source)
{
    DragPolicyTransition::Input in;
    in.oldReason = oldReason;
    in.policy = m_currentDragPolicy;
    in.source = source;
    in.bypassLatched = m_dragBypassedForEngine;
    in.keyboardGrabbed = m_keyboardGrabbed;
    in.daemonUp = m_daemonGate.serviceRegistered;
    in.windowLive = w && !w->isDeleted();
    in.tileHeld = !windowId.isEmpty() && m_tilingHandler->isTrackedWindow(windowId);
    in.floating = !windowId.isEmpty() && isWindowFloating(windowId);
    in.floatedThisDrag = m_dragActivation.floatedWindowIds.contains(windowId);
    const DragPolicyTransition::Plan p = DragPolicyTransition::plan(in);

    // Entering a tiling engine's screen sends the daemon nothing: its own flip
    // already hid what the snap path showed, and a cancelSnap here latched the
    // cancel for the rest of the drag, so coming back could never snap.
    if (p.enterBypass) {
        m_dragBypassedForEngine = true;
    }
    if (p.bypassScreen) {
        m_dragBypassScreenId = *p.bypassScreen;
    }
    if (p.leave != DragPolicyTransition::Leave::None) {
        // Id-keyed bookkeeping, so a window that died mid-drag still has its
        // tracking settled. On a flip the daemon still holds the tile and the
        // drop decides, so the tile is only suspended effect-side, with no
        // relay. On the reply no engine owns the start screen, so the effect's
        // own stale tracking goes.
        if (!windowId.isEmpty()) {
            if (p.leave == DragPolicyTransition::Leave::SuspendTile) {
                m_tilingHandler->suspendTileForSnapDrag(windowId);
                m_dragActivation.tileSuspended = true;
            } else {
                m_tilingHandler->cleanupAutotileTracking(windowId);
            }
        }
        m_dragBypassedForEngine = false;
        m_dragActivation.detected = false;
    }
    if (p.floatNow) {
        m_tilingHandler->handleDragToFloat(w, windowId, /*immediate=*/true);
        m_dragActivation.floatedWindowIds.insert(windowId);
    }
    // The grab follows the daemon's answer both ways. KWin::effects guarded:
    // both callers are D-Bus dispatches, which can land during compositor
    // teardown. grabKeyboard answers false when another effect holds the
    // keyboard, and recording a grab not earned would make the drag-end
    // ungrabKeyboard release that effect's.
    if (p.ungrab && KWin::effects) {
        KWin::effects->ungrabKeyboard();
        m_keyboardGrabbed = false;
    }
    if (p.grab && KWin::effects) {
        m_keyboardGrabbed = KWin::effects->grabKeyboard(this);
        if (!m_keyboardGrabbed) {
            qCWarning(lcEffect) << "drag policy: keyboard grab refused (another effect holds it) for" << windowId
                                << "- Escape will reach KWin's move filter";
        }
    }
}

} // namespace PlasmaZones
