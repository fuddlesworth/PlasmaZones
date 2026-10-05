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
#include "pretiledecisions.h"
#include "handlers/snaphandler.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "plasmazoneseffect/desktopvisibility.h"
#include "compositor/effectlogging.h"

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <core/output.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QPointer>
#include <QScopeGuard>

#include <algorithm>
#include <optional>

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
    // The correction above keys on the COMMITTED fullscreen bit, which has
    // already dropped at the fullscreen-exit edge while the frame still holds
    // the full-output rect. That caller (signals.cpp, never-tracked arm)
    // therefore passes knownFreeFloating=false and clears the spawn marker, so
    // the not-floating guard below drops the capture instead of storing the
    // output rect as free geometry. A rect equal to the output is NOT rejected
    // here: a borderless window sized to its output has exactly that free
    // geometry, and the spawn capture must store it.
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
    //
    // First-capture-wins applies WITHIN an output. Across outputs it does not:
    // an entry measured on a monitor the window has since left describes a
    // position that no longer means anything for it, and because this early
    // return never expired it, that entry was pinned for the window's whole
    // life — the reader could only ever be handed a foreign rect, however far
    // the window travelled. Re-home instead: drop the stale entry and let the
    // capture below file a fresh one under the current output. Still exactly
    // one entry per window, which is what the all-bucket reader needs.
    //
    // The stale entry is only IDENTIFIED here, never dropped here. Three
    // unconditional guards sit between this point and the single insertion
    // below (snap-owned, own-minimize-float, not-floating), so dropping on the
    // spot and then bailing out of one of them would leave the window with NO
    // free-geometry memory at all rather than a stale-but-real one — and
    // nothing re-files it, so findPreTileGeometry answers invalid for the rest
    // of the window's life. Removal is deferred to the commit, keeping the
    // "exactly one entry per window" invariant without ever passing through
    // zero.
    QString staleBucket;
    if (findPreTileGeometry(windowId, &staleBucket).isValid()) {
        if (PhosphorIdentity::VirtualScreenId::samePhysical(staleBucket, screenId)) {
            return;
        }
    } else {
        staleBucket.clear();
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
    if (isMinimizeFloated(windowId) && !m_minimizeFloatMarks.isUntiled(windowId)) {
        qCDebug(lcEffect) << "Skipped pre-autotile geometry for own minimize-float (frame is tile rect)" << windowId
                          << "on" << screenId;
        return;
    }
    if (!knownFreeFloating && !m_effect->isWindowFloating(windowId)) {
        qCDebug(lcEffect) << "Skipped pre-autotile geometry for snapped window" << windowId << "on" << screenId;
        return;
    }
    // The fullscreen-exit announce of a never-tracked window (and any re-add
    // after an effect restart) arrives while KWin still has the window at the
    // output's fullscreen area: that frame is never free geometry, and the
    // FloatingCache can read "floating" for a hold the daemon still keeps, so
    // the guard above alone does not stop it being stored first-capture-wins.
    if (!knownFreeFloating && KWin::effects) {
        const QRect fsArea = KWin::effects->clientArea(KWin::FullScreenArea, w).toRect();
        if (fsArea.isValid() && frame.toRect() == fsArea) {
            qCDebug(lcEffect) << "Skipped pre-autotile geometry at the fullscreen area" << windowId << "on" << screenId;
            return;
        }
    }
    // Drop-then-insert as one unit: every guard that could bail has now been
    // passed, so the window is never left without an entry. See the deferral
    // note at the scan above.
    if (!staleBucket.isEmpty()) {
        qCDebug(lcEffect) << "Pre-autotile geometry for" << windowId << "re-homed from" << staleBucket << "to"
                          << screenId;
        auto bucketIt = m_preTileGeometries.find(staleBucket);
        if (bucketIt != m_preTileGeometries.end()) {
            bucketIt->remove(windowId);
            // Drop the bucket when it empties, matching the const-scan
            // reasoning above: a stray empty per-screen hash is a slow leak
            // across a long session of monitor changes.
            if (bucketIt->isEmpty()) {
                m_preTileGeometries.erase(bucketIt);
            }
        }
    }
    m_preTileGeometries[screenId][windowId] = frame;
    qCDebug(lcEffect) << "Saved pre-autotile geometry for" << windowId << "on" << screenId << ":" << frame;
    if (m_effect->m_daemonGate.serviceRegistered) {
        // overwrite=knownFreeFloating: only callers vouching for the frame
        // as free geometry pass true (the window-opened spawn paths and a
        // mode-entry batch for an untiled window) and may clobber a
        // persisted daemon entry — that frame IS the authoritative
        // free-floating geometry, and a stale entry from a prior session
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
                } else if (KWin::effects && !KWin::effects->screenAt(restoreRect.center())) {
                    // outputForScreenId answers null when the captured screen
                    // was unplugged during this D-Bus round trip — which is
                    // exactly when a stale absolute rect is most likely, and the
                    // guard above then does not run at all. Fall back to the
                    // origin validation the INGEST side of this same data
                    // already performs: a rect whose centre is on no connected
                    // output would park the window where nothing can show it.
                    qCDebug(lcEffect) << "Desktop switch: declining daemon pre-tile rect for" << windowId << "— rect"
                                      << restoreRect << "is on no connected output (captured screen" << capturedScreenId
                                      << "is gone)";
                    return;
                }

                // Suppress the VS-crossing detectors across the synchronous
                // frameGeometryChanged this apply emits, for the reason given
                // in applyFreeGeometryRestore.
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
                // and the bit move TOGETHER, the same shape applyFreeGeometryRestore
                // uses and for the reason stated there: a bare clear strips a column-maximize member's bit
                // while leaving the effect recorded as still holding it, which is
                // the exact split m_maximizedToEdgesWindows' contract forbids.
                if (m_maximizedToEdgesWindows.contains(windowId)) {
                    releaseMaximizedToEdges(windowId, safeW);
                    // REQUESTED maximize, matching releaseMaximizedToEdges and
                    // applyFreeGeometryRestore. The committed bit lags a
                    // client round-trip on Wayland in both directions, so a
                    // maximize requested but not yet committed would read as
                    // "not maximized" and skip the clear, letting KWin
                    // re-assert the maximize-area rect over the restore below.
                    // The fullscreen union is correct HERE (no setFullScreen
                    // precedes this arm) and is deliberately kept.
                } else if (KWin::Window* kw = safeW->window(); kw
                           && kw->requestedMaximizeMode() != KWin::MaximizeRestore && !kw->isRequestedFullScreen()
                           && !kw->isFullScreen() && !safeW->isUserMove() && !safeW->isUserResize()) {
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
                    // applyFreeGeometryRestore, where they are live.
                    // Do not treat this as the place that guard lives.
                    applyMaximizeSuppressed(kw, KWin::MaximizeRestore);
                }
                // Snap-out: leaving zone-managed sizing.
                m_effect->applyWindowGeometry(safeW, restoreRect, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                                              PhosphorAnimation::ProfilePaths::WindowPlaceOut);
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

void TilingHandler::applyFreeGeometryRestore(KWin::EffectWindow* w, const QString& windowId, const QRectF& rect)
{
    // applyWindowGeometry's moveResize, and the maximize clear below, emit
    // windowFrameGeometryChanged synchronously. Suppress the VS-crossing
    // detectors (autotile slotWindowFrameGeometryChanged and the snapping
    // windowFrameGeometryChanged handler) so a same-screen restore is not
    // mistaken for a virtual-screen crossing, as the retile path does
    // (tiling.cpp). Save/restore, not set/clear (nesting-safe).
    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
    m_effect->m_daemonGate.inGeometryApply = true;
    const auto geomGuard = qScopeGuard([this, prevInApply] {
        m_effect->m_daemonGate.inGeometryApply = prevInApply;
    });
    // Clear any lingering KWin maximize flag first: a still-maximized window
    // makes KWin re-assert the maximize-area rect and defeat the restore, which
    // the tile-request path clears for the same reason (discussion #461).
    //
    // Through the ledger when the ledger owns the bit, so membership and the
    // bit move TOGETHER. A bare clear would strip a column-maximize member's
    // bit while leaving the effect recorded as still holding it, which is the
    // exact split m_maximizedToEdgesWindows' contract forbids.
    //
    // The GUARD is what earns its place here, not the call behind it. Every
    // caller has already called releaseMaximizedToEdges for this window, so
    // membership survives to here in exactly one case: that call SKIPPED a
    // still-fullscreen window and retained the entry on purpose. Re-calling it
    // skips again for the same reason, making the then-branch a no-op.
    // Deleting the condition and keeping only the else-branch would hand that
    // retained member the bare clear the paragraph above forbids, so keep the
    // test even though the call inside it does nothing.
    if (m_maximizedToEdgesWindows.contains(windowId)) {
        releaseMaximizedToEdges(windowId, w);
    } else if (KWin::Window* kw = w->window(); kw && kw->requestedMaximizeMode() != KWin::MaximizeRestore
               && !kw->isRequestedFullScreen() && !w->isUserMove() && !w->isUserResize()) {
        // REQUESTED bits on both axes, never the committed ones. On Wayland
        // the committed bit trails a client round-trip, and the windowed
        // fullscreen caller runs one loop body after
        // releaseWindowedFullscreenState called setFullScreen(false), inside
        // the exit gap where the requested bit already reads false and the
        // committed one is still true. Testing isFullScreen() made the clear
        // certain to skip for that population, so KWin re-asserted the
        // maximize-area rect over the restore (discussion #461). The
        // requested fullscreen term also keeps a window whose fullscreen is
        // requested but not yet committed from being moveResized down to its
        // restore rect while presenting, since maximize() has no fullscreen
        // conditional, and the gesture pair keeps it from snapping under the
        // user's pointer. This arm holds no ledger, so a skip here is
        // permanent rather than deferred.
        applyMaximizeSuppressed(kw, KWin::MaximizeRestore);
    }
    // Snap-out: leaving tile-managed sizing.
    m_effect->applyWindowGeometry(w, rect.toRect(), /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                                  PhosphorAnimation::ProfilePaths::WindowPlaceOut);
    // Re-seed the tracked screen: the bracket above suppressed the
    // VS-crossing detectors whose early return sits BEFORE their tracker
    // write, and applyWindowGeometry does not self-seed. The restore can
    // legitimately land in a different virtual screen than the tiled rect,
    // and a stale entry makes the next genuine geometry change read as a
    // spurious crossing.
    m_effect->m_trackedScreenPerWindow[w] = m_effect->getWindowScreenId(w);
}

QRectF TilingHandler::preTileRestoreRectFor(const QString& windowId, const QString& screenId,
                                            const QRectF& currentFrame) const
{
    QString bucketScreenId;
    const QRectF saved = findPreTileGeometry(windowId, &bucketScreenId);
    if (!saved.isValid()) {
        return {};
    }
    // PHYSICAL ids: virtual screens subdivide ONE output and share its
    // coordinate space, so a VS re-key leaves the rect perfectly applicable —
    // that is the case the all-bucket reader policy exists for. What decides
    // the question is the coordinate space, which is the OUTPUT.
    const bool sameOutput = PhosphorIdentity::VirtualScreenId::samePhysical(bucketScreenId, screenId);
    if (!sameOutput) {
        qCDebug(lcEffect) << "Pre-autotile restore for" << windowId << "has a rect from" << bucketScreenId
                          << "but sits on" << screenId << "— restoring size only";
    }
    // A rect measured on ANOTHER output. Its extents still describe this
    // window's free-floating size, but its ORIGIN belongs to a different
    // monitor's coordinate space, so applying it whole does not restore a
    // size — it moves the window to that monitor. Users read that as windows
    // being thrown across monitors (discussion #1028).
    //
    // Degrade rather than decline: the caller's whole purpose is to un-tile
    // the window, and refusing outright leaves it sitting at its tile rect
    // looking tiled. Size at the CURRENT position is what handleDragToFloat
    // already does for the same reason ("size is coordinate-space-independent,
    // so any bucket's rect is safe"), and it is the strictly better half of
    // the rect to keep.
    return PlasmaZones::PreTileDecisions::applicablePreTileRect(saved, sameOutput, currentFrame);
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
        m_desktopMoveStash.stash(windowId, bucketScreenId, rect);
        qCDebug(lcEffect) << "Preserved pre-autotile geometry for desktop move:" << windowId << "bucket"
                          << bucketScreenId << "rect=" << rect;
    }
    // A window leaving at its tile frame is owed its free placement where it
    // lands, rect or not; a floated tile is already at its own spot (F364).
    m_desktopMoveStash.setOwed(windowId, TilingStateHelpers::isTiledWindow(m_border, windowId));
}

void TilingHandler::restorePreTileForDesktopMove(const QString& windowId, const QString& screenId)
{
    // Only applied when the source monitor matches the destination: saved
    // rects are in absolute coordinates of the source monitor and would land
    // off-target after a cross-desktop + cross-screen move. Consumed either
    // way, so a much later re-add on the original screen cannot restore a
    // session-old position (DesktopMoveStash::consumeForManagedArrival).
    if (const std::optional<QRectF> rect = m_desktopMoveStash.consumeForManagedArrival(windowId, screenId)) {
        m_preTileGeometries[screenId][windowId] = *rect;
    }
}

void TilingHandler::payOwedFreePlacement(KWin::EffectWindow* w, const QString& windowId, const QString& screenId)
{
    if (!w || w->isDeleted() || !m_desktopMoveStash.isOwed(windowId)) {
        return;
    }
    // A window the snap side snapped into a zone on the way, or parked to
    // re-apply one on arrival, is placed by its zone (F414). The stash stays
    // for a later move back onto a tiling desktop.
    if (m_effect->isWindowMarkedSnapped(windowId)
        || (m_effect->m_snapHandler && m_effect->m_snapHandler->holdsDesktopArrivalPark(windowId))) {
        m_desktopMoveStash.setOwed(windowId, false);
        return;
    }
    // Hidden or presenting: still owed, paid when it is next in view.
    if (w->isMinimized() || w->isFullScreen()) {
        return;
    }
    // Still on another desktop or activity, where its tile frame belongs to
    // that layout (the pass-2 rule). An empty activity set is "every
    // activity" only when the session has activities at all.
    const bool everyActivity =
        w->activities().isEmpty() && KWin::effects && !KWin::effects->currentActivity().isEmpty();
    if (w->isOnAllDesktops() || w->desktops().size() > 1 || everyActivity || w->activities().size() > 1) {
        m_desktopMoveStash.setOwed(windowId, false);
        return;
    }
    bool owed = false;
    std::optional<QRectF> target = m_desktopMoveStash.takeOwedPlacement(windowId, screenId, &owed);
    if (!owed) {
        return;
    }
    if (!target && KWin::effects) {
        // No rect from this monitor. A frame on no output at all (a strip
        // column parked off the viewport) is brought onto the window's own
        // output at its size, centred in the work area (F364).
        const QRect frame = w->frameGeometry().toRect();
        const QList<KWin::LogicalOutput*> outputs = KWin::effects->screens();
        const bool onAnOutput = std::any_of(outputs.cbegin(), outputs.cend(), [&frame](const auto* output) {
            return output && QRect(output->geometry()).intersects(frame);
        });
        KWin::LogicalOutput* const output = onAnOutput ? nullptr : m_effect->windowOutput(w);
        if (output) {
            const QRect area = KWin::effects->clientArea(KWin::MaximizeArea, output).toRect();
            QRect rect(QPoint(), frame.size().boundedTo(area.size()));
            rect.moveCenter(area.center());
            target = QRectF(rect);
        }
    }
    if (target) {
        qCInfo(lcEffect) << "Desktop move: free placement for" << windowId << "on" << screenId << *target;
        applyFreeGeometryRestore(w, windowId, *target);
    }
}

void TilingHandler::noteFreeGeometryAfterGesture(KWin::EffectWindow* w, bool resized)
{
    if (!w || w->isDeleted()) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    const QString screenId = m_effect->getWindowScreenId(w);
    const QRectF free = m_effect->freeGeometryForCapture(w, QRectF(w->frameGeometry()));
    if (windowId.isEmpty() || screenId.isEmpty() || !free.isValid()) {
        return;
    }
    // A window that left a tiling desktop and was then placed by hand: its
    // stash follows the hand, and no free placement is owed any more (F334).
    if (!m_notifiedWindows.contains(windowId) && !m_effect->isWindowMarkedSnapped(windowId)) {
        m_desktopMoveStash.noteHandPlacement(windowId, screenId, free);
    }
    // A floating tile resized by hand: the size it floats back to follows, as
    // the daemon's record already does. A move rewrites neither (F369).
    if (resized && m_notifiedWindows.contains(windowId) && m_effect->isWindowFloating(windowId)
        && m_managedScreens.contains(screenId)) {
        // One entry per window across all buckets, the same re-home
        // saveAndRecordPreTileGeometry makes.
        for (auto bucket = m_preTileGeometries.begin(); bucket != m_preTileGeometries.end();) {
            bucket->remove(windowId);
            bucket = bucket->isEmpty() ? m_preTileGeometries.erase(bucket) : std::next(bucket);
        }
        m_preTileGeometries[screenId][windowId] = free;
    }
}

void TilingHandler::payOwedFreePlacementsInView(const QList<KWin::EffectWindow*>& windows)
{
    for (KWin::EffectWindow* w : windows) {
        if (!w || w->isDeleted() || !isOnOwnOutputCurrentDesktop(w) || !w->isOnCurrentActivity()) {
            continue;
        }
        const QString windowId = m_effect->getWindowId(w);
        if (!m_desktopMoveStash.isOwed(windowId) || m_notifiedWindows.contains(windowId)
            || m_savedNotifiedForDesktopReturn.contains(windowId)) {
            continue;
        }
        const QString screenId = m_effect->getWindowScreenId(w);
        if (!m_managedScreens.contains(screenId)) {
            payOwedFreePlacement(w, windowId, screenId);
        }
    }
}

} // namespace PlasmaZones
