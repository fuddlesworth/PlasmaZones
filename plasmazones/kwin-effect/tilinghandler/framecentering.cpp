// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frame-commit handling for TilingHandler: slotWindowFrameGeometryChanged,
// which centres a tile whose client committed a smaller size than it was
// asked for and reports a minimum size it discovers on the way, and
// reportMinSizeIfChanged, the min-size reporter it shares with the batch.
// Split out of tiling.cpp, which keeps the tile batch pipeline.

#include "tilinghandler.h"
#include "scrolldecisions.h"
#include "handlers/dragtracker.h"
#include "handlers/screenchangehandler.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "plasmazoneseffect/gestureenddecisions.h"
#include "compositor/windowanimator.h"
#include "compositor/effectlogging.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorIdentity/VirtualScreenId.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>

#include <QDateTime>
#include <QDBusPendingCallWatcher>
#include <QLoggingCategory>
#include <QScopeGuard>

namespace PlasmaZones {

void TilingHandler::slotWindowFrameGeometryChanged(KWin::EffectWindow* w, const QRectF& oldGeometry)
{
    // isDeleted: every other entry point bails on a corpse BEFORE the id
    // lookup, explicitly to avoid re-polluting the scrubbed id caches; this
    // slot sees strictly more geometry changes since the counter-assert
    // widened its fast bail.
    if (!w || w->isDeleted()) {
        return;
    }

    // Fast bail: skip getWindowId entirely when no consumer below needs it
    if (m_effect->m_virtualScreenDefs.isEmpty() && m_tileTargetZones.isEmpty()
        && m_effect->m_scrollCommandedRects.isEmpty()) {
        return;
    }

    const QString windowId = m_effect->getWindowId(w);

    // Counter-assert for a scroll-managed window an EXTERNAL mover relocated.
    // X11 clients for the self-mover reason below; column-maximize members on
    // either platform, because KWin's own maximize-area re-assert is an
    // external mover the self-mover reasoning does not cover.
    // Any frame change landing here outside our own apply
    // bracket was not ours: X11 clients can reposition themselves through
    // ConfigureRequests KWin honors, and a Wine game re-asserting its
    // saved window position was seen live pulling its frame back on-screen
    // out of the strip's park and straddle placements — sitting over its
    // neighbour's column until the next user scroll, because the engine's
    // emit-on-change gate had nothing to say. Re-apply the commanded rect,
    // without animation (this is enforcement, not motion). The counter is
    // RATE-LIMITED to 3 per rolling second (the window resets once a second
    // elapses since the burst started, and every fresh batch command
    // re-arms it) — a client that re-asserts on every configure gets
    // countered at most 3 times per rolling second PER COMMAND, so it does
    // not win outright. Note the budget is re-armed by every fresh batch
    // command (the insert resets the pair, deliberately), so on a scrolling
    // strip the effective ceiling is three per batch rather than three per
    // second.
    //
    // User-move/resize terms: DragTracker never tracks an interactive
    // RESIZE at all (its start handler bails on isUserResize), and a mouse
    // drag stays live past forceEnd until all buttons release — in both
    // gaps isDragging() is false while the user is actively manipulating
    // the frame, and the counter would fight the user's own gesture.
    // The commandedRect entry survives a resize that STARTS after the
    // batch (the per-batch disarm only covers one already in flight).
    //
    // Screen gate through scrollTrackedScreenFor, not the raw notified map:
    // the apply loop marks tiled (bar a window in its own fullscreen) but
    // records the screen only for notified windows, so a demoted window is a
    // tiled member with no recorded screen — the helper resolves that
    // (fail-closed either way; the helper just fails closed for the right
    // set).
    // Wayland is excluded for the SELF-mover reason above, with one exception:
    // a column-maximize member has an external mover that exists on both
    // platforms — KWin's own maximize-area re-assert. That population arms a
    // commanded rect at the apply site for exactly this, and nothing else
    // reaches it, so the general exclusion and its reasoning stand.
    // A maximized-to-edges member whose maximize bit is mid-transition is exempt
    // on EVERY platform, XWayland included, and that exemption is what keeps
    // the counter off the user's own restore.
    //
    // KWin emits frameGeometryChanged from INSIDE maximize(), before
    // maximizedChanged, so on a restore click this slot runs first: the frame
    // has already moved to the restore rect while m_scrollCommandedRects still
    // holds the maximized column's rect, and the interception has not yet run,
    // let alone dispatched the toggle the engine will answer with a narrower
    // batch. Unqualified, the counter reads that as an external mover and
    // shoves the window back to full width, which the arriving batch then
    // undoes — a full-width bounce on every restore, seen live at 14ms.
    //
    // Nothing is given up. The mover this arm was added for is KWin's own
    // maximize-area re-assert, which by definition happens while the window IS
    // maximized, so it still passes. What no longer passes is a frame change
    // that arrives with the bit already gone, which is never that mover: it is
    // a maximize/restore transition, and the commanded rect describing the
    // layout the engine is in the middle of replacing has no authority over
    // it. Enforcement resumes as soon as the batch re-arms the entry.
    // The transition test gates the WHOLE predicate, not just the Wayland
    // member arm. XWayland windows are handled inside this Wayland session and
    // report isWaylandClient() false, so a member arm qualified only on the
    // right of the || is unreachable for them: the first disjunct
    // short-circuits true and the restore bounce described above survives
    // untouched on every XWayland scroll-managed window. Hoisting it keeps the
    // X11 arm intact for the mover it exists for (a client moving ITSELF, the
    // Wine case) while exempting the one frame change that is never an
    // external mover on either platform.
    KWin::Window* kwCounter = w->window();
    const bool inMaximizeTransition = kwCounter && m_maximizedToEdgesWindows.contains(windowId)
        && kwCounter->requestedMaximizeMode() != KWin::MaximizeFull;
    const bool externallyMovable = !inMaximizeTransition
        && (!w->isWaylandClient()
            || (kwCounter && m_maximizedToEdgesWindows.contains(windowId)
                && kwCounter->requestedMaximizeMode() == KWin::MaximizeFull));
    if (externallyMovable && !m_effect->m_daemonGate.inGeometryApply && !w->isUserMove() && !w->isUserResize()) {
        const auto cit = m_effect->m_scrollCommandedRects.find(windowId);
        if (cit != m_effect->m_scrollCommandedRects.end() && isScrollingScreen(scrollTrackedScreenFor(windowId))
            && !(m_effect->m_dragTracker && m_effect->m_dragTracker->isDragging()
                 && windowId == m_effect->m_dragTracker->draggedWindowId())) {
            const QRect actual = w->frameGeometry().toRect();
            {
                // Budget arithmetic is pure and unit-tested
                // (scrolldecisions.h, test_scroll_decisions).
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                if (ScrollDecisions::shouldCounterAssert(cit->burstStartMs, cit->burstCount, nowMs,
                                                         actual != cit->rect)) {
                    // Copy out of the hash node first: the apply below emits
                    // frameGeometryChanged synchronously on X11 and re-enters
                    // this slot — holding a reference into the node across
                    // re-entrant code is undefined the day anything mutates
                    // the map from inside that window.
                    const QRect commanded = cit->rect;
                    qCInfo(lcEffect) << "Countering external move of scroll-managed window" << windowId << "from"
                                     << actual << "back to" << commanded;
                    // Bracketed like every sibling moveResize site: the
                    // synchronous re-entry must not fall through to the
                    // VS-crossing detector for a move the effect itself made.
                    // Routed through applyWindowGeometry ON PURPOSE, unlike
                    // the fullscreen-ack re-commit (signals.cpp): its
                    // already-at-target skip cannot fire here (this slot runs
                    // because the frame DIFFERS from the commanded rect), and
                    // its user-move defer is wanted — countering a live mouse
                    // drag would fight the user, and the drag machinery owns
                    // the re-insert; a superseding batch mooting the deferred
                    // replay is the correct outcome, not a drop.
                    // Save/restore, not set/clear (nesting-safe), through
                    // qScopeGuard like the four siblings in this file and
                    // signals.cpp. Hand-restoring was correct as written —
                    // applyWindowGeometry does not throw and the return is
                    // immediate — but it is the one site where an early exit
                    // added between the call and the restore would strand the
                    // flag set for the rest of the frame, silently muting the
                    // VS-crossing detector.
                    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
                    m_effect->m_daemonGate.inGeometryApply = true;
                    const auto counterGuard = qScopeGuard([this, prevInApply] {
                        m_effect->m_daemonGate.inGeometryApply = prevInApply;
                    });
                    // Re-asserting the commanded rect is not a new command, so
                    // the window keeps its stamp: a strip entry the next batch
                    // has already scheduled for it must still land.
                    const quint64 latestCommand = m_effect->m_daemonGate.commandStamps.current(w);
                    m_effect->applyWindowGeometry(w, commanded, /*allowDuringDrag=*/false, /*skipAnimation=*/true);
                    m_effect->m_daemonGate.commandStamps.reinstate(w, latestCommand);
                    return;
                }
            }
        }
    }

    // Virtual screen change detection: KWin's outputChanged only fires on
    // physical monitor changes. When a window moves between virtual screens
    // on the same physical monitor (e.g., A/vs:0 → A/vs:1), no outputChanged
    // fires. Detect the change here so the autotile engine can transfer the
    // window. Only check windows we're already tracking (m_notifiedWindowScreens)
    // and only when the physical screen has virtual subdivisions.
    // Skip during a daemon-driven apply (slotWindowsTileRequested /
    // slotApplyGeometriesBatch): the daemon is the authoritative source of the
    // window's intended VS during VS swap/rotate, and the cached
    // m_virtualScreenDefs may still reflect pre-rotation regions.
    if (m_notifiedWindows.contains(windowId) && !m_effect->m_virtualScreenDefs.isEmpty()
        && m_effect->m_daemonGate.virtualScreensReady && !m_effect->m_daemonGate.inGeometryApply) {
        // Don't detect VS crossings for the window in a gesture: the drop
        // handler (callDragStopped / autotile drag end) owns a drag's
        // transitions, and the resize drain owns a resize's (F486). Detecting
        // mid-gesture would transfer the window before the user lets go.
        // Other windows (e.g., a terminal reflowing) should still get VS crossing checks.
        // A tile whose client acks a larger size than its tile has not moved
        // either (F248): the centring pass below is what answers it.
        const bool heldByGesture = (m_effect->m_dragTracker && m_effect->m_dragTracker->isDragging()
                                    && windowId == m_effect->m_dragTracker->draggedWindowId())
            || m_effect->m_resizeHold.window == w;
        if (!heldByGesture && !GestureEndDecisions::isSizeOnlyChange(oldGeometry, QRectF(w->frameGeometry()))) {
            const QString newScreenId = m_effect->getWindowScreenId(w);
            const QString oldScreenId = m_notifiedWindowScreens.value(windowId);
            if (PhosphorIdentity::VirtualScreenId::isVirtualScreenCrossing(oldScreenId, newScreenId)) {
                // Virtual screen changed on the same physical monitor — delegate to
                // the same handler used by outputChanged. The re-entrancy guard
                // inside handleWindowOutputChanged prevents infinite loops from
                // geometry changes caused by tiling. The transfer itself runs
                // during a screen change too (the tile must follow its frame);
                // only the daemon notice waits, since the settle replays nothing
                // for a tracked tile.
                if (handleWindowOutputChanged(w) && !m_effect->m_screenChangeHandler->isScreenChangeInProgress()) {
                    m_effect->m_screenChangeHandler->reportCrossing(w, oldScreenId, newScreenId);
                }
                return;
            }
        }
        // Fall through to centering logic below for all windows (including dragged)
    }

    // Everything from here down is the reactive centring pass, and it is
    // WAYLAND-ONLY despite reading engine-general: m_tileTargetZones has
    // exactly one writer (the batch apply in this file), and that write sits
    // inside an `isWaylandClient()` arm. An X11 client never reaches this
    // block: constrainTileGeometry pre-centres its frame inside the zone
    // before the apply commits. An external mover is the counter-assert's on a
    // scrolling screen; on an autotile screen nothing counters it, and the
    // backstop below drops a target the mover took the window out of.
    if (m_tileTargetZones.isEmpty()) {
        return;
    }

    // Never centre from inside the effect's own apply bracket, the gate the two
    // other consumers in this slot take (the counter-assert and the VS
    // crossing check). A frame change emitted mid-apply carries the position
    // already but the size still awaiting the client's ack, and the pass would
    // read that stale size as a refusal and issue a competing moveResize that
    // supersedes the resize the client is answering. applyWindowGeometry
    // drops the window's entry before it commits, so this gate is what covers
    // the bracketed raw moveResize sites (the counter-assert, the fullscreen
    // re-commit) and this pass's own synchronous re-entry. Losing an enlarge
    // that way pinned the window a zone-width short for good, the centred
    // stamp latching it on every later batch (discussion #1028: dead bands
    // either side of a survivor, where focus-follows-mouse finds nothing).
    if (m_effect->m_daemonGate.inGeometryApply) {
        return;
    }

    auto it = m_tileTargetZones.find(windowId);
    if (it == m_tileTargetZones.end()) {
        return;
    }

    // The user owns the frame for the length of a move or resize. Centring a
    // mid-gesture frame would moveResize the window under the pointer (the
    // first step of a shrinking resize, a drag of a centred tile, a Reorder
    // drag) and stamp the old zone as centred. The tile no longer describes
    // the window either way, so the target and the stamp both go; whatever
    // the gesture ends in (a drop outcome, the next batch) commands it anew.
    if (w->isUserMove() || w->isUserResize()) {
        m_tileTargetZones.erase(it);
        m_centeredWaylandZones.remove(windowId);
        return;
    }

    const QRect& targetZone = it.value();
    const QRectF actual = w->frameGeometry();

    // Never centre while a resize configure is still in flight. KWin
    // reconciles moveResizeGeometry to the client's COMMITTED size (a
    // smaller-than-requested commit included) before it emits
    // frameGeometryChanged — XdgSurfaceWindow::handleNextWindowGeometry
    // calls maybeUpdateMoveResizeGeometry, then updateGeometry, which emits
    // — so at rest the two sizes agree and a genuinely-refusing client
    // passes this on the very commit that refused. The sizes diverge only
    // while an unacknowledged resize configure is pending, and a frame event
    // that arrives THEN (a move applied synchronously, an earlier commit)
    // still carries the pre-resize size. Centring on it would issue a
    // competing moveResize at that stale size and supersede the resize the
    // client is still answering — the enlarge-loss above, minus the second
    // batch: this closes the same race for a single batch against a slow
    // client, which the inGeometryApply gate cannot see.
    //
    // Skipping does NOT consume the entry: the commit that acknowledges the
    // configure re-fires this slot, agrees with moveResizeGeometry, and the
    // pass runs then. Sizes only — positions legitimately diverge mid-move —
    // and with a tolerance, because moveResizeGeometry holds the requested
    // fractional rect while the frame is snapped to pixels.
    if (KWin::Window* kwPending = w->window()) {
        const QRectF commandedRect = kwPending->moveResizeGeometry();
        // The entry describes the tile apply that recorded it, and only for
        // as long as that apply is the window's latest command. Every command
        // the effect itself issues drops it (applyWindowGeometry calls
        // dropCenteringTarget), which covers a snap zone apply landing before
        // the tiling release (discussion #1124). This is the backstop for a
        // mover outside the effect, a user or KWin script move, that takes
        // the window out of its tile: centring the frame that move produced
        // into the dead tile rect would drag the window back, possibly onto
        // an output it has left. The commanded origin is the witness: a tile
        // apply and every refusing or oversized commit keep it at the zone
        // origin. Containment rather than equality on purpose: a centred
        // origin also sits inside the zone, and an entry armed by an older
        // batch must not read a window this pass centred as moved away. An
        // origin outside the zone means another command owns the window now.
        // The centred stamp goes too, or the redundant-apply skip in
        // slotWindowsTileRequested would honour it if the window is tiled into
        // the same zone again.
        if (!QRectF(targetZone).adjusted(-1.0, -1.0, 1.0, 1.0).contains(commandedRect.topLeft())) {
            qCDebug(lcEffect) << "Autotile centering: target superseded for" << windowId
                              << "commanded=" << commandedRect << "target=" << targetZone << "- dropping";
            m_tileTargetZones.erase(it);
            m_centeredWaylandZones.remove(windowId);
            return;
        }
        const QSizeF commanded = commandedRect.size();
        if (qAbs(commanded.width() - actual.width()) > 1.0 || qAbs(commanded.height() - actual.height()) > 1.0) {
            qCDebug(lcEffect) << "Autotile centering: configure in flight for" << windowId << "commanded=" << commanded
                              << "actual=" << actual.size() << "- waiting";
            return;
        }
    }

    constexpr qreal MinCenteringDelta = 3.0;

    const qreal dw = targetZone.width() - actual.width();
    const qreal dh = targetZone.height() - actual.height();

    // Window fills the zone (or close enough) — no centering needed; consume entry
    if (qAbs(dw) <= MinCenteringDelta && qAbs(dh) <= MinCenteringDelta) {
        qCDebug(lcEffect) << "Autotile centering: matched" << windowId << "dw=" << dw << "dh=" << dh;
        m_tileTargetZones.erase(it);
        return;
    }

    // Window doesn't match zone — center it within the zone so it's visually
    // balanced rather than stuck at the zone origin.
    // Clamp offsets to non-negative: when the window is LARGER than the zone
    // (oversized, dx < 0), left/top-align instead of centering. Centering an
    // oversized window pushes it to a negative position (off-screen left/top),
    // which is worse than a slight overflow to the right/bottom. The daemon
    // receives the min-size report below and will retile with adjusted zones.
    const qreal dx = qMax(0.0, dw / 2.0);
    const qreal dy = qMax(0.0, dh / 2.0);
    QRectF centered(targetZone.x() + dx, targetZone.y() + dy, actual.width(), actual.height());

    // Defensive bounds clamp: if the (oversized) window would extend past the
    // physical output containing the zone, shift it left/up so it stays on
    // the same output. Without this, a window whose min size exceeds its
    // zone leaks into an adjacent monitor — KWin then reassigns the window's
    // output and the autotile engine ejects it. The daemon-side bounds clamp
    // in recalculateLayout already shifts zones to fit, so this is a backstop
    // for cases where the zone still violates min size (script algorithms,
    // unsatisfiable constraints, residual rounding).
    //
    // Scope: this clamps to the physical Output*, NOT the virtual-screen
    // sub-region. Overflow that crosses a virtual-screen boundary on the
    // same physical monitor is the daemon-side clamp's responsibility (it
    // resolves the VS region from screenGeometry(screenId); the effect side
    // has no reliable lookup for that here).
    //
    // Contract parity with PhosphorGeometry::clampZonesToScreen: both keep
    // an "effective rect" inside the bounds. The two implementations
    // intentionally use different size sources — daemon-side uses
    // max(zone.size, declared minSize) because it runs *before* KWin
    // enforces min size, while the effect runs *after* and reads the actual
    // (already-enforced) frame size from `centered`. Same contract, different
    // input source and rect type (QRectF here, QRect there). Keep the four
    // shift formulas in sync at the contract level.
    if (auto* output = KWin::effects ? KWin::effects->screenAt(targetZone.center()) : nullptr) {
        const QRect screenGeo = output->geometry();
        // Use exclusive edges (x + width / y + height) since QRectF::right()
        // and QRect::right() disagree (QRect is x+width-1, QRectF is x+width).
        const qreal screenLeft = screenGeo.x();
        const qreal screenTop = screenGeo.y();
        const qreal screenRight = screenGeo.x() + screenGeo.width();
        const qreal screenBottom = screenGeo.y() + screenGeo.height();
        const QRectF preClamp = centered;
        if (centered.x() + centered.width() > screenRight) {
            centered.moveLeft(qMax(screenLeft, screenRight - centered.width()));
        }
        if (centered.y() + centered.height() > screenBottom) {
            centered.moveTop(qMax(screenTop, screenBottom - centered.height()));
        }
        // Symmetric left/top underflow: a centered position before the screen
        // origin (target zone with negative offset, oversized window centered
        // off-edge) gets snapped back. Matches the daemon-side clamp.
        if (centered.x() < screenLeft) {
            centered.moveLeft(screenLeft);
        }
        if (centered.y() < screenTop) {
            centered.moveTop(screenTop);
        }
        // Symmetric with daemon-side clampZonesToScreen logging: when the
        // clamp actually fired, log the before/after so a "clamp ran but
        // didn't fix it" report is diagnosable from one side.
        if (Q_UNLIKELY(lcEffect().isDebugEnabled()) && centered.topLeft() != preClamp.topLeft()) {
            qCDebug(lcEffect) << "Autotile centering: clamp adjusted" << windowId << "from" << preClamp.topLeft()
                              << "to" << centered.topLeft() << "screen=" << screenGeo;
        }
    } else {
        // screenAt may return null if the zone center happens to fall in the
        // air between outputs (unusual; daemon assigns zones to a real
        // screen). Log so the silent skip is diagnosable rather than
        // mysterious.
        qCDebug(lcEffect) << "Autotile centering: screenAt(" << targetZone.center()
                          << ") returned null — skipping bounds clamp for" << windowId;
    }

    // Window refused to shrink below its actual size — report its declared
    // minimum to the daemon so future retiles can account for it. Only report
    // when the window is larger than the zone (negative delta = oversized).
    // Ahead of the already-centred return below: an oversized window is
    // left-aligned at the zone origin, which is usually where it already
    // sits, so a report placed after that return never ran for the common
    // oversized shape and a hint the client raised late went unreported.
    //
    // IMPORTANT: Only use the window's declared minSize() from the compositor.
    // The frame geometry is the current size, which may be transiently larger
    // during resize animations (Wayland configure round-trips) or media player
    // loading. Reporting the frame geometry as the min-size creates a feedback
    // loop: inflated min → expanded zone → window fills expanded zone →
    // inflated min confirmed → ratio stuck.
    //
    // Previously, windows without a declared min-size fell back to
    // targetZone.width() as a bounded hint. This caused the same feedback
    // loop: the zone width became the stored min-size, which then prevented
    // the algorithm from reducing the zone on subsequent retiles — even when
    // the user adjusted the split ratio or a screen geometry change required
    // reflow. The stale min-size persisted until the window was removed or
    // unfloated (minimize+restore), making the ratio appear "stuck."
    //
    // Without the fallback, apps that don't declare a min-size simply won't
    // get min-size enforcement from this path. They still get the initial
    // min-size from the windowOpened D-Bus call (kw->minSize() at open time),
    // and the centering code handles the visual placement correctly.
    // declaredMinSize() carries the internal-window guard (KWin's
    // InternalWindow::minSize() segfaults on a null backing QWindow, see
    // discussion #511); internal windows never reach the autotile-centering
    // pipeline, but the helper keeps the call site safe independently of the
    // upstream eligibility filter.
    // The whole declared pair, through the same change-gated cache as the
    // batch poll (reportMinSizeIfChanged), so the two writers always agree.
    if (dw < -MinCenteringDelta || dh < -MinCenteringDelta) {
        const QSize declaredMin = declaredMinSize(w);
        if (declaredMin.width() > 0 || declaredMin.height() > 0) {
            reportMinSizeIfChanged(windowId, declaredMin);
        }
    }

    // Already at the centered position — record and consume. The stamp keeps
    // the centred FRAME beside the zone: the redundant-apply skip honours it
    // only while the window still sits exactly there, so an in-zone change
    // nothing re-centres (a client self-resize, a KWin or script move) cannot
    // latch the window uncentred.
    if (qAbs(actual.x() - centered.x()) < 1.0 && qAbs(actual.y() - centered.y()) < 1.0) {
        m_centeredWaylandZones[windowId] = targetZone;
        m_centeredWaylandFrames[windowId] = actual;
        m_tileTargetZones.erase(it);
        return;
    }

    KWin::Window* kw = w->window();
    if (!kw) {
        // No KWin::Window — consume stale entry to prevent perpetual lookups
        m_tileTargetZones.erase(it);
        return;
    }

    qCInfo(lcEffect) << "Centering autotile window" << windowId << "actual=" << actual.size()
                     << "zone=" << targetZone.size() << "offset=(" << dx << "," << dy << ")";

    // Erase BEFORE moveResize to prevent re-entrancy: moveResize emits
    // windowFrameGeometryChanged synchronously, which would re-enter
    // this slot and find the entry still present → infinite recursion → crash.
    m_centeredWaylandZones[windowId] = targetZone;
    m_centeredWaylandFrames[windowId] = centered;
    m_tileTargetZones.erase(it);
    // A live leg (usually the tile apply's own, still running at the client's
    // first ack) is RETARGETED onto the centred rect, not reaped: a reap jumped
    // the window mid-leg, and a geometry-owning morph lost its progress source,
    // fell to the expiry path and never ran its completion. PreservePosition
    // keeps the pixels on screen and bends toward the new rect, and the morph
    // is re-anchored at the same departure rect the animator now uses. Not
    // routed through applyWindowGeometry: its dropCenteringTarget would erase
    // the stamp just written.
    if (m_effect->m_windowAnimator->hasAnimation(w)) {
        const QRectF visualPos = m_effect->m_windowAnimator->currentValue(w, actual);
        const auto retarget = m_effect->m_windowAnimator->retargetWithResult(
            w, centered, PhosphorAnimation::RetargetPolicy::PreservePosition);
        auto* mt = m_effect->m_shaderManager.findTransition(w);
        if (retarget == PhosphorAnimation::RetargetResult::Accepted && mt && mt->cached
            && mt->cached->iFromRectLoc >= 0) {
            mt->fromGeometry = visualPos;
            mt->toGeometry = centered;
        }
    }
    // Bracketed like every other effect-issued commit: the synchronous frame
    // change this emits must not reach the VS-crossing detector as an
    // external move, and the pass's own re-entry returns at the gate above.
    const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
    m_effect->m_daemonGate.inGeometryApply = true;
    const auto applyGuard = qScopeGuard([this, prevInApply] {
        m_effect->m_daemonGate.inGeometryApply = prevInApply;
    });
    kw->moveResize(centered);
}

void TilingHandler::reportMinSizeIfChanged(const QString& windowId, const QSize& declared)
{
    // The ONE writer of windowMinSizeUpdated after announce, shared by the
    // batch's change poll and the centring pass. It always sends the whole
    // declared pair, because the daemon's store replaces the whole QSize and
    // re-tiles on any change: a centring pass that sent only the oversized
    // axis (0 in the other) flipped the stored minimum against the batch's
    // full pair on every round, and each flip re-tiled, re-applied and
    // re-centred the window, for as long as it stayed oversized.
    const auto lastIt = m_effect->m_lastReportedMinSize.constFind(windowId);
    if (lastIt != m_effect->m_lastReportedMinSize.constEnd() && *lastIt == declared) {
        return;
    }
    // Gate like every other call in this handler: with no daemon registered
    // the call only queues a D-Bus error, and the cache is left unwritten so
    // the first batch after bring-up reports it.
    if (!m_effect->m_daemonGate.serviceRegistered) {
        return;
    }
    m_effect->m_lastReportedMinSize.insert(windowId, declared);
    qCDebug(lcEffect) << "Reporting min size for" << windowId << ":" << declared;
    // Watched rather than fire-and-forget, purely for the rollback: this leg
    // is change-gated, so a lost call would leave the cache recording a size
    // the daemon never heard and the engine modelling the old minimum until
    // the hints move AGAIN or the window closes — the "full-width game over a
    // half-width model" failure the batch poll exists to fix. The announce
    // sites roll back for the same reason.
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::Tiling,
                                                   QStringLiteral("windowMinSizeUpdated"),
                                                   {windowId, declared.width(), declared.height()}),
        m_effect);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId, declared](QDBusPendingCallWatcher* pw) {
        pw->deleteLater();
        if (!pw->isError()) {
            return;
        }
        qCWarning(lcEffect) << "windowMinSizeUpdated failed for" << windowId << pw->error().message();
        // Only roll back OUR value: a newer report may have landed while this
        // call was in flight, and clearing that would cost a redundant
        // re-report.
        const auto cached = m_effect->m_lastReportedMinSize.constFind(windowId);
        if (cached != m_effect->m_lastReportedMinSize.constEnd() && *cached == declared) {
            m_effect->m_lastReportedMinSize.remove(windowId);
        }
    });
}

} // namespace PlasmaZones
