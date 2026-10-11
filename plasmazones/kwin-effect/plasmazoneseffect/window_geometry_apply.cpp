// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Window geometry application for PlasmaZonesEffect: applyWindowGeometry, the
// one chokepoint every placement path funnels its rect through (snap, tile,
// strip, restore), with the X11 size-hint prediction it applies first and the
// repaint it leaves behind. Split out of drag_snap.cpp, which keeps the drag
// policy transitions and the async snap call.

#include "plasmazoneseffect.h"
#include "placementstatement.h"

#include "tilinghandler/tilinghandler.h"
#include "compositor/windowanimator.h"
#include "shader_resolve.h"
#include "window_query.h"
#include "compositor/effectlogging.h"

#include <effect/effecthandler.h>
#include <window.h>

#include <QLoggingCategory>
#include <QPointer>
#include <QScopeGuard>
#include <QtMath>

#include <memory>
#include <optional>

namespace PlasmaZones {

namespace {
// The rect the window holds once every configure sent so far is acked: KWin's
// moveResizeGeometry, which is the committed frame at rest and the pending
// request while a resize is unacked. Comparing against the committed frame
// instead treated a bounce back to the committed size, issued while a
// different size was still in flight, as already done (or as a pure move),
// and the stale pending size then landed.
QRect commandedRectOf(KWin::EffectWindow* window)
{
    if (KWin::Window* kw = window->window()) {
        return QRectF(kw->moveResizeGeometry()).toRect();
    }
    return window->frameGeometry().toRect();
}

// moveResize @p target, arming the stale-ack answer (WindowCommandStamps::
// StaleAck) when KWin will apply it without a configure: it asks for the size
// the client already has while a different size is still in flight
// (XdgSurfaceWindow::moveResizeInternal sends no configure for an equal client
// size), so the in-flight configure's ack would otherwise land the stale size.
void moveResizeAnsweringStaleAck(KWin::EffectWindow* window, KWin::Window* kw, const QRectF& target, quint64 stamp,
                                 WindowCommandStamps& stamps)
{
    const QSize committed = window->frameGeometry().toRect().size();
    const bool supersedesUnacked = commandedRectOf(window).size() != committed && target.toRect().size() == committed;
    kw->moveResize(target);
    if (supersedesUnacked) {
        stamps.staleAcks.insert(window, {target.toRect(), stamp});
    } else {
        stamps.staleAcks.remove(window);
    }
}
} // namespace

void PlasmaZonesEffect::repaintSnapRegions(KWin::EffectWindow* window, const QRectF& oldFrame, const QRect& newGeo)
{
    // Null-guarded although every call site passes a checked pointer: the test costs nothing.
    if (!window) {
        return;
    }
    window->addRepaintFull();
    // The KWin::effects test is belt and braces: the global outlives every
    // effect (see PlasmaZonesEffect::windowOutput). The window-local repaint
    // above needs only the live EffectWindow the caller checked.
    if (KWin::effects) {
        if (oldFrame.isValid()) {
            KWin::effects->addRepaint(KWin::Rect(oldFrame.toAlignedRect()));
        }
        KWin::effects->addRepaint(KWin::Rect(newGeo));
    }
}

QRect PlasmaZonesEffect::constrainTileGeometry(KWin::EffectWindow* window, const QRect& geometry) const
{
    // For X11/XWayland windows, KWin constrains the frame size to align with
    // WM_SIZE_HINTS (size increments for terminals like Ghostty, Kitty, etc.;
    // fixed-size hints for game launchers). Pre-compute the constrained size
    // and center the window in its zone so the gap is distributed evenly
    // instead of all at the bottom-right.
    // The X11 prediction applies to every placement that reaches
    // applyWindowGeometry (zone snap, tile, resnap, restore). A Wayland client
    // negotiates its size asynchronously (constrainFrameSize only checks
    // min/max, not a char-cell grid): a TILE whose client answers another size
    // is centred afterwards by the tiling handler's reactive pass
    // (TilingHandler::slotWindowFrameGeometryChanged); a snap zone has no such
    // pass, so such a client keeps its own size at the zone's top-left.
    //
    // Split out of applyWindowGeometry so the scrolling batch path can predict
    // the rect a tile request will REQUEST of KWin: its animation origins and
    // the degenerate-leg comparisons must be built against that, not the raw
    // column rect — the mismatch drew a fixed-size X11 game at its column's
    // top-left (the top of the screen) for the length of every park and
    // arrival.
    //
    // "Predict" is the honest word, not "commit". Two known divergences, both
    // harmless for the callers as written: a Wayland client passes through
    // here untouched and negotiates its own size asynchronously (a min size
    // larger than the column lands elsewhere), and on a scaled XWayland output
    // KWin round-trips the request through device pixels, so the committed
    // frame is this rect rounded. The scroll consumers are all intersects()
    // tests or animation endpoints, so neither divergence bites there — do not
    // add an equality comparand among THOSE without revisiting this. The one
    // consumer that COMMITS the result is the windowed-fullscreen ack
    // re-commit in slotWindowFullScreenChanged, a raw moveResize that routes
    // through here so it and applyWindowGeometry read the column rect alike.
    //
    // Five comparisons against this rect are sanctioned, all in
    // applyWindowGeometry. The first four compare against commandedRectOf()
    // (KWin's moveResizeGeometry, `.toRect()`) so they share one rounding rule
    // and see a still-unacked resize as the window's size:
    //
    //   1. the already-at-target no-op skip, on the whole rect;
    //   2. the size-preserving move()/moveResize() split on the non-animated
    //      arm, on the size alone;
    //   3. the same split on the animated arm;
    //   4. moveResizeAnsweringStaleAck's test that the request asks for the
    //      committed size while another is in flight.
    //
    // The fifth, the split-target bail on the animated arm, compares the
    // committed frame (frameGeometry().toRect()) on purpose: it asks whether
    // the park already landed.
    //
    // All five are safe for the same reason: a MISS costs only the more
    // conservative route — a redundant moveResize (KWin's own moveResize is
    // internally a no-op at matching geometry) for the first, and the ordinary
    // configure path for the other two. None can produce a wrong rect. The
    // divergences documented above can defeat these comparisons; they cannot
    // make one fire on the wrong window position, which is what makes the
    // failure direction acceptable.
    //
    // A sixth comparand needs the same argument made explicitly, and it must
    // compare against toRect() rather than qRound()-ing the extent separately —
    // QRectF::toRect derives the integer size from the rect's POSITION too, so
    // the two disagree by a pixel exactly where fractional-scale residue lives.
    //
    // Idempotent — and the rounding direction below is what makes it so.
    // KWin's own constrainFrameSize is already a fixed point (clamp to
    // [min,max], then floor to base + n*increment); it is not the source of
    // any non-idempotence. The rounding to integers HERE was. On a
    // fractional-scale XWayland output KWin runs that arithmetic in
    // fractional logical units (it divides the increment and the base by the
    // xwayland scale), so the returned size can sit exactly ON a grid point
    // with a fractional part. qRound moved it BELOW that point, and the next
    // pass floored into the previous bucket — a whole increment lost, plus a
    // second centring shift, every time the rect was re-constrained. The
    // deferred user-move replay is exactly such a second caller: it re-enters
    // applyWindowGeometry with a rect this function already produced.
    //
    // qCeil is a fixed point in every case (increments below 1, a zero base,
    // an already-integral size), which is why it must not be changed back to
    // qRound. It can overshoot a fractional max size by up to a pixel, which
    // KWin's programmatic moveResize does commit without re-enforcing the
    // size hints — bounded, non-compounding, and the right direction: the
    // undershoot qRound produced clipped client content instead.
    if (!window || !window->isX11Client()) {
        return geometry;
    }
    KWin::Window* kw = window->window();
    if (!kw) {
        // Fails open to the unconstrained rect, which for the scroll path is
        // the pre-fix prediction (the column rect) — worth a trace, because a
        // null KWin::Window behind a live X11 EffectWindow is anomalous rather
        // than routine, and the symptom it produces is the very jump this
        // function exists to prevent.
        qCDebug(lcEffect) << "constrainTileGeometry: no KWin window behind X11 client, predicting the raw rect"
                          << geometry;
        return geometry;
    }
    QRect geo = geometry;
    const QSizeF constrained = kw->constrainFrameSize(QSizeF(geo.size()));
    const int cw = qCeil(constrained.width());
    const int ch = qCeil(constrained.height());
    if (cw != geo.width() || ch != geo.height()) {
        // BOTH directions, not shrink-only: a min size larger than
        // the zone previously skipped this branch entirely, so KWin
        // committed the constrained-larger frame unanchored while
        // every downstream comparand held the requested rect. Apply
        // the constrained size here so the pre-computed geo matches
        // what KWin will commit. Clamp the centring shift to
        // non-negative: when min-size exceeds the zone in a
        // dimension the window stays anchored at the zone's origin
        // rather than shifting past its edge.
        const int dx = qMax(0, geo.width() - cw) / 2;
        const int dy = qMax(0, geo.height() - ch) / 2;
        geo = QRect(geo.x() + dx, geo.y() + dy, cw, ch);
        qCDebug(lcEffect) << "Pre-centered X11 window with size constraints:"
                          << "zone=" << geometry.size() << "constrained=" << constrained << "adjusted=" << geo;
    }
    return geo;
}

quint64 PlasmaZonesEffect::beginGeometryCommand(KWin::EffectWindow* window)
{
    const quint64 commandStamp = m_daemonGate.commandStamps.bump(window);
    if (auto prior = m_deferredGeometryReplay.find(window); prior != m_deferredGeometryReplay.end()) {
        disconnect(*prior);
        m_deferredGeometryReplay.erase(prior);
    }
    return commandStamp;
}

bool PlasmaZonesEffect::fullscreenBailsApply(KWin::EffectWindow* window) const
{
    if (!window) {
        return false;
    }
    KWin::Window* kw = window->window();
    const bool requested = kw && kw->isRequestedFullScreen();
    if (!(kw ? requested : window->isFullScreen())) {
        return false;
    }
    // isEmpty() fast path for sessions that never use the feature, and the
    // isDeleted() term: getWindowId on a corpse would re-insert the
    // reverse-map entry buildWindowMap deliberately skips.
    const bool windowedFsMember = !m_windowedFullscreenWindows.isEmpty() && !window->isDeleted()
        && m_windowedFullscreenWindows.contains(getWindowId(window));
    return PlacementStatement::fullscreenBails(kw != nullptr, window->isFullScreen(), requested, windowedFsMember);
}

void PlasmaZonesEffect::applyWindowGeometry(KWin::EffectWindow* window, const QRect& geometry, bool allowDuringDrag,
                                            bool skipAnimation, const QString& profilePath,
                                            const QRectF& originOverride, const QRectF& visualTargetOverride,
                                            std::optional<PlacementStatement::Purpose> statementOnDeferredReplay)
{
    if (!window) {
        qCWarning(lcEffect) << "applyGeometry: window is null";
        return;
    }

    // Normalize so width/height are non-negative; reject invalid rects
    QRect geo = geometry.normalized();
    if (!geo.isValid() || geo.width() <= 0 || geo.height() <= 0) {
        qCWarning(lcEffect) << "applyGeometry: invalid or empty geometry:" << geometry;
        // Release the open-restore suppression on the way out, like the
        // fullscreen bail and the already-at-target skip below. Nothing is
        // going to reposition this window, so a suppressed one would be
        // withheld from compositing until the hard deadline for nothing.
        endRestoreSuppression(window);
        return;
    }

    // This is now the window's latest geometry command, whatever becomes of it
    // below: the fullscreen bail, the no-op skip and an immediate mid-drag
    // commit are commands too. Every deferred apply scheduled for the window
    // before it is stale (WindowCommandStamps), and a mid-drag replay still
    // pending goes now, so the older rect can never land over this one at the
    // gesture's end. A replay re-entering here has already dropped its own
    // handle, and the defer below registers a fresh one under this stamp.
    const quint64 commandStamp = beginGeometryCommand(window);
    // A repeat of the same rect (engines often emit a batch twice) still owes
    // the answer to a superseded configure's ack, so the answer follows it.
    if (auto stale = m_daemonGate.commandStamps.staleAcks.find(window);
        stale != m_daemonGate.commandStamps.staleAcks.end() && stale->target == geo) {
        stale->stamp = commandStamp;
    }

    // Don't call moveResize() on fullscreen windows, it can crash KWin.
    // See KDE bugs #429752, #301529, #489546 (X11-era; moveResize on a
    // fullscreen window is spike-verified safe on KWin >= 6.7).
    if (fullscreenBailsApply(window)) {
        qCDebug(lcEffect) << "applyGeometry: window is fullscreen, skipping";
        // Release the hold-suppression on this bail like the no-op skip
        // below does: no reposition is coming at all, so a suppressed
        // window would be withheld from compositing until the hard
        // 250 ms deadline for nothing.
        endRestoreSuppression(window);
        return;
    }

    // This apply is now the window's latest command, so a tile the reactive
    // centring pass is still waiting to centre it in no longer applies. Left
    // standing, the entry fired on the client's ack of THIS command: a snap
    // zone apply that landed before the tiling release (a cross-mode handoff,
    // a return from tiling to snapping) was re-centred into the dead tile,
    // on the output the window had just left or at the old tile's centre
    // (discussion #1124). The tile batch records its target after its own
    // apply, so a tile applied now keeps its entry, and one deferred to the
    // gesture's end records none. Before the no-op skip below: a window
    // already at the new rect is still no longer that tile's.
    if (!window->isDeleted()) {
        m_tilingHandler->dropCenteringTarget(getWindowId(window));
    }

    // For X11/XWayland windows, pre-compute the size KWin will actually commit
    // and center it in the zone — see constrainTileGeometry.
    geo = constrainTileGeometry(window, geo);

    // A window held invisible until repositioned on open (RestoreSuppression)
    // gets the resolved rect stamped as its settle target, which the frame
    // hook needs before it treats a geometry change as the reposition. Not
    // when deferred to a user move's end (the replay stamps then).
    const bool deferredToMoveEnd = !allowDuringDrag && (window->isUserMove() || window->isUserResize());
    if (auto supIt = m_restoreSuppress.find(window); supIt != m_restoreSuppress.end() && !deferredToMoveEnd) {
        supIt->targetGeometry = geo;
    }

    // Skip no-op: if window is already at the target geometry AND there is
    // no in-flight animation, calling moveResize() is redundant and can have
    // subtle stacking side effects on some KWin versions (e.g. during daemon
    // restart double-processing).
    //
    // When an animation IS in flight, frameGeometry() already reflects the
    // committed target from the previous applyWindowGeometry's moveResize —
    // but the visual position is still mid-transition. A rapid reversal
    // (float → unfloat, rotate → rotate back) legitimately targets the same
    // committed geometry and must NOT be skipped, because the animation needs
    // to play from the current visual position to that target.
    // Compared against the COMMANDED rect (commandedRectOf): a target equal to
    // the committed frame while a different size is still unacked is not a
    // no-op, it is the command that cancels the pending one. Integer-aligned:
    // the geometry carries qreal precision and fractional-scale residue, so a
    // float-bit-exact equality against an integer `geo` would silently miss.
    if (geo == commandedRectOf(window) && !m_windowAnimator->hasAnimation(window)) {
        qCDebug(lcEffect) << "moveResize: window already at target geometry, skipping:" << geo;
        // Release first-frame open suppression here. The settle-detection
        // hook on windowFrameGeometryChanged would otherwise wait forever
        // for a configure that never fires (the resolved zone equals the
        // spawn position — happens on KWin session restore where the
        // saved geometry already matches a snap zone). Hold-suppression
        // exists only to mask the placement→reposition flash; with no
        // reposition coming, the window must paint immediately.
        endRestoreSuppression(window);
        return;
    }

    // INFO level: a standing record of every resolved window placement.
    // Generally useful operationally, and the resolved pixel rect is the one
    // number a support report needs to diagnose zone-geometry bugs (the zone
    // id is logged elsewhere; the resolved rect previously was not). Mirrors
    // the autotile path, which already logs "Autotile tile request: QRect=".
    qCInfo(lcEffect) << "Setting window geometry from" << window->frameGeometry() << "to" << geo;

    // Capture old frame before moveResize for repaint region.
    const QRectF trueOldFrame = window->frameGeometry();
    // The animation's departure rect. Identical to the true frame except for a
    // scrolling strip tile, whose parked position is chosen for safety and so
    // says nothing about which edge it should appear to come from — see the
    // originOverride contract on the declaration. Only the ANIMATION uses
    // this; the repaint region below must keep the true frame, or the pixels
    // the window actually vacated never get repainted.
    const QRectF oldFrame = originOverride.isValid() ? originOverride : trueOldFrame;

    // allowDuringDrag applies even under a user move or resize; only the drag-time size restores pass it. Zone and
    // tile placements pass false and defer to the end of the gesture (the replay below).
    if (deferredToMoveEnd) {
        qCDebug(lcEffect) << "Window in user move/resize, deferring geometry via windowFinishUserMovedResized";
        QPointer<KWin::EffectWindow> safeWindow = window;
        // Snapshot the supersession context at defer time: the fire can land
        // arbitrarily later (the user keeps dragging). Replaying this rect after
        // any newer command for the window would clobber it, and so would
        // replaying it after the gesture carried the window to another screen
        // (a keyboard move, which has no drag-end dispatch to bump the stamp).
        // The previous replay, if any, was retired at the top of this function.
        const QString deferScreen = getWindowScreenId(window);
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(window, &KWin::EffectWindow::windowFinishUserMovedResized, this,
                        [this, safeWindow, geo, skipAnimation, profilePath, conn, deferScreen, commandStamp,
                         originOverride, visualTargetOverride, statementOnDeferredReplay](KWin::EffectWindow*) {
                            disconnect(*conn);
                            // Drop the spent handle on every exit so the map holds only pending replays.
                            if (safeWindow) {
                                m_deferredGeometryReplay.remove(safeWindow.data());
                            }
                            if (!safeWindow || safeWindow->isDeleted()) {
                                return;
                            }
                            // Same predicate as the top-of-function fullscreen
                            // bail, exemptions included, and the same release:
                            // this replay is the reposition, and it is not
                            // happening.
                            if (fullscreenBailsApply(safeWindow.data())) {
                                endRestoreSuppression(safeWindow.data());
                                return;
                            }
                            const QString nowScreen = getWindowScreenId(safeWindow.data());
                            if (nowScreen != deferScreen
                                || !m_daemonGate.commandStamps.isCurrent(safeWindow.data(), commandStamp)) {
                                qCDebug(lcEffect) << "Deferred geometry superseded (screen or newer command), dropping:"
                                                  << getWindowId(safeWindow.data());
                                endRestoreSuppression(safeWindow.data());
                                return;
                            }
                            // Pay the placement statement the caller's mid-gesture
                            // preparePlacement skipped (see the header doc): the
                            // gesture is over now, and the hand-back must run before
                            // the moveResize below for the same reason it runs before
                            // the immediate apply.
                            QRect replayRect = geo;
                            if (statementOnDeferredReplay) {
                                const PlacementStatement::Verdict verdict = m_tilingHandler->preparePlacement(
                                    safeWindow.data(), geo, *statementOnDeferredReplay);
                                if (!verdict.apply) {
                                    return; // the state was kept: nothing moves
                                }
                                replayRect = verdict.applyRect;
                            }
                            // Re-assert the self-caused-frame-change guard the
                            // original (batch) apply held. Without it the
                            // synchronous frame change from this moveResize
                            // reads as an external move and can report a
                            // phantom cross-VS unsnap.
                            const auto applyGuard = geometryApplyScope();
                            // Forward BOTH scroll overrides: dropping them replayed a
                            // leaving column as a direct animate-to-park, sweeping it
                            // backwards across the screen — the exact artifact the
                            // override split exists to prevent. They are frame-relative
                            // snapshots from defer time, valid because the stamp guard above
                            // dropped the replay if any newer command for this window landed
                            // since (another window moving does not invalidate them).
                            applyWindowGeometry(safeWindow, replayRect, false, skipAnimation, profilePath,
                                                originOverride, visualTargetOverride, statementOnDeferredReplay);
                            // The bracket hid this move's own output change from the
                            // crossing arm, which also skips the stamp; a stamp written
                            // during the gesture would otherwise name the screen the
                            // window just left (F74).
                            if (safeWindow && !safeWindow->isDeleted()) {
                                m_trackedScreenPerWindow[safeWindow.data()] = pendingWindowScreenId(safeWindow.data());
                            }
                        });
        m_deferredGeometryReplay.insert(window, *conn);
        return;
    }

    // Animation: moveResize to the final geometry immediately, then morph
    // the window visually from its old position/size to the new one using
    // translate + scale in paintWindow(). This follows the standard KDE
    // effect pattern — effects are visual overlays, never per-frame moveResize.
    //
    // shouldAnimateWindow adds the user's Window Filtering gate. A matching
    // rule with a live effect action overrides the min-size and app / class
    // filters; the transient and notification / OSD filters yield only to a
    // rule whose match targets the window type. Falling through to
    // the non-animated path just runs the moveResize without the snap
    // motion / shader.
    //
    // BUT never let the geometry morph supersede an in-flight
    // window.open animation. A window that is snapped / placed AS IT OPENS
    // (snap-restore, autotile, daemon placement) should show its OPEN animation
    // at the snapped position, not a snap morph — otherwise the geometry morph
    // (the snap default) installs over the just-started open transition
    // and the open animation never plays. The open transition holds the
    // WindowAddedGrabRole (addedGrabHeld), so detect it and fall through to the
    // instant-moveResize path below: the window jumps to its snapped geometry and
    // the open animation plays over it. A snap that is NOT on a freshly-opened
    // window (drag-snap, retile, focus move) has no such transition and morphs
    // normally.
    const ShaderTransition* const inFlight = m_shaderManager.findTransition(window);
    const bool openAnimationInFlight = inFlight && inFlight->addedGrabHeld;
    // Caller-owned memoisation slot: when the gate builds the WindowQuery for
    // its rule probes, the resolver pass below reuses it instead of walking
    // the ~30 accessors a second time per animated apply.
    std::optional<PhosphorRules::WindowQuery> sharedQuery;
    const bool mayAnimate =
        !skipAnimation && !allowDuringDrag && !openAnimationInFlight && m_windowAnimator->isEnabled();
    // A leg that carries the window onto another screen is matched against the
    // screen it lands on, as the crossing's own invalidation would after it
    // (F653): the animation verdicts cached on the source screen go, and the
    // query is stamped with the destination before the gate and both resolvers
    // read it. A strip tile is left alone: its screen is the engine's, and a
    // parked column's rect lies outside it by design.
    if (mayAnimate && !m_shaderManager.animationRuleSet().isEmpty() && !window->isDeleted()) {
        const QString id = getWindowId(window);
        const QPoint centre = geo.center();
        const QString legScreen = resolveEffectiveScreenId(centre, KWin::effects->screenAt(centre));
        const bool stripTile =
            m_tilingHandler->hasScrollingScreens() && !m_tilingHandler->scrollTrackedScreenFor(id).isEmpty();
        if (!legScreen.isEmpty() && !stripTile && legScreen != getWindowScreenId(window, id)) {
            m_shaderManager.animationRuleEvaluator().evictCached(id);
            sharedQuery = ruleQuery(window, legScreen);
        }
    }
    if (mayAnimate && shouldAnimateWindow(window, &sharedQuery)) {
        const QRectF targetFrame(geo);
        // Where the window is COMMITTED (targetFrame) versus where the motion
        // is seen to END (animTarget). Identical unless the caller split them
        // — see visualTargetOverride on the declaration. Everything the
        // animator and the shader morph touch below uses animTarget; only the
        // moveResize uses targetFrame.
        const QRectF animTarget = visualTargetOverride.isValid() ? visualTargetOverride : targetFrame;

        // Bail before any work when the in-flight animation already
        // targets this frame — saves both the moveResize signal
        // emission AND the rule resolve on rapid retargets to the same
        // zone. Pre-Pass-2 the moveResize ran first and was redundant
        // here (kwin's moveResize is internally a no-op when geometry
        // already matches, but still pays signal-dispatch cost on the
        // hot path of rapid drag retargets).
        //
        // When the caller SPLIT the visual target from the committed frame
        // (leaving scroll columns), the bail must ALSO require the committed
        // frame to already match: two successive leaving-column batches can
        // share an animTarget (derived from the screen edge and the window's
        // size) while carrying DIFFERENT park rects — bailing on animTarget
        // alone would skip the moveResize and strand the committed geometry
        // at the previous park. Without a split, animTarget IS geo and the
        // extra term is deliberately not evaluated: frameGeometry() can lag
        // a size-changing moveResize until the client acks, and a defeated
        // bail would fall through to a retarget that re-anchors the running
        // animation on every rapid identical retarget.
        if (m_windowAnimator->hasAnimation(window) && m_windowAnimator->isAnimatingToTarget(window, animTarget)
            && (!visualTargetOverride.isValid() || window->frameGeometry().toRect() == geo)) {
            // Release the open-restore suppression on the way out, like the
            // other early returns that commit nothing here (the invalid-rect,
            // fullscreen, no-op and null-Window bails); the deferral keeps it for
            // its replay and the committed exits leave it to the settle hook.
            // Reaching here means an
            // EARLIER apply already committed this geometry and started the
            // animation, so the reposition the suppression was waiting to mask
            // has happened — the settle hook has nothing further to wait for,
            // and holding on would withhold the window from compositing until
            // the hard deadline for a configure that will never come.
            endRestoreSuppression(window);
            return; // Already animating to this target (with the frame committed, when split)
        }

        // Apply final geometry immediately — client starts re-rendering at new size.
        // Do this before touching the animator so the controller's
        // downstream bounds / padding queries see the updated
        // expandedGeometry for this frame.
        KWin::Window* kw = window->window();
        if (!kw) {
            // Same bail as the non-animated sibling at the tail of this
            // function, and for the same reason: with no KWin::Window there is
            // no way to commit the geometry, and animating anyway would morph
            // the window toward a target it will never actually reach — it
            // would snap back the moment the animation expired. window() is
            // never null in modern KWin, so this is defensive; log it like the
            // sibling does rather than proceeding silently.
            qCWarning(lcEffect) << "Cannot get underlying Window from EffectWindow";
            endRestoreSuppression(window);
            return;
        }
        // Same size-preserving split as the non-animated arm at the tail of
        // this function, and for the same reason: a pure move() takes KWin's
        // Move branch, which clears the pending-position flag on queued
        // configures, so a client that acks a superseded configure cannot be
        // re-anchored to where that older request would have put it. A strip
        // step-scroll is animated by default, so this is the arm the drifting
        // Picture-in-Picture case actually takes — fixing only the sibling left
        // the common path on the configure route.
        //
        // Against the commanded rect and integer-aligned, like the sibling: the
        // geometry carries qreal precision and a fractional-scale output leaves
        // sub-pixel residue, so an exact QSizeF equality would miss.
        {
            // This commit precedes this arm's own retarget of the leg below;
            // the frame-change hook's strip retarget must leave it alone.
            const KWin::EffectWindow* prevCommit = m_daemonGate.animatedApplyCommit;
            m_daemonGate.animatedApplyCommit = window;
            const auto commitGuard = qScopeGuard([this, prevCommit] {
                m_daemonGate.animatedApplyCommit = prevCommit;
            });
            if (targetFrame.toRect().size() == commandedRectOf(window).size()) {
                kw->move(targetFrame.topLeft());
            } else {
                moveResizeAnsweringStaleAck(window, kw, targetFrame, commandStamp, m_daemonGate.commandStamps);
            }
        }

        // Per-window animation motion-cascade: rule → per-event motion node
        // (incl. the `window.movement` "All") → global animator profile. A
        // Timing Rule for this (windowClass, eventPath) wins; below it, the
        // motion ProfileTree's per-event / "All" duration override applies;
        // the global animator profile is the floor. Retarget intentionally does
        // not re-apply the cascade — once an animation is in flight, it
        // stays on the curve that started it for visual continuity.
        //
        // Reuse the gate's query when it built one (rules present); build
        // only when a consumer below actually asks — matches the shape
        // `shouldAnimateWindow` uses for its own rule-override gate (the lazy
        // accessor in window_filtering.cpp), so a rule that gates the
        // animation also resolves its curve / timing / shader slots without
        // either side paying a second `ruleQuery` walk (~30 KWin accessors
        // plus the QString copies).
        //
        // Memoises straight back into the caller-owned slot, so the build
        // survives for whichever consumer asks next. Returning a const& also
        // drops the by-value WindowQuery copy the old form made on EVERY
        // animated apply. The build itself is skipped only on the legs that
        // never reach a consumer — the motion cascade is gated on a non-empty
        // tree / rule set, and the shader resolve sits behind the animator
        // actually having taken the leg (and behind this block's own early
        // returns).
        const auto query = [this, window, &sharedQuery]() -> const PhosphorRules::WindowQuery& {
            if (!sharedQuery) {
                sharedQuery = ruleQuery(window);
            }
            return *sharedQuery;
        };
        const QString windowId = getWindowId(window);
        const auto& baseProfile = m_windowAnimator->profile();
        // Resolve the fully-cascaded motion profile for this event (curve +
        // duration): global animator profile → category "All" → per-node
        // motion-tree override → per-window Rule. Shared SSOT with the
        // time-driven shader path (tryBeginShaderForEvent), so an autotile
        // rotate / mode-change / snap reposition animates on the SAME per-event
        // curve + duration the user configured — including a `window.movement`
        // "All" override. The WindowAnimator consumes the whole profile, so the
        // per-event curve rides along; without this the morph always used the
        // global animator profile.
        //
        // Gated on a non-empty tree OR rule set so the default-state user keeps
        // the historical fast-path — no resolve, no deep `Profile::operator!=`
        // (which walks `curve->equals` virtual + 5 std::optional comparisons).
        // Compared against the animator's own `baseProfile` so the override is
        // passed whenever the effective profile differs from what the animator
        // would use unaided.
        const bool hasMotionOverrides = m_shaderManager.motionProfileTree().hasAnyOverride();
        const bool hasAnimationRules = !m_shaderManager.animationRuleSet().isEmpty();
        const PhosphorAnimation::Profile* motionOverridePtr = nullptr;
        PhosphorAnimation::Profile motionProfile;
        if (hasMotionOverrides || hasAnimationRules) {
            motionProfile = resolveEventMotionProfile(profilePath, query(), windowId);
            if (motionProfile != baseProfile)
                motionOverridePtr = &motionProfile;
        }

        // Where the animator's replacement animation departs FROM. The
        // shader geometry-morph below must anchor its iFromRect at the
        // same point (see the re-anchor branch there): the animator's
        // retarget resets progress to 0 and re-anchors at this rect, so a
        // morph that keeps its old fromGeometry would draw
        // lerp(originalFrom, newTo, 0) on the next frame — a visible jump
        // back to the ORIGINAL departure rect on every rapid successive
        // move (discussion #795).
        QRectF morphAnchor(oldFrame);
        if (m_windowAnimator->hasAnimation(window)) {
            // Capture the displaced animation's endpoints before retarget
            // modifies or deletes the entry. On a rapid reversal where
            // advance() hasn't ticked, m_current still equals m_from
            // (the animation's start point), so retarget(newTarget) sees
            // current ≈ newTarget when the reversal goes back to the
            // original zone — degenerate. Use the displaced animation's
            // TARGET as the visual origin for the replacement: that's
            // where the window was visually heading (and where moveResize
            // just committed to), so animating from there to the new
            // target matches the user's expectation.
            const QRectF displacedTarget = m_windowAnimator->animationFor(window)->to();
            const QRectF visualPos = m_windowAnimator->currentValue(window, QRectF(oldFrame));
            const auto result = m_windowAnimator->retargetWithResult(
                window, animTarget, PhosphorAnimation::RetargetPolicy::PreserveVelocity);
            morphAnchor = visualPos;
            if (result == PhosphorAnimation::RetargetResult::DegenerateReap) {
                // Retarget collapsed (current visual ≈ new target). The reap
                // already dropped the displaced animation; start a fresh one
                // from the displaced target (where the window was heading) to
                // the new target. If that's also degenerate (same point),
                // startAnimation returns false and no animation plays — correct,
                // since there's no visual distance to cover. The reap fires the
                // animator's completion, which ends an animator-driven morph
                // (durationMs 0) at once, so in the no-replay sub-case nothing is
                // left and the declined branch below also runs; with a replay the
                // morph block below installs a FRESH transition anchored at
                // morphAnchor. morphAnchor is
                // still set so that, when a replacement DOES play, its iFromRect
                // matches the animator's re-anchored departure point.
                const QRectF animFrom = (displacedTarget != animTarget) ? displacedTarget : visualPos;
                m_windowAnimator->startAnimation(window, animFrom, animTarget, motionOverridePtr);
                morphAnchor = animFrom;
            }
        } else {
            m_windowAnimator->startAnimation(window, QRectF(oldFrame), animTarget, motionOverridePtr);
        }

        if (m_windowAnimator->hasAnimation(window)) {
            // Same cascade as tryBeginShaderForEvent: rule layer wins
            // for matching windows; engaged-empty rule effectId blocks
            // the tree fallthrough. Goes through the same memoised `query`
            // accessor as the motion cascade above, so the WindowQuery is
            // built at most once for this apply however many consumers run.
            //
            // Route through `resolveAnimationShaderProfile` (which
            // uses `evaluator.resolveCached(windowId, query)`). When a rule
            // set is configured, the sister `resolveEventMotionProfile`
            // call above already warmed the per-window cache slot for this
            // query, so this cached read is a hit. (An empty rule set still
            // goes through resolveCached, but its walk over zero rules is
            // trivially cheap and the cache slot dedups it.) The earlier shape
            // called a standalone uncached shader-profile resolver here, which
            // paid an extra priority-order walk per snap on every
            // non-empty rule set — same regression the shim was
            // introduced to fix for `tryBeginShaderForEvent` (see the
            // historical-pair note in shader_resolve.cpp).
            //
            // The resolver returns no duration (it reads only the shader
            // slot): the snap leg installs with durationMs 0 and paintWindow
            // rides the WindowAnimator's timeline, whose duration already
            // carries a Timing rule through motionProfile above.
            const auto resolved = PlasmaZones::resolveAnimationShaderProfile(
                m_shaderManager.animationRuleEvaluator(), m_shaderManager.profileTree(),
                m_shaderManager.presetRegistry(), windowId, query(), profilePath);
            auto shaderProfile = resolved.profile;
            if (!resolved.shaderSlotFromRule && shaderProfile.effectiveEffectId().isEmpty()) {
                // No rule matched and no tree override resolved a shader for
                // this snap event — apply the built-in per-event default
                // (window-morph for snap / layoutSwitch) via the shared SSOT,
                // which respects an explicit tree "None". Keeps the default
                // consistent with what the settings UI shows
                // (resolvedShaderProfile uses the same helper) without
                // persisting it into config. Gated on `!shaderSlotFromRule`: a
                // per-app rule that set "None" (engaged-empty effectId)
                // is a deliberate opt-out and must NOT be overridden here.
                // Flattened: REPLACES the flattened profile above.
                shaderProfile = PhosphorAnimationShaders::withPresetsResolved(
                    PhosphorAnimationShaders::resolveShaderWithDefault(m_shaderManager.profileTree(), profilePath),
                    m_shaderManager.presetRegistry());
            }
            // Runtime applicability gate — same canonical-predicate check
            // as tryBeginShaderForEvent (resolvedShaderAppliesToEvent): the
            // rule layer or a stale config can deliver a pack that provably
            // cannot drive this snap leg (a move-physics or desktop pack).
            // Refusing here keeps the C++ WindowAnimator geometry animation
            // as the fallback instead of paying capture + paint cost for an
            // identity no-op transition.
            const QString snapShaderId = shaderProfile.effectiveEffectId();
            const bool snapShaderApplies =
                !snapShaderId.isEmpty() && resolvedShaderAppliesToEvent(snapShaderId, profilePath);
            // Tear down a live transition this snap leg is NOT going to replace.
            // Both no-install outcomes leave a stale-morph hazard, so both are
            // handled here (reachable whenever two successive legs ride different
            // event paths, place-in then place-out or a layout switch, because the
            // shader resolves per path and a per-event "None" is honoured; an open
            // leg never reaches here, it holds addedGrabHeld and the enclosing
            // block is skipped via openAnimationInFlight):
            //
            //  1. A REFUSED pack (non-empty id that provably cannot drive this
            //     leg): clear ANY live transition — a morph from an earlier leg of
            //     this drag, or a settling wobble / in-flight focus leg — for a
            //     clean slate.
            //  2. An EMPTY id (the event's path resolves to "None", by the tree, a rule or an edit):
            //     clear only a transition that OWNS GEOMETRY (declares iFromRect).
            //     Its from/to rects are frozen at the PREVIOUS leg's endpoints and
            //     nothing retargets them, so leaving it would keep painting toward
            //     the OLD target while the WindowAnimator (retargeted above) heads
            //     to the new one — the identical stale-morph failure case 1 exists
            //     to prevent. A NON-geometry transition is deliberately left alone
            //     here: a settling wobble rings out over the WindowAnimator
            //     translate, exactly as on a long drag that snaps mid-settle. The
            //     bundled move pack declares no iFromRect, so shaderOwnsGeometry
            //     stays false, the animator keeps the geometry, and the two
            //     compose. A hybrid move+geometry pack that DOES declare iFromRect
            //     would own geometry — and is therefore torn down, correctly.
            if (!snapShaderApplies) {
                const ShaderTransition* live = m_shaderManager.findTransition(window);
                const bool liveOwnsGeometry = live && live->cached && live->cached->iFromRectLoc >= 0;
                if (live && (!snapShaderId.isEmpty() || liveOwnsGeometry)) {
                    endShaderTransition(window);
                }
            }
            if (snapShaderApplies) {
                const bool installed = beginShaderTransition(window, shaderProfile);
                // Identity gate before mutating the live leg — the same rule
                // as the heldMove stamp and the maximize morph endpoints. The
                // applicability gate above filtered the empty/refused shapes.
                // beginShaderTransition also returns false when the live leg is
                // this same pack (the same-effect short-circuit of a rapid
                // retarget), and then findTransition returns the snap's OWN leg,
                // which the cache test below recognises. It returns false with an
                // UNRELATED leg live for a compile failure, the sticky null-shader
                // sentinel, a registry miss, a collapsed surface or a minimized
                // window (a maximize morph mid-flight is the reachable one). Retargeting that
                // leg's endpoints toward this snap would mutate a foreign
                // event's animation; but leaving a foreign GEOMETRY leg alive
                // is the frozen-stale-morph hazard the declined branch below
                // documents, because the window has already moved. So: owned
                // leg → retarget; foreign geometry-owning leg → tear down,
                // exactly as the animator-declined branch does.
                bool ownsSnapLeg = installed;
                auto* mt = m_shaderManager.findTransition(window);
                if (!ownsSnapLeg && mt) {
                    const auto cacheIt = m_shaderManager.m_shaderCache.find(snapShaderId);
                    ownsSnapLeg = cacheIt != m_shaderManager.m_shaderCache.end() && cacheIt->second.shader
                        && mt->cached == &cacheIt->second;
                }
                if (!ownsSnapLeg) {
                    if (mt && mt->cached && mt->cached->iFromRectLoc >= 0) {
                        endShaderTransition(window);
                    }
                    repaintSnapRegions(window, trueOldFrame, geo);
                    return;
                }
                // If the installed shader is a geometry morph (declares
                // iFromRect), hand it the old/new frames and request the
                // old-content snapshot. The morph then owns the visual
                // geometry animation — it interpolates the drawn rect from
                // oldFrame to targetFrame and cross-fades the old snapshot
                // into the live new content — so paintWindow gates off the
                // C++ WindowAnimator translate+scale for this window. The
                // WindowAnimator still runs (durationMs == 0) purely to drive
                // the morph's progress timeline.
                if (mt && mt->cached && mt->cached->iFromRectLoc >= 0) {
                    // Always retarget the morph to the new destination.
                    mt->toGeometry = animTarget;
                    // On a live RETARGET mid-morph (a reap installs fresh),
                    // beginShaderTransition short-circuits (same shader) and
                    // keeps the existing transition,
                    // so its captured snapshot already holds the ORIGINAL old
                    // content. Preserve the snapshot — re-capturing here would
                    // grab the mid-morph/new content and collapse the
                    // cross-fade — but RE-ANCHOR fromGeometry at the animator's
                    // new departure rect: the retarget above reset the animator
                    // progress to 0, so the morph's very next frame draws
                    // lerp(fromGeometry, toGeometry, 0). Keeping the stale
                    // origin made rapid successive moves visibly jump back to
                    // the original departure rect and replay (#795). Only a
                    // fresh morph (no snapshot yet) requests the capture.
                    // morphAnchor == oldFrame on a fresh start, so the
                    // unconditional assignment also covers the pre-fix
                    // fresh-morph anchoring — and fixes the sibling mismatch
                    // where a retarget landing before the first paint anchored
                    // at the already-committed previous target instead of the
                    // animator's departure rect.
                    mt->fromGeometry = morphAnchor;
                    // See ShaderTransition::fromIsSynthetic. morphAnchor is
                    // the originOverride whenever the caller supplied one
                    // (fresh start), and on the retarget path it is the
                    // displaced animation's visual position — which for a leg
                    // that STARTED from a synthetic origin is a point along a
                    // path the window never occupied either. Conservative
                    // choice: any origin override in play marks the from-rect
                    // synthetic and routes the snapshot to the raw capture.
                    // STICKY within the leg: a no-override retarget on a
                    // synthetic-origin leg re-anchors at a point on the same
                    // never-occupied path, so the marker must survive it —
                    // clearing it would hand a pre-first-paint capture the
                    // composite-seed path with a rect the window never held.
                    // (Once fromGeometry is rewritten to a REAL rect, the
                    // writer clears the flag — see beginMaximizeShaderMorph.)
                    mt->fromIsSynthetic = originOverride.isValid() || mt->fromIsSynthetic;
                    // Gate on the compiled shader actually LINKING uOldWindow,
                    // matching the three sibling request sites (the move-start
                    // hookup, beginMaximizeShaderMorph and the tab-swap install)
                    // and the bind in decoration_render.cpp / unbind in
                    // paint_shader_window.cpp, which already test this same predicate. The bundled window-morph is
                    // vertex-only and samples no old frame, so an ungated request paid a full-window drawWindow
                    // re-entry plus an RGBA8 allocation on every snap, tile and reflow to fill a texture nothing would
                    // ever read. A cross-fade pack keeps its snapshot by declaring the uniform.
                    if (mt->cached->iOldWindowLoc >= 0 && !mt->oldSnapshot) {
                        mt->needsSnapshot = true;
                    }
                }
            }
        } else {
            // The animator DECLINED this leg — SnapPolicy refused the spec, a
            // degenerate retarget's replay was declined, or the
            // move fell under Profile::minDistance with no size change (a
            // user-settable 0-200px threshold, so this is reachable in a default-ish
            // config, not just a corner case). The whole install block above is
            // therefore skipped, including its stale-morph teardown — but the
            // moveResize higher up has ALREADY committed the new geometry.
            //
            // A live transition that OWNS GEOMETRY (declares iFromRect) froze its
            // from/to rects at the previous leg's endpoints and nothing retargets
            // them, so it would go on painting toward the OLD target across a window
            // that already sits at the new one. Same hazard the in-branch teardown
            // exists to prevent, on the path where the animator never ran.
            if (const ShaderTransition* live = m_shaderManager.findTransition(window);
                live && live->cached && live->cached->iFromRectLoc >= 0) {
                endShaderTransition(window);
            }
        }

        repaintSnapRegions(window, trueOldFrame, geo);
        return;
    }

    // No animation path (disabled, during drag, etc.): apply moveResize directly.
    // The null check runs BEFORE the removeAnimation so the drop and the
    // geometry commit share a branch: removeAnimation's contract requires the
    // caller to commit geometry immediately after (it schedules no damage),
    // and dropping the animation on the null-window path would strand the
    // last animated frame. window() is never null in modern KWin, so the
    // else-branch is defensive.
    KWin::Window* kwinWindow = window->window();
    if (kwinWindow) {
        const bool droppedLeg = m_windowAnimator->hasAnimation(window);
        if (droppedLeg) {
            m_windowAnimator->removeAnimation(window);
        }
        // DEBUG: the resolved rect is already logged at INFO above ("Setting
        // window geometry from ... to ..."), which covers both the animated
        // and non-animated paths — keep this one at debug to avoid a
        // duplicate INFO line for the same apply.
        qCDebug(lcEffect) << "moveResize: QRect=" << geo << "-> QRectF=" << QRectF(geo);
        // A placement that changes no SIZE goes through move(), not
        // moveResize(), and the difference is not cosmetic.
        //
        // Both apply the new position immediately, but only move() takes
        // KWin's MoveResizeMode::Move branch, which additionally CLEARS the
        // pending-position flag on every configure event still queued for the
        // client (xdgshellwindow.cpp, moveResizeInternal). moveResize() leaves
        // those armed, and when the client later acks one, KWin re-anchors the
        // window through gravity.apply() against the bounds rect snapshotted
        // when THAT configure was sent — not against what we just asked for.
        //
        // Harmless for a client that acks promptly, because the stale bounds
        // and the fresh request agree. Not harmless for one that renegotiates
        // over several round-trips: a size-enforcing client (Firefox
        // Picture-in-Picture holds 16:9) routinely acks a configure we have
        // already superseded, and the window jumps back to where that older
        // request would have put it. On a scrolling strip, where a column's
        // position changes constantly, that reads as the window drifting
        // around the screen edge instead of being cropped at it.
        //
        // Size-equal is the whole gate: a real resize has to keep the
        // configure path, because the size genuinely needs the client's
        // agreement.
        // Against the commanded rect and integer-aligned, matching the no-op
        // skip above: the geometry carries qreal precision and a fractional-scale output leaves
        // sub-pixel residue, so an exact QSizeF equality would miss and send a
        // size-preserving placement down the configure path anyway.
        if (geo.size() == commandedRectOf(window).size()) {
            kwinWindow->move(QRectF(geo).topLeft());
        } else {
            moveResizeAnsweringStaleAck(window, kwinWindow, QRectF(geo), commandStamp, m_daemonGate.commandStamps);
        }
        // removeAnimation runs no completion, and an animator-driven shader
        // leg (durationMs == 0, the default window-morph on the placement
        // legs) has no other progress source or teardown: left installed it
        // reads as inactive and the expiry path presents one frame at the
        // stale mid-leg rect after this commit. End it, as the animated arm's
        // declined branch does. Time-driven legs keep their own timer.
        if (droppedLeg) {
            if (const auto* orphan = m_shaderManager.findTransition(window); orphan && orphan->durationMs == 0) {
                endShaderTransition(window);
            }
        }

        repaintSnapRegions(window, trueOldFrame, geo);
    } else {
        qCWarning(lcEffect) << "Cannot get underlying Window from EffectWindow";
        endRestoreSuppression(window);
    }
}

} // namespace PlasmaZones
