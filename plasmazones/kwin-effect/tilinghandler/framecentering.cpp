// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frame-commit handling for TilingHandler: slotWindowFrameGeometryChanged,
// which centres a tile whose client committed a smaller size than it was
// asked for and reports a minimum size it discovers on the way, and
// reportDiscoveredMinSize. Split out of tiling.cpp, which keeps the tile
// batch pipeline.

#include "tilinghandler.h"
#include "scrolldecisions.h"
#include "handlers/dragtracker.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "compositor/windowanimator.h"
#include "compositor/effectlogging.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorIdentity/VirtualScreenId.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>

#include <QDateTime>
#include <QLoggingCategory>
#include <QScopeGuard>

namespace PlasmaZones {

void TilingHandler::slotWindowFrameGeometryChanged(KWin::EffectWindow* w, const QRectF& oldGeometry)
{
    Q_UNUSED(oldGeometry)
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
                    m_effect->applyWindowGeometry(w, commanded, /*allowDuringDrag=*/false, /*skipAnimation=*/true);
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
        // Don't detect VS crossings for the dragged window — the drop handler
        // (callDragStopped / autotile drag end) owns state transitions.
        // Detecting mid-drag would transfer the window before the user drops it.
        // Other windows (e.g., a terminal reflowing) should still get VS crossing checks.
        const bool isDraggedWindow = m_effect->m_dragTracker && m_effect->m_dragTracker->isDragging()
            && windowId == m_effect->m_dragTracker->draggedWindowId();
        if (!isDraggedWindow) {
            const QString newScreenId = m_effect->getWindowScreenId(w);
            const QString oldScreenId = m_notifiedWindowScreens.value(windowId);
            if (PhosphorIdentity::VirtualScreenId::isVirtualScreenCrossing(oldScreenId, newScreenId)) {
                // Virtual screen changed on the same physical monitor — delegate to
                // the same handler used by outputChanged. The re-entrancy guard
                // inside handleWindowOutputChanged prevents infinite loops from
                // geometry changes caused by tiling.
                handleWindowOutputChanged(w);
                return;
            }
        }
        // Fall through to centering logic below for all windows (including dragged)
    }

    // Everything from here down is the reactive centring pass, and it is
    // WAYLAND-ONLY despite reading engine-general: m_tileTargetZones has
    // exactly one writer (the batch apply in this file), and that write sits
    // inside an `isWaylandClient()` arm. An X11 client never reaches this
    // block — constrainTileGeometry pre-centres its frame inside the zone
    // before the apply commits, and an external mover is dealt with by the
    // counter-assert above, not here.
    if (m_tileTargetZones.isEmpty()) {
        return;
    }

    // Never centre from inside the effect's own apply bracket. This is the
    // same gate the two other consumers in this slot take (the counter-assert
    // and the virtual-screen crossing check above), and its absence here was
    // a defect on its own.
    //
    // What it costs us without the gate: `applyWindowGeometry` commits at the
    // top of the batch loop but the target zone is not recorded until the end
    // of it, so a batch cannot trip its own apply — the map is still empty
    // when its moveResize lands. A SECOND batch carrying the same zone can,
    // and the autotile engine emits exactly that on a removal, which it
    // retiles immediately and uncoalesced (AutotileEngine::onWindowRemoved).
    // The first batch's entry is live while the second one applies, so the
    // frame change KWin emits mid-apply — position taken, size still awaiting
    // the client's ack — reaches the pass with the PRE-resize frame. The pass
    // reads that stale size as "the client refused to fill its zone" and
    // issues a competing moveResize at the old size, which supersedes the
    // enlarge the client was still answering. The window is then pinned a
    // zone-width short for the rest of its life, because the centring stamps
    // m_centeredWaylandZones and the redundant-apply skip in
    // slotWindowsTileRequested honours that entry on every later batch.
    //
    // Discussion #1028: a window count dropping 2 -> 1 hands the survivor the
    // ungapped full-screen zone, and losing that enlarge leaves it centred
    // with a dead band down each side that belongs to no tiled window — which
    // is where focus-follows-mouse then finds nothing to focus.
    if (m_effect->m_daemonGate.inGeometryApply) {
        return;
    }

    auto it = m_tileTargetZones.find(windowId);
    if (it == m_tileTargetZones.end()) {
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
        // origin, so an origin outside the zone means another command owns
        // the window now. The centred stamp goes too, or the redundant-apply
        // skip in slotWindowsTileRequested would honour it if the window is
        // tiled into the same zone again.
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

    // Already at the centered position — record and consume
    if (qAbs(actual.x() - centered.x()) < 1.0 && qAbs(actual.y() - centered.y()) < 1.0) {
        m_centeredWaylandZones[windowId] = targetZone;
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

    // Window refused to shrink below its actual size — report its declared
    // minimum to the daemon so future retiles can account for it. Only report
    // when the window is larger than the zone (negative delta = oversized).
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
    if (dw < -MinCenteringDelta || dh < -MinCenteringDelta) {
        const QSize declaredMin = declaredMinSize(w);
        int discoveredMinW = 0;
        int discoveredMinH = 0;
        if (dw < -MinCenteringDelta && declaredMin.width() > 0) {
            discoveredMinW = declaredMin.width();
        }
        if (dh < -MinCenteringDelta && declaredMin.height() > 0) {
            discoveredMinH = declaredMin.height();
        }
        if (discoveredMinW > 0 || discoveredMinH > 0) {
            reportDiscoveredMinSize(windowId, discoveredMinW, discoveredMinH);
        }
    }

    // Erase BEFORE moveResize to prevent re-entrancy: moveResize emits
    // windowFrameGeometryChanged synchronously, which would re-enter
    // this slot and find the entry still present → infinite recursion → crash.
    m_centeredWaylandZones[windowId] = targetZone;
    m_tileTargetZones.erase(it);
    m_effect->m_windowAnimator->removeAnimation(w);
    kw->moveResize(centered);
}

void TilingHandler::reportDiscoveredMinSize(const QString& windowId, int minWidth, int minHeight)
{
    if (minWidth <= 0 && minHeight <= 0) {
        return;
    }

    qCInfo(lcEffect) << "Discovered min size for" << windowId << ":" << minWidth << "x" << minHeight
                     << "- reporting to daemon for future retiles";

    // This is a SECOND writer of windowMinSizeUpdated carrying a per-axis
    // pair with 0 in the axis that did not shrink, and the daemon's store
    // replaces the whole QSize — so a (900, 0) discovery clears a stored
    // height minimum. Evict the last-reported cache rather than recording
    // the half-pair as sent: the next batch's change poll then re-asserts
    // the true declared pair instead of being silenced by its own cache.
    m_effect->m_lastReportedMinSize.remove(windowId);

    // Gate like every other fireAndForget in this handler: with no daemon
    // registered the call only queues a D-Bus error, and the eviction above
    // already ensures the discovery is re-reported after bring-up.
    if (!m_effect->m_daemonGate.serviceRegistered) {
        return;
    }

    PhosphorProtocol::ClientHelpers::fireAndForget(
        m_effect, PhosphorProtocol::Service::Interface::Tiling, QStringLiteral("windowMinSizeUpdated"),
        {windowId, minWidth, minHeight}, QStringLiteral("windowMinSizeUpdated"));
}

} // namespace PlasmaZones
