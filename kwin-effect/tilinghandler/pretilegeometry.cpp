// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Pre-tile geometry capture and restore for TilingHandler.
//
// Split out of state.cpp by concern. This is the memory of what a window looked
// like BEFORE the engine sized it, so a later float-out or unsnap can hand that
// size back. Two properties make it its own concern rather than part of the
// screen-state plumbing: every rect is filed under the BUCKET screen it was
// captured on, because a restore across a monitor boundary must decline a rect
// from another coordinate space, and the capture has to refuse the tile rect
// itself, since recording a tiled frame as free geometry poisons the restore
// permanently.

#include "tilinghandler.h"
#include "handlers/snaphandler.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "compositor/effectlogging.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <core/output.h>
#include <effect/effectwindow.h>
#include <window.h>

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QPointer>
#include <QScopeGuard>

namespace PlasmaZones {

void TilingHandler::saveAndRecordPreTileGeometry(const QString& windowId, const QString& screenId,
                                                 KWin::EffectWindow* w, const QRectF& frameIn, bool knownFreeFloating)
{
    if (windowId.isEmpty() || screenId.isEmpty()) {
        qCDebug(lcEffect) << "Skipped pre-autotile geometry save: empty id" << windowId << screenId;
        return;
    }
    // Correct for maximize/fullscreen (shared with SnapHandler's capture): a maximized
    // window's frameGeometry() is the full monitor, and storing that as the float-back
    // size floats the window back maximized. This store is the SAME daemon free-geometry
    // record snap reads, so an unguarded capture here would poison snap's restore too.
    const QRectF frame = m_effect->freeGeometryForCapture(w, frameIn);
    if (!frame.isValid() || frame.width() <= 0 || frame.height() <= 0) {
        qCDebug(lcEffect) << "Skipped pre-autotile geometry save: invalid frame" << frame << "for" << windowId;
        return;
    }
    // Use EXACT windowId match only — NOT an appId/stableId fallback.
    // Multiple instances of the same app (e.g., 3 Dolphin windows) share an
    // appId; a fuzzy contains-check would return true after the first
    // instance is saved, preventing all other instances from saving their own
    // geometry. On restore, all instances would get the first instance's
    // geometry — scrambling window positions on every autotile ↔ snapping toggle.
    //
    // ALL buckets, matching findPreTileGeometry — a per-screen check would let a
    // re-announce on a different screen add a SECOND entry for the same window,
    // and the reader returns whichever bucket it reaches first, so the restore
    // could pick a rect measured in the other monitor's coordinate space.
    // A const scan, so a guard-bail below never inserts an empty per-screen
    // bucket (operator[] would); the bucket is created only at the genuine
    // insertion point (below).
    if (findPreTileGeometry(windowId).isValid()) {
        return;
    }
    // Only save geometry for floating windows — snapped/tiled windows have zone
    // dimensions in frameGeometry(), not the original free-floating size. Storing
    // zone geometry here would cause handleDragToFloat to restore to zone size.
    //
    // EXCEPTION: freshly-opened windows are not tracked in the FloatingCache yet,
    // so isWindowFloating() returns false even though their frame IS the authoritative
    // free-floating spawn geometry. Callers that know they are processing a fresh
    // window pass knownFreeFloating=true to bypass the guard. Without that bypass,
    // the save is silently dropped and every later float-restore for this window
    // falls through to stale cross-session data (or, with exact-only lookups, nothing).
    // A snap-managed window's frame IS its zone rect, never a free-floating
    // position — this holds EVEN on the knownFreeFloating fast path, which fires
    // when a window is re-added to autotile on a snap→autotile toggle. Storing the
    // zone rect as the pre-autotile float-back is the per-mode leak: a later
    // float-in-autotile then teleports the window to the snap zone instead of its
    // genuine pre-snap free position. isWindowFloating() below misses this because
    // knownFreeFloating bypasses it, so check the snap-managed state explicitly and
    // unconditionally.
    const SnapHandler* snap = m_effect->snapHandler();
    if (m_effect->isWindowMarkedSnapped(windowId) || (snap && snap->isMinimizeFloated(windowId))) {
        qCDebug(lcEffect) << "Skipped pre-autotile geometry for snap-owned window (frame is zone rect)" << windowId
                          << "on" << screenId;
        return;
    }
    // Own-side twin of the guard above: a window THIS handler holds as a
    // minimize-float was tiled when it minimized (the daemon-restart re-claim
    // path re-adds such windows with knownFreeFloating routing), so its frame
    // is the TILE rect. The UNTILED subset is carved out — those windows'
    // rects belong to the PRIOR mode, and the snap-owned guard above already
    // rejects zone rects, so a surviving untiled rect is a genuine free
    // position worth capturing. isMinimizeFloated (not the raw marker set):
    // a window mid-unfloat sits in m_unfloatInFlight instead, and its frame
    // is still the tile rect until the restore lands — capturing during that
    // interval is the same poison.
    if (isMinimizeFloated(windowId) && !m_untiledMinimizeFloats.contains(windowId)) {
        qCDebug(lcEffect) << "Skipped pre-autotile geometry for own minimize-float (frame is tile rect)" << windowId
                          << "on" << screenId;
        return;
    }
    if (!knownFreeFloating && !m_effect->isWindowFloating(windowId)) {
        qCDebug(lcEffect) << "Skipped pre-autotile geometry for snapped window" << windowId << "on" << screenId;
        return;
    }
    m_preTileGeometries[screenId][windowId] = frame;
    qCDebug(lcEffect) << "Saved pre-autotile geometry for" << windowId << "on" << screenId << ":" << frame;
    if (m_effect->m_daemonGate.serviceRegistered) {
        // overwrite=knownFreeFloating: only the window-opened spawn paths
        // (the sole callers passing true) may clobber a persisted daemon
        // entry — the spawn frame IS the authoritative free-floating
        // geometry, and a stale appId-keyed entry from a prior session
        // would otherwise block the fresh capture and leave float-restore
        // teleporting the window to ancient coordinates.
        // Every other caller (autotile toggle, unminimize-unfloat,
        // cross-screen transfer) pushes non-destructively: an
        // overflow-floated window can pass the isWindowFloating() guard
        // while its frame still sits at the TILED position, and an
        // overwrite there would destroy the daemon's correct free-position
        // entry — exactly what the toggle path's explicit overwrite=false
        // back-fill exists to preserve.
        // qRound, not truncation: fractional-scale sub-pixel residue (see the
        // toRect() geometry-capture convention in window_lifecycle.cpp).
        PhosphorProtocol::ClientHelpers::fireAndForget(
            m_effect, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("storePreTileGeometry"),
            {windowId, qRound(frame.x()), qRound(frame.y()), qRound(frame.width()), qRound(frame.height()), screenId,
             knownFreeFloating},
            QStringLiteral("storePreTileGeometry"));
    }
}

void TilingHandler::requestDaemonPreTileRestore(KWin::EffectWindow* w, const QString& windowId,
                                                const QString& capturedScreenId)
{
    QPointer<KWin::EffectWindow> safeW = w;
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("getValidatedPreTileGeometry"), {windowId}),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, safeW, windowId, capturedScreenId](QDBusPendingCallWatcher* pw) {
                pw->deleteLater();
                QDBusPendingReply<bool, int, int, int, int> reply = *pw;
                // No arity term: QDBusPendingReply<...>::count() is the compile-time
                // sizeof...(Types), so a "count() < 5" test can never fire. isValid
                // plus the success flag plus the positive-extent check below cover
                // the short/mis-typed-reply failure modes (a signature mismatch
                // default-constructs argumentAt<0>() to false).
                if (!reply.isValid() || !reply.argumentAt<0>()) {
                    return;
                }
                const int rw = reply.argumentAt<3>();
                const int rh = reply.argumentAt<4>();
                if (rw <= 0 || rh <= 0 || !safeW || safeW->isDeleted()) {
                    return;
                }
                // Anything that took (back) ownership of the window during the
                // round-trip supersedes this orphan restore: another desktop
                // switch, a re-tile (re-notified), the screen re-entering
                // autotile, a snap commit, a float toggle, or the user actively
                // moving/resizing it.
                // capturedScreenId, not a fresh getWindowScreenId(): the caller
                // resolved the screen while the engine-authoritative override was
                // still live, and by now the same loop iteration has demoted the
                // window's tracking, so a re-resolve of a parked (off-canvas) frame
                // can positionally land on a neighbouring output — skipping the
                // restore and stranding the window at its parked rect.
                if (!safeW->isOnCurrentDesktop() || !safeW->isOnCurrentActivity()
                    || m_notifiedWindows.contains(windowId) || m_managedScreens.contains(capturedScreenId)
                    || m_effect->isWindowMarkedSnapped(windowId) || m_effect->isWindowFloating(windowId)
                    || safeW->isUserMove() || safeW->isUserResize()) {
                    return;
                }
                // The rect is ABSOLUTE compositor coordinates, and the daemon
                // resolved it against its OWN screenForWindow() rather than the
                // window's live monitor. A stale tracked screen therefore answers
                // a rect that lands on a different output, and applying it does
                // not restore a size — it MOVES the window to that output, which
                // is what a desktop switch looked like to the reporter of
                // discussion #1028. Refuse it, matching the local-bucket arm's
                // cross-screen decline in slotScreensChanged: capturedScreenId is
                // the screen the caller resolved while the window was still on it,
                // and it is already this lambda's authority for the managed-set
                // guard above.
                const QRect restoreRect(reply.argumentAt<1>(), reply.argumentAt<2>(), rw, rh);
                if (const KWin::LogicalOutput* out = m_effect->outputForScreenId(capturedScreenId)) {
                    if (const QRect g = out->geometry(); g.isValid() && !g.contains(restoreRect.center())) {
                        qCDebug(lcEffect) << "Desktop switch: declining daemon pre-tile rect for" << windowId
                                          << "— rect" << restoreRect << "is not on" << capturedScreenId << g;
                        return;
                    }
                }

                // Suppress the VS-crossing detectors across the synchronous
                // frameGeometryChanged this apply emits — same rationale as the
                // local-bucket restore path in slotScreensChanged.
                // Save/restore, not set/clear: a clearing guard nested inside an outer
                // apply would hand the outer scope back an un-flagged window.
                const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
                m_effect->m_daemonGate.inGeometryApply = true;
                const auto geomGuard = qScopeGuard([this, prevInApply] {
                    m_effect->m_daemonGate.inGeometryApply = prevInApply;
                });
                // Clear any lingering KWin maximize flag first or KWin re-asserts
                // the maximize-area rect and defeats the restore (discussion #461).
                //
                // Through the ledger when the ledger owns the bit, so membership
                // and the bit move TOGETHER — the same shape the two sibling
                // pre-tile restores in screenschanged.cpp use, and for the reason
                // stated there: a bare clear strips a column-maximize member's bit
                // while leaving the effect recorded as still holding it, which is
                // the exact split m_maximizedToEdgesWindows' contract forbids.
                if (m_maximizedToEdgesWindows.contains(windowId)) {
                    releaseMaximizedToEdges(windowId, safeW);
                } else if (KWin::Window* kw = safeW->window(); kw && kw->maximizeMode() != KWin::MaximizeRestore
                           && !kw->isRequestedFullScreen() && !kw->isFullScreen() && !safeW->isUserMove()
                           && !safeW->isUserResize()) {
                    // The fullscreen and gesture pair every sibling maximize
                    // write in this tree carries: maximize() has no fullscreen
                    // conditional and would moveResize a presenting surface
                    // down to its restore rect, and mid-gesture it snaps the
                    // window under the user's pointer.
                    //
                    // Unlike releaseMaximizedToEdges, which skips on the same
                    // conditions and RETAINS membership so a later arm pays
                    // the bit, this is the non-member arm and holds no ledger,
                    // so a skip here is permanent rather than deferred. That
                    // is the accepted trade against shrinking a presenting
                    // surface.
                    //
                    // The gesture terms are REDUNDANT in this file: the
                    // enclosing lambda already returns early on the same pair
                    // above. They are kept so this arm reads identically to
                    // its twin in screenschanged.cpp, where they are live.
                    // Do not treat this as the place that guard lives.
                    applyMaximizeSuppressed(kw, KWin::MaximizeRestore);
                }
                // Snap-out: leaving zone-managed sizing.
                m_effect->applyWindowGeometry(safeW, restoreRect, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                                              PhosphorAnimation::ProfilePaths::WindowSnapOut);
                // Re-seed the tracked screen from the applied position: the gate
                // above suppressed the VS-crossing detectors whose early return sits
                // before their tracker write, and applyWindowGeometry does not
                // self-seed (the daemon-apply and engine-flip callers re-seed
                // themselves; this restore must too). Re-check the QPointer: this
                // lambda runs after an ASYNC D-Bus round-trip, and the window can be
                // closed at any point around it — a nullptr key would have no
                // destroyed-cleanup to remove it. (The synchronous monocle re-seeds
                // above need no such guard: their pointer comes from a resolve
                // moments earlier and maximize() cannot delete an EffectWindow.)
                if (safeW) {
                    m_effect->m_trackedScreenPerWindow[safeW.data()] = m_effect->getWindowScreenId(safeW.data());
                }
                qCInfo(lcEffect) << "Desktop switch: restored pre-snap geometry from daemon for orphaned window"
                                 << windowId;
            });
}

QRectF TilingHandler::findPreTileGeometry(const QString& windowId, QString* bucketScreenId) const
{
    for (auto sgIt = m_preTileGeometries.constBegin(); sgIt != m_preTileGeometries.constEnd(); ++sgIt) {
        const QRectF rect = sgIt->value(windowId);
        if (rect.isValid()) {
            if (bucketScreenId) {
                *bucketScreenId = sgIt.key();
            }
            return rect;
        }
        // Found-but-invalid entry: keep scanning. A valid rect may still be
        // stored under another screen's bucket from a mid-session
        // autotile-screen transfer.
    }
    return QRectF();
}

void TilingHandler::savePreTileForDesktopMove(const QString& windowId)
{
    // Preserve the window's pre-autotile geometry before onWindowClosed clears it.
    // When the window is re-added on the target desktop, this geometry is restored
    // so that float-restore returns to the original position, not the tiled frame.
    //
    // Stamped with the BUCKET's screen (not the caller's) so the restore
    // path can detect a cross-screen desktop move and decline a saved rect
    // from a different monitor's coordinate space.
    QString bucketScreenId;
    const QRectF rect = findPreTileGeometry(windowId, &bucketScreenId);
    if (rect.isValid()) {
        m_savedPreTileForDesktopMove[windowId] = {bucketScreenId, rect};
        qCDebug(lcEffect) << "Preserved pre-autotile geometry for desktop move:" << windowId << "bucket"
                          << bucketScreenId << "rect=" << rect;
    }
}

void TilingHandler::restorePreTileForDesktopMove(const QString& windowId, const QString& screenId)
{
    auto savedIt = m_savedPreTileForDesktopMove.find(windowId);
    if (savedIt == m_savedPreTileForDesktopMove.end()) {
        return;
    }
    // Only apply when the source screen matches the destination — saved rects
    // are in absolute coordinates of the source monitor and would land
    // off-target on a different screen after a cross-desktop + cross-screen
    // move. Consumed either way: a rect that cannot be applied here has no
    // later consumer, and leaving it behind would let a much later re-add on
    // the original screen restore a rect from a session-old position.
    if (savedIt.value().first == screenId) {
        m_preTileGeometries[screenId][windowId] = savedIt.value().second;
    } else {
        qCDebug(lcEffect) << "Desktop move: dropping cross-screen pre-autotile rect for" << windowId
                          << "source=" << savedIt.value().first << "dest=" << screenId;
    }
    m_savedPreTileForDesktopMove.erase(savedIt);
}

} // namespace PlasmaZones
