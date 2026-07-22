// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "compositor/compositorclock.h"
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

#include "compositor/windowanimator.h"
#include "paint_internal.h"

namespace PlasmaZones {

using ShaderInternal::shaderClockNowMs;

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
        const PaintWindowContext ctx{renderTarget, viewport, w, mask, deviceRegion, data, frameNowMs};
        if (paintShaderTransitionWindow(ctx, st) == ShaderBranchOutcome::Handled) {
            return;
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

} // namespace PlasmaZones
