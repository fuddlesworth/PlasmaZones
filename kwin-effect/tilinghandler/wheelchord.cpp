// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Wheel-chord dispatch and the native-fullscreen release that precedes a user
// strip verb.
//
// Split out of state.cpp, which holds the per-session state the daemon
// publishes and this side consumes. Nothing here is such state: this is the
// effect's own input path (the two configurable wheel chords, their per-axis
// accumulators and the cursor-resolved target screen) plus the release both
// user-verb dispatch sites run before their verb goes out. The two only ever
// shared a file because the wheel settings that arm the chords are published
// state and the chord reading them is not.
//
// The release has a second caller that is no wheel event at all: the daemon's
// keyboard shortcut gate, which reaches it as slotLeaveNativeFullscreenRequested
// over Scrolling.leaveNativeFullscreenRequested. That slot lives here rather
// than beside the other D-Bus slots in state.cpp so it sits next to the
// function it defers to.

#include "tilinghandler.h"
#include "handlers/dragtracker.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "compositor/effectlogging.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effectwindow.h>
// KWin::Window is only forward-declared through the effect header; the release
// below needs isRequestedFullScreen and setFullScreen on the complete type.
#include <window.h>

#include <QLoggingCategory>
#include <QPointF>
#include <QScopeGuard>
#include <QStringList>

#include <cmath>
#include <utility> // std::as_const over the collected exit list

namespace PlasmaZones {

namespace {
/// Most strip steps one axis event may spend. Bounds both the work done on
/// KWin's main thread and the D-Bus fan-out, since each step is its own
/// message. A real wheel notch is 1.0 and even a coalesced high-resolution
/// frame stays in single digits, so this only ever truncates garbage.
constexpr int kMaxWheelStepsPerEvent = 16;

/// One discrete wheel notch, in deltaV120 units. libinput reports high
/// resolution wheels as fractions of this and KWin passes the value through
/// unchanged, so dividing by it yields notches directly.
constexpr qreal kV120PerNotch = 120.0;

/// One notch worth of the smooth `delta` field, used only when the event
/// carries no deltaV120 (touchpads and other continuous sources). The wheel
/// itself never lands here: KWin fills deltaV120 for every wheel event.
///
/// The value is libinput's legacy degrees-per-detent, which is also the scale
/// KWin's smooth delta uses for a wheel, so a continuous source has to travel
/// about as far as one notch to spend a step. Treating that field as notches
/// directly is what made a single notch fire a whole screenful of steps.
constexpr qreal kSmoothUnitsPerNotch = 15.0;
} // namespace

bool TilingHandler::handleWheelChord(qreal delta, qint32 deltaV120, Qt::Orientation orientation,
                                     Qt::KeyboardModifiers mods, Qt::MouseButtons buttons)
{
    // Fast path first, in the order that costs least: the enable setting,
    // then "does any screen run the strip at all". Every axis event in the
    // session reaches here, so a session with no scrolling screen pays two
    // reads and nothing more.
    // Any bail below drops the banked sub-notch remainder. A partial notch is
    // only meaningful inside the gesture that produced it, and the gesture is
    // over the moment we stop claiming events: the user releases the chord,
    // starts a drag, wheels onto a screen with no strip, or turns the feature
    // off. Carrying a residue across that boundary makes the NEXT gesture
    // fire its first step early, or late, depending on the sign.
    //
    // Two endings this cannot catch, both benign. The residue they carry is
    // normally under one notch, so at most one step fires early; the
    // exception is an event that hit the per-event step cap, which leaves
    // whatever the cap did not spend:
    //
    // A user who stops scrolling and only THEN releases the modifier sends no
    // further axis event, so nothing runs to clear the residue and it is
    // spent on the next gesture's first event. Dropping it on entry to every
    // non-claiming path rather than only the no-match one is what keeps that
    // window as small as it can be without a timer.
    //
    // Switching chord mid-scroll (holding Meta, then adding Shift) is a new
    // gesture on the same axis, and nothing here keys the accumulator on
    // WHICH chord claimed the event, so the focus chord's residue carries
    // into the view chord's first step. Tracking the claiming chord would
    // close it, at the cost of more state in the one place on this path that
    // has any.
    if (!m_wheelFocusEnabled || m_scrollingScreens.isEmpty()) {
        resetWheelAccumulators();
        return false;
    }
    // A zero delta carries no direction to act on. It reaches us as the
    // stop/cancel tick that ends a kinetic touchpad stream, and turning it
    // into a signed verb would scroll the strip one step on every stream end.
    // That tick IS the end of a stream, so it takes the residue with it.
    //
    // Non-finite is refused in the same breath, and it has to be refused
    // BEFORE the accumulator: NaN passes qFuzzyIsNull, poisons the running
    // total, and then fails every subsequent magnitude comparison, so the
    // chord would swallow every event on that axis while firing nothing.
    //
    // Gated on deltaV120 being ABSENT, because the notch conversion below
    // prefers that field and reads `delta` only as a fallback. Testing delta
    // unconditionally dropped an event carrying a nonzero v120 with a zero or
    // non-finite smooth delta, and took its banked remainder with it. When
    // v120 carries the event `delta` is never read, so a NaN there cannot
    // reach the accumulator either way.
    const bool useV120 = deltaV120 != 0;
    if (!useV120 && (qFuzzyIsNull(delta) || !std::isfinite(delta))) {
        resetWheelAccumulators();
        return false;
    }
    // Convert to NOTCHES before anything downstream looks at the magnitude.
    // KWin's two delta fields are not notch counts: deltaV120 is 120 per
    // notch, and the smooth delta is the source's own scale (degrees for a
    // wheel, pixels for a touchpad). deltaV120 is exact and is what a wheel
    // always carries, so prefer it and fall back to the smooth field only for
    // the continuous sources that leave it zero.
    const qreal notches = deltaV120 != 0 ? deltaV120 / kV120PerNotch : delta / kSmoothUnitsPerNotch;
    // Not while a window drag is in flight. The shipped defaults cannot
    // collide (drag activation is Alt, the chords are Meta and Meta+Shift),
    // but both sides are user-configurable now, and a user who binds the same
    // modifier to both would otherwise reflow the strip out from under the
    // window they are dragging, once per wheel notch.
    if (m_effect->m_dragTracker && m_effect->m_dragTracker->isDragging()) {
        resetWheelAccumulators();
        return false;
    }
    // Focus is tested BEFORE view. The two chords are matched exactly (see
    // exactModifierMatch), so no event can satisfy both and the order is a
    // formality for the stock pair — but a user is free to bind the SAME
    // chord to both, and then this order is the tie-break. Focus wins because
    // it is the verb that also moves the view, so the other reading loses
    // nothing the user can see.
    const bool focusMatch = TriggerParser::anyTriggerHeldExact(m_wheelFocusTriggers, mods, buttons);
    const bool viewMatch = !focusMatch && TriggerParser::anyTriggerHeldExact(m_wheelViewTriggers, mods, buttons);
    if (!focusMatch && !viewMatch) {
        resetWheelAccumulators();
        return false;
    }
    // Resolve the target BEFORE touching the accumulators. wheelTargetScreen
    // is empty when the chord matched over a screen that does not run the
    // strip, and that event must pass through to the app underneath whole —
    // including the sub-notch events, which the accumulator branch below
    // would otherwise swallow.
    const QString screenId = wheelTargetScreen();
    if (screenId.isEmpty()) {
        resetWheelAccumulators();
        return false;
    }
    // Accumulate to a whole notch before acting, which is the threshold
    // KWin's axis-shortcut path applied for us before the matching moved
    // here. A discrete wheel notch normalises to exactly 1.0 and so still
    // fires on its first event; a touchpad or high-resolution wheel spends
    // several fractional events per step instead of one verb each.
    qreal& accum = orientation == Qt::Vertical ? m_wheelAccumVertical : m_wheelAccumHorizontal;
    qreal& other = orientation == Qt::Vertical ? m_wheelAccumHorizontal : m_wheelAccumVertical;
    accum += notches;
    if (qAbs(accum) < 1.0) {
        // Sub-notch, but still part of the chord gesture: consume it so the
        // app underneath does not scroll its own content while the user is
        // mid-step on the strip.
        return true;
    }
    // The sign is fixed for this event; only the magnitude is spent below.
    const qreal whole = accum > 0 ? 1.0 : -1.0;
    // Spend the WHOLE accumulated magnitude, not one notch of it. A fast
    // discrete wheel and a coalesced high-resolution frame can both deliver
    // more than one notch in a single event, and taking one step per event
    // there would bank the rest forever: the strip would lag the wheel by a
    // growing margin and the unspent remainder would be dropped at the end of
    // the gesture.
    //
    // Computed arithmetically and CAPPED rather than looped down. A loop here
    // is a compositor hang waiting to happen: it runs on KWin's main thread,
    // and a delta large enough that subtracting 1.0 no longer changes it
    // never terminates. The cap also bounds the D-Bus fan-out below, since
    // each step is its own message and no real gesture needs more than a
    // handful per event.
    const bool capped = qAbs(accum) > static_cast<qreal>(kMaxWheelStepsPerEvent);
    const int steps = static_cast<int>(qMin(qAbs(accum), static_cast<qreal>(kMaxWheelStepsPerEvent)));
    if (capped) {
        // TRUNCATE at the cap, do not bank the excess. Subtracting only what
        // was spent leaves the remainder in the accumulator, and since the cap
        // binds again on the next event, one garbled delta fires the full 16
        // steps — sixteen D-Bus messages — on EVERY later event of the gesture
        // until the chord is released. Discarding makes the behaviour match
        // what the cap's own doc says it does, and there is nothing worth
        // keeping: reaching this branch means the delta already exceeded any
        // real gesture by an order of magnitude.
        accum = 0.0;
    } else {
        // Under the cap the remainder is a genuine sub-notch fraction from a
        // high-resolution wheel or touchpad, and carrying it is the whole
        // point of the accumulator.
        accum -= steps * whole;
    }
    // This gesture belongs to one axis. Zeroing the other stops a diagonal
    // drift from banking a second, opposite step on the axis the user is not
    // actually scrolling along.
    other = 0.0;
    // Sign, not magnitude: one notch is one column (or one view step), and
    // the engine owns the step size. A wheel DOWN or RIGHT moves toward the
    // end of the strip, matching niri and the scroll direction of the axis.
    // Which way that points on screen is resolved downstream against the
    // screen's own strip axis, so one rule serves a horizontal and a vertical
    // strip alike and a horizontal (tilted) wheel needs no separate arm.
    int step = whole > 0 ? 1 : -1;
    if (m_wheelFocusInverted) {
        step = -step;
    }
    const QLatin1String verb = focusMatch ? QLatin1String("focusColumn") : QLatin1String("scrollView");
    qCDebug(lcEffect) << "Wheel chord:" << verb << "step" << step << "x" << steps << "on" << screenId;
    leaveNativeFullscreenTiles(screenId);
    // One verb per notch. The engine owns the step SIZE, so a two-notch event
    // is two single steps rather than one double-sized one, which keeps the
    // strip's own animation identical to scrolling those notches separately.
    for (int i = 0; i < steps; ++i) {
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Scrolling,
                                                       QString(verb), {screenId, step}, QString(verb));
    }
    return true;
}

// Leaves the OWN fullscreen (a client F11, a video going fullscreen) of every
// scroll-tracked tile on `screenId`, so a user strip verb never runs against a
// window whose geometry the compositor is refusing. No-op when the screen holds
// no such tile, which is the overwhelmingly common case.
void TilingHandler::leaveNativeFullscreenTiles(const QString& screenId)
{
    // Scrolling a strip that holds a natively-fullscreen tile LEAVES that
    // fullscreen first.
    //
    // A window in its OWN fullscreen (a client F11, a video going fullscreen —
    // not the windowed-fullscreen feature, whose members are committed at their
    // column rect on purpose) refuses every geometry commit through
    // applyWindowGeometry's fullscreen bail. The engine does not know that, so
    // it goes on scrolling and PARKING that column while the screen still shows
    // the fullscreen window: the model says "parked off-strip", the user sees a
    // video, and the two owners stay split for the whole hold. Measured live,
    // one wheel notch at a time, the same window's target walked (8,54) ->
    // (1924,54) -> the park (1932,2176), each answered "fullscreen, skipping".
    //
    // Called from the VERB DISPATCH — the wheel chord here in the effect, and
    // the daemon's keyboard shortcut gate over
    // Scrolling.leaveNativeFullscreenRequested — rather than from the batch
    // apply. A batch cannot tell a user verb from an insert-driven reflow, and
    // gating the exit on the batch's own strip-motion fields (viewDelta /
    // scrollEdge / hasVisualPos) dropped the fullscreen whenever an unrelated
    // window merely OPENED and slid the strip — measured. A dispatch site
    // carries no such ambiguity: something the user pressed is what reaches it.
    //
    // Both callers exit BEFORE their verb goes out, so the engine's own
    // relayout already places a window the compositor will accept, rather than
    // the exit racing a batch that was built against the fullscreen. That
    // ordering is why this is a separate signal rather than a flag on the
    // batch: a flag arrives with the geometry it was supposed to precede.
    //
    // SELECTED first, ACTED on second, and never with m_notifiedWindows under
    // an open iterator. setFullScreen emits windowFrameGeometryChanged and
    // outputChanged SYNCHRONOUSLY on XWayland, and the exit restores the
    // window's pre-fullscreen rect — for a strip column that is routinely a
    // park rect on the neighbouring output, so outputChanged genuinely fires.
    // Its handler reaches handleWindowOutputChanged and, on a cross-mode arm,
    // cleanupAutotileTracking, which removes from the very set a range-for
    // would be walking. The collect/act split is the same one this file
    // already takes for maximizeClaimsLeavingScrolling, and for the same
    // stated reason.
    //
    // Floating tracked windows are excluded outright: a float holds no column,
    // so the engine is not parking one out from under it and there is nothing
    // for a scroll to reconcile.
    QStringList fullscreenTilesToExit;
    for (const QString& tiledId : m_notifiedWindows) {
        if (m_notifiedWindowScreens.value(tiledId) != screenId) {
            continue;
        }
        if (m_effect->m_windowedFullscreenWindows.contains(tiledId)) {
            continue;
        }
        if (m_effect->isWindowFloating(tiledId)) {
            continue;
        }
        KWin::EffectWindow* fsWin = m_effect->findWindowByIdExact(tiledId);
        if (!fsWin || fsWin->isDeleted() || !fsWin->isFullScreen()) {
            continue;
        }
        KWin::Window* kwFs = fsWin->window();
        if (!kwFs || !kwFs->isRequestedFullScreen()) {
            continue;
        }
        fullscreenTilesToExit.append(tiledId);
    }
    for (const QString& tiledId : std::as_const(fullscreenTilesToExit)) {
        // Re-resolved per entry rather than carried as a pointer: an earlier
        // entry's synchronous exit can have destroyed a later one, and
        // isDeleted() on a dangling EffectWindow* is undefined rather than a
        // guard (the QPointer note on maximizeClaimsLeavingScrolling above).
        KWin::EffectWindow* fsWin = m_effect->findWindowByIdExact(tiledId);
        if (!fsWin || fsWin->isDeleted()) {
            continue;
        }
        KWin::Window* kwFs = fsWin->window();
        if (!kwFs) {
            continue;
        }
        qCInfo(lcEffect) << "Strip verb on a screen holding a fullscreen tile — leaving fullscreen for" << tiledId;
        {
            // Own inGeometryApply bracket, exactly as releaseWindowedFullscreenState
            // takes one around the same call: none of the handlers that answer
            // the synchronous frame/output change is suppressed by the
            // fullscreen-changed counter, and ungated they re-enter the
            // cross-screen migration paths for a move the effect itself made.
            // Save/restore rather than set/clear, so a caller already inside an
            // apply is handed its own state back.
            const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
            m_effect->m_daemonGate.inGeometryApply = true;
            const auto geomGuard = qScopeGuard([this, prevInApply] {
                m_effect->m_daemonGate.inGeometryApply = prevInApply;
            });
            // Suppressed, so our own slotWindowFullScreenChanged does not read the
            // effect's write as a user toggle. setFullScreen flips the REQUESTED
            // state synchronously while the committed isFullScreen() lags a client
            // round-trip, so the bail in applyWindowGeometry — which reads that same
            // pair — already resolves false for the batch this scroll produces.
            applyFullScreenSuppressed(kwFs, false);
        }
        // The suppression above bought re-entrancy safety at the cost of the
        // exit branch's own repair, so deliver that repair here. The ENTER
        // branch shed this window's tiled tracking (clearWindowTiledAllScreens)
        // and its decoration, and neither comes back on its own: the verb below
        // only produces a batch when the engine's rects actually move, and a
        // focusColumn or scrollView at the end of the strip moves nothing. The
        // window would then sit at whatever rect KWin restored — for a column
        // that was parked during the hold, off the union entirely — untiled and
        // undecorated for the rest of the session.
        markWindowTiled(screenId, tiledId);
        // Re-seed the tracker the bracket's swallowed outputChanged would have
        // written, the pairing rule every bracketed apply follows. AFTER the
        // re-mark, so getWindowScreenId answers from the engine-authoritative
        // override rather than resolving a still-parked frame positionally.
        m_effect->m_trackedScreenPerWindow[fsWin] = m_effect->getWindowScreenId(fsWin);
        // Same dispatch the fullscreen-exit branch makes, for the same reason:
        // KWin re-applies the PRE-fullscreen rect a client round-trip later and
        // the engine's emit-on-change gate stays silent because its own rects
        // never moved, so nothing else corrects the stray frame.
        //
        // Both of its gates hold on either caller's path, by different means.
        // The wheel chord resolved this screen through wheelTargetScreen, which
        // tests isScrollingScreen and refuses a closed daemon gate. The keyboard
        // caller cannot reach here at all unless the daemon is up and its own
        // shortcut gate found the screen in scrolling mode, and a screen this
        // process holds no tile for selects nothing above.
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Scrolling,
                                                       QStringLiteral("reapplyWindowGeometry"), {tiledId},
                                                       QStringLiteral("reapplyWindowGeometry"));
    }
    if (!fullscreenTilesToExit.isEmpty()) {
        // shouldDecorateWindow's fullscreen reject has lifted for these
        // windows, and the enter branch's removeWindowDecoration is what left
        // them bare. Once per scroll, not once per window.
        m_effect->updateAllDecorations();
    }
}

void TilingHandler::resetWheelAccumulators()
{
    m_wheelAccumVertical = 0.0;
    m_wheelAccumHorizontal = 0.0;
}

QString TilingHandler::wheelTargetScreen() const
{
    if (!m_effect->m_daemonGate.serviceRegistered || !KWin::effects) {
        return QString();
    }
    // The strip that moves is the one under the CURSOR (a wheel chord is a
    // pointer gesture, not a focus verb): resolve the cursor's effective
    // screen — virtual subdivisions included — and only forward when it
    // actually runs the scrolling engine. On any other screen this returns
    // empty and the caller passes the event through untouched: matching is
    // per event, not a registration, so nothing is consumed and the app
    // underneath scrolls normally.
    const QPointF pos = KWin::effects->cursorPos();
    const QPoint rounded(qRound(pos.x()), qRound(pos.y()));
    const auto* output = KWin::effects->screenAt(rounded);
    if (!output) {
        return QString();
    }
    const QString screenId = m_effect->resolveEffectiveScreenId(rounded, output);
    // isScrollingScreen, not the raw set: it intersects with the managed union,
    // so a screen the union already dropped cannot still swallow the chord and
    // forward a verb the engine no longer owns.
    if (!isScrollingScreen(screenId)) {
        return QString();
    }
    return screenId;
}

void TilingHandler::slotLeaveNativeFullscreenRequested(const QString& screenId)
{
    // The keyboard twin of the wheel chord's own call. The daemon emits this
    // immediately before dispatching a strip verb, so the exit lands ahead of
    // the relayout rather than racing it.
    //
    // No screen filtering here: leaveNativeFullscreenTiles already selects by
    // m_notifiedWindowScreens, so a screen this process holds no tiles for
    // selects nothing. Guarding on isScrollingScreen as well would only add a
    // second answer to the same question, which can disagree.
    if (screenId.isEmpty()) {
        return;
    }
    leaveNativeFullscreenTiles(screenId);
}

} // namespace PlasmaZones
