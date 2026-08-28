// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Monocle and windowed-fullscreen lifecycle for TilingHandler.
//
// Split out of state.cpp by concern. These two share one invariant and belong
// together: both maintain an OWNERSHIP LEDGER of a compositor state PlasmaZones
// imposed on a window (KWin maximize for monocle, KWin fullscreen plus a
// keep-flag demotion for windowed fullscreen), and in both cases the effect is
// the only thing that can hand that state back. Membership is therefore shed
// only on an arm that actually restores, never merely because the window's
// situation changed. See unmaximizeMonocleWindow for why the fullscreen guard
// sits above the removal rather than below it.

#include "tilinghandler.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "compositor/effectlogging.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effectwindow.h>
#include <window.h>

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QList>
#include <QLoggingCategory>
#include <QScopeGuard>

namespace PlasmaZones {

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

// ═══════════════════════════════════════════════════════════════════════════════
// Column maximize (scrolling)
// ═══════════════════════════════════════════════════════════════════════════════

void TilingHandler::applyMaximizeSuppressed(KWin::Window* kw, KWin::MaximizeMode mode)
{
    if (!kw) {
        return;
    }
    // Counter for the same reason applyFullScreenSuppressed uses one: the
    // batch consumer and the interception arm nest their own brackets, and a
    // plain set/clear would hand an outer scope back an un-suppressed window.
    ++m_suppressMaximizeChanged;
    kw->maximize(mode);
    --m_suppressMaximizeChanged;
}

bool TilingHandler::interceptMaximizeRequest(KWin::EffectWindow* w)
{
    if (!w || w->isDeleted()) {
        return false;
    }
    const QString windowId = m_effect->getWindowId(w);
    if (windowId.isEmpty()) {
        return false;
    }
    // A FLOATED window on a scrolling screen is not a tile, and float is the
    // one genuine pass-through: the user took it out of the strip, so its
    // maximize is KWin's business and the engine has no column to act on.
    if (!isTiledWindow(windowId)) {
        return false;
    }
    // The screen the strip placed it on, not wherever it sits this instant.
    // A window mid-way through a cross-screen move has already been notified
    // for its destination, and that is the strip whose focused column the
    // verb acts on.
    QString screenId = m_notifiedWindowScreens.value(windowId);
    if (screenId.isEmpty()) {
        screenId = m_effect->getWindowScreenId(w);
    }
    if (!isScrollingScreen(screenId)) {
        return false;
    }
    KWin::Window* kw = w->window();
    if (!kw) {
        return false;
    }
    // CANCEL, then dispatch. The bit goes back to whatever the engine last
    // said (membership), never to what the click asked for: the engine owns
    // this state and is about to answer for itself, so writing the user's
    // request here would make the effect a second authority and open the
    // race resolveColumnMaximizeAction's contract note says does not exist.
    //
    // The cancel is also what makes a REFUSED verb safe. toggleMaximizeColumn
    // is a silent no-op for a screen the engine does not own or a context
    // gate that is closed, and in that case no batch follows at all — without
    // the cancel the window would simply stay KWin-maximized, fighting the
    // strip's next rect with nothing left to correct it.
    const KWin::MaximizeMode restored =
        m_columnMaximizedWindows.contains(windowId) ? KWin::MaximizeFull : KWin::MaximizeRestore;
    // ALREADY AGREES — nothing was requested, so nothing is redirected. This
    // is the arm that makes the interception idempotent against the echo of
    // its OWN cancel below: m_suppressMaximizeChanged covers the synchronous
    // X11 emission from inside maximize(), but on Wayland the committed
    // signal arrives a client round-trip later with the counter back at 0,
    // and without this the echo would read as a fresh user maximize and
    // dispatch a second toggle that undoes the first.
    //
    // Claimed rather than declined: the caller must not run its maximize
    // shader for a state change this handler authored.
    if (kw->maximizeMode() == restored) {
        return true;
    }
    {
        // maximize() emits windowFrameGeometryChanged synchronously on X11
        // and moveResizes to the restore rect, which can sit in a different
        // virtual-screen region — the same edge unmaximizeMonocleWindow
        // guards. Save/restore so the guard nests inside an already-guarded
        // caller.
        const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
        m_effect->m_daemonGate.inGeometryApply = true;
        const auto geomGuard = qScopeGuard([this, prevInApply] {
            m_effect->m_daemonGate.inGeometryApply = prevInApply;
        });
        applyMaximizeSuppressed(kw, restored);
        // Tracker re-seed, pairing with the suppressed VS-crossing detectors
        // exactly as unmaximizeMonocleWindow does.
        m_effect->m_trackedScreenPerWindow[w] = m_effect->getWindowScreenId(w);
    }
    dispatchMaximizeColumnToggle(screenId);
    return true;
}

void TilingHandler::dispatchMaximizeColumnToggle(const QString& screenId)
{
    // Fire and forget. There is no marker to arm and so nothing an error
    // reply could un-arm: the effect changed no state of its own above beyond
    // undoing KWin's, and a lost call leaves the window exactly where the
    // cancel put it — the pre-click state, which is the correct answer for a
    // request that never arrived.
    if (!m_effect->m_daemonGate.serviceRegistered) {
        return;
    }
    PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::Scrolling,
                                                   QStringLiteral("toggleMaximizeColumn"), {screenId},
                                                   QStringLiteral("toggleMaximizeColumn"));
}

void TilingHandler::releaseColumnMaximized(const QString& windowId, KWin::Window* kw)
{
    if (!m_columnMaximizedWindows.contains(windowId)) {
        return;
    }
    m_columnMaximizedWindows.remove(windowId);
    // Fullscreen guard, for the reason unmaximizeMonocleWindow spells out:
    // maximize() has no fullscreen conditional, so on a still-fullscreen
    // window it moveResizes down to geometryRestore and shrinks a presenting
    // surface. Reachable here because windowed fullscreen and column
    // maximize are deliberately NOT exclusive.
    //
    // Membership is shed ABOVE the guard rather than below it, the opposite
    // of the monocle ledger's choice, and for a reason specific to this
    // state: the maximize bit here is a MIRROR of engine state that the next
    // batch re-asserts through the Apply arm whenever the engine still says
    // maximized. Retaining membership on the skip would instead leave the
    // effect claiming to hold a bit it never restored, and the Apply arm's
    // kwinMaximized term would then read the window's own fullscreen bit as
    // proof the mirror is current.
    if (!kw || kw->isFullScreen() || kw->isRequestedFullScreen()) {
        return;
    }
    if (kw->maximizeMode() != KWin::MaximizeRestore) {
        applyMaximizeSuppressed(kw, KWin::MaximizeRestore);
    }
}

void TilingHandler::restoreAllColumnMaximized()
{
    if (m_columnMaximizedWindows.isEmpty()) {
        return;
    }
    // Snapshot and clear FIRST — same iterator-invalidation hazard as
    // restoreAllMonocleMaximized: maximize() can synchronously re-enter
    // cleanupClosedWindowState through the output-changed path.
    const QStringList ids = m_columnMaximizedWindows.values();
    m_columnMaximizedWindows.clear();
    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
    m_effect->m_daemonGate.inGeometryApply = true;
    const auto geomGuard = qScopeGuard([this, prevInApply] {
        m_effect->m_daemonGate.inGeometryApply = prevInApply;
    });
    ++m_suppressMaximizeChanged;
    for (const QString& wid : ids) {
        // EXACT resolve — the fuzzy appId fallback would un-maximize an
        // unrelated same-app sibling under suppression, invisibly.
        KWin::EffectWindow* w = m_effect->findWindowByIdExact(wid);
        if (!w) {
            continue;
        }
        KWin::Window* kw = w->window();
        if (kw && !kw->isFullScreen() && !kw->isRequestedFullScreen() && kw->maximizeMode() != KWin::MaximizeRestore) {
            kw->maximize(KWin::MaximizeRestore);
            m_effect->m_trackedScreenPerWindow[w] = m_effect->getWindowScreenId(w);
        }
    }
    --m_suppressMaximizeChanged;
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
                // path left to heal a stale entry, and neither the teardown nor
                // drainDeadSessionState clears this map across the restart.
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

} // namespace PlasmaZones
