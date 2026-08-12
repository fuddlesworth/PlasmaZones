// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tilinghandler.h"
#include "compositor/stripviewanimator.h"
#include "handlers/navigationhandler.h"
#include "handlers/snaphandler.h"
#include "plasmazoneseffect/plasmazoneseffect.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effectwindow.h>
#include <window.h>

#include <QAction>
#include <QDBusArgument>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QHash>
#include <QList>
#include <QLoggingCategory>
#include <QMetaType>
#include <QPointer>
#include <QScopeGuard>
#include <QVariant>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

// ═══════════════════════════════════════════════════════════════════════════════
// Monocle helpers
// ═══════════════════════════════════════════════════════════════════════════════

void TilingHandler::unmaximizeMonocleWindow(const QString& windowId)
{
    if (!m_monocleMaximizedWindows.contains(windowId)) {
        return;
    }
    // EXACT resolve: a stale monocle entry whose window is gone must restore
    // nothing — the fuzzy appId fallback would un-maximize an unrelated
    // same-app sibling under suppression, invisibly.
    KWin::EffectWindow* w = m_effect->findWindowByIdExact(windowId);
    if (!w) {
        m_monocleMaximizedWindows.remove(windowId);
        return;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        m_monocleMaximizedWindows.remove(windowId);
        return;
    }
    // KWin's maximize() has NO fullscreen conditional: called on a
    // still-fullscreen window it takes both "no longer maximized" branches
    // and moveResizes to geometryRestore, shrinking a fullscreen game or
    // video out of its presentation. Skip while the window holds (or has
    // requested) fullscreen — requested included for the same committed-lag
    // reason releaseWindowedFullscreenState and isEligibleForTilingNotify take
    // the union: on Wayland the committed bit trails a client round-trip, and a
    // restore landing inside our own enter gap would still shrink the surface
    // out from under the pending commit.
    //
    // MEMBERSHIP IS RETAINED ON THIS ARM, deliberately, and it is why the
    // remove above became a contains. Shedding it here while the window is
    // still KWin-MaximizeFull would strand it with nothing left owning the
    // flag — byte for byte the defect that removing the fullscreen-enter drop
    // was meant to end. And nothing would take it back: the sole insert site
    // (the monocle tile batch) is gated on the window NOT already being
    // maximized, so "the next batch re-establishes membership" is false.
    // Holding the entry means the next call on a non-fullscreen window does
    // the real restore.
    if (kw->isFullScreen() || kw->isRequestedFullScreen()) {
        return;
    }
    m_monocleMaximizedWindows.remove(windowId);
    // maximize() emits windowFrameGeometryChanged SYNCHRONOUSLY, and the
    // restore rect can sit in a different virtual-screen region of the same
    // monitor. Without the geometry-apply gate that edge takes the
    // VS-crossing path (handleWindowOutputChanged -> windowClosed +
    // notifyWindowAdded) and tears down whatever float/tile transition the
    // caller is mid-way through. Save/restore rather than set/clear so the
    // guard nests inside already-guarded callers.
    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
    m_effect->m_daemonGate.inGeometryApply = true;
    const auto geomGuard = qScopeGuard([this, prevInApply] {
        m_effect->m_daemonGate.inGeometryApply = prevInApply;
    });
    ++m_suppressMaximizeChanged;
    kw->maximize(KWin::MaximizeRestore);
    --m_suppressMaximizeChanged;
    // The gate suppressed the VS-crossing detectors, whose early return sits
    // BEFORE their m_trackedScreenPerWindow write — and unlike a daemon
    // apply this move is not transient, so the tracker must be re-seeded
    // here (the pairing daemon_apply.cpp documents). Post-move resolve is
    // authoritative on this path: no daemon rotation is in flight, so the
    // restore rect's position is the answer.
    m_effect->m_trackedScreenPerWindow[w] = m_effect->getWindowScreenId(w);
}

void TilingHandler::restoreAllMonocleMaximized()
{
    if (m_monocleMaximizedWindows.isEmpty()) {
        return;
    }
    // Snapshot and clear FIRST: maximize() can synchronously re-enter
    // cleanupClosedWindowState (via the VS-crossing / output-changed path),
    // which mutates m_monocleMaximizedWindows — iterating the live set here
    // is iterator invalidation in a compositor loop.
    const QStringList ids = m_monocleMaximizedWindows.values();
    m_monocleMaximizedWindows.clear();
    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
    m_effect->m_daemonGate.inGeometryApply = true;
    const auto geomGuard = qScopeGuard([this, prevInApply] {
        m_effect->m_daemonGate.inGeometryApply = prevInApply;
    });
    ++m_suppressMaximizeChanged;
    for (const QString& wid : ids) {
        // EXACT resolve — same sibling hazard as unmaximizeMonocleWindow.
        KWin::EffectWindow* w = m_effect->findWindowByIdExact(wid);
        if (w) {
            KWin::Window* kw = w->window();
            // Fullscreen members are skipped for the reason spelled out in
            // unmaximizeMonocleWindow: maximize() has no fullscreen
            // conditional and would moveResize a presenting surface down to
            // its restore rect. Membership was cleared before the loop, so a
            // skipped member loses it here — which is right, because this IS
            // the effect giving up ownership.
            if (kw && !kw->isFullScreen() && !kw->isRequestedFullScreen()) {
                kw->maximize(KWin::MaximizeRestore);
                // Same tracker re-seed as unmaximizeMonocleWindow, and more
                // load-bearing here: the daemon-loss caller has no apply
                // path left to heal a stale entry, and onDaemonReady keeps
                // this map across the restart.
                m_effect->m_trackedScreenPerWindow[w] = m_effect->getWindowScreenId(w);
            }
        }
    }
    --m_suppressMaximizeChanged;
}

void TilingHandler::forgetWindowedFullscreen(const QString& windowId)
{
    // Membership half only, deliberately without the compositor call: the
    // desktop-switch demote must shed membership BEFORE its geometry restore
    // (or the apply-path exemption still fires) while the setFullScreen(false)
    // has to land AFTER m_managedScreens is rewritten (or the deferred
    // committed-exit signal evaluates against the stale set) — one atomic
    // helper cannot sit on both sides of that write.
    m_effect->m_windowedFullscreenWindows.remove(windowId);
}

void TilingHandler::applyWindowedFullscreenLayerDemotion(const QString& windowId, KWin::Window* kw)
{
    // KWin promotes an active fullscreen window to the ActiveLayer — and
    // keeps it there while the active window sits on ANOTHER output
    // (Window::isActiveFullScreen) — which stacks a windowed-fullscreen
    // tile above every strip neighbour and above the daemon's own overlay
    // surfaces. Scrolling then slides the incoming column UNDERNEATH the
    // tile: seen live as the neighbour "not rendering" and windows
    // overlapping the fullscreen-presented game. The keep-below flag is
    // the one input belongsToLayer() consults BEFORE the fullscreen
    // promotion, so holding it keeps the tile stacked with its strip.
    // Snapshot-once + restore mirrors the SetWindowLayer rule discipline;
    // reconcileRuleWindowLayer skips flagged windows so the two flag
    // owners never fight mid-hold.
    // Null guard matching the restore's contract: both current callers hold
    // a checked pointer, but an unguarded deref here is a compositor crash
    // the day a third caller does not.
    if (!kw) {
        return;
    }
    if (!m_effect->m_windowedFsLayerSnapshots.contains(windowId)) {
        m_effect->m_windowedFsLayerSnapshots.insert(windowId, {kw->keepAbove(), kw->keepBelow()});
    }
    kw->setKeepAbove(false);
    kw->setKeepBelow(true);
}

void TilingHandler::restoreWindowedFullscreenLayerDemotion(const QString& windowId, KWin::Window* kw)
{
    const auto it = m_effect->m_windowedFsLayerSnapshots.find(windowId);
    if (it == m_effect->m_windowedFsLayerSnapshots.end()) {
        return;
    }
    // Erase BEFORE the setters, the reconcileRuleWindowLayer shape: they
    // emit KWin signals, and holding a QHash iterator across re-entrant
    // code is undefined the day a connection routes back into this map.
    const WindowLayerSnapshot snapshot = *it;
    m_effect->m_windowedFsLayerSnapshots.erase(it);
    if (!kw) {
        return; // window already gone; dropping the snapshot IS the cleanup
    }
    kw->setKeepAbove(snapshot.keepAbove);
    kw->setKeepBelow(snapshot.keepBelow);
}

void TilingHandler::releaseWindowedFullscreenState(const QString& windowId)
{
    // Compositor half: drop the client's KWin fullscreen state if it still
    // holds it. Deliberately does NOT consult the membership hash — callers
    // that forget first (the demote) and callers that snapshot-and-clear
    // (the bulk restore) both arrive here with the entry already gone.
    KWin::EffectWindow* w = m_effect->findWindowByIdExact(windowId);
    if (!w || w->isDeleted()) {
        restoreWindowedFullscreenLayerDemotion(windowId, nullptr);
        return;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        restoreWindowedFullscreenLayerDemotion(windowId, nullptr);
        return;
    }
    // Keep-flag restore runs even when fullscreen is already gone (the
    // client may have exited on its own before this release landed).
    restoreWindowedFullscreenLayerDemotion(windowId, kw);
    // Requested-state term: during our OWN enter round-trip the committed
    // state lags behind while requested is already true (the same lag
    // drag_snap's apply bail and the self-heal arm handle explicitly). A
    // release landing inside that gap must still un-set, or the client
    // commits fullscreen with membership already gone — the apply bail then
    // rejects every geometry and the ack arm can't fire, stranding the
    // window fullscreen at full-output size with no owner.
    if (!kw->isFullScreen() && !kw->isRequestedFullScreen()) {
        return;
    }
    // Own inGeometryApply bracket: setFullScreen(false) synchronously emits
    // windowFrameGeometryChanged on X11, and none of the release call sites
    // holds the guard — without it the VS-crossing detector re-enters the
    // very teardown loop that is running. Save/restore, not set/clear: the
    // demote and funnel callers can nest inside an outer apply.
    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
    m_effect->m_daemonGate.inGeometryApply = true;
    const auto geomGuard = qScopeGuard([this, prevInApply] {
        m_effect->m_daemonGate.inGeometryApply = prevInApply;
    });
    ++m_suppressFullScreenChanged;
    kw->setFullScreen(false);
    --m_suppressFullScreenChanged;
}

void TilingHandler::dispatchWindowedFullscreenClear(const QString& windowId)
{
    // Arm the in-flight marker: a batch the daemon emitted BEFORE
    // processing this clear can still carry flag=true (cross-direction
    // D-Bus has no ordering guarantee), and its adopt arm would
    // re-fullscreen the window the user just exited. The batch arm's
    // flag-off echo consumes the marker on success.
    //
    // Reply-gated, NOT fireAndForget: on a LOST clear the daemon keeps its
    // flag true, so every subsequent batch entry carries flag=true and the
    // flag-off echo never arrives — the marker would latch and refuse
    // re-adoption for the rest of the session. The error arm drops it so
    // the next batch can adopt again (the daemon's flag surviving an error
    // is exactly the state re-adoption reconciles).
    m_windowedFsClearInFlight.insert(windowId);
    QDBusPendingCall call = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::Scrolling, QStringLiteral("clearWindowedFullscreen"), {windowId});
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        const QDBusPendingReply<> reply = *w;
        if (reply.isError()) {
            qCWarning(lcEffect) << "clearWindowedFullscreen failed for" << windowId
                                << "- dropping in-flight marker:" << reply.error().message();
            m_windowedFsClearInFlight.remove(windowId);
        }
    });
}

void TilingHandler::restoreAllWindowedFullscreen()
{
    // The clear-in-flight markers die unconditionally, BEFORE the empty-hash
    // early return: this is the shared drain for disable, teardown, daemon
    // loss AND bring-up, and a marker surviving a daemon that died before
    // echoing its clear would silently refuse the NEW daemon's adopt batches
    // forever. Both marker-arm sites remove membership before inserting the
    // marker, so the last-member case arrives here with the hash EMPTY and
    // the marker set — a drain gated behind the early return would miss it.
    m_windowedFsClearInFlight.clear();
    if (!m_effect->m_windowedFullscreenWindows.isEmpty()) {
        // Snapshot and clear FIRST, the restoreAllMonocleMaximized shape and
        // for a sharper reason here: setFullScreen can synchronously re-enter
        // handleWindowOutputChanged → cleanupAutotileTracking, which now
        // forgets hash entries — iterating the live hash is iterator
        // invalidation in a compositor loop, and the funnel's re-entrant
        // forget must find the entry already gone.
        const QStringList ids = m_effect->m_windowedFullscreenWindows.keys();
        m_effect->m_windowedFullscreenWindows.clear();
        for (const QString& wid : ids) {
            releaseWindowedFullscreenState(wid);
        }
    }
    // Keep-flag sweep for snapshots with no membership behind them. The drain
    // above is membership-keyed, so an entry whose membership was dropped by
    // a path that did not also release would survive every teardown — and the
    // flag it owns is a real client state (setKeepBelow(true)) with nothing
    // left to hand it back. Runs OUTSIDE the membership guard on purpose: the
    // orphan case is precisely the one where the membership hash can already
    // be empty, so a sweep gated behind that early return would never see it.
    //
    // Keys snapshotted first: the release erases from this very hash and then
    // emits KWin signals through the keep-flag setters, so iterating it live is
    // the same re-entrancy hazard the drain above documents. A key the drain
    // already consumed is a no-op, and a vanished window resolves to
    // drop-the-snapshot, which is the whole cleanup for a dead client.
    //
    // releaseWindowedFullscreenState, NOT restoreWindowedFullscreenLayerDemotion:
    // the demotion restores only the keep flags. An orphan is by definition a
    // snapshot whose membership was dropped without a release, so KWin fullscreen
    // may still be held — and leaving it held is exactly the "stranded in
    // fullscreen with nothing owning the flag" state the destructor calls this
    // function to prevent. The release is a strict superset: it calls the
    // demotion itself, resolves the window on its own, and does not consult
    // membership, so it is correct for a key that has none.
    if (m_effect->m_windowedFsLayerSnapshots.isEmpty()) {
        return;
    }
    const QStringList orphanIds = m_effect->m_windowedFsLayerSnapshots.keys();
    for (const QString& wid : orphanIds) {
        releaseWindowedFullscreenState(wid);
    }
}

void TilingHandler::clearTiledTracking()
{
    // Bookkeeping only. Physical title-bar restores are the
    // DecorationManager's job — teardown callers pair this with
    // DecorationManager::restoreAll().
    m_border.tiledWindowsByScreen.clear();
    // The screen set belongs to the daemon session that published it. Both
    // callers (daemon loss, effect teardown) mean that session is gone —
    // keeping the set let stale membership answer isAutotileScreen until the
    // next bringup reply, and left the bringup's fresh-set replacement with
    // no removed-screen delta to act on.
    //
    // This write takes the named teardown exemption from the
    // scrollingScreenIntersection snapshot/compare/invalidate contract (see
    // the header) — it is valid only while every caller is a teardown.
    m_managedScreens.clear();
}

void TilingHandler::setFocusFollowsMouse(bool enabled)
{
    m_focusFollowsMouse = enabled;
    if (ffmOffEverywhere()) {
        // handleCursorMoved bails before the suppression latch while FFM is
        // off everywhere, so a latch set just before the setting was turned
        // off would survive with a long-stale anchor and swallow the first
        // move after it is turned back on. The latch is shared across both
        // modes, so it clears only when NO screen can focus-follow — which is
        // the ffmOffEverywhere predicate, per-screen scrolling membership
        // included.
        m_ffmSuppressPending = false;
    }
}

void TilingHandler::setScrollingFocusFollowsMouse(bool enabled)
{
    m_scrollingFocusFollowsMouse = enabled;
    if (ffmOffEverywhere()) {
        // Same shared-latch reasoning as setFocusFollowsMouse.
        m_ffmSuppressPending = false;
    }
}

void TilingHandler::setWheelFocusEnabled(bool enabled)
{
    if (m_wheelFocusEnabled == enabled) {
        return;
    }
    m_wheelFocusEnabled = enabled;
    // Re-evaluate registration immediately: the flag is part of the want
    // predicate, and no screen-set change will fire on a settings save.
    updateScrollWheelShortcuts();
}

void TilingHandler::setWheelFocusInverted(bool inverted)
{
    if (m_wheelFocusInverted == inverted) {
        return;
    }
    m_wheelFocusInverted = inverted;
    // No re-registration pass, unlike setWheelFocusEnabled: the flag is not
    // part of the want predicate, only read at trigger time to pick a
    // direction.
}

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
                if (KWin::Window* kw = safeW->window(); kw && kw->maximizeMode() != KWin::MaximizeRestore) {
                    ++m_suppressMaximizeChanged;
                    kw->maximize(KWin::MaximizeRestore);
                    --m_suppressMaximizeChanged;
                }
                // Snap-out: leaving zone-managed sizing.
                m_effect->applyWindowGeometry(safeW, QRect(reply.argumentAt<1>(), reply.argumentAt<2>(), rw, rh),
                                              /*allowDuringDrag=*/false, /*skipAnimation=*/false,
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

bool TilingHandler::isManagedScreen(const QString& screenId) const
{
    return m_managedScreens.contains(screenId);
}

void TilingHandler::slotScrollEffectBehaviourChanged(const QVariantMap& behaviour)
{
    applyScrollEffectBehaviour(behaviour);
}

void TilingHandler::applyScrollEffectBehaviour(const QVariantMap& behaviour)
{
    // The daemon publishes the whole map every time (never a delta), so a
    // straight replace is correct even if a previous signal was missed.
    //
    // Boundary validation, mirroring slotActiveLayoutsChanged: this map
    // crosses D-Bus from another process, and both halves decide compositor
    // behaviour (focus stealing, forced composition). An a{sv} value arrives
    // either already demarshalled (the property Get path, which qdbus_cast
    // unwraps) or still wrapped in a QDBusVariant (a signal delivered without
    // a registered argument type) — unwrap one level before the type test, or
    // every live update silently clears BOTH sets. Empty screen ids are
    // dropped: no window resolves to one, and they only defeat the change
    // gate below. A wire regression is warned about rather than being
    // indistinguishable from a legitimately-off session.
    const auto toSet = [](const QVariant& raw, QLatin1StringView key) {
        QVariant v = raw;
        if (v.typeId() == QMetaType::fromType<QDBusVariant>().id()) {
            v = qvariant_cast<QDBusVariant>(v).variant();
        }
        // Qt's demarshaller hands an `as` back as a ready QStringList, but a
        // container it did not special-case arrives as a raw QDBusArgument —
        // which converts to nothing and would read as "off everywhere". Demarshal
        // it explicitly rather than letting a transport-shape change silently
        // disable both behaviours.
        if (v.typeId() == QMetaType::fromType<QDBusArgument>().id()) {
            v = QVariant::fromValue(qdbus_cast<QStringList>(v));
        }
        QSet<QString> out;
        if (!v.isValid()) {
            // Absent half. Not a warning: the daemon may legitimately publish
            // only the keys it has resolved, and an absent key reads as "off
            // everywhere", the same safe direction bring-up takes.
            return out;
        }
        if (!v.canConvert<QStringList>()) {
            qCWarning(lcEffect) << "scrollEffectBehaviour: dropping non-list value for" << key << "type"
                                << v.typeName();
            return out;
        }
        const QStringList list = v.toStringList();
        out.reserve(list.size());
        for (const QString& screenId : list) {
            if (screenId.isEmpty()) {
                qCWarning(lcEffect) << "scrollEffectBehaviour: dropping empty screen id from" << key;
                continue;
            }
            out.insert(screenId);
        }
        return out;
    };
    const QSet<QString> ffm =
        toSet(behaviour.value(QStringLiteral("focusFollowsMouse")), QLatin1String("focusFollowsMouse"));
    const QSet<QString> crop =
        toSet(behaviour.value(QStringLiteral("cropStraddlers")), QLatin1String("cropStraddlers"));
    // Seeded BEFORE the change gate, the m_activeLayoutsSeeded shape: an
    // identical map is still a real map, and the daemon's first publish is
    // legitimately all-empty on a session with no scrolling screen. Gating the
    // flag would leave blocksDirectScanout permanently falling back to the
    // global setting there.
    m_scrollEffectBehaviourSeeded = true;
    m_scrollFocusFollowsMouseScreens = ffm;
    // Fourth site of the ffmOffEverywhere predicate: this write is what can
    // take the LAST focus-follows-mouse screen away while both globals were
    // already off, and handleCursorMoved's bail (the latch's only other
    // disarm) sits behind the very predicate that just went true — so a latch
    // armed by an engine-driven strip move would survive here with a stale
    // anchor and swallow the first move after the rule turns FFM back on.
    if (ffmOffEverywhere()) {
        m_ffmSuppressPending = false;
    }
    if (crop == m_scrollCropStraddlerScreens) {
        return;
    }
    m_scrollCropStraddlerScreens = crop;
    // The crop set is PAINTED state: a screen that just started (or stopped)
    // cropping has stale pixels on it, and nothing else will revisit them —
    // the strip's geometry did not move, so no tile batch is coming. The
    // focus-follows-mouse set needs no such bookend; it is read fresh on the
    // next pointer move.
    if (KWin::effects) {
        KWin::effects->addRepaintFull();
    }
}

void TilingHandler::clearScrollEffectBehaviourForTeardown()
{
    m_scrollEffectBehaviourSeeded = false;
    m_scrollFocusFollowsMouseScreens.clear();
    if (ffmOffEverywhere()) {
        // Same latch reasoning as the apply above — the dead session's set was
        // the last thing keeping FFM alive anywhere, and its anchor names a
        // cursor position from before the teardown.
        m_ffmSuppressPending = false;
    }
    if (m_scrollCropStraddlerScreens.isEmpty()) {
        return;
    }
    m_scrollCropStraddlerScreens.clear();
    // Painted state, so the same bookend the live apply takes: the clip stops
    // cutting on every screen that was cropping, and nothing else repaints
    // those outputs.
    if (KWin::effects) {
        KWin::effects->addRepaintFull();
    }
}

void TilingHandler::slotScrollingScreensChanged(const QStringList& screenIds)
{
    // Mode discriminator — no per-screen LIFECYCLE transitions here (the
    // union set arriving via slotScreensChanged owns those). But the set IS
    // an input to ruleQuery's Mode stamp, and rule verdicts are memoised per
    // window: on an autotile↔scrolling flip the union does not move, so
    // slotScreensChanged never invalidates anything and a `Mode Equals
    // "scrolling"` border/opacity/decoration rule would keep its stale
    // verdict indefinitely. Invalidate + sweep on a GENUINE change only
    // (identical-set desktop-switch re-emits stay free).
    setScrollingScreens(QSet<QString>(screenIds.cbegin(), screenIds.cend()));
}

void TilingHandler::setScrollingScreens(const QSet<QString>& newSet, bool announceFlipped)
{
    // Any authoritative write voids in-flight property replies, identical
    // set or not — the writer is always newer than a reply dispatched
    // earlier (see the m_scrollingScreensGeneration doc).
    ++m_scrollingScreensGeneration;
    if (newSet == m_scrollingScreens) {
        // Skipping updateScrollWheelShortcuts at the tail is deliberate and
        // stays correct only while its want predicate reads nothing but the
        // enable flag and the set's emptiness, neither of which an identical
        // set moves. A predicate that starts reading the set's CONTENTS would
        // have to be re-evaluated here.
        return;
    }
    const QSet<QString> oldSet = m_scrollingScreens;

    // Engine-flip re-announce. A screen that changes tiling ENGINE while
    // staying in the union (autotile↔scrolling) never transits
    // managedScreensChanged — the union is emit-on-change and does not move —
    // so slotScreensChanged cannot demote and re-announce its windows. The
    // daemon side has already torn the old engine's state down and the new
    // engine claims an EMPTY screen: windows keep their old rects and every
    // verb on the new engine refuses. Re-announce the flipped screens'
    // windows here; the daemon routes windowOpened by the screen's current
    // mode, so the receiving engine adopts them (order-seeded from the
    // capture the daemon took during the flip). Cross-union transitions
    // (snapping↔scrolling) still announce exactly once regardless of which
    // signal lands first: whichever handler sees the screen inside
    // m_managedScreens does the work, the other filters it out
    // (notifyWindowsAddedBatch drops screens outside the union, and
    // slotScreensChanged only processes union membership changes).
    QSet<QString> flipped = (newSet - oldSet) + (oldSet - newSet);
    flipped &= m_managedScreens;
    const bool announcing = announceFlipped && !flipped.isEmpty();

    // The re-announce's per-window screen ids are resolved HERE, under the
    // OLD scrolling set, and threaded into the batch. getWindowScreenId's
    // engine-authoritative override is gated on m_scrollingScreens membership
    // (via scrollTrackedScreenFor), so the moment the assignment below drops a
    // screen from the set, a parked strip column — which the strip places
    // ENTIRELY outside its own output — resolves POSITIONALLY onto the
    // neighbouring output. The batch would then filter it out against
    // `flipped` and never announce it, stranding the window at its parked rect
    // with neither engine owning it. The entering direction needs no such care
    // (those windows are on-canvas, so positional and override agree), but
    // resolving both under the old set keeps one rule for the whole batch.
    QList<KWin::EffectWindow*> announceWindows;
    QHash<KWin::EffectWindow*, QString> announceScreens;
    if (announcing && KWin::effects) {
        announceWindows = KWin::effects->stackingOrder();
        announceScreens.reserve(announceWindows.size());
        for (KWin::EffectWindow* w : std::as_const(announceWindows)) {
            // Close-grabbed dying windows linger in the stacking order and
            // resolving one re-pollutes the scrubbed id caches — the same bail
            // the batch itself takes before any id lookup.
            if (w && !w->isDeleted()) {
                announceScreens.insert(w, m_effect->getWindowScreenId(w));
            }
        }
    }

    // Windowed fullscreen ends for windows on screens leaving the scrolling
    // set: their strip is gone, and the new engine's batches (which would
    // otherwise un-flag them entry by entry) skip windows it floats, so the
    // batch path alone cannot be relied on. Collected against the
    // PRE-CAPTURED screen map — the engine-authoritative override dies with
    // the assignment below, and a live resolve after it lands parked
    // columns on the wrong output (the announceScreens comment above).
    // Membership keyed iteration: the hash value is a rect, so the window's
    // screen comes from the capture, never from the hash.
    //
    // DEPENDENCY, stated rather than removed: announceScreens is populated
    // only when `announcing` is true, so this release rides the RE-ANNOUNCE
    // and not the set shrinking on its own. An announceFlipped=false call, or
    // one whose flipped screens fall outside m_managedScreens, leaves the
    // enumeration empty and releases nothing. Both such callers compensate
    // deliberately and say so at their own site — the bring-up fetch
    // (wiring.cpp) runs before any batch has populated the membership hash,
    // and the daemon-handover path (tilinghandler.cpp) calls
    // restoreAllWindowedFullscreen immediately BEFORE its
    // setScrollingScreens({}, false). A future announceFlipped=false caller
    // that can reach here with live membership must do the same, or resolve
    // the leaving screens independently of the announce.
    QStringList windowedFsLeavingScrolling;
    if (!m_effect->m_windowedFullscreenWindows.isEmpty()) {
        const QSet<QString> leavingScrolling = oldSet - newSet;
        for (auto it = announceScreens.constBegin(); it != announceScreens.constEnd(); ++it) {
            if (!leavingScrolling.contains(it.value())) {
                continue;
            }
            const QString wid = m_effect->getWindowId(it.key());
            if (m_effect->m_windowedFullscreenWindows.contains(wid)) {
                forgetWindowedFullscreen(wid);
                windowedFsLeavingScrolling.append(wid);
            }
        }
    }

    m_scrollingScreens = newSet;
    for (const QString& wid : std::as_const(windowedFsLeavingScrolling)) {
        releaseWindowedFullscreenState(wid);
    }

    // A screen LEAVING the scrolling set mid-leg must take its view spring
    // and strip shader pass with it. The instant the set changes,
    // scrollManagedOutputFor answers null for every column on that screen,
    // so the paint path stops applying the offset (the columns snap to
    // committed geometry) — but the spring and the armed pass know nothing
    // of the set, so the pass would keep capturing and decorating a scene
    // that is no longer scrolling for the leg's remaining duration, with
    // the whole capture now classified as wallpaper-under-everything.
    // forgetOutput fires no repaint of its own, so damage the output too:
    // the last presented frame carries the dying offset/pass and nothing
    // else is scheduled to repaint it away.
    for (const QString& removedScreen : oldSet - newSet) {
        if (KWin::LogicalOutput* out = m_effect->outputForScreenId(removedScreen)) {
            m_effect->m_stripTransition.outputRemoved(out);
            m_effect->m_stripViewAnimator->forgetOutput(out);
            if (KWin::effects) {
                KWin::effects->addRepaint(out->geometry());
            }
        }
    }

    m_effect->invalidateAllRuleCaches();
    m_effect->scheduleBorderSweep();
    // Mode is a ruleQuery input too, so the same static-window problem the
    // active-layout write documents applies here: a `Mode Equals "scrolling"`
    // SetOpacity rule flips verdict on an engine swap, and an undamaged
    // window would keep its last-painted alpha until incidental damage.
    if (m_effect->m_shaderManager.hasOpacityRules() && KWin::effects) {
        KWin::effects->addRepaintFull();
    }

    if (announcing) {
        qCInfo(lcEffect) << "Scrolling flip within managed union — re-announcing windows on" << flipped;
        // A flipped screen's pending staggered applies were computed by the
        // OLD engine; void them per-screen before the re-announce drives the
        // new engine's batch. The new batch captures its generations at
        // build time, after this bump, so it is unaffected. (This is the
        // union-internal twin of slotScreensChanged's removed-screens bump —
        // the global epoch stays reserved for desktop switches.)
        for (const QString& screenId : std::as_const(flipped)) {
            ++m_tileStaggerGenByScreen[screenId];
        }
        // enteringAutotile=true: the flag is a MODE-ENTRY discriminator, not an
        // autotile-specific one. Left false, an already-minimized window on the
        // flipped screen took claimAlreadyMinimizedAsFloated's early return and
        // got neither the untiled-minimize marker nor the per-screen float
        // re-assert, so on unminimize it sat at the PRIOR engine's rect for the
        // animation grace and then visibly hopped into its new tile — the same
        // class as the minimized-window-on-mode-swap regression.
        notifyWindowsAddedBatch(announceWindows, flipped, /*resetNotified=*/true,
                                /*enteringAutotile=*/true, announceScreens);
    }
    updateScrollWheelShortcuts();
}

void TilingHandler::slotActiveLayoutsChanged(const QVariantMap& activeLayouts)
{
    // Boundary validation: this map crosses D-Bus from another process and
    // lands directly in a rule-match input. An empty key would be a screen id
    // no window can ever resolve to (dead weight that still defeats the
    // change gate), and a non-string value would silently stringify to
    // something no authored rule can match.
    QHash<QString, QString> next;
    next.reserve(activeLayouts.size());
    for (auto it = activeLayouts.cbegin(); it != activeLayouts.cend(); ++it) {
        if (it.key().isEmpty()) {
            qCWarning(lcEffect) << "activeLayouts: dropping entry with empty screen id";
            continue;
        }
        // a{sv} values can arrive either already demarshalled to their inner
        // type (the property Get path, which qdbus_cast unwraps) or still
        // wrapped in a QDBusVariant (a signal delivered without a registered
        // argument type). Unwrap one level before the type test so the guard
        // filters genuinely wrong types instead of silently discarding every
        // entry the moment the transport shape changes.
        QVariant value = it.value();
        if (value.typeId() == QMetaType::fromType<QDBusVariant>().id()) {
            value = qvariant_cast<QDBusVariant>(value).variant();
        }
        if (value.typeId() != QMetaType::QString) {
            qCWarning(lcEffect) << "activeLayouts: dropping non-string layout id for screen" << it.key() << "type"
                                << value.typeName();
            continue;
        }
        next.insert(it.key(), value.toString());
    }
    setActiveLayouts(next);
}

void TilingHandler::setActiveLayouts(const QHash<QString, QString>& activeLayouts)
{
    // Any authoritative write voids in-flight property replies, identical
    // map or not — the writer is always newer than a reply dispatched
    // earlier (see the m_activeLayoutsGeneration doc).
    ++m_activeLayoutsGeneration;
    // Seed BEFORE the change gate: an identical map is still a real map, and
    // the daemon's very first push is legitimately empty on a session with no
    // engine-managed screen. Gating the flag would leave the effect
    // permanently unseeded there, holding every ActiveLayout rule out of the
    // evaluator for the whole session.
    if (!m_activeLayoutsSeeded) {
        m_activeLayoutsSeeded = true;
        // Seeding edge: ActiveLayout-referencing rules were held out of every
        // effect-bound rule set while the map was unknown (see
        // activeLayoutsSeeded). Re-fetch the store so the admission filter
        // re-runs with the field admitted. Above the change gate because an
        // all-empty first map is still the edge; async, so it lands after
        // everything below regardless of where it sits here.
        //
        // Gated on the effect's withheld marker: the re-drive costs a
        // getAllRules round-trip, a full RuleSet parse and an
        // updateAllDecorations sweep, and it can only change an outcome when
        // the last admission pass actually dropped a rule for referencing
        // ActiveLayout. A pass that ran with the map already seeded leaves the
        // marker false, which is also the right answer: it admitted the field
        // itself. The other writer is clearActiveLayoutsForTeardown, which
        // sets the marker when its re-slice actually removed a rule — so an
        // unseed that withheld nothing does not arm this edge either. The
        // marker is consumed here so a later unseed→seed cycle re-drives only
        // on its own evidence.
        if (m_effect->m_activeLayoutRulesWithheld) {
            m_effect->m_activeLayoutRulesWithheld = false;
            m_effect->loadRuleAnimationsFromDbus();
        }
    }
    if (activeLayouts == m_activeLayouts) {
        return;
    }
    m_activeLayouts = activeLayouts;
    // Rule verdicts are memoised per window and ActiveLayout is a ruleQuery
    // input: a layout/template change on any screen can flip a border,
    // opacity, decoration, or animation-exclusion verdict, so the whole
    // cache goes (there is no per-screen invalidation surface) and the
    // sweep repaints borders that changed.
    m_effect->invalidateAllRuleCaches();
    m_effect->scheduleBorderSweep();
    // The sweep re-folds decorations, but a SetOpacity rule scoped on
    // ActiveLayout alters paint output for windows that are otherwise static
    // and undamaged — those never reach a paint pass to pick the new alpha
    // up. Same bookend loadRuleAnimationsFromDbus takes on a rule edit,
    // gated on rule PRESENCE so a session with no opacity rule pays nothing.
    // The KWin::effects term matches every sibling bookend in this file: the
    // teardown orderings that leave the handle null all reach a setter of one
    // kind or another, and this one is no less reachable than setScrollingScreens'.
    if (m_effect->m_shaderManager.hasOpacityRules() && KWin::effects) {
        KWin::effects->addRepaintFull();
    }
}

void TilingHandler::clearActiveLayoutsForTeardown()
{
    ++m_activeLayoutsGeneration;
    m_activeLayouts.clear();
    m_activeLayoutsSeeded = false;
    // Take the rules that resolve against the map back out of the effect's
    // rule sets — see the header doc for why leaving them bound over the
    // daemon-down interval over-matches. This also arms the seed edge above
    // (it sets m_activeLayoutRulesWithheld when it removed anything), so the
    // rules are re-admitted from the live store on the next real map.
    //
    // No border sweep and no cache invalidation here, by the call-site
    // contract: both callers run invalidateAllRuleCaches (which drops the
    // verdicts these removals change, and carries the window-layer sweep)
    // immediately after, and each then rebuilds what those verdicts had baked
    // into decorations its own way — onDaemonReady with scheduleBorderSweep,
    // the serviceUnregistered teardown with clearAllDecorations, which tears
    // the decorations down outright and so needs no sweep. The re-slice does take
    // the SetOpacity repaint bookend, which neither caller covers on the
    // handover path — see its own doc.
    m_effect->sliceActiveLayoutRulesForUnseededMap();
}

void TilingHandler::updateScrollWheelShortcuts()
{
    // The enable setting folds into the want predicate so turning it off
    // genuinely releases the axis chords back to the compositor, rather
    // than swallowing them.
    const bool want = m_wheelFocusEnabled && !m_scrollingScreens.isEmpty();
    if (want == !m_scrollWheelActions.isEmpty()) {
        return;
    }
    if (!want) {
        // Destroying the QAction unregisters the axis shortcut (KWin's
        // shortcut manager erases entries on QAction::destroyed), releasing
        // the chord for any later registrant. deleteLater rather than a
        // manual delete: these are parented QObjects, and a delete here
        // would run inside whatever emitted the mode change. The sub-turn
        // window before the deferred delete lands is benign — KWin APPENDS
        // duplicate registrations and matches the FIRST, and a still-live
        // doomed action drives the same wheelFocusColumn as its replacement.
        for (QAction* action : std::as_const(m_scrollWheelActions)) {
            action->deleteLater();
        }
        m_scrollWheelActions.clear();
        qCInfo(lcEffect) << "Scroll wheel shortcuts unregistered (no scrolling screens)";
        return;
    }
    // niri's default Mod+wheel bindings: wheel down / right focuses the
    // next column to the right, wheel up / left the previous one. The
    // horizontal pair covers tilted wheels, and horizontal touchpad scrolls
    // once the accumulated delta clears KWin's 1.0 threshold (processAxis
    // only fires on |delta| >= 1.0).
    //
    // Meta ONLY — no Meta+Alt fallback, and the mechanics matter (verified
    // against KWin 6.7 source): KWin's GlobalShortcutsManager APPENDS
    // duplicate axis registrations and match() returns the FIRST entry, so
    // whoever registered earlier wins. KWin core registers
    // Meta+Alt+WheelUp/Down for Switch to Next/Previous Desktop at init,
    // before any effect loads — a Meta+Alt pair here could only ever lose
    // that match and sit dead. Plain Meta is free on a stock setup: the
    // zoom effect's axis modifiers default to Meta+Ctrl, not Meta. A user
    // who rebinds zoom onto plain Meta creates a duplicate whose winner is
    // whichever effect registered earlier in the session.
    const auto add = [this](Qt::KeyboardModifiers mods, KWin::PointerAxisDirection axis, int delta,
                            const QString& name) {
        auto* action = new QAction(this);
        action->setObjectName(name);
        connect(action, &QAction::triggered, this, [this, delta]() {
            wheelFocusColumn(delta);
        });
        KWin::effects->registerAxisShortcut(mods, axis, action);
        m_scrollWheelActions.append(action);
    };
    add(Qt::MetaModifier, KWin::PointerAxisDown, 1, QStringLiteral("pz-scroll-column-right"));
    add(Qt::MetaModifier, KWin::PointerAxisUp, -1, QStringLiteral("pz-scroll-column-left"));
    add(Qt::MetaModifier, KWin::PointerAxisRight, 1, QStringLiteral("pz-scroll-column-right-h"));
    add(Qt::MetaModifier, KWin::PointerAxisLeft, -1, QStringLiteral("pz-scroll-column-left-h"));
    qCInfo(lcEffect) << "Scroll wheel shortcuts registered (Meta+wheel focuses columns)";
}

void TilingHandler::wheelFocusColumn(int delta)
{
    if (!m_effect->m_daemonGate.serviceRegistered) {
        return;
    }
    // Re-gate on the enable flag: between setWheelFocusEnabled(false)'s
    // deleteLater and the deferred delete actually landing, the doomed
    // action is still registered and can fire one more tick.
    if (!m_wheelFocusEnabled) {
        return;
    }
    if (m_wheelFocusInverted) {
        delta = -delta;
    }
    // The strip that moves is the one under the CURSOR (Meta+wheel is a
    // pointer gesture, not a focus verb): resolve the cursor's effective
    // screen — virtual subdivisions included — and only forward when it
    // actually runs the scrolling engine. On any other screen the chord is
    // consumed but inert; registration is per-session, not per-screen.
    const QPointF pos = KWin::effects->cursorPos();
    const QPoint rounded(qRound(pos.x()), qRound(pos.y()));
    const auto* output = KWin::effects->screenAt(rounded);
    if (!output) {
        return;
    }
    const QString screenId = m_effect->resolveEffectiveScreenId(rounded, output);
    // isScrollingScreen, not the raw set: it intersects with the managed union,
    // so a screen the union already dropped cannot still swallow the chord and
    // forward a focusColumn the engine no longer owns.
    if (!isScrollingScreen(screenId)) {
        return;
    }
    qCDebug(lcEffect) << "Wheel focus column: delta" << delta << "on" << screenId;
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Scrolling,
                                                   QStringLiteral("focusColumn"), {screenId, delta},
                                                   QStringLiteral("focusColumn"));
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

bool TilingHandler::isEligibleForTilingNotify(KWin::EffectWindow* w, bool* rejectedOnlyBecauseMinimized) const
{
    if (rejectedOnlyBecauseMinimized) {
        *rejectedOnlyBecauseMinimized = false;
    }
    // Null first, so the gates below need no per-gate `w &&` prefix.
    if (!w) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (null window)";
        return false;
    }
    // Close-grabbed dying windows survive in the stacking order for the
    // close-animation duration; announcing one as opened would insert an
    // orphan into the tiling tree (shrinking live tiles) until a later
    // retile cleans it up.
    if (w->isDeleted()) {
        return false;
    }
    // Early-out: KWin internal surfaces (overlay QQuickViews, zone overlays, etc.)
    // are never eligible for autotile notification. KWin's InternalWindow::minSize()
    // segfaults when the backing QWindow is null. See discussion #511.
    if (w->window() && w->window()->isInternal()) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (internal window)" << m_effect->getWindowId(w);
        return false;
    }
    // Compute the scrolling windowed-fullscreen exemption BEFORE the
    // handleable check and thread it through: shouldHandleWindow's structural
    // fullscreen reject would otherwise fire here, 30 lines before this
    // function's own fullscreen clause below, and at effect bring-up (empty
    // membership hash) that made the documented re-adoption of a flagged
    // column unreachable — the window was never announced, so no batch could
    // ever restore membership. Mirrors notifyWindowActivated's exemption.
    // REQUESTED or committed. EffectWindow::isFullScreen() is the committed
    // bit, which lags a client round-trip on Wayland — so a window the
    // OpenFullscreen rule just fullscreened (the flip writes the requested bit
    // synchronously at windowAdded, decoration_rules.cpp) read as NOT
    // fullscreen here and was announced, pushing the about-to-be-fullscreen
    // frame to the daemon as free geometry with overwrite=true, which the
    // free-geometry guard's own contract forbids. The union closes that window
    // and costs nothing elsewhere: the exit path re-announces on the committed
    // exit signal, by which point neither bit is set.
    KWin::Window* kwFs = w->window();
    const bool fullScreen = w->isFullScreen() || (kwFs && kwFs->isRequestedFullScreen());
    const bool fullscreenOnScrollingScreen = fullScreen && isScrollingScreen(m_effect->getWindowScreenId(w));
    if (!m_effect->shouldHandleWindow(w, nullptr, /*exemptFullscreen=*/fullscreenOnScrollingScreen)) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (not handleable)" << m_effect->getWindowId(w);
        return false;
    }
    if (!m_effect->isTileableWindow(w)) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (not tileable)" << m_effect->getWindowId(w);
        return false;
    }
    // A window that is fullscreen at first contact (opened fullscreen, or
    // present-fullscreen when autotile is enabled / the daemon restarts):
    // KWin owns its geometry and re-asserts the fullscreen frame, so
    // announcing it would (a) push the fullscreen frame as free geometry
    // with overwrite=true and (b) make the daemon try to tile a window KWin
    // won't let move. The exit-fullscreen slot re-announces it via
    // notifyWindowAdded once it returns to a normal frame.
    //
    // Exempt: a scrolling WINDOWED-FULLSCREEN window. Its fullscreen state
    // is the effect's own doing and its geometry is the column rect the
    // daemon still owns, so the rationale above does not apply — and a
    // re-announce (screen churn, effect bring-up) must not silently drop it.
    //
    // The membership half of that exemption cannot fire at effect BRING-UP:
    // the hash fills from the daemon's batches, and the announce is what
    // triggers the batch. So a SCROLLING screen exempts fullscreen windows
    // wholesale — a restarted effect re-announces a flagged window, the
    // daemon's stash claim re-flags it, and the adopt-on-batch arm restores
    // membership. A genuinely fullscreen window announced this way is the
    // acceptable half of the trade: the strip tiles it behind the
    // fullscreen surface (the daemon never untiles on fullscreen anyway),
    // the geometry apply bails on its requested state, and its exit lands
    // in an already-consistent strip instead of a never-announced limbo.
    // Snapping/autotile screens keep the reject in full.
    // The fullscreen term first so the overwhelmingly common non-fullscreen
    // window pays neither the id lookup nor the screen resolve. Reuses the
    // requested-OR-committed answer computed above — see its comment for why
    // the committed bit alone is not enough — and the scrolling-screen answer
    // already resolved into fullscreenOnScrollingScreen, which is exactly this
    // term inside the `fullScreen &&` short-circuit. Recomputing it here paid
    // getWindowScreenId (a scroll-tracking lookup) twice for every fullscreen
    // window.
    if (fullScreen
        && !(m_effect->m_windowedFullscreenWindows.contains(m_effect->getWindowId(w)) || fullscreenOnScrollingScreen)) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (fullscreen)" << m_effect->getWindowId(w);
        return false;
    }
    if (!w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (wrong desktop/activity)" << m_effect->getWindowId(w);
        return false;
    }
    // Reject windows smaller than the user-configured minimum size.
    // Prevents small utility windows (emoji picker, color picker, etc.)
    // from entering the tiling tree and disrupting the layout.
    const QRectF frame = w->frameGeometry();
    if ((m_effect->m_cachedMinWindowWidth > 0 && frame.width() < m_effect->m_cachedMinWindowWidth)
        || (m_effect->m_cachedMinWindowHeight > 0 && frame.height() < m_effect->m_cachedMinWindowHeight)) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (too small)" << m_effect->getWindowId(w)
                          << "size=" << frame.size() << "threshold=" << m_effect->m_cachedMinWindowWidth << "x"
                          << m_effect->m_cachedMinWindowHeight;
        return false;
    }
    // Checked LAST so the out-flag means "every other gate passed": the batch
    // announce claims such a window as minimize-floated (minimizedChanged
    // never fires for a window that was already minimized when its screen
    // entered autotile, so the runtime minimize→float path cannot cover it).
    // frameGeometry stays at the pre-minimize frame while minimized, so the
    // min-size gate above evaluates real dimensions for this ordering.
    if (w->isMinimized()) {
        qCDebug(lcEffect) << "isEligibleForTilingNotify: rejected (minimized)" << m_effect->getWindowId(w);
        if (rejectedOnlyBecauseMinimized) {
            *rejectedOnlyBecauseMinimized = true;
        }
        return false;
    }
    qCDebug(lcEffect) << "isEligibleForTilingNotify: accepted" << m_effect->getWindowId(w) << "size=" << frame.size()
                      << "class=" << w->windowClass() << "skipSwitcher=" << w->isSkipSwitcher()
                      << "keepAbove=" << w->keepAbove() << "transient=" << (w->transientFor() != nullptr);
    return true;
}

void TilingHandler::applyFloatCleanup(const QString& windowId)
{
    // Windowed fullscreen dies on float (the engine clears its tile flag by
    // taking the window OUT of the strip, so no batch entry ever arrives to
    // un-flag it here) — drop the client's fullscreen state now or it stays
    // fullscreen-configured while free-floating.
    // Route through the shared release helper: it carries the isDeleted
    // check, the keep-flag restore, AND the inGeometryApply bracket that
    // setFullScreen(false) needs — on X11 it synchronously emits
    // windowFrameGeometryChanged, and no float call site holds the guard,
    // so an unbracketed drop re-enters the VS-crossing detector mid-cleanup.
    if (m_effect->m_windowedFullscreenWindows.remove(windowId)) {
        releaseWindowedFullscreenState(windowId);
    }
    // The clear-in-flight marker dies with the hold, like the untrack
    // funnel and the snap↔snap belt drop it — a marker outliving the strip
    // membership can only refuse a future adopt (usually consumed by the
    // next flag-off entry, but a float means no batch entry ever arrives).
    m_windowedFsClearInFlight.remove(windowId);
    // A floating window is free to move itself — stop countering.
    m_effect->m_scrollCommandedRects.remove(windowId);
    m_effect->m_navigationHandler->setWindowFloating(windowId, true);
    // A floating window is no longer tile-managed on any screen — clear tiled
    // tracking. clearWindowTiledAllScreens re-resolves the window's rules when the
    // tiled status flips, so a baseline border / title-bar rule scoped to tiled
    // windows stops drawing / hiding on the now-floating window (the setWindowFloating
    // above also re-resolves on the IsFloating flip; both coalesce).
    clearWindowTiledAllScreens(windowId);
    // Drop centering/target tracking too — a floated window isn't being
    // tiled anymore so a stale entry here would trigger centering on the
    // next frameGeometryChanged, snapping the floated window back into an
    // old zone rect. slotWindowsTileRequested no longer clears these
    // globally (it can't without wiping sibling-VS state), so the float
    // path has to clean up after itself.
    m_tileTargetZones.remove(windowId);
    m_centeredWaylandZones.remove(windowId);
    // And the parked-column paint hint, for the same reason and with a
    // sharper failure. A floating window is never a parked column, but the
    // float leaves the entry behind: floatWindowInternal takes the window OUT
    // of the strip, so the batch its own relayout emits does not contain it,
    // and the per-entry write in slotWindowsTileRequested never runs to clear
    // it. The orphan is inert only while BOTH halves of scrollManagedOutputFor
    // stay shut, and a later snap on another screen reopens both: the snap
    // adds tiled membership, and scrollTrackedScreenFor falls back to
    // m_notifiedWindowScreens — which this cleanup does not clear — so it
    // answers with the OLD scrolling screen. The paint pass for the window's
    // real output then skips it as belonging elsewhere, and the window is
    // simply not drawn. The removal changes where the paint path draws the
    // window, so it pairs with damage per m_scrollVisualDelta's contract (the
    // float paths have no guaranteed follow-up geometry apply).
    if (m_effect->m_scrollVisualDelta.remove(windowId) > 0 && KWin::effects) {
        KWin::effects->addRepaintFull();
    }
    // Geometry first, then the decoration funnel — the order every sibling
    // exit path uses: the monocle unmaximize is a geometry change, and
    // resolving the chain after it means the resolve sees the window's
    // final shape.
    unmaximizeMonocleWindow(windowId);
    // Shared placement-flip funnel (update-or-remove in the same turn) —
    // the bare removal here left the float paths WITHOUT a bulk
    // updateAllDecorations follow-up (daemon auto-float past maxWindows)
    // undecorated until an unrelated refresh, the same drag-start blackout
    // the snap engine had. The tiled/floating facts were flipped above, so
    // the funnel resolves the floating-state chain.
    m_effect->reconcileDecorationOnPlacementFlip(windowId);
}

void TilingHandler::applyPassiveFloatShed(const QString& windowId)
{
    // The shed half of applyFloatCleanup, for the WindowTracking interface's
    // float channel (PlasmaZonesEffect::slotWindowFloatingChanged). That slot
    // receives floats from producers that never reach
    // TilingHandler::slotWindowFloatingChanged (the scroll passive channel's
    // windowFloatingStateSynced among them), so applyFloatCleanup never runs
    // for them and every one of these sheds was silently bypassed.
    // Deliberately EXCLUDES applyFloatCleanup's setWindowFloating: the passive
    // slot performs that write itself (daemon_apply.cpp, immediately before
    // this call), so repeating it here would only re-drive an idempotent
    // setter. unmaximizeMonocleWindow is excluded too, but on different
    // grounds — nothing on this channel covers it, and re-driving a maximize
    // restore from a passive float signal has not been shown safe against the
    // monocle batch that owns that membership. The tiled-membership clear IS
    // performed below: no caller on this channel does it, and a floating
    // window left recorded as tiled keeps the tiled appearance scope.
    //
    // Membership-OR-snapshot guard: the membership remove() is consumed by
    // the first call, and every arm of releaseWindowedFullscreenState (the
    // no-window miss included) erases the snapshot — so a SURVIVING snapshot
    // means membership was dropped by a path that never called the release
    // at all and the release is still owed. (cleanupAutotileTracking used to
    // be such a path; it now forgets and releases together, so no CURRENT
    // caller leaves a lone snapshot — the guard stays because the invariant
    // it protects is cheap and the next such path would be silent.) releaseWindowedFullscreenState is idempotent and
    // deliberately does not consult the membership hash, so re-driving it
    // off the snapshot is safe.
    const bool hadMembership = m_effect->m_windowedFullscreenWindows.remove(windowId);
    const bool released = hadMembership || m_effect->m_windowedFsLayerSnapshots.contains(windowId);
    if (released) {
        releaseWindowedFullscreenState(windowId);
    }
    // A stale clear-in-flight marker must not outlive the hold it guarded —
    // same rationale as the untrack funnel (cleanupAutotileTracking) and the
    // snap↔snap transfer belt.
    m_windowedFsClearInFlight.remove(windowId);
    // A floating window is free to move itself — stop countering.
    m_effect->m_scrollCommandedRects.remove(windowId);
    // A floating window is no longer tile-managed on any screen, and this
    // channel has no other writer of that fact (the passive slot's own
    // clearWindowSnapped covers the SNAP facts only). Left standing, IsTiled
    // stays true for a floated window and a tiled-scoped border / title-bar
    // rule keeps drawing or hiding. The helper is change-gated and
    // re-resolves the window's rules itself on the flip, so it costs nothing
    // for a window that was not tiled. Placed before the decoration re-drive
    // below, per reconcileDecorationOnPlacementFlip's flip-facts-first
    // contract.
    clearWindowTiledAllScreens(windowId);
    // Same rationale as applyFloatCleanup for all three: a stale target
    // re-triggers centering on the next frameGeometryChanged, and a stale
    // relocation-delta entry makes a later snap on another screen paint the window
    // at the dead strip position (or not at all).
    m_tileTargetZones.remove(windowId);
    m_centeredWaylandZones.remove(windowId);
    // The removal changes where the paint path draws the window (relocated
    // position → nothing), and unlike the active channel this path has no
    // follow-up geometry apply guaranteed to damage — so pair it.
    if (m_effect->m_scrollVisualDelta.remove(windowId) > 0 && KWin::effects) {
        KWin::effects->addRepaintFull();
    }
    // Decoration re-drive: the sibling exit paths (the self-exit arms, the
    // active channel's applyFloatCleanup) all re-resolve chrome on this
    // transition, and shouldDecorateWindow's fullscreen reject lifts the
    // moment the release above lands — without this the window comes back
    // with no PlasmaZones chrome until an unrelated sweep. The rule-cache
    // invalidation the passive slot performs later early-returns in a
    // default-config session, so it cannot substitute. Gated on the RELEASE
    // having run, not on membership: the caller's clearWindowSnapped
    // reconciled before this shed (fact-flip contract), i.e. while the
    // window was still fullscreen, so a snapshot-only release with no
    // follow-up reconcile here would leave the window undecorated until an
    // unrelated sweep.
    if (released) {
        m_effect->reconcileDecorationOnPlacementFlip(windowId);
    }
}

void TilingHandler::markWindowTiled(const QString& screenId, const QString& windowId)
{
    const bool wasTiled = isTiledWindow(windowId);
    // Single-owner enforced HERE, not by call-site discipline: a window
    // belongs to exactly one screen bucket, and readers that answer with the
    // first bucket found (screenForTiledWindow, the outputchange scroll
    // guard) are only correct while that holds. A future caller that skipped
    // its own removeFromOtherScreens would otherwise break them silently.
    TilingStateHelpers::removeFromOtherScreens(m_border, windowId, screenId);
    TilingStateHelpers::addTiledOnScreen(m_border, screenId, windowId);
    // Re-resolve only on the false→true transition: a window already tiled on
    // another screen stays tiled, so re-adding it changes no rule outcome.
    if (!wasTiled) {
        m_effect->invalidateRuleCacheForStateChange(windowId);
    }
}

void TilingHandler::clearWindowTiledAllScreens(const QString& windowId)
{
    if (TilingStateHelpers::removeFromAllScreens(m_border, windowId)) {
        // Was tiled on at least one screen and now is not — IsTiled flipped.
        m_effect->invalidateRuleCacheForStateChange(windowId);
    }
}

void TilingHandler::clearWindowTiledOnScreen(const QString& screenId, const QString& windowId)
{
    if (TilingStateHelpers::removeTiledOnScreen(m_border, screenId, windowId) && !isTiledWindow(windowId)) {
        // Removed from this screen and not tiled on any other — IsTiled flipped.
        m_effect->invalidateRuleCacheForStateChange(windowId);
    }
}

} // namespace PlasmaZones
