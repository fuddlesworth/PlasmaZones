// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

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
#include <window.h>

#include <QDBusConnection>
#include <QDBusMessage>
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
    QDBusPendingCall call = PhosphorProtocol::ClientHelpers::asyncCall(interface, method, args);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
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
                    QRect geo(reply.argumentAt<0>(), reply.argumentAt<1>(), reply.argumentAt<2>(),
                              reply.argumentAt<3>());
                    qCInfo(lcEffect) << method << "snapping" << windowId << "to:" << geo;
                    if (storePreSnap)
                        // `window` is non-null inside this branch (guarded by
                        // the `reply.argumentAt<4>() && window` check above),
                        // so frameGeometry() needs no null-guard here.
                        m_snapHandler->ensurePreSnapGeometryStored(window, windowId, QRectF(window->frameGeometry()));
                    // A surviving KWin maximize fights the zone rect and arms
                    // a cross-screen restore — drop it before the apply. Runs
                    // AFTER the pre-snap capture, whose freeGeometryForCapture
                    // reads the maximize state to substitute the true free
                    // rect; demoting first would consume it.
                    //
                    // Deliberately UNGATED on isManagedScreen, unlike the
                    // daemon_apply sites: their slots also carry float
                    // restores, so they ride the pre-existing commit
                    // discriminator, while this funnel always commits a zone
                    // placement and applies the rect unconditionally below.
                    // Engine-held claims are already skipped inside the
                    // demote, and gating only the demote here would leave the
                    // maximize fighting the rect on managed screens — the
                    // defect this call exists to fix.
                    m_tilingHandler->demoteMaximizeForSnapPlacement(window, geo);
                    applyWindowGeometry(window, geo, false, skipAnimation,
                                        PhosphorAnimation::ProfilePaths::WindowPlaceIn, QRectF(), QRectF(),
                                        /*demoteMaximizeOnDeferredReplay=*/true);
                    // Async snap (keyboard / empty-zone / last-zone / auto-fill)
                    // committed — record in snapping's border set, but only for
                    // a resolved snap-mode screen (autotile windows are tracked
                    // by TilingHandler; an empty screen is left untracked,
                    // mirroring the batch path's discriminator).
                    if (const QString asyncScr = getWindowScreenId(window);
                        !asyncScr.isEmpty() && !m_tilingHandler->isManagedScreen(asyncScr)) {
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
    const PhosphorProtocol::DragBypassReason newReason = newPolicy.bypassReason;
    // The latch has to agree with the reason for this to be a genuine no-op,
    // not just the two reasons matching. The un-bypass transition below gates
    // on the effect's OWN latch precisely because the drag-start fast path can
    // set it while m_currentDragPolicy still holds the conservative default
    // (reason None) — and when the beginDrag reply errors, its correction arm
    // never runs, so that mismatch persists. A later None→None emission from
    // the daemon (which compares against its own record, not ours) then landed
    // here and returned early, so the un-bypass never ran: tracking stayed
    // held, the keyboard was never grabbed, and Escape went uncaught for the
    // rest of the drag. Requiring the latch to agree lets that case fall
    // through to the transition it was always meant to reach.
    const bool latchAgreesWithReason =
        m_dragBypassedForEngine == (newReason == PhosphorProtocol::DragBypassReason::EngineOwnedScreen);
    if (oldReason == newReason && latchAgreesWithReason) {
        // Same reason but different screenId (autotile→autotile cross-VS):
        // update the captured screen so endDrag's ApplyFloat uses the right one.
        m_currentDragPolicy = newPolicy;
        if (newReason == PhosphorProtocol::DragBypassReason::EngineOwnedScreen) {
            m_dragBypassScreenId = newPolicy.screenId;
        }
        return;
    }

    qCInfo(lcEffect) << "slotDragPolicyChanged:" << windowId << oldReason << "->" << newReason
                     << "screen=" << newPolicy.screenId;

    m_currentDragPolicy = newPolicy;

    if (newReason == PhosphorProtocol::DragBypassReason::EngineOwnedScreen) {
        // Snap → autotile (or context-disabled → autotile). Cancel any
        // active snap overlay, enter bypass mode. Mirrors the old
        // effect-side flip block's "snap→autotile" branch, but driven by
        // daemon truth rather than an effect-cached screen set.
        if (!m_dragBypassedForEngine) {
            m_snapHandler->callCancelSnap();
            m_dragBypassedForEngine = true;
            m_dragBypassScreenId = newPolicy.screenId;
        } else {
            // Already in bypass but on a different autotile screen — just
            // update the captured screen id.
            m_dragBypassScreenId = newPolicy.screenId;
        }
        return;
    }

    // Gate on the effect's OWN latch, not merely on the daemon's previous
    // reason. The drag-start fast path latches the bypass from the union
    // isManagedScreen without consulting the context-disable lists, while the
    // daemon checks ContextDisabled FIRST and so answers ContextDisabled (not
    // EngineOwnedScreen) for an engine-managed screen whose context is disabled.
    // The beginDrag correction layer only clears the latch on a reply of None,
    // so the drag can be underway latched-bypassed with a policy that was never
    // EngineOwnedScreen. Keying this transition on oldReason alone then let
    // ContextDisabled -> None (and SnappingDisabled -> None) fall through to the
    // no-op tail with the latch still set for the rest of the drag: the engine
    // tracking drop never ran (the window kept its tile tracking and hidden
    // title bar while it snapped), the keyboard was never grabbed, and the
    // activation state was never reset. Scrolling widens the reachable surface
    // because every scrolling screen is in the union the fast path latches on.
    if (oldReason == PhosphorProtocol::DragBypassReason::EngineOwnedScreen || m_dragBypassedForEngine) {
        // Autotile → snap (or autotile → context-disabled). Drop the
        // bypass flag and initialize snap-drag state as if the drag just
        // started on this snap screen. Remove the window from autotile
        // tracking so slotWindowFrameGeometryChanged doesn't fight the
        // snap geometry on subsequent geometry changes.
        //
        // Do NOT call handleDragToFloat here: the mid-drag schedule would
        // race against the zone snap at drop, making the window jump after
        // the user lets go. onWindowClosed alone clears the tracking state.
        // Guarded on the ID, not the dragged-window pointer: the call is
        // id-keyed bookkeeping that never derefs the window, and a
        // died-mid-drag pointer must not skip the tracking cleanup for a
        // still-valid id.
        if (!windowId.isEmpty()) {
            // releaseWindowTracking, NOT onWindowClosed: the window is live
            // and mid-drag, and the close relay's capture would record the
            // drag frame as its float-back.
            m_tilingHandler->releaseWindowTracking(windowId, m_dragBypassScreenId);
        }
        m_dragBypassedForEngine = false;
        // Cleared with the flag, as the equivalent transition in
        // lifecycle_wiring.cpp does: leaving a stale engine screen id behind
        // meant it survived into any later re-bypass until the EngineOwnedScreen
        // branch above happened to overwrite it.
        m_dragBypassScreenId.clear();
        m_dragActivation.detected = false;
        // KWin::effects guarded: this slot runs from a D-Bus signal
        // dispatch (slotDragPolicyChanged), which can land during compositor
        // teardown when the global is already gone — same rule and reason
        // repaintSnapRegions documents (window_geometry_apply.cpp).
        if (!m_keyboardGrabbed && KWin::effects) {
            // The return value is the grab: KWin refuses when another effect
            // already holds the keyboard. Latching m_keyboardGrabbed true
            // without having earned it made the unconditional ungrabKeyboard at
            // drag end release the OTHER effect's grab, silently cutting it off
            // from keys for the rest of its session.
            m_keyboardGrabbed = KWin::effects->grabKeyboard(this);
            if (!m_keyboardGrabbed) {
                qCWarning(lcEffect) << "dragPolicyChanged: keyboard grab refused (another effect holds it) for"
                                    << windowId << "- Escape will reach KWin's move filter";
            }
        }
        return;
    }

    // Other transitions (snap ↔ context_disabled / snapping_disabled) with no
    // bypass latch held: no compositor-level work needed. The daemon will
    // return a NoOp at endDrag for disabled paths.
}

} // namespace PlasmaZones
