// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// D-Bus signal slot handlers for TilingHandler.
// Part of TilingHandler — split from tilinghandler.cpp for SRP.

#include "tilinghandler.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "compositor/windowanimator.h"
#include "handlers/navigationhandler.h"
#include "handlers/snaphandler.h" // cross-mode minimize-float adoption
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorIdentity/WindowId.h>

#include <effect/effect.h> // Effect::animationTime, the deferred-unfloat grace
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <workspace.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QPointer>
#include <QScopeGuard>
#include <QTimer>

#include <chrono> // std::chrono::milliseconds for Effect::animationTime

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

// The minimize/unminimize float state machine and its tuning constants live
// in minimizefloat.cpp.

// ═══════════════════════════════════════════════════════════════════════════════
// D-Bus signal slot handlers
// ═══════════════════════════════════════════════════════════════════════════════

void TilingHandler::slotEnabledChanged(bool enabled)
{
    qCInfo(lcEffect) << "Autotile enabled state changed:" << enabled;
    if (!enabled) {
        // Internal-state cleanup only — fast, no compositor round-trips.
        // Title-bar restores are NOT done here: slotScreensChanged (which
        // always fires alongside this signal, both originating from the same
        // setActiveScreens(QSet()) call in the engine) handles them via a
        // delayed defer so the resnap dispatch can land first. Doing it here
        // would race with that defer and block applyGeometriesBatch.
        restoreAllMonocleMaximized();
        // The exact analogue for windowed fullscreen, and deliberately the
        // context-agnostic full-set drain: the slotScreensChanged sweep skips
        // sticky and off-current-desktop windows (its per-context discipline
        // protects setNoBorder), but setFullScreen and the keep flags are
        // global per-window properties, and with the engine disabled no later
        // batch or per-context pass exists to release a skipped window — it
        // would stay KWin-fullscreen with keep-below held for the session.
        restoreAllWindowedFullscreen();
        m_savedAutotileStackingOrder.clear();
        m_savedNotifiedForDesktopReturn.clear();
        // Drop any in-flight debounced minimize→float commits — they must not
        // fire against a disabled engine.
        clearAllPendingMinimizeFloats();
        // Cancel the ASYNC unfloat continuations too: clearing the in-flight
        // map makes their generation checks miss, so a D-Bus reply landing
        // after the disable cannot mutate ownership state against a torn-down
        // engine. The minimize-float MARKERS deliberately survive — the snap
        // handler adopts them when the screen's new mode fields the
        // unminimize.
        m_unfloatInFlight.clear();
        m_unfloatRetryAttempts.clear();
        m_effect->updateAllDecorations();
    }
}

void TilingHandler::slotWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId)
{
    qCInfo(lcEffect) << "Autotile floating changed:" << windowId << "isFloating:" << isFloating
                     << "screen:" << screenId;

    // Re-key to the live id BEFORE either branch, the same re-key the batch
    // apply performs (tiling.cpp documents why): the daemon can still name a
    // pre-restore uuid. On the float side applyFloatCleanup's id-keyed drops
    // — the windowed-fullscreen release among them — silently miss on the
    // stale spelling; on the UNFLOAT side the FloatingCache entry (written
    // under the live id by the float side) would be cleared under the stale
    // spelling instead, leaving the live entry answering "floating" to every
    // reader (FFM pause, pre-tile capture guard, the desktop-switch and
    // fullscreen-exit skips) for the rest of the session — and this slot is
    // the ONLY writer for an autotile unfloat (the WindowTracking relay
    // deliberately excludes AutotileEngine::windowFloatingChanged). The
    // pending-focus stamp uses the live id too: its consumer resolves via
    // findWindowByIdExact, which refuses the fuzzy fallback.
    // Resolved ONCE and reused by both branches below. Re-resolving with the
    // wire spelling would run the fuzzy same-app fallback a second time, so the
    // branch could act on a different window than the one liveId names, and
    // stamp the pending-focus id for one while raising another.
    // QPointer, not a raw pointer: applyFloatCleanup below reaches
    // unmaximizeMonocleWindow, whose maximize() emits frameGeometryChanged
    // synchronously into effect handlers, and this pointer is dereferenced
    // after that. The pre-hoist code re-resolved at each use and so could not
    // hold a stale one.
    const QPointer<KWin::EffectWindow> resolvedWin = m_effect->findWindowById(windowId);
    QString liveId = windowId;
    if (resolvedWin) {
        liveId = m_effect->getWindowId(resolvedWin);
    }

    if (!isFloating) {
        m_effect->m_navigationHandler->setWindowFloating(liveId, false);
        KWin::EffectWindow* unfloatWin = resolvedWin;
        // Showing-desktop guard: this refocus is automatic (daemon float-state
        // signal), and activateWindow() would synchronously cancel a peek.
        if (unfloatWin && unfloatWin == KWin::effects->activeWindow() && !PlasmaZonesEffect::isShowingDesktop()) {
            m_pendingAutotileFocusWindowId = liveId;
            KWin::effects->activateWindow(unfloatWin);
        }
    } else {
        applyFloatCleanup(liveId);

        // Raise the floated window if it's the active window (user-initiated float)
        KWin::EffectWindow* floatWin = resolvedWin;
        if (!floatWin) {
            qCDebug(lcEffect) << "Autotile: window not found for float raise:" << windowId;
        } else if (floatWin == KWin::effects->activeWindow() && !PlasmaZonesEffect::isShowingDesktop()) {
            // Showing-desktop guard mirrors the unfloat branch above. The
            // == activeWindow() predicate usually covers this (peek focuses
            // the desktop window), but with no desktop window to take focus
            // the floated window can still be "active" while hidden.
            m_pendingAutotileFocusWindowId = liveId;
            auto* ws = KWin::Workspace::self();
            if (ws) {
                KWin::Window* kw = floatWin->window();
                if (kw) {
                    ws->raiseWindow(kw);
                }
            }
            KWin::effects->activateWindow(floatWin);
        }
    }
}

void TilingHandler::slotWindowMaximizedStateChanged(KWin::EffectWindow* w, bool horizontal, bool vertical)
{
    // isDeleted: same close-grab hazard the fullscreen slot documents.
    if (m_suppressMaximizeChanged || !w || w->isDeleted()) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    if (!m_monocleMaximizedWindows.contains(windowId)) {
        return;
    }
    if (horizontal || vertical) {
        return;
    }
    // Membership drops ONLY when the daemon can actually be told, the same rule
    // the windowed-fullscreen exit follows. The float dispatch below is what
    // makes the daemon agree the window is no longer monocle-maximized; with
    // the gate closed it never goes out, and dropping the membership anyway
    // left the effect having forgotten while the daemon still modelled the
    // window as tiled, with nothing to re-drive it. Kept, the membership keeps
    // the mismatch legible to the paths that DO drain the set (the fullscreen
    // exit, the untrack funnel, restoreAllMonocleMaximized). None of those
    // announces the float either, so a manual unmaximize while the daemon is
    // down still ends in a one-sided view — this only decides which side holds
    // it, and the effect is the side that can act on it later.
    if (!m_effect->m_daemonGate.serviceRegistered) {
        qCInfo(lcEffect) << "Monocle window manually unmaximized with no daemon:" << windowId
                         << "- keeping membership until the float can be announced";
        return;
    }

    m_monocleMaximizedWindows.remove(windowId);
    // Screen ID (EDID-based) for daemon tracking D-Bus calls. Resolved below
    // the gate so the no-daemon path does not pay for it.
    const QString screenId = m_effect->getWindowScreenId(w);
    qCInfo(lcEffect) << "Monocle window manually unmaximized:" << windowId << "- floating";
    PhosphorProtocol::ClientHelpers::fireAndForget(
        m_effect, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("setWindowFloatingForScreen"),
        {windowId, screenId, true}, QStringLiteral("setWindowFloatingForScreen"));
}

void TilingHandler::slotWindowFullScreenChanged(KWin::EffectWindow* w)
{
    // isDeleted: a dying window's fullscreen flip must not create a stale
    // decoration claim / border-tracking entry that only close-cleanup sweeps.
    if (!w || w->isDeleted()) {
        return;
    }
    // Our own windowed-fullscreen setFullScreen call is in flight: neither
    // branch below may run. The enter branch would shed the very
    // tiled/decoration state the flag keeps, and the exit branch would
    // re-drive decorations for a window whose column rect never changed.
    if (m_suppressFullScreenChanged > 0) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    // A windowed-fullscreen window's ENTER just committed (this signal
    // fires on the COMMITTED state, a client round-trip after the batch
    // flipped the requested state and applied the column rect): KWin
    // re-asserts the FullScreenArea on that ack commit, so the column rect
    // must be re-applied here or the window ends up covering the output.
    // The enter-branch's tracking shed below may not run for it — the
    // window stays a managed strip tile for ROUTING (tiled bucket,
    // eligibility, activation reporting). Presentation is a different
    // story, deliberately: the decoration gates undecorate it like any
    // fullscreen window (a client in fullscreen presentation gets no
    // PlasmaZones chrome), which also means IsFullscreen-scoped appearance
    // rules (SetBorder/SetOpacity/tints) match but render nothing for it.
    // SetWindowLayer rules are PAUSED for the window while the flag holds:
    // the feature owns the keep flags (the layer demotion that defeats
    // KWin's active-fullscreen promotion), and reconcileRuleWindowLayer
    // skips members so the two owners never trade flags mid-hold.
    if (w->isFullScreen() && m_effect->m_windowedFullscreenWindows.contains(windowId)) {
        // The generic enter branch's centering-map shed applies to our
        // transition too: a stale zone-centering target consumed against
        // the fullscreen ack's frame change would moveResize against the
        // fullscreen geometry (the exact hazard that cleanup documents).
        m_tileTargetZones.remove(windowId);
        m_centeredWaylandZones.remove(windowId);
        // A live per-window leg means a batch committed the right rect this
        // very tick — reaping it (the skipAnimation tail calls
        // removeAnimation) would snap one column out of the strip's slide.
        // But the clobber still has to be countered: the leg's own moveResize
        // already ran BEFORE the ack (the animator never re-commits), so
        // re-commit the column rect directly on the KWin window without
        // touching the animator — in BOTH arms. The former no-animation arm
        // routed through applyWindowGeometry, whose already-at-target no-op
        // skip could swallow the re-commit when this slot runs before KWin's
        // FullScreenArea clobber lands (the frame still reads as the column
        // rect, the skip drops the apply, the later clobber stands). The
        // direct moveResize is skip-proof, and none of applyWindowGeometry's
        // extra services apply here (the window is live, visible and not
        // mid-drag). Bracketed: moveResize emits frameGeometryChanged
        // synchronously on X11 and this slot's caller holds no apply guard —
        // an unbracketed re-entry would reach the VS-crossing detector (and
        // the counter-assert gate) for a move the effect itself made.
        //
        // The rect goes through constrainTileGeometry rather than being
        // committed raw, so that this site and applyWindowGeometry cannot
        // disagree about what the stored column rect means. On the path that
        // actually runs the call is a no-op: the helper returns its input
        // unchanged for anything that is not an X11 client, and the note below
        // records that the X11 enter signal is suppressed inside the bracketed
        // setFullScreen, so this branch is Wayland's in practice. It is kept
        // because the helper is idempotent and free here, and because a raw
        // commit would be wrong the moment an X11 path did reach it — for a
        // size-constrained client KWin clamps the size on moveResize without
        // re-centring, which for a parked column cancels the relocation back
        // to the pre-fix behaviour.
        {
            const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
            m_effect->m_daemonGate.inGeometryApply = true;
            const auto restoreGate = qScopeGuard([this, prevInApply] {
                m_effect->m_daemonGate.inGeometryApply = prevInApply;
            });
            if (KWin::Window* kw = w->window()) {
                kw->moveResize(
                    QRectF(m_effect->constrainTileGeometry(w, m_effect->m_windowedFullscreenWindows.value(windowId))));
                // The bracket swallowed any outputChanged this move emitted, so
                // the detector never ran its own tracker write. Re-seed it,
                // the same pairing rule the two other bracketed applies in
                // this file follow. For this branch the window is a windowed-
                // fullscreen member and therefore normally strip-tracked, so
                // the value read back is the engine-authoritative screen rather
                // than a position re-detect. Where tracking has already lapsed
                // it falls through to the positional resolve, which for a
                // windowed-fullscreen frame agrees anyway. Either way the write
                // reasserts rather than repairs, and exists so the bracket's
                // pairing rule holds uniformly.
                m_effect->m_trackedScreenPerWindow[w] = m_effect->getWindowScreenId(w);
            }
        }
        // Shed the decoration now: shouldDecorateWindow rejects on the
        // COMMITTED fullscreen state, which was still false when the batch's
        // onComplete sweep ran on Wayland — without this, PlasmaZones chrome
        // stays drawn over the fullscreen presentation until an unrelated
        // sweep. (On X11 the enter signal is suppressed inside the bracketed
        // setFullScreen, so this branch never runs there and the onComplete
        // sweep sheds correctly.)
        m_effect->removeWindowDecoration(windowId);
        return;
    }
    // A windowed-fullscreen window left fullscreen on its own (the client's
    // in-app toggle — mpv's f key): the strip flag no longer matches
    // reality. Reconcile: drop local membership and tell the daemon, whose
    // engine clears the tile flag and re-applies. Decorations return with
    // the daemon's next batch pass, same as the tracked-retile exit arm.
    //
    // Membership drops ONLY when the daemon can actually be told: with the
    // gate closed, dropping it here desyncs the two owners in the daemon's
    // favour, and the daemon's next batch (its flag persisted, its emit
    // gate silent) would re-fullscreen the window the user just exited.
    // Kept, the membership makes that state legible instead: the batch
    // consumer's self-heal arm sees flagged+member+not-fullscreen and
    // delivers this same reconcile when the daemon returns.
    if (!w->isFullScreen() && m_effect->m_windowedFullscreenWindows.contains(windowId)) {
        if (m_effect->m_daemonGate.serviceRegistered) {
            m_effect->m_windowedFullscreenWindows.remove(windowId);
            restoreWindowedFullscreenLayerDemotion(windowId, w->window());
            qCInfo(lcEffect) << "Windowed-fullscreen window left fullscreen on its own:" << windowId;
            // In-flight marker + reply-gated dispatch: an already-emitted
            // batch still carrying flag=true must not re-adopt against this
            // exit (the batch arm's flag-off echo consumes the marker), and
            // the helper drops the marker on a failed clear so it cannot
            // latch for the session.
            dispatchWindowedFullscreenClear(windowId);
            m_effect->updateAllDecorations();
        } else {
            qCInfo(lcEffect) << "Windowed-fullscreen window left fullscreen with the daemon gone:" << windowId
                             << "- reconcile deferred to the next batch";
            m_effect->updateAllDecorations();
        }
        return;
    }
    if (!w->isFullScreen()) {
        // EXIT fullscreen: a window the daemon still tiles (it stayed in
        // m_notifiedWindows the whole time — the daemon never untiles on
        // fullscreen) returns to its tiled rect, so re-establish the
        // decoration claim and border tracking the enter-path released.
        // EVERY exit path below must re-drive decorations, not just the
        // tracked-retile one: the ENTER branch unconditionally removed this
        // window's decoration, and shouldDecorateWindow's fullscreen reject
        // has now lifted — a snap-mode or plain-floating window (untracked
        // here, screen not managed) otherwise stays undecorated until an
        // unrelated sweep (focus change, rule edit). KWin does not re-fire
        // windowActivated for an already-active window on fullscreen exit,
        // so nothing else heals it.
        const QString screenId = m_notifiedWindowScreens.value(windowId);
        if (!m_notifiedWindows.contains(windowId)) {
            // Never-tracked window: a window that OPENED fullscreen was
            // rejected by isEligibleForTilingNotify (fullscreen guard) and
            // never announced. Now that it has a normal frame, announce it so
            // the daemon tiles it (notifyWindowAdded re-checks eligibility,
            // including the current desktop/activity).
            const QString currentScreen = m_effect->getWindowScreenId(w);
            if (m_managedScreens.contains(currentScreen)) {
                notifyWindowAdded(w, /*knownFreeFloating=*/true);
            }
            m_effect->updateAllDecorations();
            return;
        }
        if (screenId.isEmpty()) {
            m_effect->updateAllDecorations();
            return;
        }
        // Autotile was disabled on this window's tracked screen while it was
        // fullscreen on a non-current desktop (the genuine-toggle path skips
        // non-current-desktop windows, so the tracking survived). Re-claiming
        // onto a no-longer-autotiled screen would hide the title bar with no
        // retile coming — demote the stale tracking and release instead.
        if (!m_managedScreens.contains(screenId)) {
            m_notifiedWindows.remove(windowId);
            m_notifiedWindowScreens.remove(windowId);
            m_effect->updateAllDecorations();
            return;
        }
        // Floating windows stay untracked: a window floated while fullscreen
        // (manual toggle, minimize-float, overflow batch-float — all keep
        // m_notifiedWindows intact) is free-floating on exit.
        if (m_effect->isWindowFloating(windowId)) {
            m_effect->updateAllDecorations();
            return;
        }
        markWindowTiled(screenId, windowId);
        // A scrolling screen's fullscreen exit must not trust KWin's
        // restore: KWin re-applies the window's PRE-fullscreen rect one
        // client round-trip after the strip already placed it (a full-area
        // rect for a window that went fullscreen before announce, a stale
        // PARK rect for a windowed-fullscreen toggle-off — both seen
        // live), and the engine's emit-on-change gate stays silent because
        // its own rects never moved, so no batch would ever correct the
        // stray frame. Tell the engine to evict that window's emit-gate
        // memory and re-emit. (The first attempt pulled
        // WindowTracking.getUpdatedWindowGeometries here; that method is
        // snap-mode-only — it resolves zone-assigned windows — and returns
        // nothing for a scrolling screen, which is why the ghostty
        // toggle-off strand survived it.)
        if (isScrollingScreen(screenId) && m_effect->m_daemonGate.serviceRegistered) {
            PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::Scrolling,
                                                           QStringLiteral("reapplyWindowGeometry"), {windowId},
                                                           QStringLiteral("reapplyWindowGeometry"));
        }
        // Title-bar (borderless) state is driven by rules.
        m_effect->updateAllDecorations();
        return;
    }
    // Clear border tracking so borders are not drawn over fullscreen content
    // (title-bar restores flow through the rule path).
    clearWindowTiledAllScreens(windowId);
    if (m_monocleMaximizedWindows.remove(windowId)) {
        qCInfo(lcEffect) << "Monocle window went fullscreen:" << windowId << "- removed from tracking";
    }
    // Drop any unconsumed zone-centering entries: a window that fullscreens
    // right after being tiled must not have the fullscreen frame change
    // consume a stale centering target and moveResize against the fullscreen
    // geometry (same cleanup applyFloatCleanup performs).
    m_tileTargetZones.remove(windowId);
    m_centeredWaylandZones.remove(windowId);
    // The per-session scroll companions go too. The membership clear above
    // makes them inert for as long as the window stays fullscreen (the paint
    // path resolves the scroll output through that membership), but the EXIT
    // branch re-marks the window tiled — so a stale park relocation computed
    // before the fullscreen would be applied to the restored frame, for at
    // least the round trip of the reapply request the exit dispatches, and
    // indefinitely when the daemon gate is closed. The float exits shed the
    // same pair for the same reason.
    m_effect->m_scrollCommandedRects.remove(windowId);
    if (m_effect->m_scrollVisualDelta.remove(windowId) > 0 && KWin::effects) {
        KWin::effects->addRepaintFull();
    }
    m_effect->removeWindowDecoration(windowId);
}

} // namespace PlasmaZones
