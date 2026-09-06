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
// The tab wheel below walks the indicator model to find the neighbouring tab.
#include "compositor/scrolltabindicatorpainter.h"

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
#include <optional>
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

/// One axis event's worth of whole steps, banking the sub-notch remainder.
///
/// Shared by the two wheel gestures (the chords over the strip and the tab
/// wheel over an indicator) because the arithmetic here is subtle in ways a
/// second copy would get wrong: the two delta fields are different scales,
/// the cap TRUNCATES rather than banking, and the opposite axis is zeroed so
/// a diagonal drift cannot bank a second, opposite step. Each gesture owns
/// its own accumulator pair; only the maths is common.
///
/// @p accum / @p other are this event's axis and its opposite. Returns 0 for
/// a sub-notch event (the caller still consumes it — the gesture is live),
/// and writes the direction to @p step.
int spendWheelNotches(qreal notches, qreal& accum, qreal& other, int& step)
{
    accum += notches;
    if (qAbs(accum) < 1.0) {
        step = 0;
        return 0;
    }
    const qreal whole = accum > 0 ? 1.0 : -1.0;
    const bool capped = qAbs(accum) > static_cast<qreal>(kMaxWheelStepsPerEvent);
    const int steps = static_cast<int>(qMin(qAbs(accum), static_cast<qreal>(kMaxWheelStepsPerEvent)));
    if (capped) {
        // TRUNCATE at the cap rather than banking the excess: keeping the
        // remainder would re-fire the full cap on every later event of the
        // gesture, since the cap binds again immediately.
        accum = 0.0;
    } else {
        accum -= steps * whole;
    }
    other = 0.0;
    step = whole > 0 ? 1 : -1;
    return steps;
}

/// Notches for one axis event, or nullopt when it carries no usable
/// direction (the zero/non-finite stop tick that ends a kinetic stream).
///
/// deltaV120 is exact and is what a wheel always carries, so prefer it and
/// fall back to the smooth field only for continuous sources that leave it
/// zero. Neither field is a notch count on its own.
std::optional<qreal> wheelNotches(qreal delta, qint32 deltaV120)
{
    if (deltaV120 != 0) {
        return deltaV120 / kV120PerNotch;
    }
    if (qFuzzyIsNull(delta) || !std::isfinite(delta)) {
        return std::nullopt;
    }
    return delta / kSmoothUnitsPerNotch;
}
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
    const std::optional<qreal> maybeNotches = wheelNotches(delta, deltaV120);
    if (!maybeNotches) {
        resetWheelAccumulators();
        return false;
    }
    const qreal notches = *maybeNotches;
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
    // Spend the WHOLE accumulated magnitude, not one notch of it: a fast
    // discrete wheel and a coalesced high-resolution frame can both deliver
    // more than one notch in a single event, and taking one step per event
    // would bank the rest forever, leaving the strip lagging the wheel by a
    // growing margin.
    qreal& accum = orientation == Qt::Vertical ? m_wheelAccumVertical : m_wheelAccumHorizontal;
    qreal& other = orientation == Qt::Vertical ? m_wheelAccumHorizontal : m_wheelAccumVertical;
    // Sign, not magnitude: one notch is one column (or one view step), and
    // the engine owns the step size. A wheel DOWN or RIGHT moves toward the
    // end of the strip, matching niri and the scroll direction of the axis.
    // Which way that points on screen is resolved downstream against the
    // screen's own strip axis, so one rule serves a horizontal and a vertical
    // strip alike and a horizontal (tilted) wheel needs no separate arm.
    int step = 0;
    const int steps = spendWheelNotches(notches, accum, other, step);
    if (steps == 0) {
        // Sub-notch, but still part of the chord gesture: consume it so the
        // app underneath does not scroll its own content while the user is
        // mid-step on the strip.
        return true;
    }
    if (m_wheelFocusInverted) {
        step = -step;
    }
    const QLatin1String verb = focusMatch ? QLatin1String("focusColumn") : QLatin1String("scrollView");
    qCDebug(lcEffect) << "Wheel chord:" << verb << "step" << step << "x" << steps << "on" << screenId;
    leaveNativeFullscreenTiles(screenId);
    // One verb per notch. The engine owns the step SIZE, so a two-notch event
    // is two single steps rather than one double-sized one, which keeps the
    // strip's own animation identical to scrolling those notches separately.
    // Built once rather than twice per iteration: this is the pointer input
    // path, and a coalesced high-resolution frame can carry the full
    // kMaxWheelStepsPerEvent, all of them naming the same verb.
    const QString verbName(verb);
    for (int i = 0; i < steps; ++i) {
        PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Scrolling, verbName,
                                                       {screenId, step}, verbName);
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

void TilingHandler::resetTabWheelAccumulators()
{
    m_tabWheelAccumVertical = 0.0;
    m_tabWheelAccumHorizontal = 0.0;
    // The walk anchor is gesture state too. Note this covers only the paths
    // that REJECT an event; a gesture that simply stops sending them ends
    // without reaching here, so the anchor is also retired from the model
    // relay in rebuildScrollTabIndicators when the column's active tab turns
    // out to have moved by something other than the wheel.
    m_tabWheelAnchor.clear();
    m_tabWheelAnchorPending = false;
}

bool TilingHandler::handleTabWheel(const QPointF& pos, qreal delta, qint32 deltaV120, Qt::Orientation orientation,
                                   Qt::KeyboardModifiers mods, Qt::MouseButtons buttons)
{
    // Same cheap gate the chord path opens with: every axis event in the
    // session reaches here.
    if (m_scrollingScreens.isEmpty()) {
        resetTabWheelAccumulators();
        return false;
    }
    // UNMODIFIED wheel only. A modified wheel that matched no chord belongs
    // to whatever is underneath (Ctrl+wheel is an app's zoom on every
    // toolkit), and a held button means a drag is spending the wheel.
    // Claiming those would make the indicator a dead zone for gestures that
    // have nothing to do with tabs.
    //
    // The trigger is fixed rather than a configurable trigger list, unlike
    // the two strip wheel gestures (Scrolling.Wheel.Focus and .View). Those
    // are chords competing for the whole screen, so which modifier owns them
    // has to be the user's call. This one is scoped to the pixels of an
    // indicator the user is already pointing at, and "no modifier over a
    // pill" is the only spelling that does not collide with the chords it
    // sits beside. It follows the same reasoning that the click on a pill is
    // not configurable either. The gesture is off whenever the indicator is,
    // since scrollTabPillAt answers empty with nothing painted.
    //
    // It also does not honour m_wheelFocusInverted: that setting is scoped
    // to the strip's column-focus chord, and a tab run reads as a list rather
    // than as a strip, so inheriting the strip's inversion would be a guess.
    if (mods != Qt::NoModifier || buttons != Qt::NoButton) {
        resetTabWheelAccumulators();
        return false;
    }
    // Reuses activateScrollTabAt's hit test, so the pill under the cursor is
    // resolved by exactly the rules that decide whether a CLICK lands: the
    // painted-pixels gate, the view-offset shift and the occlusion probe.
    const QString hovered = scrollTabPillAt(pos);
    if (hovered.isEmpty()) {
        resetTabWheelAccumulators();
        return false;
    }
    // The same drag and show-desktop refusals the click path carries, and for
    // the same reason: switching the visible tab restructures the strip under
    // a drag that is aiming at it.
    if (PlasmaZonesEffect::isShowingDesktop()
        || (m_effect->m_dragTracker
            && (m_effect->m_dragTracker->isDragging() || m_effect->m_dragTracker->compositorMoveResizeActive()))) {
        resetTabWheelAccumulators();
        return false;
    }
    // Resolve the output BEFORE spending any notches. scrollTabPillAt above
    // already resolved it to answer the hit test, so a null here is not
    // reachable today, but bailing after the spend would hand a partially
    // consumed stream to the ScrollFactor path if it ever became reachable.
    //
    // The EVENT's position, where the chord path uses wheelTargetScreen's
    // cursorPos(). The two are not interchangeable here: this gesture is
    // anchored to a pill the hit test already resolved from `pos`, so
    // resolving the output from anything else could name a different screen
    // from the one the pill was found on. The chord has no such anchor and
    // asks where the cursor is.
    KWin::LogicalOutput* out = KWin::effects ? KWin::effects->screenAt(pos.toPoint()) : nullptr;
    if (!out) {
        resetTabWheelAccumulators();
        return false;
    }
    const std::optional<qreal> notches = wheelNotches(delta, deltaV120);
    if (!notches) {
        resetTabWheelAccumulators();
        return false;
    }
    qreal& accum = orientation == Qt::Vertical ? m_tabWheelAccumVertical : m_tabWheelAccumHorizontal;
    qreal& other = orientation == Qt::Vertical ? m_tabWheelAccumHorizontal : m_tabWheelAccumVertical;
    int step = 0;
    const int steps = spendWheelNotches(*notches, accum, other, step);
    if (steps == 0) {
        // Sub-notch of a live gesture: consume it, or the app under the pill
        // scrolls its own content between the steps the user does spend.
        return true;
    }
    const ScrollTabIndicatorPainter* painter = m_effect->m_scrollTabPainter.get();
    // Anchor the walk on the tab the COLUMN is showing, NEVER on the pill
    // under the cursor. Activating a tab recolours the pills but does not
    // reorder them, so the cursor keeps naming the same pill for the whole
    // gesture; anchoring there would resolve every event to that one pill's
    // neighbour and the wheel would move a single tab and then stick.
    //
    // The gesture's own last target wins while it is still in this run,
    // because the model's `active` flag only catches up once the daemon
    // relays the focus back and the next notch routinely arrives first.
    // Falling back to the model covers the first notch of a fresh gesture,
    // and to the hovered pill when the run has no active tab at all.
    QString target = m_tabWheelAnchor;
    if (target.isEmpty() || painter->indicatorFor(out, hovered) != painter->indicatorFor(out, target)) {
        target = painter->activePillFor(out, hovered);
    }
    if (target.isEmpty()) {
        target = hovered;
    }
    const QString anchor = target;
    // Walk the ring one tab per step rather than jumping `steps` at once: the
    // painter's model is the only thing that knows the run, and each hop must
    // start from where the last one landed. The hop is resolved entirely from
    // the model — nothing round-trips to the daemon between steps, so a
    // multi-notch event cannot read a half-applied strip.
    for (int i = 0; i < steps; ++i) {
        const QString next = painter->neighbourPill(out, target, step);
        if (next.isEmpty()) {
            break;
        }
        target = next;
    }
    // A single-tab indicator (or a model that lost the id mid-gesture) leaves
    // the target where it started. Consume anyway: the cursor IS over a pill,
    // and letting that one case fall through to the app would scroll the
    // window's content out from under an indicator the user is pointing at.
    if (target == anchor) {
        return true;
    }
    // Unlike the click path, which falls through to whatever is underneath
    // when the tab's window died between the payload and the press, a wheel
    // tick here is CONSUMED. A click is a single deliberate act and passing
    // it on is recoverable; a wheel gesture is a stream, and letting one tick
    // of it reach the app would scroll that app's content mid-gesture.
    if (!m_effect->findWindowByIdExact(target)) {
        return true;
    }
    // The activation the click path uses. One owner of "which tab is active":
    // focus the tab's window and let the strip learn through windowFocused.
    m_tabWheelAnchor = target;
    m_tabWheelAnchorPending = true;
    slotFocusWindowRequested(target);
    return true;
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
