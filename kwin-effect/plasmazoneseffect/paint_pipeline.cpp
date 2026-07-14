// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"
#include "../compositorclock.h"
#include "shader_internal.h"
#include "surface_fold.h"
#include "shader_resolve.h"
#include "window_query.h"

#include <PhosphorAnimation/AnimationLimits.h>

#include <core/output.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>
#include <scene/item.h>
#include <scene/windowitem.h>

#include <QDate>
#include <QDateTime>
#include <QPointer>
#include <QScopeGuard>
#include <QTime>
#include <QVector2D>
#include <QVector4D>

#include <chrono>
#include <type_traits>

#include "../windowanimator.h"

namespace PlasmaZones {

using ShaderInternal::shaderClockNowMs;

namespace {

/// Progress for a TIME-DRIVEN transition (`durationMs > 0`) at @p nowMs: linear
/// ratio → timing-curve ease → held-move pin → release down-ramp. @p active
/// reports whether paintWindow would paint the leg this frame.
///
/// Does NOT apply the `reverse` flip — callers do, because paintWindow shares
/// that final step with its animator-driven branch.
///
/// @p stepCurve owns the SINGLE per-frame integrator step for a stateful
/// (spring) curve. paintWindow passes true. The backdrop capture passes false
/// and reads the last stepped value (at most one frame stale), so it can predict
/// the drawn rect without double-stepping the integrator paintWindow owns.
///
/// Both callers route through this one function on purpose: the capture must
/// sample the scene where the quad is actually drawn, and a partial replica
/// drifts. During a held-move release, for instance, the draw ramps progress
/// back toward 0 while an un-ramped predictor sits pinned at 1 — the frost pane
/// then samples the live frame while the draw lerps toward the grab frame.
qreal timeDrivenProgress(ShaderTransition& st, qint64 nowMs, bool stepCurve, bool& active)
{
    active = false;
    if (st.durationMs <= 0) {
        return 0.0;
    }
    qreal progress = 0.0;
    const qint64 elapsed = nowMs - st.startTimeMs;
    if (elapsed >= 0 && elapsed <= st.durationMs) {
        // Ease the linear time progress through the per-event timing curve
        // (resolved global → "All" → node → rule at begin time), so a node's
        // curve shapes its shader iTime exactly as it shapes the animator-driven
        // branch. `lastPaintTimeMs` still holds the previous tick here — it is
        // advanced later, alongside iTimeDelta. Shared with the desktop switch;
        // see easeProgress for the dt cap and the stateful/stateless split.
        const qreal linear = qreal(elapsed) / qreal(st.durationMs);
        progress = ShaderInternal::easeProgress(st.progressCurve.get(), st.progressCurveState, st.lastPaintTimeMs,
                                                nowMs, linear, stepCurve);
        active = true;
    } else if ((st.holdUntilRelease || (st.meshSim.initialized && !st.meshSim.settled)) && elapsed > st.durationMs) {
        // HELD move/resize: the drag outlives the nominal duration by design.
        // Stay active with progress pinned at 1 — the motion uniforms
        // (iMoveVelocity / iMoveOffset / iMoveMesh), not the clock, drive the
        // shader from here, and a stateful curve deliberately stops stepping. The
        // pin is exact: reading the frozen CurveState instead would sit below 1
        // for an underdamped spring. After release (holdUntilRelease cleared) a
        // mesh-driven transition stays active until its lattice settles, so the
        // wobble rings out instead of being cut off at the release frame.
        progress = 1.0;
        active = true;
    }
    // NOTE: there is deliberately NO "completion frame" branch here for a
    // stateful curve. One was tried and was unreachable: it needed a paint at
    // `elapsed > durationMs`, but tryBeginShaderForEvent's teardown timer fires
    // AT durationMs (and Qt's coarse timer may fire up to 5% early), so the leg
    // is erased before such a frame can land. The residual it was meant to hide
    // is instead removed at the source — Spring's settling band is 0.5%, not the
    // 2% control-theory convention, so the integrator is already within half a
    // percent of 1.0 when the clock expires. See SettleBand in spring.cpp.
    // Held-move release leg (velocity/trail move packs): the release handler
    // stamps releaseStartMs instead of clearing the hold flag, and progress ramps
    // back toward 0 over the same durationMs — the shader plays iTime 1→0 after
    // release, so a dissolve-while-held pack (phosphor-vortex) rematerialises
    // instead of snapping at teardown. Subtracting from the base progress (not
    // from a hard 1.0) bounds a grab shorter than the nominal duration by its
    // grab-in value. The ramp is deliberately LINEAR even when the base was eased:
    // this leg is exclusive to window.movement.move, whose packs drive their
    // visuals from the motion uniforms, not from iTime. Mesh packs never stamp
    // this and are untouched. Runs BEFORE the caller's reverse flip, which is
    // correct — the stamp only exists on the held-move path, and window.move
    // installs with reverse == false.
    if (active && st.releaseStartMs >= 0) {
        const qreal down = qreal(nowMs - st.releaseStartMs) / qreal(st.durationMs);
        // Route through the ONE clamp policy, not a bare qBound. A held-move leg
        // whose grab was SHORTER than the nominal duration takes the eased branch
        // above, not the pin — so `progress` can legitimately carry an overshooting
        // curve's overshoot (1.08), and an unconditional qBound would snap it to 1.0
        // at the exact moment the button lets go. The lower bound matters equally:
        // an overshooting curve dips below 0, and a 0.0 floor kills that too.
        progress = ShaderInternal::clampProgressForCurve(progress - qMax(down, 0.0), st.progressCurve.get());
    }
    // Re-grab resume: the accrued down-ramp, decaying back to 0 over the same
    // durationMs. At the re-grab frame the offset equals the ramp the release
    // leg above had accrued, so the first resumed frame reproduces the last
    // released frame exactly — continuous by construction. Applied to the
    // PAINTED progress (not to the clock) so it is correct for a stateless
    // curve and a stateful spring alike: the spring's integrator keeps its own
    // value and this only lifts the offset off it. See the rationale on
    // ShaderTransition::regrabStartMs for why rewinding startTimeMs cannot work.
    if (active && st.regrabStartMs >= 0) {
        const qreal back = qreal(nowMs - st.regrabStartMs) / qreal(st.durationMs);
        const qreal offset = st.regrabDownOffset - qMax(back, 0.0);
        if (offset <= 0.0) {
            // Retire the exhausted offset only on the PAINT pass. `stepCurve` is the
            // "I own the per-frame mutation" flag, and the backdrop predictor passes
            // false precisely so it can read this function without side effects — but
            // this reset was outside that guard, so the predictor was clearing state
            // paintWindow's own call was about to read. Benign today (both share the
            // pinned frame clock, so both see the same expiry), and a free bug for
            // whoever next changes either arm.
            if (stepCurve) {
                st.regrabStartMs = -1;
                st.regrabDownOffset = 0.0;
            }
        } else {
            // Same clamp policy as the release ramp above — see there.
            progress = ShaderInternal::clampProgressForCurve(progress - offset, st.progressCurve.get());
        }
    }
    return progress;
}

} // namespace

void PlasmaZonesEffect::prePaintScreen(KWin::ScreenPrePaintData& data)
{
    // KWin 6.7 no longer passes a presentTime; sample the steady clock
    // ourselves. CompositorClock's epoch is steady_clock by contract, so a
    // current-time sample is the correct (and only available) source — KWin's
    // own effects likewise read "now" rather than the target present time.
    const auto presentTime =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());

    // Feed presentTime to the clock for THIS output so animations
    // bound to other outputs' clocks read stale `now` on their
    // AnimatedValue::advance() calls this tick and step with dt=0
    // (correct: they tick when their own output paints, not when any
    // output paints).
    //
    // The fallback clock is intentionally NOT fed per-output presentTime
    // here. It self-drives from std::chrono::steady_clock — on an
    // N-output desktop, prePaintScreen fires N× per vsync, and pushing
    // presentTime into the fallback every call would step fallback-bound
    // animations N× per frame. Fallback's now() reads steady_clock
    // directly so it advances once per wall-clock moment regardless of
    // how many outputs painted. See CompositorClock::now()/updatePresentTime
    // for the fallback branch; epoch identity is shared (both rooted at
    // steady_clock) so rebinds between per-output and fallback remain
    // compatible.
    if (data.screen) {
        auto it = m_motionClocksByOutput.find(data.screen);
        if (it != m_motionClocksByOutput.end()) {
            // Pass `data.screen` so the clock can cross-check in debug
            // builds that it is being fed presentTime only for the
            // output it was constructed against. The map lookup above
            // already guarantees this by construction, but the extra
            // argument makes the invariant explicit at the call site —
            // a future refactor that stops keying by output will fire
            // the assertion instead of silently latching another
            // output's timestamps.
            it->second->updatePresentTime(presentTime, data.screen);
        }
    }

    // advanceAnimations iterates all animations regardless of which
    // clock was just updated; each animation reads its own clock's
    // `now()` in AnimatedValue::advance and steps with its own dt.
    // Cost is O(#animations) per prePaintScreen — typical paths see
    // single-digit counts.
    m_windowAnimator->advanceAnimations();

    if (m_windowAnimator->hasActiveAnimations() || !m_shaderManager.empty()) {
        // Windows have translation transforms that move them outside their
        // frame geometry bounds — force full compositing mode. Shader
        // transitions also need this: without
        // PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS the compositor skips
        // our paintWindow override on stable, undamaged windows (focus,
        // open after the fade settles, minimise, etc.), which means
        // the shader installs and silently expires unrendered.
        //
        // First-frame open suppression does NOT need the screen-level
        // flag: prePaintWindow already calls `data.setTransformed()` for
        // every suppressed window via the same predicate
        // (`m_restoreSuppress.contains(w)`), and the postPaintScreen
        // suppression damage loop schedules per-output repaints to keep
        // the deadline check ticking. Adding the screen-level flag here
        // would force every other window on every output through the
        // transformed-windows paint path while ANY window is suppressed
        // (up to 250 ms per opened window) — pure overhead.
        data.mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;
    }

    // A live desktop-switch transition replaces the whole screen with its own
    // two-desktop blend, so force a full-screen paint (our paintScreen skips the
    // normal scene for the transitioning output). Gate on THIS output's liveness:
    // a per-output switch (Plasma 6.7 per-output desktops) must not push the
    // non-transitioning outputs through the transformed-paint path. When the output
    // is unknown (null screen) fall back to the global flag as the safe default.
    const bool transitionOnThisOutput =
        data.screen ? m_desktopTransition.isRunningForOutput(data.screen) : m_desktopTransition.isRunning();
    if (transitionOnThisOutput) {
        data.mask |= PAINT_SCREEN_TRANSFORMED;
    }

    // Cache cursor pos once per frame for the iMouse uniforms. paintWindow
    // runs once per active transition (and may run multiple times across
    // outputs); reading KWin::effects->cursorPos() at every call multiplies
    // up. Caching here also guarantees every consumer this frame reads an
    // identical iMouse, eliminating sub-frame jitter. Decorated windows read
    // it too (hover-reactive surface packs via pushBorderUniforms), so the
    // refresh also runs while any decoration exists, not only mid-transition.
    if (KWin::effects && (!m_shaderManager.empty() || !m_windowDecorations.isEmpty())) {
        m_shaderManager.m_cachedCursorGlobal = KWin::effects->cursorPos();
    }

    // Frame-pin the shader clock. KWin can invoke `paintWindow` more than
    // once per compositor cycle (multi-output, multi-pass, back-to-back
    // paint cycles scheduled by our own `effects->addRepaint` calls in
    // postPaintScreen). If every paintWindow call re-sampled
    // `shaderClockNowMs()`, each call would see a slightly later
    // timestamp and compute a slightly different `progress`, painting
    // the surface-extent quad at a slightly different position. With
    // back-to-back paint cycles spaced milliseconds apart the
    // accumulated framebuffer holds several visibly offset copies of
    // the in-flight window — the staggered "main one slow + copies
    // fast" ghosting symptom. Sampling once here and reading the cached
    // value from paintWindow pins every paint within this cycle to the
    // same timestamp.
    const qint64 frameClockMs = ShaderInternal::shaderClockNowMs();
    m_shaderManager.setCurrentFrameClockMs(frameClockMs);

    KWin::effects->prePaintScreen(data);
}

void PlasmaZonesEffect::paintScreen(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                                    int mask, const KWin::Region& deviceRegion, KWin::LogicalOutput* screen)
{
    // While a desktop-switch transition is live for this output, paintOutput
    // draws the two-desktop blend into the screen target and returns true, so we
    // skip the normal scene paint. Otherwise (no transition, or it just settled)
    // chain straight through to the standard scene — this override is a no-op for
    // every non-transitioning frame.
    if (m_desktopTransition.paintOutput(renderTarget, viewport, mask, deviceRegion, screen)) {
        return;
    }
    KWin::effects->paintScreen(renderTarget, viewport, mask, deviceRegion, screen);
}

void PlasmaZonesEffect::postPaintScreen()
{
    // Schedule targeted repaints for active animations instead of full-screen
    m_windowAnimator->scheduleRepaints();
    // Keep the desktop-switch transition ticking (per-output repaints) while live.
    m_desktopTransition.scheduleRepaints();
    // Time-based shader transitions (window.*) ride a steady-clock
    // timer, not m_windowAnimator, so paintWindow would only fire on
    // surface damage and iTime would stall. Mirror KWin's own
    // `AnimationEffect::postPaintScreen`: while a time-based transition
    // is live, inject expanded-geometry layer repaint per active
    // window so the next vsync runs our paint chain. Animator-driven
    // transitions (durationMs == 0) are kept alive by
    // m_windowAnimator->scheduleRepaints above.
    if (!m_shaderManager.empty()) {
        const qint64 now = shaderClockNowMs();
        for (const auto& [w, transition] : m_shaderManager.shaderTransitions()) {
            if (!w) {
                continue;
            }
            // Skip windows KWin is not painting. An off-desktop window never reaches
            // paintWindow, and paintWindow is the ONLY teardown for a durationMs == 0
            // (animator-driven) leg — it has no timer. Without this, snapping a window
            // and then switching virtual desktop mid-morph leaves the arm below
            // requesting a FULL-OUTPUT repaint every vsync, indefinitely.
            //
            // EXCEPT a leg we hold the close grab on. A closing window is isDeleted()
            // for its ENTIRE close animation (we keep the corpse alive with that very
            // grab), and it emits no damage of its own because the client is gone — so
            // it is the leg that needs this pump MOST. Skipping it leaves the close
            // shader running only as long as something else happens to damage the
            // output. `closeGrabHeld` is precisely "this leg is animating a corpse we
            // are keeping alive", and paintWindow already calls screen()/frameGeometry()
            // on that same ref-held Deleted every close frame.
            if (!transition.closeGrabHeld && (w->isDeleted() || !w->isOnCurrentDesktop())) {
                continue;
            }
            const bool timeBasedActive =
                transition.durationMs > 0 && (now - transition.startTimeMs) <= transition.durationMs;
            // An animator-driven leg is live only while the ANIMATOR is. Gating on
            // the mode flag alone (durationMs == 0) keeps repainting for a leg whose
            // animation finished but whose teardown has not run.
            const bool animatorActive = transition.durationMs == 0 && m_windowAnimator->hasAnimation(w);
            // Held move/resize transitions live past their nominal duration
            // (timeBasedActive goes false), and a soft-body lattice keeps
            // ringing AFTER release when the window emits no damage of its
            // own — so drive repaints off the hold flag and the lattice's
            // settle state for BOTH extent modes. Without the held arm on the
            // anchor-extent branch below, a held anchor pack's idle/iTime
            // motion freezes the instant the pointer stops moving (the window
            // emits no damage while stationary). meshSim is seeded only for
            // packs that declare iMoveMesh (typically surface-extent packs), so
            // for a pack without a live lattice this reduces to holdUntilRelease.
            const bool heldActive =
                transition.holdUntilRelease || (transition.meshSim.initialized && !transition.meshSim.settled);
            if (transition.surfaceExtent) {
                // Surface-extent transitions paint the window translated
                // far past its frame (bounce lifts it a full window-
                // height above). The damage region MUST cover every
                // screen pixel the shader sweeps, otherwise prior frames'
                // pixels survive in the back buffer as a stacked, ghosted
                // trail (worst with longer durations — 2 s fly-ins showed
                // 5+ overlapping copies of the window).
                //
                // `w->addLayerRepaint(r)` is the wrong primitive here:
                // it does `mapFromScene(r)` and feeds the result to
                // `WindowItem::scheduleRepaint`, which the scene clips
                // to the window-item's bounding rect. The OUTSIDE-the-
                // frame band the shader paints is never marked dirty,
                // so the compositor's incremental present skips it.
                // Use `effects->addRepaint(output)` — screen-level
                // damage with no per-window clip — to mark the whole
                // output as dirty every frame the transition is live. The
                // `heldActive` arm (hoisted above) keeps a held/ringing
                // lattice repainting after the duration timer stands down.
                if ((timeBasedActive || animatorActive || heldActive) && KWin::effects) {
                    if (const auto* output = w->screen()) {
                        KWin::effects->addRepaint(output->geometry());
                    } else {
                        KWin::effects->addRepaintFull();
                    }
                }
            } else if (timeBasedActive || heldActive) {
                // Damage the whole output every frame an anchor-extent
                // time-based OR held shader is live. The vertex stage
                // translates the redirected quad past the window's natural
                // rect (bounce drops it in from above, fly-in slides it from
                // the edge); the band it sweeps — both the off-frame
                // destination and the vacated origin — must be marked
                // dirty so the compositor recomposites it each frame.
                // Without this the swept band keeps stale pixels. The held
                // arm covers a held anchor-extent move whose duration timer
                // has stood down: the window emits no damage while the
                // pointer is stationary, so its idle motion would otherwise
                // freeze until release.
                if (KWin::effects) {
                    if (const auto* output = w->screen()) {
                        KWin::effects->addRepaint(output->geometry());
                    } else {
                        KWin::effects->addRepaintFull();
                    }
                }
            }
        }
    }
    // Keep withheld (first-frame suppression) windows in the paint loop:
    // paintWindow draws nothing for them, so without an explicit repaint a
    // suppressed window with no open-shader transition driving damage
    // would never get another paintWindow call — its deadline check and
    // settle detection would stall.
    //
    // Use `effects->addRepaint(output)` (screen-level damage), NOT
    // `sw->addLayerRepaint(output)` — `addLayerRepaint` runs `mapFromScene`
    // and feeds the result to `WindowItem::scheduleRepaint`, which the
    // scene CLIPS to the window-item's bounding rect (the documented
    // failure mode for surface-extent transitions in the loop above).
    // For a centred-placement-suppressed window on a 4K output, the
    // window's bounding rect is much smaller than the output, so the
    // clip would shrink the damage to the centred placement region. That
    // is technically enough to keep paintWindow ticking the deadline, but
    // it also means a co-installed surface-extent open shader's off-frame
    // band sweeps would not be marked dirty (the shader-transition loop
    // above only iterates `m_shaderManager.m_shaderTransitions`, so
    // suppression-active-but-shader-active windows would lose their
    // surface-extent damage). Screen-level damage avoids the clip entirely.
    if (KWin::effects) {
        // KWin::effects cannot go null mid-loop (it's a global owned by
        // KWin's effect system, not by any window we touch), so hoist the
        // null-check above the loop — the per-iteration test was dead
        // overhead in a hot path that runs every frame while any window
        // has a restore-suppression active.
        for (auto it = m_restoreSuppress.cbegin(); it != m_restoreSuppress.cend(); ++it) {
            KWin::EffectWindow* sw = it.key();
            if (!sw || sw->isDeleted()) {
                continue;
            }
            if (const auto* output = sw->screen()) {
                KWin::effects->addRepaint(output->geometry());
            } else {
                KWin::effects->addRepaintFull();
            }
        }
    }
    // Drive continuous repaints for windows whose surface decoration animates
    // (a pack in the chain references iTime). Without content damage their
    // paintWindow would not fire and iTime would stall, so damage each such
    // window's full area every frame while the border owns the slot (idle — a
    // live transition drives its own repaints in the loop above and the surface
    // composite degrades to single-pass there anyway). A purely static
    // decoration (border-only) is not matched, so this is a no-op in the common
    // case. windowSurfaceAnimates is per-pack-cache hash lookups.
    if (KWin::effects && !m_windowDecorations.isEmpty()) {
        // The clock prePaintScreen pinned for this cycle (it is unpinned at the end
        // of this function, so it is still live here). Read once: a live per-window
        // sample would let two windows in the same frame disagree about the time.
        const qint64 pinnedMs = m_shaderManager.currentFrameClockMs();
        const qint64 frameClockMs = pinnedMs >= 0 ? pinnedMs : ShaderInternal::shaderClockNowMs();
        for (auto it = m_windowDecorations.cbegin(); it != m_windowDecorations.cend(); ++it) {
            if (!it->shaderApplied) {
                continue;
            }
            KWin::EffectWindow* const sw = findWindowByIdExact(it.key());
            // Exact-id discipline (mirrors reconcileDecorationOnPlacementFlip and
            // the teardown paths): findWindowById's fuzzy appId fallback can
            // return a same-app sibling for a stale id, and repainting the
            // sibling would be wrong. Skip unless it re-derives to this exact id.
            if (!sw || getWindowId(sw) != it.key() || sw->isDeleted() || !sw->isOnCurrentDesktop()) {
                continue;
            }
            // needsBackdrop chains are repainted for backdrop changes that
            // land no damage on the window itself, rate-limited to ~30fps
            // (the better-blur-dx model): between refolds the present blit
            // reuses the existing composite, so frost over a video costs a
            // fold every ~33ms instead of every vsync. Window-own damage
            // still paints (and refolds) immediately through KWin's normal
            // scheduling, unaffected by this gate.
            bool backdropDue = false;
            if (it->needsBackdrop) {
                constexpr qint64 kBackdropRefoldIntervalMs = 33;
                const auto stateIt = m_surfaceMultipass.find(it.key());
                // A repaint we have already ASKED FOR is not due again. This driver is the
                // one repaint source with no damage behind it, and a repaint is a request,
                // not a promise: KWin declines to paint a window fully occluded by an opaque
                // one above it, so the fold never runs and lastFoldMs never advances. A
                // clock-only test then says "due" again on the very next frame — a full
                // repaint every vsync, forever, and the desktop can never idle. Which is
                // precisely the runaway this ~30fps rate limit exists to prevent.
                //
                // backdropRepaintPending is armed below when we ask, and cleared by the fold
                // when it actually runs. So an unpainted window costs ONE wasted repaint,
                // not one per frame, and a window that is genuinely painted is unaffected —
                // its fold clears the flag on the frame the repaint lands.
                //
                // A window that has never folded at all has nothing to refresh yet either;
                // its first real paint creates the state and the next pass picks it up.
                //
                // Read the clock PINNED for this frame, not a live sample: every other
                // consumer in this file reads it, so a live read here would let two windows
                // in one frame disagree about what time it is.
                backdropDue = stateIt != m_surfaceMultipass.end() && stateIt->second.lastFoldMs >= 0
                    && !stateIt->second.backdropRepaintPending
                    && (frameClockMs - stateIt->second.lastFoldMs) >= kBackdropRefoldIntervalMs;
            }
            // Decorations.Performance: is this window's chain allowed to animate
            // right now (session not idle, and either it is focused or we animate
            // everything)? A window that is not allowed stops being driven from here,
            // and the fold freezes its clock (see decorationMayAnimate), so it keeps
            // painting its last composite: it still LOOKS decorated, it just stops
            // moving. That is what lets the GPU drop out of its top performance state,
            // which no amount of making the frame cheaper can achieve.
            //
            // Two exemptions from the FOCUS half of that gate:
            //
            //   A focus cross-fade must be allowed to finish, or a window losing
            //   focus would freeze mid-ramp between its active and inactive look.
            //   The ramp clamps at both ends, so it self-terminates.
            //
            //   A needsBackdrop chain must keep its ~30fps backdrop refold. That
            //   driver exists for backdrop changes landing NO damage on the window
            //   itself (see above), so without the exemption an unfocused glass
            //   window would keep presenting a blur baked at the instant it lost
            //   focus — the scene behind it would move and the frost would not.
            //   "Animate only the active window" is a promise about MOTION, not a
            //   licence to show a stale reflection of the desktop.
            //
            // The IDLE half takes neither exemption, and that is deliberate rather
            // than an oversight in the shape of the condition. An idle session is one
            // nobody is looking at, so a stale reflection has no viewer — and anything
            // that IS worth looking at while the user sits still (a video) holds an
            // idle inhibitor, which stops the compositor reporting idle at all. A
            // focus ramp cannot be in flight here either: it lasts at most
            // FocusFadeMsMax, and idle takes seconds of no input while a focus change
            // IS input.
            const bool focusRamping = focusRampInFlight(it.key());
            const bool idleGated = m_pauseAnimationWhenIdle && m_sessionIdle;
            if (idleGated || (!focusRamping && !backdropDue && !decorationMayAnimate(sw))) {
                continue;
            }
            if (backdropDue) {
                // Arm the one-shot: we are asking for this repaint now, and we will not ask
                // again until a fold tells us it landed.
                if (const auto sit = m_surfaceMultipass.find(it.key()); sit != m_surfaceMultipass.end()) {
                    sit->second.backdropRepaintPending = true;
                }
            }
            if (backdropDue || windowSurfaceAnimates(it.key())) {
                // Mark this repaint as OURS. addRepaintFull raises
                // EffectWindow::windowDamaged (the signal fires on repaint
                // scheduling, not only on client content damage), and the
                // decoration capture cache listens to that signal to know when
                // the window's content went stale. Without this guard the
                // repaint we issue here to keep the animation ticking would
                // invalidate the capture on every single frame, so the cache
                // would never hit and we'd re-run the most expensive step of the
                // fold for a window whose content never changed.
                // Scope-guarded, like the sibling m_capturingSnapshot latch: a
                // leaked `true` would silently disable capture invalidation for
                // EVERY decorated window for the rest of the session, freezing
                // their content under a still-animating decoration with no crash to
                // point at. Strictly worse than the failure the sibling guards.
                const auto selfRepaint = selfRepaintScope();
                sw->addRepaintFull();
                // A padded chain's margin band sits OUTSIDE the window item;
                // per-window repaints clip to it, so damage the band at
                // screen level (the documented addLayerRepaint pitfall).
                damagePaddedBand(sw, it->outerPadding);
            }
        }
    }
    KWin::effects->postPaintScreen();
    // Unpin the per-frame clock. Any paintWindow() invocation outside
    // the prePaintScreen→postPaintScreen bracket (defensive bootstrap,
    // future test harness, an unexpected mid-cycle paint) then falls
    // back to the live `shaderClockNowMs()` via the -1 sentinel branch in
    // this file's own clock read, instead of reading a stale pinned
    // timestamp from this cycle.
    m_shaderManager.setCurrentFrameClockMs(-1);
    // Drop the per-frame SetOpacity cache so next frame's prePaintWindow
    // re-resolves against any rule-set or window-metadata changes that
    // landed between frames. See ShaderTransitionManager's cache-block
    // comment for the per-frame contract rationale.
    m_shaderManager.clearFrameOpacityCache();
}

void PlasmaZonesEffect::prePaintWindow(KWin::RenderView* view, KWin::EffectWindow* w, KWin::WindowPrePaintData& data)
{
    // Derived ONCE. This runs per window, per output, per frame, and the three
    // branches below (padded transform, SetOpacity, chain translucency) each used to
    // re-derive the id and re-look-up the same decoration entry.
    const QString windowId = w ? getWindowId(w) : QString();
    const auto decoIt = w ? m_windowDecorations.constFind(windowId) : m_windowDecorations.constEnd();
    const bool decorated = decoIt != m_windowDecorations.constEnd() && decoIt->shaderApplied;

    const bool transformDriven =
        w && (m_windowAnimator->hasAnimation(w) || m_shaderManager.hasTransition(w) || m_restoreSuppress.contains(w));
    if (transformDriven) {
        // Mark as transformed so paintWindow applies our translate+scale, and
        // so the OffscreenEffect redirect drives full-window repaints for the
        // shader leg's iTime advance even when the underlying window content
        // hasn't changed (lifecycle-event shaders need this; without the
        // transformed flag, paintWindow only fires on actual window damage).
        //
        // Damage-region expansion for actor-expansion transitions lives in
        // `postPaintScreen`, which damages the whole output through
        // `KWin::effects->addRepaint(output->geometry())` rather than
        // addLayerRepaint — the scene clips a layer repaint to the window
        // item's bounding rect, which is exactly the margin the expansion
        // needs to paint past. prePaintWindow doesn't drive that on KWin 6;
        // `WindowPrePaintData::devicePaint` is the dirty region in
        // device coords and isn't the right surface for declaring "I
        // want to paint this many pixels past the natural frame".
        data.setTransformed();

        // Mark the window non-opaque for the duration of the transition.
        // Its shader-/animator-driven render bears no relation to its
        // natural opaque content region: a surface-extent shader
        // translates the window out of its frame (bounce, fly-in) and
        // paints transparent where the frame's opaque region sits. If
        // KWin keeps treating that region as opaque it skips
        // recompositing whatever is underneath there, and the stale
        // buffer pixels read back as a ghost / trail of the window for
        // the whole animation. setTranslucent() clears the opaque region
        // so every frame fully recomposites under the window.
        data.setTranslucent();
    } else if (w && !m_windowDecorations.isEmpty()) {
        // Padded decoration chains (WindowDecoration::outerPadding) present on a
        // quad LARGER than the window's natural rect (see apply()); mark the
        // window transformed so KWin paints the padded quad unclipped. The
        // opaque region stays — the window's own content still covers it, so
        // occlusion culling underneath remains valid.
        if (decorated && decoIt->outerPadding > 0) {
            data.setTransformed();
        }
    }

    // Resolve + cache the rule-resolved opacity for a window with SetOpacity
    // rules WHILE a shader transition is in flight on it. SetOpacity is
    // layer-backed: the plain opacity-tint layer folds it into its pack param
    // at updateWindowDecoration time, so the cache here feeds the ONE
    // consumer that reads the rule live per frame — the shader-transition
    // draw's bare-uTexture0 fallback (the path where an opacity-baking
    // chain's fold didn't run). That consumer only executes for a window
    // whose transition is active, so the hasTransition(w) gate keeps idle
    // frames from paying a discarded rule-cascade walk per SetOpacity-rule
    // window. No KWin paint-data opacity is set anywhere: a window without
    // the layer simply does not honour SetOpacity — custom chains dim
    // through their own pack params (frost/glass contentOpacity), and a
    // transparent theme keeps its own alpha untouched. No setTranslucent()
    // needed either: every shader-applied decorated window is already marked
    // translucent below, and the rule dims nothing anywhere else. Skip our
    // own overlay / plasma-shell surfaces so a broad user rule can't dim our
    // UI or the panel; short-circuit on an empty rule set to keep the
    // default-state hot path at two pointer reads.
    if (w && m_shaderManager.hasOpacityRules() && m_shaderManager.hasTransition(w)) {
        const QString winClass = w->windowClass();
        if (!isOwnOverlayClass(winClass) && !isPlasmaShellSurface(winClass)) {
            m_shaderManager.cacheFrameOpacity(w, resolveWindowOpacity(resolveRuleActions(w, getWindowId(w))));
        }
    }

    // A decorated window is TRANSLUCENT. Clear its opaque region so KWin keeps
    // compositing whatever sits behind it.
    //
    // This is an OCCLUSION hint, not a rendering one, and it cannot be expressed in
    // the fragment stage: KWin decides what to composite BEHIND a window before any
    // of our shaders run, so by the time a pack outputs alpha < 1 the scene has
    // already skipped whatever is underneath and the pack blends against stale
    // framebuffer pixels.
    //
    // It is unconditional because EVERY chain is in fact translucent, and that is a
    // property of the shader, not a conservative guess. Reading
    // data/surface/shared/surface_lib.glsl:
    //
    //   borderComposite  ba = edge * insideMask * col.a — the band's output alpha IS
    //                    the border colour's alpha, and a translucent border colour is
    //                    a supported feature, not an edge case.
    //   standardBorderBand  radius = (cornerRadius + borderWidth) * uSurfaceScale —
    //                    the OUTER radius includes the border width, so even a zero
    //                    corner radius arcs the window's outer corners away whenever
    //                    the border has any width. And the smoothstep feather leaves
    //                    the outermost ring of the frame partially transparent
    //                    regardless.
    //
    // So a chain covering every texel of the frame would need a zero-width border —
    // one that draws nothing — and even that is feathered. Deriving a per-window
    // "is it opaque" flag was tried and deleted: its true branch could not fire, and
    // it read as a fix while changing nothing. Proving opacity would need a pack
    // metadata contract that does not exist (metadata declares what a pack NEEDS —
    // needsBackdrop, handlesOpacity, padding — never that its output is total).
    //
    // Note what this is NOT for. It used to be set to keep the window in KWin's paint
    // set so drawWindow kept firing on idle frames. That was a repaint-scheduling hack
    // riding an occlusion flag, and it was not even needed: an undamaged window's
    // pixels, border and all, are already on screen in the last-presented composite.
    // The cases where the composite changes with no window damage (a focus cross-fade,
    // an iTime pack, a backdrop refresh) schedule their own repaints in postPaintScreen.
    if (!transformDriven && decorated) {
        data.setTranslucent();
    }

    OffscreenEffect::prePaintWindow(view, w, data);
}

void PlasmaZonesEffect::paintWindow(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                                    KWin::EffectWindow* w, int mask, const KWin::Region& deviceRegion,
                                    KWin::WindowPaintData& data)
{
    // Read the cached per-frame clock pinned by prePaintScreen. Multiple
    // paintWindow calls within one compositor cycle (multi-output,
    // multi-pass, back-to-back paint cycles driven by our addRepaint)
    // would otherwise each see a slightly later `shaderClockNowMs()`
    // and paint the surface-extent quad at a slightly different
    // progress — visible as staggered ghost copies of the in-flight
    // window. Fall back to a live read if prePaintScreen hasn't pinned
    // the clock yet (test harness, defensive bootstrap path).
    //
    // Sentinel for "not pinned" is -1, established at construction
    // (ShaderTransitionManager::m_currentFrameClockMs default). 0 is a
    // legitimate (if astronomically unlikely) pinned value — the
    // initial steady_clock tick on a fresh process — so we admit it as
    // pinned. Anything strictly negative is the unpinned sentinel.
    const qint64 pinnedNow = m_shaderManager.currentFrameClockMs();
    const qint64 frameNowMs = pinnedNow >= 0 ? pinnedNow : ShaderInternal::shaderClockNowMs();

    // Backdrop capture for needsBackdrop decoration chains (frost / glass):
    // snapshot the scene UNDER this window's padded canvas from the live
    // render target BEFORE any fold below runs — at this point in the scene
    // walk the target holds exactly the content painted below this window.
    // Live windows only: a closing window's decoration reuses its frozen
    // composite (renderSurfaceChain) and must never re-capture. Covers both
    // fold sites (the rest-path composite further down AND the transition
    // branch's renderSurfaceChain), hence the shaderApplied-or-transition
    // gate rather than shaderApplied alone.
    if (w && !w->isDeleted() && !m_capturingSnapshot && !m_windowDecorations.isEmpty()) {
        const auto backIt = m_windowDecorations.constFind(getWindowId(w));
        if (backIt != m_windowDecorations.constEnd() && backIt->needsBackdrop
            && (backIt->shaderApplied || m_shaderManager.findTransition(w))) {
            // While an animation is drawing the window somewhere other than
            // its resting rect, capture the backdrop where the quad actually
            // IS this frame, or the pane shows the wrong slice of the scene
            // for the whole animation (every snap/zone change here). Two of
            // the three animation classes expose exact geometry:
            //   1. C++ WindowAnimator translate+scale — its current rect.
            //   2. Geometry-morph transitions — lerp(from, to, progress)
            //      with the same eased progress the draw uses (below).
            //   3. Non-morph shader transitions: anchor-extent packs warp in
            //      place (rest rect is already right); surface-extent movers
            //      (fly-in / bounce) place pixels only the shader knows, so
            //      they keep the rest-rect capture — stale-position frost
            //      that rides the quad, which motion masks. The reference
            //      blur effects disable blur outright on transformed
            //      windows; this degrades strictly less.
            //
            // PRECEDENCE MUST MIRROR THE DRAW. paintWindow gives a geometry-morph
            // shader priority: when the live transition declares iFromRect
            // (`shaderOwnsGeometry`) it interpolates the drawn rect itself and the
            // WindowAnimator's translate+scale is SKIPPED. Consulting the animator
            // first here would invert that — a maximize morph installed while a
            // snap animation is still in flight (the snap's animator entry is not
            // removed) would predict the snap's rect while the draw paints the
            // morph's, and the pane would sample the wrong scene slice for the
            // overlap.
            QRectF animatedFrame;
            ShaderTransition* st = m_shaderManager.findTransition(w);
            const bool shaderOwnsGeometry = st && st->cached && st->cached->iFromRectLoc >= 0;
            if (shaderOwnsGeometry && st->fromGeometry.isValid() && st->toGeometry.isValid() && st->durationMs > 0) {
                // Same progress the draw will use, via the shared SSOT.
                // stepCurve=false: paintWindow owns the stateful curve's single
                // per-frame step, so this read must not advance it. The reverse
                // flip is applied here, as paintWindow does.
                bool stActive = false;
                qreal t = timeDrivenProgress(*st, frameNowMs, /*stepCurve=*/false, stActive);
                // Honour `active`: an installed-but-expired leg (elapsed past
                // durationMs, not held) paints no shader this frame and the window
                // sits at its rest rect. Lerping its 0-progress would snap the pane
                // back to fromGeometry — the PRE-maximize rect — on the expiry
                // frame. Leaving animatedFrame invalid falls back to the rest-rect
                // capture, which is where the draw actually is.
                if (stActive) {
                    if (st->reverse) {
                        t = 1.0 - t;
                    }
                    // Mirror the pack's OWN split: POSITION takes the raw t (the
                    // overshoot IS the bounce, and it is where the eye reads it),
                    // SIZE takes the clamped t. That is exactly what window-morph
                    // does — `mix(iFromRect.xy, iToRect.xy, t)` alongside
                    // `mix(iFromRect.zw, iToRect.zw, tc)` — because extrapolating an
                    // EXTENT is nonsense at a large ratio (a maximize computes a
                    // negative width) while extrapolating a POSITION is the feature.
                    // Lerping both axes with the raw t, as this briefly did, fixed
                    // the position and broke the size.
                    //
                    // CAVEAT, and it is real: one hard-coded lerp shape cannot track
                    // five packs that each choose their own. `fold` eases its rect
                    // through a smoothstep, `ripple-snap` squares a time-compressed
                    // progress, and `flow` / `phosphor-stream` stagger it PER VERTEX —
                    // there is no single rect to predict for those. The predictor
                    // approximates for everything except window-morph, and always has.
                    // Making it exact needs the rect curve to come from the pack (a
                    // metadata field), not from a guess here. The cost of being wrong
                    // is bounded: the frost pane samples a slightly-off scene slice.
                    // It does not corrupt the draw.
                    const qreal tc = qBound(0.0, t, 1.0);
                    const QRectF& f = st->fromGeometry;
                    const QRectF& g = st->toGeometry;
                    animatedFrame = QRectF(f.x() + (g.x() - f.x()) * t, f.y() + (g.y() - f.y()) * t,
                                           qMax(1.0, f.width() + (g.width() - f.width()) * tc),
                                           qMax(1.0, f.height() + (g.height() - f.height()) * tc));
                }
            }
            if (!animatedFrame.isValid()) {
                // No morph owns the geometry (or it is a durationMs == 0 morph
                // riding the animator's timeline, whose rect the animator has):
                // the animator's current rect is what the draw transforms by.
                animatedFrame = m_windowAnimator->currentValue(w, QRectF());
            }
            captureWindowBackdrop(renderTarget, viewport, w, *backIt, animatedFrame);
        }
    }

    // First-frame open suppression: a window repositioned on open
    // (snap-restore / autotile) is withheld from compositing until its
    // moveResize configure lands, so it never flashes at KWin's centred
    // placement. Paint nothing until then. The deadline is the safety net
    // — if the reposition never lands, release and paint normally rather
    // than risk a permanently invisible window.
    if (auto supIt = m_restoreSuppress.find(w); supIt != m_restoreSuppress.end()) {
        // Tick per-frame book-keeping for any in-flight transition so the
        // first post-suppression paint doesn't see a stale clock. Open
        // shaders (window.open: bounce, fly-in) are installed in
        // slotWindowAdded BEFORE beginRestoreSuppression, so their
        // `startTimeMs` is stamped at install time. While suppressed,
        // `paintWindow` returns without rendering — but if `startTimeMs`
        // were left at install time, `progress = (frameNowMs -
        // startTimeMs) / durationMs` would already be 0..1 (or beyond) by
        // the time suppression releases, and the surface-extent open
        // animation would play its entire timeline INVISIBLY. Stamp
        // `startTimeMs = frameNowMs` every suppressed frame so the
        // progress baseline tracks the moment the window first becomes
        // visible. Reset `lastPaintTimeMs = -1` for the same reason —
        // the first visible paint computes iTimeDelta = 0 ("first frame"
        // semantics), avoiding a 250ms-stale spike.
        if (auto* st = m_shaderManager.findTransition(w)) {
            if (st->durationMs > 0) {
                st->startTimeMs = frameNowMs;
            }
            st->lastPaintTimeMs = -1;
        }
        if (frameNowMs < supIt->deadlineMs) {
            return;
        }
        // Release in-place: erase the entry so the rest of paintWindow
        // proceeds normally. Calling endRestoreSuppression here would
        // re-enter compositing via addRepaintFull from inside the paint
        // loop — fragile on some KWin versions. The next natural repaint
        // (driven by transition damage or the postPaintScreen suppression
        // loop's already-scheduled layer repaint) brings the window back.
        m_restoreSuppress.erase(supIt);
    }

    // SetOpacity deliberately sets NO KWin paint-data opacity. The rule is
    // layer-backed: the plain opacity-tint layer folds it into its pack
    // param, custom chains dim through their own pack params (frost/glass
    // contentOpacity), and a window without the layer does not honour it at
    // all — the user's packs own the look, and a transparent theme keeps
    // its own alpha. See the prePaintWindow cache comment for the one
    // remaining shader consumer (the transition fallback).

    // Re-entrancy guard: captureOldWindowSnapshot below calls
    // effects->drawWindow, which walks the chain back through our
    // OffscreenEffect::drawWindow (to render the raw window into our capture
    // FBO). Don't apply the C++ transform or any morph processing during that
    // raw pass — just continue the chain plainly.
    if (m_capturingSnapshot) {
        KWin::effects->drawWindow(renderTarget, viewport, w, mask, deviceRegion, data);
        return;
    }

    // Apply the C++ translate+scale geometry morph — UNLESS a shader
    // geometry-morph owns this window's visual transition. A morph shader
    // (one that declares iFromRect) interpolates the drawn rect itself and
    // cross-fades old->new content, so letting WindowAnimator::applyTransform
    // also translate+scale would double-transform the window. The animator's
    // animation still exists (it drives the morph's progress timeline); we
    // just skip its paint-data transform here.
    {
        const auto* morphSt = m_shaderManager.findTransition(w);
        const bool shaderOwnsGeometry = morphSt && morphSt->cached && morphSt->cached->iFromRectLoc >= 0;
        if (!shaderOwnsGeometry) {
            m_windowAnimator->applyTransform(w, data);
        }
    }

    auto* st = m_shaderManager.findTransition(w);
    if (st && st->cached && st->cached->shader) {
        // Non-const reference because the per-frame book-keeping (`frameCount`,
        // `lastPaintTimeMs`) advances on every paintWindow tick that feeds
        // the transition. Without the mutation, `iFrame` would stay at 0 and
        // `iTimeDelta` would always read 0.
        auto& transition = *st;
        // Phase 0: capture the OLD window content on the first morph frame,
        // before the moveResize configure round-trips and the client re-renders
        // at the new size. Capture-only frame — we render the snapshot into our
        // own FBO and return WITHOUT drawing the window to screen this frame, so
        // there is exactly one TOP-LEVEL on-screen effects->drawWindow per outer
        // paintWindow (the capture's own drawWindow renders only into the
        // snapshot FBO) — avoiding the chain-iterator re-entrancy that
        // ghost-trails the surface-extent path.
        // One skipped frame at the very start of a snap is imperceptible. If
        // capture fails, needsSnapshot is cleared inside the helper and the
        // morph shader falls back to no cross-fade.
        if (transition.needsSnapshot && !transition.oldSnapshot) {
            captureOldWindowSnapshot(transition, w);
            return;
        }
        // Two progress sources, picked by the transition's mode (see
        // ShaderTransition's docstring). Lifecycle events started via
        // tryBeginShaderForEvent set durationMs > 0 and drive progress from
        // monotonic steady-clock elapsed; the window.movement.* geometry events
        // that flow through applyWindowGeometry leave durationMs = 0 and ride the
        // m_windowAnimator timeline so the shader matches the geometry
        // animation.
        qreal progress = 0.0;
        bool active = false;
        if (transition.durationMs > 0) {
            // Shared with the backdrop capture (see timeDrivenProgress). This is
            // the site that OWNS the stateful curve's single per-frame step.
            progress = timeDrivenProgress(transition, frameNowMs, /*stepCurve=*/true, active);
        } else {
            const auto* anim = m_windowAnimator->animationFor(w);
            if (anim && anim->isAnimating()) {
                // Same clamp POLICY as the time-driven source (ShaderInternal::
                // easeProgress): an overshooting curve's value passes through raw,
                // everything else is clamped. Clamping here unconditionally — as
                // this once did — flattened iTime at 1.0 for exactly the
                // window.movement.* events whose GEOMETRY visibly bounces, so the
                // shader and the rect disagreed about the same curve. That is the
                // divergence the unclamped policy exists to prevent, and this was
                // the majority event class.
                progress = ShaderInternal::clampProgressForCurve(anim->state().value, anim->spec().profile.curve.get());
                active = true;
            }
        }
        // Flip the timeline for `going-away` events so a single user-
        // assigned shader covers both directions of a paired event:
        // window.open plays 0→1, window.close plays the same shader 1→0;
        // going-to-minimized plays 1→0 while unminimize plays 0→1; same
        // for maximize/unmaximize. The flip is symmetric about 0.5 and preserves
        // whatever range the curve produced — an overshooting curve leaves [0,1]
        // (1.08 mirrors to -0.08, the correct reversed overshoot), so this is NOT
        // a bound-preserving operation and must not be assumed to be one.
        if (transition.reverse) {
            progress = 1.0 - progress;
        }
        if (active) {
            const CachedShader* cached = transition.cached;
            KWin::GLShader* shader = cached->shader.get();

            // Animation-shader contract — see
            // `PhosphorAnimationShaders::AnimationShaderContract`. iTime
            // is 0..1 progress, iResolution is the surface size, and
            // per-effect declared parameters land in `customParams[N]`
            // slots populated at transition begin time by
            // `translateAnimationParams`. iTimeDelta / iFrame / iDate /
            // iMouse mirror the daemon's SurfaceAnimator semantics so a
            // single shader source observes equivalent state on either
            // runtime. Audio / multipass / texture uniforms are still
            // unpopulated on the kwin ANIMATION-transition path (window
            // open/close/move/…) — those need C++ wiring (CAVA subscription,
            // FBO chain, texture cache) that is out of scope here. NB: the
            // surface DECORATION path (persistent border packs) DOES wire audio
            // now via the effect's own CavaSpectrumProvider — see
            // bindSurfaceAudio in surfacelayers.cpp; this comment is only about
            // the transition shaders driven from this function.
            //
            // setUniform must run with the shader bound: KWin's
            // `GLShader::setUniform` calls `glUniform*` directly, which
            // writes into whichever program is active. KWin's
            // `OffscreenEffect::drawWindow` only binds our shader later
            // inside `OffscreenData::paint`'s ShaderBinder, so without
            // this push the writes either hit GL_INVALID_OPERATION or
            // land on the prior effect's program. Uniform values are
            // stored in the program object, so push → set → pop leaves
            // them in place for OffscreenEffect's subsequent bind.
            //
            // -1 from a uniformLocation lookup means the linker dropped
            // the uniform (unreferenced in GLSL). GL silently ignores
            // glUniform on -1, but the explicit `loc < 0` guard at every
            // call site makes the intent ("only push uniforms the shader
            // actually declared") explicit and survives a future GL
            // backend that doesn't honour the -1-is-noop convention.
            // iTimeDelta + iFrame are book-keeping that must advance every
            // tick regardless of whether the shader declares the
            // uniforms — they're inputs to the per-leg state machine.
            // `nowMs` is pinned to the frame clock (see frameNowMs at
            // the top of paintWindow) so multiple paint passes within
            // one cycle agree on the timestamp.
            const qint64 nowMs = frameNowMs;
            // Cap the frame delta at Limits::MaxShaderTimeDeltaSeconds, matching
            // the daemon's overlay push (overlayservice/shader.cpp) and the
            // SurfaceAnimator. A transition can live up to MaxAnimationDurationMs,
            // so a compositor stall or a suspend/resume mid-transition would
            // otherwise hand the shader a multi-second dt in a single frame and
            // jump every dt-integrated effect (particle motion, noise advance,
            // sparkle drift) far past where it should be. The -1 sentinel still
            // yields 0 on the first paint.
            const float iTimeDelta = (transition.lastPaintTimeMs < 0)
                ? 0.0f
                : qMin(static_cast<float>(nowMs - transition.lastPaintTimeMs) / 1000.0f,
                       PhosphorAnimation::Limits::MaxShaderTimeDeltaSeconds);
            transition.lastPaintTimeMs = nowMs;
            // Pin the GLSL contract: `iFrame` is declared `uniform int`
            // in animation_uniforms.glsl; bumping iFrameValue's type to
            // unsigned without updating the shader (or vice versa) would
            // silently reinterpret bit patterns at the SRB boundary.
            static_assert(std::is_same_v<decltype(transition.frameCount), int>,
                          "transition.frameCount must stay `int` to match GLSL `uniform int iFrame;`");
            const int iFrameValue = transition.frameCount++;
            const QRectF geo = w->frameGeometry();
            // Three rects feed the shader's geometry uniforms. They are
            // NOT interchangeable on the surface-extent path:
            //
            //   • anchorGeo   — the captured window = frame geometry. The
            //     "anchor" the shader's effect-space math is rigid about
            //     (bounce lifts THIS, fly-in slides THIS).
            //   • textureGeo  — the rect the shader's vTexCoord [0,1]
            //     spans. anchor-extent: KWin's redirected FBO (= the
            //     expanded rect). surface-extent: the whole output, since
            //     apply() lays an output-spanning quad.
            //   • expandedGeo — the rect uTexture0 itself covers. KWin's
            //     OffscreenEffect always redirects the whole window item
            //     INCLUDING decoration + shadow, so uTexture0 is the
            //     expanded rect, never the bare frame.
            //
            // computeAnchorUniforms(anchorGeo, textureGeo) drives
            // iResolution / iAnchorSize / iAnchorPosInFbo — the shader
            // uses those to convert vTexCoord into anchor [0,1] space
            // (anchorRemap).
            //
            // iAnchorRectInTexture is a SEPARATE uniform: it tells
            // surfaceColor() where the anchor sits inside uTexture0.
            // Surface-extent shaders sample with anchor-space [0,1]
            // coordinates, but uTexture0 spans the shadow-padded expanded
            // rect — sampling it at anchor [0,1] without this remap
            // stretches frame+shadow into the frame's screen region and
            // the window content animates smaller than it lands. Anchor-
            // extent shaders sample uTexture0 directly, so they get the
            // (0,0,1,1) identity.
            //
            // expandedGeometry is empty for a window with no decoration
            // or shadow extents; fall back to the frame there.
            const QRectF anchorGeo = geo;
            QRectF expandedGeo = w->expandedGeometry();
            if (expandedGeo.isEmpty()) {
                expandedGeo = geo;
            }
            QRectF textureGeo = expandedGeo;
            if (transition.surfaceExtent) {
                if (const auto* output = w->screen()) {
                    textureGeo = QRectF(output->geometry());
                }
            }
            const ShaderInternal::AnchorUniforms anchorUniforms =
                ShaderInternal::computeAnchorUniforms(anchorGeo, textureGeo);
            const QVector4D anchorRectInTexture = transition.surfaceExtent
                ? ShaderInternal::computeTextureSubRect(anchorGeo, expandedGeo)
                : QVector4D(0.0f, 0.0f, 1.0f, 1.0f);
            // uSurfaceLayer's own remap: the layer canvas is the expanded rect
            // inflated by the decoration chain's outer margin (the SAME canvas
            // renderSurfaceChainComposite captures into), so the rect that
            // surfaceColor's uv spans (the anchor for surface-extent, the
            // expanded texture for anchor-extent — mirroring
            // anchorRectInTexture's split) is located WITHIN that padded
            // canvas. Unpadded windows reduce to the uTexture0 mapping.
            qreal layerPad = 0.0;
            bool chainBakesOpacity = false;
            double foldedOpacity = 1.0;
            if (const auto lbIt = m_windowDecorations.constFind(getWindowId(w));
                lbIt != m_windowDecorations.constEnd()) {
                layerPad = lbIt->outerPadding;
                chainBakesOpacity = lbIt->chainBakesOpacity;
                foldedOpacity = lbIt->foldedOpacity;
            }
            // Surface-layer stack (border / rounded corners, ...): render the
            // window's active layers into an FBO so the animation composites
            // OVER the layered surface and the border stays visible for the
            // whole transition instead of vanishing when the animation shader
            // takes the draw slot. Runs BEFORE the animation shader is bound —
            // it briefly swaps the redirect's bound shader to the border and
            // back, so it must not sit inside the ShaderBinder scope below.
            // Null when the window has no surface layers (the common no-border
            // case), in which case surfaceColor() samples the bare uTexture0.
            // Pinned per-window scale, matching the rest path so the transition folds into the
            // same-sized canvas the cache already holds.
            KWin::GLTexture* const surfaceLayerTex = renderSurfaceChain(transition, w, windowSurfaceScale(w));
            // Prefer the composite's RECORDED canvas rect over recomputing
            // from live geometry: it describes the texture that actually
            // exists, which matters for a CLOSING (deleted) window whose
            // frozen composite is reused while its live geometry may drift.
            // Derived AFTER the fold above on purpose: the fold re-stamps
            // canvasGeo (and may reallocate the composite at a new size), so
            // reading it earlier would sample this frame's texture through
            // last frame's rect on every geometry-change frame. The live-
            // geometry fallback stays for the fold's failure paths, which can
            // erase the multipass entry.
            QRectF layerCanvasGeo = expandedGeo.adjusted(-layerPad, -layerPad, layerPad, layerPad);
            if (const auto lsIt = m_surfaceMultipass.find(getWindowId(w)); lsIt != m_surfaceMultipass.end()) {
                if (lsIt->second.canvasGeo.isValid()) {
                    layerCanvasGeo = lsIt->second.canvasGeo;
                }
            }
            const QVector4D layerRectInTexture = ShaderInternal::computeTextureSubRect(
                transition.surfaceExtent ? anchorGeo : expandedGeo, layerCanvasGeo);
            // Whether this draw bound the audio spectrum on kSurfaceAudioUnit —
            // read by the post-draw hygiene block to unbind it, mirroring the
            // fold's mainAudioBound/passAudioBound cleanup.
            bool audioBound = false;
            {
                KWin::ShaderBinder binder(shader);
                if (cached->iTimeLoc >= 0) {
                    shader->setUniform(cached->iTimeLoc, static_cast<float>(progress));
                }
                if (cached->iResolutionLoc >= 0) {
                    shader->setUniform(cached->iResolutionLoc, anchorUniforms.resolution);
                }
                if (cached->iTimeDeltaLoc >= 0) {
                    shader->setUniform(cached->iTimeDeltaLoc, iTimeDelta);
                }
                if (cached->iFrameLoc >= 0) {
                    shader->setUniform(cached->iFrameLoc, iFrameValue);
                }
                if (cached->iDateLoc >= 0) {
                    // iDate: local-time (year, month, day, seconds-since-
                    // midnight). Hoisted behind the loc>=0 guard so a
                    // shader that doesn't read iDate doesn't pay the
                    // QDateTime + QDate + QTime build cost on every
                    // paint (a 144 Hz display × multiple in-flight
                    // transitions would otherwise pay it dozens of
                    // times per frame).
                    //
                    // iDate.w decomposes seconds-since-midnight from
                    // hour/minute/second/msec rather than dividing
                    // `msecsSinceStartOfDay()` by 1000 — at 12:00 the
                    // raw msec count is ~43.2M, and a single-precision
                    // float divide there only resolves to ~4 ms steps
                    // (vs the ~1 µs steps produced by the decomposed
                    // form). Matches `PhosphorShaders::BaseUniformProfile`
                    // exactly so a shader that reads iDate.w sees the
                    // same value on both runtimes.
                    // 1Hz cache: re-decompose the QDateTime only when at
                    // least 1000 ms have elapsed since the last refresh
                    // (or this is the first paint to read iDate). Mirrors
                    // PhosphorShaders::BaseUniformProfile — sub-second iDate
                    // variation is invisible for typical shader use
                    // (clocks, time-of-day tints, etc.), and multiple
                    // in-flight transitions on a high-Hz display would
                    // otherwise pay the QDateTime / QDate / QTime build
                    // cost per transition per frame.
                    //
                    // Use shaderClockNowMs() (steady_clock) for the
                    // staleness gate even though the cached value itself
                    // is wall-clock-derived: a backwards NTP correction
                    // on the wall clock would push `nowMs` below
                    // `m_shaderManager.m_lastIDateRefreshMs`, the diff would go negative,
                    // and the cache would never refresh again until the
                    // wall clock caught back up. steady_clock guarantees
                    // monotonic increase, matching the rest of the
                    // shader-timing path. Reuses the outer-scope `nowMs`
                    // captured for iTimeDelta / iFrame above so all of
                    // this paint tick's monotonic readings come from the
                    // same clock sample. The `== 0` arm forces a decompose on
                    // the first paint that reads iDate (the refresh stamp starts
                    // at 0), so iDate is valid from frame one even under 1 s
                    // after boot, when the steady-clock `nowMs` can itself be
                    // below 1000.
                    if (m_shaderManager.m_lastIDateRefreshMs == 0
                        || nowMs - m_shaderManager.m_lastIDateRefreshMs >= 1000) {
                        const QDateTime nowDateTime = QDateTime::currentDateTime();
                        const QDate date = nowDateTime.date();
                        const QTime t = nowDateTime.time();
                        const float seconds = static_cast<float>(t.hour() * 3600 + t.minute() * 60 + t.second())
                            + static_cast<float>(t.msec()) / 1000.0f;
                        m_shaderManager.m_cachedIDate =
                            QVector4D(static_cast<float>(date.year()), static_cast<float>(date.month()),
                                      static_cast<float>(date.day()), seconds);
                        m_shaderManager.m_lastIDateRefreshMs = nowMs;
                    }
                    shader->setUniform(cached->iDateLoc, m_shaderManager.m_cachedIDate);
                }
                if (cached->iMouseLoc >= 0) {
                    // iMouse: cursor position in window-local logical
                    // pixels (.xy), with `(-1, -1)` sentinel when the
                    // cursor is outside the window's frame. .zw carry
                    // the same position normalised to surface size,
                    // computed unconditionally from the same xy values
                    // — matches the daemon's animation-shader iMouse
                    // semantics: the `(-1, -1)` sentinel is applied at
                    // the higher layer by `SurfaceAnimator` via its
                    // `QQuickHoverHandler` (see
                    // `surfaceanimator.cpp::seedShaderUniformsAtAttach`
                    // — when the handler reports `!isHovered()` the
                    // animator pushes `setIMouse(QPointF(-1, -1))`).
                    // The rendering layer's
                    // `ShaderNodeRhi::syncBaseUniforms` itself always
                    // writes the live position; the sentinel is
                    // applied at the higher layer. We synthesise the
                    // same sentinel here directly because there is no
                    // hover-handler equivalent in the KWin-effect
                    // pipeline. Hoisted behind the loc>=0 guard for
                    // the same reason as iDate above.
                    //
                    // Edge inclusivity: exclusive on right/bottom edges
                    // — matches `QRectF::contains` parity with the
                    // daemon's `ShaderNodeRhi::setMousePosition`. With
                    // inclusive edges, a cursor parked exactly on the
                    // boundary between two abutting outputs would
                    // register as inside both windows simultaneously,
                    // and the resulting iMouse uniform would diverge
                    // from the daemon's. Spell out the exclusive check
                    // so the sentinel synthesis matches QRectF.
                    // Cursor pos cached once per prePaintScreen — paintWindow
                    // is called per active transition (and per output), so
                    // re-reading KWin::effects->cursorPos() per call would
                    // multiply up across high-Hz multi-output setups.
                    const QPointF cursorGlobal = m_shaderManager.m_cachedCursorGlobal;
                    float localX = -1.0f;
                    float localY = -1.0f;
                    const bool inside = cursorGlobal.x() >= geo.x() && cursorGlobal.x() < geo.x() + geo.width()
                        && cursorGlobal.y() >= geo.y() && cursorGlobal.y() < geo.y() + geo.height();
                    if (inside) {
                        localX = static_cast<float>(cursorGlobal.x() - geo.x());
                        localY = static_cast<float>(cursorGlobal.y() - geo.y());
                    }
                    QVector4D iMouseValue(localX, localY, 0.0f, 0.0f);
                    if (geo.width() > 0.0)
                        iMouseValue.setZ(localX / static_cast<float>(geo.width()));
                    if (geo.height() > 0.0)
                        iMouseValue.setW(localY / static_cast<float>(geo.height()));
                    shader->setUniform(cached->iMouseLoc, iMouseValue);
                }
                if (cached->iIsReversedLoc >= 0) {
                    shader->setUniform(cached->iIsReversedLoc, transition.reverse ? 1 : 0);
                }
                if (cached->iSurfaceScreenPosLoc >= 0) {
                    // (surfaceX, surfaceY, screenW, screenH) in logical pixels.
                    // Window position is the redirected surface origin on
                    // screen; screen size is the logical output's geometry.
                    // Kept in classic-GL setUniform form here — the UBO-
                    // extension isolation that protects the daemon's zone
                    // shaders from BaseUniforms growth doesn't apply on this
                    // runtime (kwin uses default-block uniforms, no UBO).
                    // .xy is the redirected surface's origin — the same
                    // anchor rect the anchorRemap uniforms describe, so
                    // fly-in's edge-distance math (iSurfaceScreenPos +
                    // iAnchorSize) stays internally consistent.
                    QVector4D surfaceScreenPos(static_cast<float>(anchorGeo.x()), static_cast<float>(anchorGeo.y()),
                                               0.0f, 0.0f);
                    if (const auto* output = w->screen()) {
                        const QRect screenGeo = output->geometry();
                        surfaceScreenPos.setZ(static_cast<float>(screenGeo.width()));
                        surfaceScreenPos.setW(static_cast<float>(screenGeo.height()));
                    }
                    shader->setUniform(cached->iSurfaceScreenPosLoc, surfaceScreenPos);
                }
                if (cached->iAnchorSizeLoc >= 0) {
                    // The captured window ("anchor") size. For anchor-
                    // extent transitions this equals iResolution; for
                    // surface-extent transitions it stays the window size
                    // while iResolution grows to the output.
                    shader->setUniform(cached->iAnchorSizeLoc, anchorUniforms.anchorSize);
                }
                if (cached->iAnchorPosInFboLoc >= 0) {
                    // The window's top-left within the shader's render
                    // target. (0, 0) for anchor-extent transitions, where
                    // the FBO covers the window 1:1 and `anchorRemap`
                    // collapses to identity. For surface-extent transitions
                    // (bounce, fly-in, broken-glass, morph) it is the
                    // window's offset within its output, so `anchorRemap`
                    // converts surface-UV back to anchor-space — matching
                    // the daemon's surface-sized-FBO path.
                    shader->setUniform(cached->iAnchorPosInFboLoc, anchorUniforms.anchorPosInFbo);
                }
                if (cached->iWindowOpacityLoc >= 0) {
                    // SetOpacity is layer-backed: only a chain carrying the
                    // plain opacity-tint layer bakes the window's opacity
                    // into its composite (the folded opacity param), and when
                    // the fold produced the composite this shader samples
                    // (surfaceLayerTex) the alpha is already in it —
                    // re-applying here would dim twice. So push 1.0
                    // everywhere EXCEPT the bare-uTexture0 fallback of an
                    // opacity-baking chain: the fold didn't run there, so
                    // this push is the only dim the window gets for the
                    // transition's duration. The decoration's foldedOpacity
                    // carries the resolved value for BOTH sources (config
                    // default with the SetOpacity rule winning); the per-frame
                    // rule cache refines it when present, but is populated
                    // only while opacity RULES are loaded, so it cannot be
                    // the sole source — config-only opacity would render
                    // opaque here. Custom chains and undecorated windows
                    // never honour the setting, so they stay at 1.0 outright.
                    float winOpacity = 1.0f;
                    if (chainBakesOpacity && !surfaceLayerTex) {
                        winOpacity = static_cast<float>(qBound(0.0, foldedOpacity, 1.0));
                        if (m_shaderManager.frameOpacityCached(w)) {
                            if (const auto cachedOpacity = m_shaderManager.cachedFrameOpacity(w)) {
                                winOpacity = static_cast<float>(*cachedOpacity);
                            }
                        }
                    }
                    shader->setUniform(cached->iWindowOpacityLoc, winOpacity);
                }
                if (cached->iAnchorRectInTextureLoc >= 0) {
                    // The anchor's UV sub-rect within uTexture0 (KWin's
                    // redirected expanded-geometry FBO). surfaceColor()
                    // maps the shader's anchor-space sample coordinate
                    // through this, so a surface-extent transition samples
                    // the frame's sub-region of the shadow-padded texture
                    // instead of the whole texture (which would render the
                    // window smaller than its frame). For anchor-extent
                    // transitions the value is (0, 0, 1, 1) — set above —
                    // and surfaceColor() is an identity passthrough.
                    shader->setUniform(cached->iAnchorRectInTextureLoc, anchorRectInTexture);
                }
                for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams; ++slot) {
                    const int loc = cached->customParamsLoc[slot];
                    if (loc < 0)
                        continue; // shader didn't declare / reference this slot
                    shader->setUniform(loc, transition.customParamsValues[slot]);
                }
                for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors; ++slot) {
                    const int loc = cached->customColorsLoc[slot];
                    if (loc < 0)
                        continue;
                    shader->setUniform(loc, transition.customColorsValues[slot]);
                }
                // Held-move motion state: integrate the underdamped velocity
                // spring on the frame clock and publish it. springLag tracks
                // the instantaneous frame velocity through a spring (k, c
                // slightly underdamped), so after the pointer stops or
                // releases the published velocity rings down through zero and
                // a velocity-driven deformation settles like a real object.
                // toGeometry mirrors the live frame each frame so a vertex
                // stage can anchor to the current rect via iToRect.
                // fromGeometry is anchored once at the grab (drag-start
                // hookup in window_lifecycle.cpp) so rect-driven packs read
                // a valid iFromRect instead of the zero vec4; it is not
                // re-mirrored here — the held leg is not a morph.
                if (transition.holdUntilRelease) {
                    const QPointF pos = w->frameGeometry().topLeft();
                    if (transition.lastMoveSampleMs >= 0) {
                        const qreal dt = qBound(0.0, qreal(nowMs - transition.lastMoveSampleMs) / 1000.0, 0.05);
                        if (dt > 0.0) {
                            const QPointF inst = (pos - transition.lastMovePos) / dt;
                            constexpr qreal kSpring = 90.0; // rad^2 — tracking stiffness
                            constexpr qreal kDamp = 12.0; // < 2*sqrt(kSpring): underdamped
                            transition.springVel +=
                                ((inst - transition.springLag) * kSpring - transition.springVel * kDamp) * dt;
                            transition.springLag += transition.springVel * dt;
                            // Loose sibling: lower stiffness, lighter damping
                            // (zeta ~0.5) — trails the tight spring and rings
                            // longer, the phase-spread source for jelly packs.
                            constexpr qreal kSpring2 = 30.0;
                            constexpr qreal kDamp2 = 5.5;
                            transition.springVel2 +=
                                ((inst - transition.springLag2) * kSpring2 - transition.springVel2 * kDamp2) * dt;
                            transition.springLag2 += transition.springVel2 * dt;
                        }
                    }
                    transition.lastMovePos = pos;
                    transition.lastMoveSampleMs = nowMs;
                    transition.toGeometry = w->frameGeometry();
                    // Motion-history ring (iMoveTrail): record the origin on a
                    // fixed cadence so the shader can sample the drag PATH at a
                    // per-vertex delay. Pure bookkeeping — a paint stall longer
                    // than the whole ring just refills it with the current
                    // position (the window's past is unknown, and "no motion"
                    // is the artifact-free guess).
                    if (transition.trailLastMs < 0
                        || nowMs - transition.trailLastMs
                            >= qint64(ShaderTransition::kTrailStepMs * ShaderTransition::kTrailSlots)) {
                        for (int k = 0; k < ShaderTransition::kTrailSlots; ++k) {
                            transition.moveTrail[k] = pos;
                        }
                        transition.trailLastMs = nowMs;
                    }
                    while (nowMs - transition.trailLastMs >= qint64(ShaderTransition::kTrailStepMs)) {
                        for (int k = ShaderTransition::kTrailSlots - 1; k > 0; --k) {
                            transition.moveTrail[k] = transition.moveTrail[k - 1];
                        }
                        transition.moveTrail[0] = pos;
                        transition.trailLastMs += qint64(ShaderTransition::kTrailStepMs);
                    }
                }
                // Advance the soft-body lattice every active frame (held AND
                // settling-after-release), so it integrates and eventually
                // reports settled to release the transition. Outside the
                // hold block above because that block stops running the
                // instant the drag ends.
                if (transition.meshSim.initialized) {
                    const qint64 mdt = (transition.meshLastMs < 0) ? 0 : (nowMs - transition.meshLastMs);
                    transition.meshLastMs = nowMs;
                    const bool wasSettled = transition.meshSim.settled;
                    ShaderInternal::stepMeshSim(transition.meshSim, w->frameGeometry(), static_cast<qreal>(mdt));
                    // Request ONE more frame on the settle edge. The lattice flips
                    // `settled` here, INSIDE the paint — but postPaintScreen has
                    // already decided whether to inject a repaint for this window, and
                    // its `heldActive` arm reads the same flag. So on the frame a
                    // wobble settles, nothing schedules the next paint, and the expiry
                    // teardown (which only runs from paintWindow, on a frame where the
                    // leg is inactive) never gets one. The leg then survives on the
                    // 4 s mesh SAFETY cap — which exists for a lattice that never
                    // settles, not for the normal path — holding its redirect, its
                    // shader, and the whole compositor's transformed-windows paint
                    // path for ~3.5 s after every single drag.
                    //
                    // Deliberately NOT m_selfRepainting-flagged, unlike the other
                    // repaints the effect issues to drive its own animation. This one
                    // fires inside a live transition, where the capture cache is off
                    // anyway (captureCacheable excludes a transition, which supplies its
                    // own restore shader), so there is no cache for it to invalidate —
                    // and it is a one-shot settle edge, not a per-frame driver. Flag it
                    // if either of those ever stops being true.
                    if (!wasSettled && transition.meshSim.settled) {
                        w->addRepaintFull();
                    }
                }
                if (cached->iMoveMeshLoc >= 0) {
                    // Publish node deflections from ideal grid position, px.
                    // Zero for a lattice that never ran (non-held / uninit).
                    GLfloat mesh[MeshSim::kCount * 2] = {};
                    if (transition.meshSim.initialized) {
                        for (int n = 0; n < MeshSim::kCount; ++n) {
                            const QPointF d = transition.meshSim.position[n] - transition.meshSim.origin[n];
                            mesh[2 * n] = static_cast<GLfloat>(d.x());
                            mesh[2 * n + 1] = static_cast<GLfloat>(d.y());
                        }
                    }
                    glUniform2fv(cached->iMoveMeshLoc, MeshSim::kCount, mesh);
                }
                if (cached->iMoveVelocityLoc >= 0) {
                    shader->setUniform(cached->iMoveVelocityLoc,
                                       QVector2D(static_cast<float>(transition.springLag.x()),
                                                 static_cast<float>(transition.springLag.y())));
                }
                if (cached->iMoveVelocity2Loc >= 0) {
                    shader->setUniform(cached->iMoveVelocity2Loc,
                                       QVector2D(static_cast<float>(transition.springLag2.x()),
                                                 static_cast<float>(transition.springLag2.y())));
                }
                if (cached->iMoveTrailLoc >= 0) {
                    // Upload relative to the CURRENT origin: slot k = where the
                    // window was ~k*15 ms ago, as an offset from where it is
                    // now. All zeros at rest and for non-held transitions.
                    GLfloat trail[ShaderTransition::kTrailSlots * 2] = {};
                    if (transition.holdUntilRelease && transition.trailLastMs >= 0) {
                        const QPointF cur = w->frameGeometry().topLeft();
                        for (int k = 0; k < ShaderTransition::kTrailSlots; ++k) {
                            trail[2 * k] = static_cast<GLfloat>(transition.moveTrail[k].x() - cur.x());
                            trail[2 * k + 1] = static_cast<GLfloat>(transition.moveTrail[k].y() - cur.y());
                        }
                    }
                    glUniform2fv(cached->iMoveTrailLoc, ShaderTransition::kTrailSlots, trail);
                }
                if (cached->iMoveOffsetLoc >= 0) {
                    const QPointF off = transition.holdUntilRelease
                        ? (w->frameGeometry().topLeft() - transition.grabOrigin)
                        : QPointF();
                    shader->setUniform(cached->iMoveOffsetLoc,
                                       QVector2D(static_cast<float>(off.x()), static_cast<float>(off.y())));
                }
                // Geometry-morph endpoints. The window already jumped to its
                // destination via moveResize; the morph shader animates the
                // visual transition by interpolating its drawn content from
                // the old frame (iFromRect) to the new frame (iToRect) by
                // iTime. Pushed in logical screen pixels (x, y, width, height).
                // Non-morph transitions leave from/toGeometry default-invalid
                // → zero vec4s, which morph shaders read as "no morph". Guarded
                // on loc >= 0 so non-morph shaders (which don't declare these)
                // pay nothing.
                if (cached->iFromRectLoc >= 0) {
                    const QRectF& f = transition.fromGeometry;
                    shader->setUniform(cached->iFromRectLoc, QVector4D(f.x(), f.y(), f.width(), f.height()));
                }
                if (cached->iToRectLoc >= 0) {
                    const QRectF& t = transition.toGeometry;
                    shader->setUniform(cached->iToRectLoc, QVector4D(t.x(), t.y(), t.width(), t.height()));
                }
                // Task-manager icon rect for minimize-to-icon packs
                // (genie). Same space and same per-frame push discipline
                // as the morph rects above — the shader program is shared
                // across windows, so an install-time push would let two
                // concurrent transitions on the same pack clobber each
                // other's target. Null rect (no task-manager entry)
                // pushes (0, 0, 0, 0) = "no icon target".
                if (cached->iIconRectLoc >= 0) {
                    const QRectF& ic = transition.iconRect;
                    shader->setUniform(cached->iIconRectLoc, QVector4D(ic.x(), ic.y(), ic.width(), ic.height()));
                }
                // User textures: bind each cached GLTexture to texture
                // unit (1 + slot) — TEXTURE0 holds the redirected window
                // texture KWin's OffscreenData::paint binds during
                // drawWindow. Push the matching sampler uniform so the
                // shader knows which unit to read; populate
                // iTextureResolution[slot] so shaders that key on texture
                // size (e.g. tile-grid shaders like Matrix's glyph atlas)
                // can compute their own UV math without authors hard-
                // coding bitmap dimensions.
                //
                // Order matters: setUniform addresses program-object
                // state, glActiveTexture+bind addresses GL state. Both
                // need to be active when KWin's drawWindow issues its
                // glDraw* calls; ShaderBinder keeps the program bound,
                // and texture-unit binds outlive the program switch
                // OffscreenData::paint performs internally (program
                // switches don't reset texture-unit bindings).
                for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots;
                     ++slot) {
                    CachedTexture* entry = transition.userTextures[slot];
                    if (!entry || !entry->texture) {
                        // Referenced but unsupplied (declared file failed to
                        // load, or the pack samples a slot it never declared):
                        // bind the shared 1x1 transparent fallback so the
                        // sampler reads the contract's transparent black. A
                        // classic default-block sampler with no bind defaults
                        // to unit 0 — the redirected window — which would
                        // silently composite live content where the pack
                        // expects nothing.
                        if (cached->userTextureLoc[slot] >= 0) {
                            // Park the destination unit BEFORE the call: the
                            // first-ever call creates the texture, and
                            // GLTexture::upload binds it on the CURRENTLY
                            // ACTIVE unit — the previous iteration may have
                            // left an earlier slot's unit active. Same
                            // discipline as the audio upload below.
                            glActiveTexture(GL_TEXTURE1 + slot);
                            if (KWin::GLTexture* fallback = transparentFallbackTexture()) {
                                shader->setUniform(cached->userTextureLoc[slot], 1 + slot);
                                fallback->bind();
                            }
                        }
                        // The fallback is a 1x1 transparent, so this slot's
                        // declared size is zero. Push it explicitly: this
                        // cached program persists uniform state across
                        // transitions, so a slot that carried a real texture
                        // (and its size) on a prior leg would otherwise leave a
                        // stale non-zero iTextureResolution here.
                        if (cached->iTextureResolutionLoc[slot] >= 0) {
                            shader->setUniform(cached->iTextureResolutionLoc[slot], QVector4D(0.0f, 0.0f, 0.0f, 0.0f));
                        }
                        continue;
                    }
                    KWin::GLTexture* tex = entry->texture.get();
                    if (cached->userTextureLoc[slot] >= 0) {
                        shader->setUniform(cached->userTextureLoc[slot], 1 + slot);
                    }
                    if (cached->iTextureResolutionLoc[slot] >= 0) {
                        const QSize sz = tex->size();
                        shader->setUniform(cached->iTextureResolutionLoc[slot],
                                           QVector4D(sz.width(), sz.height(), 0.0f, 0.0f));
                    }
                    glActiveTexture(GL_TEXTURE1 + slot);
                    tex->bind();
                    // Wrap mode lives on the cached `GLTexture`'s GL
                    // state — apply the per-leg value so two transitions
                    // sharing the same path can run with different wrap
                    // modes without invalidating each other. Skip the
                    // setWrapMode call when the cache entry's last-
                    // applied wrap matches the transition's target;
                    // setWrapMode issues two `glTexParameteri`s and
                    // would otherwise fire on every paintWindow tick.
                    const GLenum wantWrap = transition.userTextureWrap[slot];
                    if (entry->lastAppliedWrap != wantWrap) {
                        tex->setWrapMode(wantWrap);
                        entry->lastAppliedWrap = wantWrap;
                    }
                }
                // Old-content snapshot (uOldWindow). Bound to a dedicated unit
                // just past the user-texture slots so the morph shader can
                // cross-fade the captured old content against the live
                // redirected content (uTexture0). Wrap mode is set once at
                // capture time (Phase 0), so no per-frame setWrapMode here.
                // Only when the shader reads it AND a snapshot was captured;
                // otherwise the shader's uOldWindow reads unit 0 / transparent
                // and a morph shader falls back to no cross-fade.
                if (cached->iOldWindowLoc >= 0 && transition.oldSnapshot) {
                    constexpr int kOldSnapshotUnit = ShaderInternal::kOldSnapshotUnit;
                    shader->setUniform(cached->iOldWindowLoc, kOldSnapshotUnit);
                    glActiveTexture(GL_TEXTURE0 + kOldSnapshotUnit);
                    transition.oldSnapshot->bind();
                } else if (cached->iOldWindowLoc >= 0) {
                    // The cached program is SHARED per effect across windows and
                    // transitions. A previous run WITH a snapshot left uOldWindow
                    // pointing at the snapshot unit; this snapshot-less leg (every
                    // close) would then sample an UNBOUND unit — opaque black on
                    // NVIDIA — and a cross-fade shader blends a full black quad in
                    // at its old-content weight. Restore the documented unit-0
                    // fallback (the live redirect) every snapshot-less frame.
                    shader->setUniform(cached->iOldWindowLoc, 0);
                }
                // Snapshot-presence flag: old-content samplers gate on this and
                // fall back to surfaceColor() when 0 — the unit-0 alias above is
                // the RAW undecorated window, and cross-fading from it blanked
                // every decoration pack for the fade-in of snapshot-less
                // lifecycle transitions (window.move at every drag start).
                // Always push: the shared program otherwise carries the flag
                // from whichever transition ran last.
                if (cached->iHasOldWindowLoc >= 0) {
                    shader->setUniform(cached->iHasOldWindowLoc, transition.oldSnapshot ? 1 : 0);
                }
                // Surface-layer stack (uSurfaceLayer). When renderSurfaceChain
                // composited the window's layers (border / rounded corners, ...)
                // into an FBO, bind it to a dedicated unit just past the
                // old-snapshot slot and flag surfaceColor() to sample it in place
                // of the bare uTexture0 — the animation then runs OVER the
                // layered surface. Always push the flag (0 when no layer) so a
                // window with no surface layers animates uTexture0 unchanged.
                if (cached->iHasSurfaceLayerLoc >= 0) {
                    shader->setUniform(cached->iHasSurfaceLayerLoc, surfaceLayerTex ? 1 : 0);
                }
                if (cached->iLayerRectInTextureLoc >= 0) {
                    shader->setUniform(cached->iLayerRectInTextureLoc, layerRectInTexture);
                }
                constexpr int kSurfaceLayerUnit = ShaderInternal::kSurfaceLayerUnit;
                // uTexture0 RETARGET: when the composite canvas maps 1:1 onto
                // the window texture (unpadded chain, anchor-extent draw —
                // the layer rect is the identity), point the shader's window
                // sampler at the composite unit. Every sample then reads the
                // DECORATED window, including the many bundled packs that
                // sample uTexture0 raw and bypass surfaceColor() — without
                // this, those shaders drop the border / frost / glass for
                // the whole transition. Padded chains (glow) keep unit 0:
                // their canvas is inflated, so a raw 1:1 sample would land
                // misaligned; surfaceColor()'s remap remains the only
                // correct path there, exactly as before.
                const bool layerIdentity = surfaceLayerTex != nullptr && !transition.surfaceExtent
                    && qAbs(layerRectInTexture.x()) < 0.002f && qAbs(layerRectInTexture.y()) < 0.002f
                    && qAbs(layerRectInTexture.z() - 1.0f) < 0.004f && qAbs(layerRectInTexture.w() - 1.0f) < 0.004f;
                const bool retargetUTexture0 = layerIdentity && cached->uTexture0Loc >= 0;
                if (surfaceLayerTex && (cached->uSurfaceLayerLoc >= 0 || retargetUTexture0)) {
                    if (cached->uSurfaceLayerLoc >= 0) {
                        shader->setUniform(cached->uSurfaceLayerLoc, kSurfaceLayerUnit);
                    }
                    glActiveTexture(GL_TEXTURE0 + kSurfaceLayerUnit);
                    surfaceLayerTex->bind();
                }
                // ALWAYS push (0 when not retargeting): the program is shared
                // across windows and a stale retarget from another window
                // would otherwise leak into this draw.
                if (cached->uTexture0Loc >= 0) {
                    shader->setUniform(cached->uTexture0Loc, retargetUTexture0 ? kSurfaceLayerUnit : 0);
                }
                // Audio spectrum (opt-in via the audio.glsl module + metadata
                // `"audio": true`). Reuses the surface-decoration CAVA
                // provider and texture: bindSurfaceAudio pushes the bar count
                // (0 while the visualizer is off or cava is absent, so the
                // pack's helpers render static) and binds the spectrum at
                // kSurfaceAudioUnit, clear of this draw's units 0..5.
                if (cached->iAudioSpectrumSizeLoc >= 0 || cached->uAudioSpectrumLoc >= 0) {
                    // Park the active unit on the audio unit BEFORE the
                    // (possibly dirty) upload: GLTexture::upload binds the
                    // fresh texture on the CURRENTLY ACTIVE unit, and the
                    // surface-layer bind above leaves kSurfaceLayerUnit
                    // active — an upload there would replace the layered
                    // surface with the tiny spectrum texture for this draw.
                    // Same hazard the fold documents (surfacelayers.cpp),
                    // which defends by uploading up front; here the upload
                    // lands on the audio unit, its destination anyway.
                    glActiveTexture(GL_TEXTURE0 + ShaderInternal::kSurfaceAudioUnit);
                    ensureAudioSpectrumTexture();
                    // A live window transition is the thing being watched, so it always animates.
                    bindSurfaceAudio(shader, cached->iAudioSpectrumSizeLoc, cached->uAudioSpectrumLoc,
                                     /*animating=*/true);
                    // Unconditional, NOT bindSurfaceAudio's return: a dirty
                    // upload above can leave the spectrum texture bound on
                    // the parked unit even when the bind reports not-live
                    // (size-only shader, audio toggled off mid-frame), and
                    // unbinding a clear unit in the hygiene block is a
                    // harmless no-op.
                    audioBound = true;
                }
                // Restore TEXTURE0 as the active unit so KWin's
                // OffscreenData::paint binds the redirected surface
                // to the unit it expects (its `m_texture->bind()` runs
                // without a preceding glActiveTexture).
                glActiveTexture(GL_TEXTURE0);
            }
            // Surface-extent transitions paint an output-spanning quad in
            // `apply()`. `OffscreenData::paint` enables GL_SCISSOR_TEST
            // when `deviceRegion != Region::infinite()`, and the
            // deviceRegion KWin hands us is the intersection of the
            // viewport and the window's expanded bounds — i.e. the
            // natural window rect. That scissor would clip the
            // output-spanning quad back to the window's frame, hiding
            // everything the surface-extent fragment shader paints in the
            // off-frame band (bounce dropping in from above, fly-in
            // sliding in from the edge). Pass Region::infinite() to
            // bypass the scissor for surface-extent transitions; the
            // spanning quad is already bounded by the output's clip
            // space. Anchor-extent transitions keep KWin's natural
            // deviceRegion since their quad sits inside the window.
            // Padded decorated windows also bypass the scissor on the
            // anchor-extent path: apply() grows their quad by the decoration's
            // outer margin, and KWin's natural deviceRegion (the window's
            // expanded bounds) would scissor the halo band right back off.
            const KWin::Region drawRegion =
                (transition.surfaceExtent || layerPad > 0.0) ? KWin::Region::infinite() : deviceRegion;
            // Enter the draw chain via `effects->drawWindow` rather than
            // calling `OffscreenEffect::drawWindow` directly. KWin's
            // `EffectsHandler::drawWindow` advances the shared
            // `m_currentDrawWindowIterator` as it walks the chain; a
            // direct base-class call leaves that iterator parked at
            // `begin()`. Then `OffscreenData::maybeRender` (inside
            // OffscreenEffect::drawWindow) calls `effects->drawWindow`
            // to capture the window into its FBO — and with the iterator
            // still at `begin()` that capture restarts the chain from
            // the top and RE-ENTERS our own OffscreenEffect::drawWindow.
            // The re-entrant call draws the offscreen texture back into
            // itself (the FBO is the bound render target during capture)
            // translated by the vertex stage: a feedback loop that
            // produced the ghost trails and the creeping black band.
            // Routing through `effects->drawWindow` leaves the iterator
            // positioned past us, so `maybeRender`'s capture continues
            // to `finalDrawWindow` (the real window) exactly as it does
            // for a normally-chained OffscreenEffect. The redirected
            // window is bound to this effect via `redirect()`/`setShader()`,
            // so the chain still reaches our OffscreenEffect::drawWindow
            // override — just once, with the iterator correct.
            KWin::effects->drawWindow(renderTarget, viewport, w, mask, drawRegion, data);
            // Hygiene: unbind our user textures from TEXTURE1+. Each
            // effect in the chain assumes TEXTURE0 is the only active
            // unit; leaving stale binds risks the next effect inheriting
            // a sampler that points at our matrix glyph atlas (or
            // whatever) when it expects a default-empty unit. A slot with a
            // resolved sampler location but no cached texture bound the
            // transparent fallback above, so it needs the same unbind.
            for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots; ++slot) {
                if (!transition.userTextures[slot] && cached->userTextureLoc[slot] < 0) {
                    continue;
                }
                glActiveTexture(GL_TEXTURE1 + slot);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            // Same hygiene for the old-content snapshot unit (uOldWindow),
            // bound just past the user-texture slots above for morph
            // transitions — don't leave it dangling for the next effect.
            if (cached->iOldWindowLoc >= 0 && transition.oldSnapshot) {
                constexpr int kOldSnapshotUnit = ShaderInternal::kOldSnapshotUnit;
                glActiveTexture(GL_TEXTURE0 + kOldSnapshotUnit);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            // Same hygiene for the surface-layer unit (uSurfaceLayer), bound one
            // unit past the old-snapshot slot — don't leave the layered surface
            // dangling on its unit for the next effect in the chain. Match the
            // bind guard above, which also binds this unit on the retargetUTexture0
            // path (a raw-uTexture0 pack whose linker dropped uSurfaceLayer, so
            // uSurfaceLayerLoc < 0). The unit is dedicated to the surface layer, so
            // clearing it when it was never bound is a harmless no-op.
            if (surfaceLayerTex) {
                constexpr int kSurfaceLayerUnit = ShaderInternal::kSurfaceLayerUnit;
                glActiveTexture(GL_TEXTURE0 + kSurfaceLayerUnit);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            // Same hygiene for the audio-spectrum unit — the fold unbinds its
            // audio bind after every pass (mainAudioBound / passAudioBound);
            // mirror that so the next effect in the chain doesn't inherit the
            // spectrum texture on kSurfaceAudioUnit.
            if (audioBound) {
                glActiveTexture(GL_TEXTURE0 + ShaderInternal::kSurfaceAudioUnit);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            glActiveTexture(GL_TEXTURE0);
            return;
        }
        // Expiry fall-through: the transition is past its duration but
        // still installed. Tearing it down synchronously here would
        // unredirect the window mid-paint; the current frame would
        // then render UN-redirected and the user would see a
        // one-frame surface flash before the next frame stabilises.
        // Defer the teardown to a queued slot so the current paint
        // cycle still consumes the redirected (final-progress) state,
        // and the unredirect runs before the next paint. The pending-
        // set guard prevents the next frame (which lands before the
        // queued slot runs) from re-queuing a duplicate end and
        // double-tearing-down the same transition.
        if (!m_shaderManager.m_pendingShaderExpiryEnd.contains(w)) {
            m_shaderManager.m_pendingShaderExpiryEnd.insert(w);
            QPointer<KWin::EffectWindow> safeWindow(w);
            // Stash the raw pointer too — used as the set key for
            // membership cleanup. We MUST NOT remove the entry if the
            // QPointer has been cleared: the EffectWindow at that
            // address may have been destroyed by KWin and a fresh
            // window allocated at the same address before this queued
            // slot runs. The windowDeleted handler already calls
            // endShaderTransition for the dying window (which removes
            // the matching pending-expiry entry), so a null QPointer
            // means cleanup already happened — removing again would
            // wipe the new window's freshly-inserted entry. The raw
            // pointer is only used for set membership cleanup (never
            // dereferenced unless the QPointer is still live).
            KWin::EffectWindow* rawWindow = w;
            // Capture the EXPIRING transition's generation at queue-time
            // so the queued lambda can confirm the transition it sees on
            // dispatch is still the same one we observed as expired.
            // Race window: between this queue and the lambda firing, a
            // fresh `beginShaderTransition` may install a SUCCESSOR at
            // the same EffectWindow* (e.g. window.focus retriggers while
            // window.maximize was on its expiry frame). Without the
            // generation check the lambda would call
            // `endShaderTransition` on the successor and kill it before
            // it ever paints. Mirrors the timer-driven teardown pattern
            // in `tryBeginShaderForEvent`'s post-install QTimer::singleShot.
            // Pointer `st` is provably non-null here: the enclosing
            // `if (st && st->cached && st->cached->shader)` guard upstream
            // already gated this branch on `st`, so the read is safe in
            // both debug and release builds without a redundant Q_ASSERT
            // (which only documented the contract in debug).
            const quint64 expiringGeneration = st->generation;
            QMetaObject::invokeMethod(
                this,
                [this, safeWindow, rawWindow, expiringGeneration]() {
                    if (!safeWindow) {
                        // Window died; windowDeleted already removed
                        // the entry. Don't risk wiping an entry that
                        // belongs to a successor sharing this address.
                        return;
                    }
                    // Remove unconditionally so the pending-set entry
                    // doesn't leak when the transition was already
                    // ended via a different path (synchronous teardown,
                    // windowDeleted, generation-mismatch successor).
                    m_shaderManager.m_pendingShaderExpiryEnd.remove(rawWindow);
                    if (const auto* live = m_shaderManager.findTransition(safeWindow.data());
                        live && live->generation == expiringGeneration) {
                        endShaderTransition(safeWindow.data());
                    }
                    // else: a successor replaced us (last-event-wins) and
                    // owns its own teardown — leave it alone.
                },
                Qt::QueuedConnection);
        }
    }

    // Decoration fold for every (non-transition) decorated window. It runs
    // HERE, not in drawWindow: the fold's capture re-enters the draw chain
    // (effects->drawWindow), which MUST happen on a fresh draw-window
    // iterator — re-entering from inside the drawWindow override corrupts
    // KWin's iterator mid-walk (crash in the following
    // OffscreenEffect::drawWindow). The override then only BINDS the ready
    // composite for the present blit. The pre-gate short-circuits the whole thing on a
    // desktop with no decorations at all, before any map lookup.
    if (!m_capturingSnapshot && !m_windowDecorations.isEmpty() && !m_shaderManager.findTransition(w)) {
        const auto bit = m_windowDecorations.constFind(getWindowId(w));
        if (bit != m_windowDecorations.constEnd() && bit->shaderApplied) {
            // Composite the whole chain into the per-window FBO (each pack's
            // main runs as an FBO pass); drawWindow presents the final slot
            // through the passthrough present shader. EVERY decorated window
            // takes this path — one-pack chains included — so a rest
            // composite always exists (the close path reuses it to carry the
            // decoration through close animations).
            // The window's PINNED scale, not this output's: see windowSurfaceScale. Handing
            // viewport.scale() here is what made a straddling window realloc and recapture
            // twice per frame forever.
            renderSurfaceChainComposite(w, windowSurfaceScale(w));
        }
    }

    // Desktop-transition capture: captureDesktop drives this paintWindow
    // DIRECTLY, outside KWin's chain walk. Terminate with a raw draw there —
    // the chain iterator sits at begin() in that context, so continuing the
    // paint chain below would re-enter this very function (double fold, the
    // animator transform applied twice to the capture) and then drive later
    // effects' paintWindow hooks without the prePaintWindow they key off,
    // which the capture's design explicitly forbids (its windows were never
    // in this frame's scene walk).
    if (m_directPaintCapture) {
        KWin::effects->drawWindow(renderTarget, viewport, w, mask, deviceRegion, data);
        return;
    }

    // Continue the PAINT chain, never a jump to the draw chain. Our chain
    // position is the default 0, so we run FIRST in the paintWindow chain,
    // and every effect ordered after us applies its WindowPaintData
    // mutations in ITS paintWindow. The old direct `effects->drawWindow`
    // jump here skipped all of them for every window on every frame — most
    // visibly windowaperture (KDE's show-desktop effect, chain position 50),
    // which held windows force-visible while its park-at-the-edges
    // translation was never applied, so Peek at Desktop showed nothing while
    // our effect was loaded.
    //
    // The re-entrancy rationale that moved this off a direct
    // OffscreenEffect::drawWindow call (ghost trails: the shared draw-window
    // iterator parked at the start) is preserved: the chain terminates in the
    // scene's finalPaintWindow, which enters the draw-window chain with the
    // iterator advancing normally, and redirected windows still present
    // through our drawWindow override from inside that chain. This path also
    // covers redirected windows in their post-transition expiry frame, which
    // are still offscreen-backed.
    KWin::effects->paintWindow(renderTarget, viewport, w, mask, deviceRegion, data);
}

void PlasmaZonesEffect::captureOldWindowSnapshot(ShaderTransition& transition, KWin::EffectWindow* window)
{
    // Mirrors KWin's OffscreenData::maybeRender (offscreeneffect.cpp): render the
    // window into an offscreen FBO sized to its expanded geometry × screen scale,
    // via effects->drawWindow. We temporarily bypass our morph shader so the
    // captured texture is the RAW old window content (the cross-fade source) —
    // drawing with the morph shader here would sample an unbound uOldWindow.
    const KWin::LogicalOutput* const screen = window->screen();
    const QRectF logicalGeometry = window->expandedGeometry();
    qreal scale = screen ? screen->scale() : 1.0;
    // Defensive size cap. The snapshot is sampled by normalised uv, so
    // downscaling via a reduced capture scale costs only resolution (no
    // distortion) — it keeps the texture within GL limits and bounds the
    // transient memory of an oversized window. Normal windows (≤ output size)
    // never hit this; it's a guard against pathological geometry.
    constexpr qreal kMaxSnapshotDim = 8192.0;
    const qreal longestPx = qMax(logicalGeometry.width(), logicalGeometry.height()) * scale;
    if (longestPx > kMaxSnapshotDim) {
        scale *= kMaxSnapshotDim / longestPx;
    }
    const QSize textureSize = (logicalGeometry.size() * scale).toSize();
    if (textureSize.isEmpty()) {
        transition.needsSnapshot = false;
        return;
    }

    // Never leak the capture's GL state (blend/viewport/clear/active texture)
    // into the on-screen draw that follows in this same frame — see
    // ScopedGlState.
    const ShaderInternal::ScopedGlState glStateGuard;

    std::unique_ptr<KWin::GLTexture> tex = KWin::GLTexture::allocate(GL_RGBA8, textureSize);
    if (!tex) {
        // Allocation failed — give up on the cross-fade; the morph shader reads
        // a transparent uOldWindow and falls back to no cross-fade.
        transition.needsSnapshot = false;
        return;
    }
    tex->setFilter(GL_LINEAR);
    tex->setWrapMode(GL_CLAMP_TO_EDGE);

    KWin::GLFramebuffer fbo(tex.get());
    if (!fbo.valid()) {
        transition.needsSnapshot = false;
        return;
    }

    // Seed the snapshot from the decorated REST composite when one exists.
    // The raw capture below draws the window WITHOUT its decoration chain:
    // the fold never runs during a capture (m_capturingSnapshot short-
    // circuits paintWindow) and the decorated appearance lives only in the
    // multipass composite, never in the window item itself. A morph shader
    // cross-fades uOldWindow against the decorated live side, so a raw old
    // side visibly strips the decoration — most glaringly the frost/blur
    // pane — for the early part of every geometry morph. The multipass entry
    // still holds the last rest-path fold of the OLD appearance (the
    // deferred erase keeps it alive through a live transition); that frozen
    // frame is exactly what the morph should carry — the same reuse the
    // close path applies to deleted windows.
    //
    // Alignment: the shader samples uOldWindow through iAnchorRectInTexture,
    // the NEW frame's sub-rect of the NEW expanded rect, while the composite
    // canvas covers the OLD padded rect. Map frame→frame affinely (the old
    // frame's pixels land at the new frame's sub-rect; the surrounding
    // canvas scales along, so border strokes and glow bands just outside the
    // frame carry over), clip the source to the canvas, and shrink the dest
    // through the same map so the copied band lands where it belongs — the
    // rest of the snapshot stays cleared. Both textures come out of the same
    // RenderViewport draw path, and blitFromFramebuffer speaks top-down
    // rects, flipping both sides against their own heights internally.
    // Falls back to the raw capture when no composite exists (undecorated
    // windows, or the entry was flushed before the transition began).
    if (const auto mpIt = m_surfaceMultipass.find(getWindowId(window)); mpIt != m_surfaceMultipass.end()) {
        const SurfaceMultipassState& mp = mpIt->second;
        KWin::GLTexture* const comp = mp.compositeTex[mp.finalSlot].get();
        const QRectF oldFrame = transition.fromGeometry;
        const QRectF newFrame = window->frameGeometry();
        if (comp && mp.canvasGeo.isValid() && !mp.canvasGeo.isEmpty() && oldFrame.width() > 0.0
            && oldFrame.height() > 0.0 && newFrame.width() > 0.0 && newFrame.height() > 0.0) {
            // T maps snapshot-space logical points into composite space:
            // T(p) = oldFrame.topLeft + (p - newFrame.topLeft) ⊙ s
            const qreal sx = oldFrame.width() / newFrame.width();
            const qreal sy = oldFrame.height() / newFrame.height();
            const QRectF srcLogical(oldFrame.x() + (logicalGeometry.x() - newFrame.x()) * sx,
                                    oldFrame.y() + (logicalGeometry.y() - newFrame.y()) * sy,
                                    logicalGeometry.width() * sx, logicalGeometry.height() * sy);
            const QRectF srcClipped = srcLogical & mp.canvasGeo;
            if (!srcClipped.isEmpty()) {
                const QRectF dstLogical(newFrame.x() + (srcClipped.x() - oldFrame.x()) / sx,
                                        newFrame.y() + (srcClipped.y() - oldFrame.y()) / sy, srcClipped.width() / sx,
                                        srcClipped.height() / sy);
                const qreal srcPxPerX = comp->width() / mp.canvasGeo.width();
                const qreal srcPxPerY = comp->height() / mp.canvasGeo.height();
                const qreal dstPxPerX = textureSize.width() / logicalGeometry.width();
                const qreal dstPxPerY = textureSize.height() / logicalGeometry.height();
                const QRect srcPx = QRectF((srcClipped.x() - mp.canvasGeo.x()) * srcPxPerX,
                                           (srcClipped.y() - mp.canvasGeo.y()) * srcPxPerY,
                                           srcClipped.width() * srcPxPerX, srcClipped.height() * srcPxPerY)
                                        .toRect();
                const QRect dstPx = QRectF((dstLogical.x() - logicalGeometry.x()) * dstPxPerX,
                                           (dstLogical.y() - logicalGeometry.y()) * dstPxPerY,
                                           dstLogical.width() * dstPxPerX, dstLogical.height() * dstPxPerY)
                                        .toRect();
                KWin::GLFramebuffer srcFbo(comp);
                if (!srcPx.isEmpty() && !dstPx.isEmpty() && srcFbo.valid()) {
                    // The clear and the blit both honour scissor, and this
                    // runs mid scene-walk; the ScopedGlState guard above
                    // restores the enable state.
                    glDisable(GL_SCISSOR_TEST);
                    KWin::GLFramebuffer::pushFramebuffer(&fbo);
                    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                    glClear(GL_COLOR_BUFFER_BIT);
                    KWin::GLFramebuffer::popFramebuffer();
                    KWin::GLFramebuffer::pushFramebuffer(&srcFbo);
                    fbo.blitFromFramebuffer(KWin::Rect(srcPx.x(), srcPx.y(), srcPx.width(), srcPx.height()),
                                            KWin::Rect(dstPx.x(), dstPx.y(), dstPx.width(), dstPx.height()), GL_LINEAR);
                    KWin::GLFramebuffer::popFramebuffer();
                    transition.oldSnapshot = std::move(tex);
                    transition.needsSnapshot = false;
                    return;
                }
            }
        }
    }

    // Bypass the morph shader for the raw capture, restore it afterwards.
    KWin::GLShader* const morphShader = transition.cached ? transition.cached->shader.get() : nullptr;
    setShader(window, nullptr);

    m_capturingSnapshot = true;
    // Guard the re-entrancy flag against a throw from the draw chain — a leaked
    // m_capturingSnapshot would corrupt every subsequent paint. Same pattern as
    // the surface-layer capture sites in surfacelayers.cpp.
    auto resetCapture = qScopeGuard([this] {
        m_capturingSnapshot = false;
    });
    {
        KWin::RenderTarget renderTarget(&fbo);
        KWin::RenderViewport viewport(logicalGeometry, scale, renderTarget, QPoint());
        KWin::GLFramebuffer::pushFramebuffer(&fbo);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        // Keep the window item renderable for the duration of the capture draw.
        KWin::ItemEffect keepRenderable(window->windowItem());
        KWin::WindowPaintData captureData;
        captureData.setOpacity(1.0);
        const int captureMask = PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_TRANSLUCENT;
        // Route through effects->drawWindow (not OffscreenEffect::drawWindow) so
        // KWin's draw-chain iterator is advanced correctly — same rationale as
        // the on-screen draw paths. The re-entrant paintWindow short-circuits on
        // m_capturingSnapshot and draws the window plainly into this FBO.
        KWin::effects->drawWindow(renderTarget, viewport, window, captureMask, KWin::Region::infinite(), captureData);
        KWin::GLFramebuffer::popFramebuffer();
    }
    resetCapture.dismiss();
    m_capturingSnapshot = false;

    setShader(window, morphShader);

    transition.oldSnapshot = std::move(tex);
    transition.needsSnapshot = false;
}

void PlasmaZonesEffect::apply(KWin::EffectWindow* window, int mask, KWin::WindowPaintData& data,
                              KWin::WindowQuadList& quads)
{
    Q_UNUSED(mask)
    Q_UNUSED(data)

    // During an old-content snapshot capture the window must be drawn with its
    // natural, undeformed quad (a raw copy). Leave `quads` untouched.
    if (m_capturingSnapshot) {
        return;
    }

    // Defensive: KWin may dispatch a paint to us for a window already
    // marked deleted (slot ordering vs. our windowDeleted handler). The
    // expandedGeometry / frameGeometry / screen accessors below would
    // deref freed Item state for a deleted window. Mirrors the same
    // guard endShaderTransition applies for the same race.
    // A CLOSING window is isDeleted() for its entire close animation (held
    // alive only by our close grab) — but its surface-extent quad rewrite MUST
    // still run: paintWindow feeds output-sized anchor uniforms and paints
    // with an infinite region for surface-extent transitions, so leaving the
    // natural window-sized quad in place mis-maps the sampled surface far past
    // the window (the close-animation overshoot). Live geometry accessors on a
    // ref-held Deleted window are already exercised every close frame by
    // paintWindow's anchor-uniform block, so reading them here is the same
    // safety class. Bail only for a deleted window with NO live transition
    // (nothing of ours paints it).
    if (!window || (window->isDeleted() && !m_shaderManager.findTransition(window))) {
        return;
    }
    // Only surface-extent transitions deform the quad list. Anchor-extent
    // transitions and plain redirected windows are drawn 1:1 over their
    // own geometry — leave their quads untouched. Non-const handle: the
    // handedness-cache block below mutates it on the first call, saving a
    // redundant `findTransition` lookup.
    auto* st = m_shaderManager.findTransition(window);

    // ── Padded decoration present ────────────────────────────────────────
    // A chain with an outer margin (WindowDecoration::outerPadding) composited
    // into a canvas LARGER than the redirect texture
    // (renderSurfaceChainComposite inflated the capture rect). Present it on
    // a matching padded quad so the margin band reaches the screen — the
    // same quad-rewrite mechanism the surface-extent transitions use (they
    // stretch to the whole output; this stretches by the padding). Gated on
    // no live transition: a transition owns the shader slot (shaderApplied
    // false) and its own quad handling wins.
    if (!st && !quads.isEmpty() && !m_windowDecorations.isEmpty()) {
        const auto bit = m_windowDecorations.find(getWindowId(window));
        if (bit != m_windowDecorations.end() && bit->shaderApplied && bit->outerPadding > 0) {
            QRectF textureGeo = window->expandedGeometry();
            if (textureGeo.isEmpty()) {
                textureGeo = window->frameGeometry();
            }
            if (textureGeo.isEmpty()) {
                return;
            }
            const qreal pad = bit->outerPadding;
            // The canvas the fold ACTUALLY produced, not a second derivation of it.
            // SurfaceMultipassState::canvasGeo is the single source for this quantity
            // (see its doc), and the layer-rect remap above already honours it: present
            // the composite on the rect it was composited into, or a window whose
            // geometry moved after the fold gets its decoration drawn against a canvas
            // the texture does not match. The live derivation stays as the fallback for
            // the fold's failure paths, which can erase the multipass entry.
            QRectF padded = textureGeo.adjusted(-pad, -pad, pad, pad);
            if (const auto psIt = m_surfaceMultipass.find(getWindowId(window)); psIt != m_surfaceMultipass.end()) {
                if (psIt->second.canvasGeo.isValid()) {
                    padded = psIt->second.canvasGeo;
                }
            }

            // quad-space <-> screen-space is a pure translation at 1:1
            // logical scale (see the surface-extent branch below for the
            // full rationale); derive the offset from the actual quads.
            double qLeft = quads.first().left();
            double qTop = quads.first().top();
            for (qsizetype i = 1; i < quads.size(); ++i) {
                qLeft = qMin(qLeft, quads[i].left());
                qTop = qMin(qTop, quads[i].top());
            }
            const double ox = qLeft + (padded.x() - textureGeo.x());
            const double oy = qTop + (padded.y() - textureGeo.y());
            const double ow = padded.width();
            const double oh = padded.height();

            // Texcoord handedness: replicate from the quad KWin handed us
            // (not hardcoded) and cache per window — identical rationale to
            // the surface-extent transitions' handedness cache below.
            if (!bit->presentHandednessCached) {
                const KWin::WindowQuad& srcQuad = quads.first();
                int topIdx = 0, bottomIdx = 0, leftIdx = 0, rightIdx = 0;
                for (int i = 1; i < 4; ++i) {
                    if (srcQuad[i].y() < srcQuad[topIdx].y())
                        topIdx = i;
                    if (srcQuad[i].y() > srcQuad[bottomIdx].y())
                        bottomIdx = i;
                    if (srcQuad[i].x() < srcQuad[leftIdx].x())
                        leftIdx = i;
                    if (srcQuad[i].x() > srcQuad[rightIdx].x())
                        rightIdx = i;
                }
                bit->uAtLeft = (srcQuad[leftIdx].u() <= srcQuad[rightIdx].u()) ? 0.0 : 1.0;
                bit->uAtRight = 1.0 - bit->uAtLeft;
                bit->vAtTop = (srcQuad[topIdx].v() <= srcQuad[bottomIdx].v()) ? 0.0 : 1.0;
                bit->vAtBottom = 1.0 - bit->vAtTop;
                bit->presentHandednessCached = true;
            }

            KWin::WindowQuad paddedQuad;
            paddedQuad[0] = KWin::WindowVertex(ox, oy, bit->uAtLeft, bit->vAtTop);
            paddedQuad[1] = KWin::WindowVertex(ox + ow, oy, bit->uAtRight, bit->vAtTop);
            paddedQuad[2] = KWin::WindowVertex(ox + ow, oy + oh, bit->uAtRight, bit->vAtBottom);
            paddedQuad[3] = KWin::WindowVertex(ox, oy + oh, bit->uAtLeft, bit->vAtBottom);
            quads.clear();
            quads.append(paddedQuad);
            return;
        }
    }

    // ── Anchor-extent transition on a PADDED window ──────────────────────
    // The animation draws on the window's natural quad, which would clip the
    // decoration's outer margin (glow halo) for the whole animation. Grow the
    // quad by the padding with texcoords EXTENDED past the natural range —
    // NOT remapped to 0..1 — so uTexture0 keeps its 1:1 mapping and
    // surfaceColor's iLayerRectInTexture remap addresses the halo band in the
    // padded uSurfaceLayer. (Out-of-range uTexture0 samples only matter when
    // the layer is absent, where CLAMP smears the feathered-transparent
    // margin edge — invisible.)
    if (st && !st->surfaceExtent && !quads.isEmpty() && !m_windowDecorations.isEmpty()) {
        const auto bit = m_windowDecorations.find(getWindowId(window));
        if (bit != m_windowDecorations.end() && bit->outerPadding > 0) {
            QRectF textureGeo = window->expandedGeometry();
            if (textureGeo.isEmpty()) {
                textureGeo = window->frameGeometry();
            }
            if (textureGeo.isEmpty() || textureGeo.width() <= 0 || textureGeo.height() <= 0) {
                return;
            }
            const qreal pad = bit->outerPadding;

            double qLeft = quads.first().left();
            double qTop = quads.first().top();
            for (qsizetype i = 1; i < quads.size(); ++i) {
                qLeft = qMin(qLeft, quads[i].left());
                qTop = qMin(qTop, quads[i].top());
            }
            const double ox = qLeft - pad;
            const double oy = qTop - pad;
            const double ow = textureGeo.width() + 2.0 * pad;
            const double oh = textureGeo.height() + 2.0 * pad;

            // Same replicated-handedness cache the padded present uses.
            if (!bit->presentHandednessCached) {
                const KWin::WindowQuad& srcQuad = quads.first();
                int topIdx = 0, bottomIdx = 0, leftIdx = 0, rightIdx = 0;
                for (int i = 1; i < 4; ++i) {
                    if (srcQuad[i].y() < srcQuad[topIdx].y())
                        topIdx = i;
                    if (srcQuad[i].y() > srcQuad[bottomIdx].y())
                        bottomIdx = i;
                    if (srcQuad[i].x() < srcQuad[leftIdx].x())
                        leftIdx = i;
                    if (srcQuad[i].x() > srcQuad[rightIdx].x())
                        rightIdx = i;
                }
                bit->uAtLeft = (srcQuad[leftIdx].u() <= srcQuad[rightIdx].u()) ? 0.0 : 1.0;
                bit->uAtRight = 1.0 - bit->uAtLeft;
                bit->vAtTop = (srcQuad[topIdx].v() <= srcQuad[bottomIdx].v()) ? 0.0 : 1.0;
                bit->vAtBottom = 1.0 - bit->vAtTop;
                bit->presentHandednessCached = true;
            }

            // Extend the texcoord range by the padding fraction in each axis,
            // direction-aware so either handedness extends outward.
            const double padU = pad / textureGeo.width();
            const double padV = pad / textureGeo.height();
            const double uDir = bit->uAtRight - bit->uAtLeft; // ±1
            const double vDir = bit->vAtBottom - bit->vAtTop; // ±1
            const double uL = bit->uAtLeft - uDir * padU;
            const double uR = bit->uAtRight + uDir * padU;
            const double vT = bit->vAtTop - vDir * padV;
            const double vB = bit->vAtBottom + vDir * padV;

            KWin::WindowQuad paddedQuad;
            paddedQuad[0] = KWin::WindowVertex(ox, oy, uL, vT);
            paddedQuad[1] = KWin::WindowVertex(ox + ow, oy, uR, vT);
            paddedQuad[2] = KWin::WindowVertex(ox + ow, oy + oh, uR, vB);
            paddedQuad[3] = KWin::WindowVertex(ox, oy + oh, uL, vB);
            quads.clear();
            quads.append(paddedQuad);
            return;
        }
    }

    if (!st || !st->surfaceExtent || quads.isEmpty()) {
        return;
    }
    const auto* output = window->screen();
    if (!output) {
        return;
    }
    // The redirected texture KWin hands us covers the window's EXPANDED
    // geometry (frame + decoration + shadow), so the incoming quad — and
    // the quad-space <-> screen-space offset derived from it below — is
    // anchored to the expanded rect, not the frame. Pair it with the
    // expanded rect (the surface-extent anchor uniforms use the same
    // rect) so the output quad lands where KWin placed the texture.
    // expandedGeometry can be empty for a window with no decoration or
    // shadow extents; fall back to the frame there.
    QRectF textureGeo = window->expandedGeometry();
    if (textureGeo.isEmpty()) {
        textureGeo = window->frameGeometry();
    }
    const QRect outputGeo = output->geometry();
    if (textureGeo.isEmpty() || outputGeo.isEmpty()) {
        return;
    }

    // The incoming quads span the captured window in KWin's quad-list
    // coordinate space. Deriving the window's top-left from the actual
    // quads (rather than assuming an origin) keeps the mapping correct
    // whether KWin hands us window-local or screen-absolute quad
    // coordinates: quad-list space ↔ screen space is a pure translation
    // at 1:1 logical scale, so the same offset that maps the window also
    // maps the output.
    double qLeft = quads.first().left();
    double qTop = quads.first().top();
    for (qsizetype i = 1; i < quads.size(); ++i) {
        qLeft = qMin(qLeft, quads[i].left());
        qTop = qMin(qTop, quads[i].top());
    }

    // Window-relative grid deformation (e.g. the `flow` window-move
    // effect). Build an NxN grid over the window's DESTINATION frame rect
    // — the same rect pushed as iToRect — so the vertex shader can pull
    // trailing rows back toward iFromRect while the leading edge settles
    // first. Anchoring the grid to the window (not the output, as the
    // single-quad path below does) keeps the deformation resolution
    // constant regardless of how small a zone the window snaps into: every
    // cell lands on the window. Texcoords are emitted as plain card uv
    // (0..1, row 0 at the window's top); KWin Y-flips window-quad
    // texcoords on upload, so the flow vertex stage re-applies the
    // canonical `1.0 - texCoord.y` flip (same as the shared kwin vertex
    // stage) to recover card uv with y = 0 at the top. Displaced trailing
    // vertices reach past the destination rect toward iFromRect;
    // surface-extent draws with Region::infinite (see paintWindow), so
    // they are not clipped.
    if (st->gridSubdivisions > 0) {
        // Destination frame rect == iToRect. The window already jumped
        // there via moveResize, so the live frameGeometry is a safe
        // fallback if a transition somehow lacks a recorded destination.
        QRectF frameRect = st->toGeometry;
        if (!frameRect.isValid() || frameRect.isEmpty()) {
            frameRect = window->frameGeometry();
        }
        if (frameRect.isEmpty()) {
            return;
        }
        // COVER THE PADDED DECORATION CANVAS, not just the frame. An outer
        // ambience pack (glow / drop shadow / fireflies) paints its halo in
        // the margin OUTSIDE the frame, folded into the multipass composite
        // over a padded canvas. A grid confined to the frame samples only
        // texcoords [0,1] = the frame region of that canvas, so the halo was
        // clipped to the frame edge during the deform. Build the grid over
        // the recorded composite canvas instead, with texcoords kept
        // FRAME-relative — so cuv runs past [0,1] into the halo band, which
        // surfaceColor()'s iLayerRectInTexture remap (also frame-anchored)
        // resolves into the composite's margin. With no decoration the
        // canvas equals the frame and this reduces to the old behaviour.
        QRectF gridRect = frameRect;
        if (const auto lsIt = m_surfaceMultipass.find(getWindowId(window));
            lsIt != m_surfaceMultipass.end() && lsIt->second.canvasGeo.isValid() && !lsIt->second.canvasGeo.isEmpty()) {
            gridRect = lsIt->second.canvasGeo;
        }
        // quad-space <-> screen-space is a pure translation at 1:1 logical
        // scale; qLeft/qTop is the captured texture's top-left in quad
        // space and textureGeo its top-left in screen space.
        const double qOffX = qLeft - textureGeo.x();
        const double qOffY = qTop - textureGeo.y();
        const double fx = frameRect.x();
        const double fy = frameRect.y();
        const double fw = frameRect.width();
        const double fh = frameRect.height();
        const int n = st->gridSubdivisions;
        quads.clear();
        quads.reserve(n * n);
        for (int gy = 0; gy < n; ++gy) {
            const double sy0 = gridRect.y() + (static_cast<double>(gy) / n) * gridRect.height();
            const double sy1 = gridRect.y() + (static_cast<double>(gy + 1) / n) * gridRect.height();
            const double v0 = (sy0 - fy) / fh; // frame-relative: past [0,1] in the halo band
            const double v1 = (sy1 - fy) / fh;
            const double y0 = sy0 + qOffY;
            const double y1 = sy1 + qOffY;
            for (int gx = 0; gx < n; ++gx) {
                const double sx0 = gridRect.x() + (static_cast<double>(gx) / n) * gridRect.width();
                const double sx1 = gridRect.x() + (static_cast<double>(gx + 1) / n) * gridRect.width();
                const double u0 = (sx0 - fx) / fw;
                const double u1 = (sx1 - fx) / fw;
                const double x0 = sx0 + qOffX;
                const double x1 = sx1 + qOffX;
                KWin::WindowQuad cell;
                cell[0] = KWin::WindowVertex(x0, y0, u0, v0);
                cell[1] = KWin::WindowVertex(x1, y0, u1, v0);
                cell[2] = KWin::WindowVertex(x1, y1, u1, v1);
                cell[3] = KWin::WindowVertex(x0, y1, u0, v1);
                quads.append(cell);
            }
        }
        return;
    }

    const double ox = qLeft + (outputGeo.x() - textureGeo.x());
    const double oy = qTop + (outputGeo.y() - textureGeo.y());
    const double ow = outputGeo.width();
    const double oh = outputGeo.height();

    // Texcoord handedness: replicate it from the quad KWin handed us
    // rather than hardcoding. KWin's WindowQuad texcoord Y convention is
    // not part of the public contract; assuming it (texcoord.y = 1 at
    // the top) rendered every surface-extent transition upside down AND
    // animating from the wrong screen edge — bounce dropped UP from the
    // bottom — while the daemon path and the non-surface kwin shaders,
    // which use KWin's own window quad, stayed correct. The surface quad
    // below must carry the SAME (screen-position <-> texcoord)
    // handedness as that window quad so the shared kwin vertex stage's
    // `1.0 - texCoord.y` flip lands `vTexCoord` Y-down on this path
    // exactly as it does for the window's own quad. Window content is
    // axis-aligned, so u is linear in x and v in y — derive each axis's
    // sign from the source quad's extreme vertices.
    //
    // Cache the result on the transition: the handedness depends only on
    // KWin's quad convention, which doesn't shift mid-transition. Without
    // the cache, every surface-extent shader pays the 3-vertex search +
    // 4 comparisons per quad per frame for its entire lifetime. `st` is
    // already known non-null (the early `!st || !st->surfaceExtent` guard at
    // the top of apply() returned otherwise), so the cache population reuses
    // that handle instead of paying a second lookup.
    if (!st->handednessCached) {
        const KWin::WindowQuad& srcQuad = quads.first();
        int topIdx = 0, bottomIdx = 0, leftIdx = 0, rightIdx = 0;
        for (int i = 1; i < 4; ++i) {
            if (srcQuad[i].y() < srcQuad[topIdx].y())
                topIdx = i;
            if (srcQuad[i].y() > srcQuad[bottomIdx].y())
                bottomIdx = i;
            if (srcQuad[i].x() < srcQuad[leftIdx].x())
                leftIdx = i;
            if (srcQuad[i].x() > srcQuad[rightIdx].x())
                rightIdx = i;
        }
        // The surface quad spans the whole output, so its texcoords are the
        // full 0..1 range; only the handedness comes from the source quad.
        st->uAtLeft = (srcQuad[leftIdx].u() <= srcQuad[rightIdx].u()) ? 0.0 : 1.0;
        st->uAtRight = 1.0 - st->uAtLeft;
        st->vAtTop = (srcQuad[topIdx].v() <= srcQuad[bottomIdx].v()) ? 0.0 : 1.0;
        st->vAtBottom = 1.0 - st->vAtTop;
        st->handednessCached = true;
    }
    const double uAtLeft = st->uAtLeft;
    const double uAtRight = st->uAtRight;
    const double vAtTop = st->vAtTop;
    const double vAtBottom = st->vAtBottom;

    // One quad covering the whole output, clockwise from top-left (the
    // vertex order WindowQuad documents). The shared kwin vertex stage
    // flips texCoord to the Y-down vTexCoord the animation-shader
    // contract expects, so vTexCoord lands 0..1 over the output. The
    // window content stays in uTexture0 (the window-sized redirect FBO);
    // surface-extent shaders place it via iAnchorPosInFbo / anchorRemap.
    KWin::WindowQuad surfaceQuad;
    surfaceQuad[0] = KWin::WindowVertex(ox, oy, uAtLeft, vAtTop);
    surfaceQuad[1] = KWin::WindowVertex(ox + ow, oy, uAtRight, vAtTop);
    surfaceQuad[2] = KWin::WindowVertex(ox + ow, oy + oh, uAtRight, vAtBottom);
    surfaceQuad[3] = KWin::WindowVertex(ox, oy + oh, uAtLeft, vAtBottom);
    quads.clear();
    quads.append(surfaceQuad);
}

} // namespace PlasmaZones
