// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "compositor/compositorclock.h"
#include "handlers/navigationhandler.h"
#include "tilinghandler/tilinghandler.h"
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

#include "compositor/stripviewanimator.h"
#include "compositor/windowanimator.h"
#include "paint_internal.h"

namespace PlasmaZones {

using ShaderInternal::shaderClockNowMs;

bool PlasmaZonesEffect::blocksDirectScanout() const
{
    // Crop mode only: with scrollingCropStraddlers on, partial edge columns
    // keep their TRUE rects and the per-output cull in paintWindow is what
    // crops the overhang off the neighbouring monitor. That cull exists only
    // in the GL composite path — a surface presented directly on a hardware
    // plane bypasses the effect chain, which is exactly how the overhang
    // leaked when cropping was the default. Forcing composition while any
    // scrolling screen exists is the price of crop mode; the default clamp
    // mode costs nothing here because its clip is the committed geometry
    // itself.
    // Known enable-order gap, deliberately unfixed: the engine flips to true
    // rects synchronously on the settings change while this cached flag
    // arrives over an async D-Bus read. The exposure is NOT bounded by the
    // retile debounce — applyLayout also runs synchronously from window
    // lifecycle and float events, so any open/close/float landing inside the
    // reply latency (one getSetting queued behind loadCachedSettings' whole
    // burst) commits overhang with scanout still permitted. Closing it needs
    // an effect-side ack the settings path does not have; crop is off by
    // default and the flip is an explicit user action, so the window is
    // accepted rather than engineered away.
    return m_cachedScrollCropStraddlers && m_tilingHandler && m_tilingHandler->hasScrollingScreens();
}

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
    m_currentPassOutput = data.screen;
    // New pass, new scroll-managed answers: the memo is only valid while the
    // strip state cannot change under it, which one output pass guarantees.
    m_scrollManagedCache.clear();
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
    // Same tick, same clocks. One spring per scrolling output rather than one
    // per window, so the cost here is bounded by the monitor count.
    m_stripViewAnimator->advanceAnimations();

    // Vertex snapping tracks the animation state (see the initRenderingAndRegistries
    // note): None while a REDIRECTED window animates so its smooth translates keep
    // sub-pixel precision, Round (KWin's default) otherwise so permanently-redirected
    // decorated windows stay device-pixel-aligned at fractional output scales
    // instead of being bilinearly resampled every frame (discussion #868).
    //
    // Gated on redirected-window animation, not on animation in general: the mode
    // only exists on KWin's per-window OffscreenData, so it affects nothing but
    // redirected (decorated / transition) windows' offscreen presentation. An
    // UNDECORATED window's morph presents through the direct scene path, where the
    // mode never applies — flipping on it only un-snapped every decorated bystander
    // for the animation's duration, a full-desktop resample tax at fractional scale
    // that bought nothing. Shader transitions always redirect their window, so
    // `!m_shaderManager.empty()` stays a sufficient condition on its own (it
    // over-includes installed-but-expired transitions; that only extends None by a
    // frame or two, which is harmless).
    //
    // Scope caveat, stated so nobody narrows the wrong half: setVertexSnappingMode
    // is EFFECT-GLOBAL (OffscreenEffect pushes it into every redirected window's
    // OffscreenData), so while ONE decorated window animates, every other decorated
    // window is also un-snapped for the duration. A per-window relax needs an
    // upstream per-window API; this predicate only narrows WHEN the global flip
    // happens (a decorated animation) versus the old any-animation trigger.
    //
    // The `it->shaderApplied` half is a proxy for "redirected", and it is correct
    // ONLY because the `!m_shaderManager.empty()` term short-circuits first: a
    // transition-owned window keeps shaderApplied true at install and only
    // reconcileDecorationShader's next run cedes the slot (clears the flag), so
    // during a transition the flag's value is timing-dependent either way —
    // which never matters, because the shader-manager term answers first for
    // the whole transition. Narrowing that term to a per-window test without
    // also fixing this half would silently stop relaxing snapping for
    // transition-owned windows.
    const bool animationsInFlight = m_windowAnimator->hasActiveAnimations() || !m_shaderManager.empty()
        || m_stripViewAnimator->hasActiveAnimations();
    // The decoration probe can only return true when decorations exist — skip
    // the per-animation id derivation entirely on an undecorated desktop.
    const bool redirectedAnimating = !m_shaderManager.empty()
        || (!m_windowDecorations.isEmpty() && m_windowAnimator->hasAnimationMatching([this](KWin::EffectWindow* aw) {
               if (!aw || aw->isDeleted()) {
                   return false;
               }
               const auto it = m_windowDecorations.constFind(getWindowId(aw));
               return it != m_windowDecorations.constEnd() && it->shaderApplied;
           }));
    if (redirectedAnimating != m_vertexSnappingDisabled) {
        setVertexSnappingMode(redirectedAnimating ? KWin::RenderGeometry::VertexSnappingMode::None
                                                  : KWin::RenderGeometry::VertexSnappingMode::Round);
        m_vertexSnappingDisabled = redirectedAnimating;
    }

    if (animationsInFlight) {
        // Windows have translation transforms that move them outside their
        // frame geometry bounds — force full compositing mode. Shader
        // transitions also need this: without
        // PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS the compositor skips
        // our paintWindow override on stable, undamaged windows (focus,
        // open after the fade settles, minimise, etc.), which means
        // the shader installs and silently expires unrendered.
        //
        // Gate PER OUTPUT (mirroring the desktop-transition gate below):
        // the KWin header documents this flag as "forces the entire screen
        // to be painted", so setting it globally makes one window animating
        // on monitor 1 force full, damage-free repaints of monitors 2 and 3
        // for the whole animation — the dominant iGPU cost of the default
        // geometry morph. An output is included when an animation's swept
        // bounds intersect it (rect intersection, NOT screen() equality, so
        // a window straddling outputs — or morphing across them — keeps the
        // flag on every output it touches) or when a live shader transition's
        // window is on it. Transition relevance uses screen() OR expanded-
        // geometry intersection: the surface-extent quad covers the whole of
        // the window's own output, and the postPaintScreen damage loops only
        // ever damage that output, so this matches what can actually paint.
        // Null screen (test paths, hotplug) falls back to the global flag.
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
        bool touchesThisOutput = !data.screen;
        if (data.screen) {
            const QRectF outputGeo = QRect(data.screen->geometry());
            touchesThisOutput = m_windowAnimator->hasAnimationsIntersecting(outputGeo);
            // The strip-wide analogue of the swept-bounds test above. A view
            // leg has no per-window bounds to sweep — it moves every column on
            // its output at once — so the output it belongs to IS its region,
            // and identity answers what an intersection would have to
            // approximate.
            if (!touchesThisOutput && m_stripViewAnimator->isAnimatingOn(data.screen)) {
                touchesThisOutput = true;
            }
            if (!touchesThisOutput) {
                for (const auto& [tw, transition] : m_shaderManager.shaderTransitions()) {
                    if (!tw) {
                        continue;
                    }
                    // Same skip predicate as the postPaintScreen repaint pump:
                    // a closing window is isDeleted() for its ENTIRE close
                    // animation (the close grab keeps the corpse alive and
                    // paintWindow calls screen() on it every close frame), so
                    // a grab-held leg must keep the flag on its output or the
                    // close shader goes unrendered on stable outputs. The
                    // off-desktop clause mirrors the pump too: a leg the pump
                    // refuses to drive must not keep this output on the
                    // transformed-windows path for a paint that never comes.
                    if (!transition.closeGrabHeld && (tw->isDeleted() || !tw->isOnCurrentDesktop())) {
                        continue;
                    }
                    // Unknown output on the transition's window: mirror
                    // postPaintScreen's addRepaintFull fallback and keep the
                    // flag on every output rather than trusting a possibly
                    // stale expandedGeometry against this one.
                    if (!tw->screen()) {
                        touchesThisOutput = true;
                        break;
                    }
                    if (tw->screen() == data.screen || QRectF(tw->expandedGeometry()).intersects(outputGeo)) {
                        touchesThisOutput = true;
                        break;
                    }
                }
            }
        }
        if (touchesThisOutput) {
            data.mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;
        }
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
    // once per OUTPUT PASS (multi-pass, back-to-back paint cycles scheduled
    // by our own `effects->addRepaint` calls in postPaintScreen). The pin's
    // scope is the prePaintScreen→postPaintScreen bracket of ONE output —
    // on an N-output desktop each output pass pins its own (microseconds
    // apart) timestamp, so a window straddling outputs still sees one value
    // per pass, not one per vsync; dt-driven consumers conserve total dt
    // across the passes, and the residual drift is iFrame advancing once per
    // pass for a straddler. If every paintWindow call re-sampled
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
    // Pass over. Defensive hygiene: every capture path in this tree reaches
    // paintWindow from INSIDE the pass (before this runs), so the clear only
    // protects a hypothetical paintWindow outside any bracket. Cleared at the
    // TOP deliberately — nothing below reads the latch, and clearing first
    // means any future reader added to this function sees the bracket as
    // already closed rather than a stale output.
    m_currentPassOutput = nullptr;
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

    // A scroll-strip window on a FOREIGN output's pass: paintWindow will skip
    // drawing it entirely, so it must not occlude either. Leaving its opaque
    // region declared tells KWin's occlusion culling that everything behind
    // the frame is covered, so the background there is never recomposited —
    // and with the window itself skipped, nothing overdraws those pixels at
    // all. The last-presented frame then persists as a ghost copy of the
    // window on the neighbouring monitor (the same stale-pixels mechanism the
    // transition branch below documents for translated renders). The ghost is
    // indistinguishable from "the clip is broken": the window was never being
    // DRAWN over there, it was being REMEMBERED there.
    if (w && !m_capturingSnapshot && m_currentPassOutput) {
        if (const KWin::LogicalOutput* managed = scrollManagedOutputFor(w); managed && managed != m_currentPassOutput) {
            data.setTranslucent();
        }
    }

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
        // window transformed so KWin paints the padded quad unclipped.
        //
        // This flag is NOT occlusion-free, and the cost is the same double loss
        // the translucent marking below pays: in KWin 6.7's workspacescene.cpp,
        // PAINT_WINDOW_TRANSFORMED excludes the window from the opaque
        // accumulation in BOTH collectDamage() and paintSimpleScreen(), and
        // BlurEffect::shouldBlur additionally refuses blur-behind for any
        // transformed window. So a padded chain forfeits occlusion culling
        // (making the interiorOpaque skip below a no-op for it) AND KWin's own
        // blur on that window, regardless of what the chain draws. Recovering
        // either needs a way to present the padded margin without the
        // transformed flag, which KWin's untransformed path does not offer (it
        // clips paint to the window item's bounding rect).
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
            m_shaderManager.cacheFrameOpacity(w, resolveWindowOpacity(resolveRuleActions(w, windowId)));
        }
    }

    // A decorated window is TRANSLUCENT — unless its whole chain proves otherwise.
    // Clear its opaque region so KWin keeps compositing whatever sits behind it.
    //
    // This is an OCCLUSION hint, not a rendering one, and it cannot be expressed in
    // the fragment stage: KWin decides what to composite BEHIND a window before any
    // of our shaders run, so by the time a pack outputs alpha < 1 the scene has
    // already skipped whatever is underneath and the pack blends against stale
    // framebuffer pixels.
    //
    // The cost is real and double-ended. In KWin 6.7's workspacescene.cpp, a
    // PAINT_WINDOW_TRANSLUCENT window contributes nothing to the opaque
    // accumulation in BOTH collectDamage() (damage from windows underneath is
    // never culled away) and paintSimpleScreen() (`visible -= deviceOpaque` is
    // skipped, so windows underneath are genuinely painted). A video playing
    // fully behind a maximized decorated window keeps driving full composites.
    //
    // Why the default is still translucent: for most chains it is a property of
    // the shader, not a conservative guess. Reading data/surface/shared/surface_lib.glsl:
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
    // So every border-family chain thins frame texels and must stay translucent. But
    // the margin-only packs (shadow, glow) provably do NOT: their halo is gated on
    // `1 - base.a` (haloFalloff) and composited additively over the transparent
    // margin (marginComposite), so the interior passes through byte-for-byte and
    // the client's own opaque region stays truthful. That is exactly the metadata
    // contract an earlier attempt at this flag lacked: packs now declare
    // `interiorOpaque` (SurfaceShaderEffect), the chain sweep in
    // updateWindowDecoration ANDs it into WindowDecoration::chainInteriorOpaque,
    // and a chain that qualifies keeps KWin's occlusion culling — PROVIDED the
    // folded opacity is at rest, since the failed-compile fail-safe dims the
    // CAPTURE itself (see foldedOpacity's doc) and that thins the interior with
    // no pack involved.
    //
    // SCOPE LIMIT, verified against the same workspacescene.cpp sources: a
    // PADDED chain (outerPadding > 0) is marked PAINT_WINDOW_TRANSFORMED
    // above, and the transformed flag independently excludes the window from
    // BOTH culling halves — so skipping setTranslucent() recovers nothing for
    // it. The two bundled interiorOpaque declarers (shadow, glow) are both
    // padded, which means the skip below is live only for an unpadded
    // interiorOpaque chain: a third-party contract today, not a bundled win.
    // Keeping the flag is still correct (it is the necessary half of the
    // recovery; the transformed presentation is the other), and the sweep's
    // AND is what a future unpadded pack or a padded-presentation redesign
    // will inherit.
    //
    // Note what this is NOT for. It used to be set to keep the window in KWin's paint
    // set so drawWindow kept firing on idle frames. That was a repaint-scheduling hack
    // riding an occlusion flag, and it was not even needed: an undamaged window's
    // pixels, border and all, are already on screen in the last-presented composite.
    // The cases where the composite changes with no window damage (a focus cross-fade,
    // an iTime pack, a backdrop refresh) schedule their own repaints in postPaintScreen.
    if (!transformDriven && decorated) {
        const bool interiorOpaque = decoIt->chainInteriorOpaque && decoIt->foldedOpacity >= 1.0;
        if (!interiorOpaque) {
            data.setTranslucent();
        }
    }

    // A parked scrolling column is drawn far from its committed rect (the paint
    // path relocates it to its strip position), so KWin must not decide where
    // it goes from that rect. Occlusion culling is forfeited for it, which
    // costs nothing: the window is off the viewport by definition — that is
    // why it parked.
    if (w && !m_scrollVisualPos.isEmpty() && m_scrollVisualPos.contains(getWindowId(w))) {
        data.setTransformed();
    }

    // The tab-indicator surface is translated off its committed rect for the
    // whole view leg (paintWindow adds the strip's offset to it), so KWin must
    // stop deciding where it goes from that rect. Marked translucent for the
    // same reason the transition branch above is: a surface drawn away from its
    // frame leaves the pixels it vacated uncomposited, and the last presented
    // frame reads back as a ghost indicator standing still while the real one
    // slides.
    if (w && m_stripViewAnimator->isAnimatingOn(w->screen()) && isScrollTabIndicatorSurface(w)) {
        data.setTransformed();
        data.setTranslucent();
    }

    OffscreenEffect::prePaintWindow(view, w, data);
}

// scrollManagedOutputFor / scrollClipGeometryFor live in scroll_clip.cpp —
// the clip predicate is its own concern; this file consumes it.

void PlasmaZonesEffect::paintWindow(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                                    KWin::EffectWindow* w, int mask, const KWin::Region& deviceRegion,
                                    KWin::WindowPaintData& data)
{
    // Scrolling-strip boundary clip. A strip column legitimately straddles
    // its screen's edge (centering the active column pushes both neighbours
    // across it). In default clamp mode the engine clamps BOTH edges
    // engine-side, so no committed rect crosses an output and this cull has
    // nothing to do; in crop mode (scrollingCropStraddlers) the engine keeps
    // the TRUE rect — the user wants the full-size window with its drawing
    // cut at the monitor boundary — and this cull is what does the cutting.
    // On the column's own output the render target already scissors at the
    // edge; the only place the overhang becomes visible is the ADJACENT
    // output's paint pass, so skip the window entirely in passes whose output
    // is not its managed screen. The predicate lives in scrollClipGeometryFor
    // and is shared with the overhang input filter, which keeps the same
    // invisible region from receiving pointer/touch input.
    //
    // OUTPUT PASSES ONLY. An offscreen capture (captureOldWindowSnapshot,
    // captureWindowSurface) re-enters paintWindow with a viewport built from
    // the WINDOW's own rect rather than an output's, so a column parked off
    // its screen — the normal state for off-viewport columns and hidden tabs —
    // would miss the clip rect, return here, and leave the FBO at its cleared
    // transparent fill. Those snapshots latch (needsSnapshot / captureValid),
    // so the blank would persist for the whole cross-fade. m_capturingSnapshot
    // marks exactly that re-entrancy; the direct-capture path builds its
    // viewport from the output geometry and pre-filters by intersection, so it
    // needs no exemption.
    if (!m_capturingSnapshot && m_currentPassOutput) {
        // Output IDENTITY, not a rect overlap: which output is being painted
        // is not something to infer from coordinate math. On a FOREIGN
        // output's pass the window is skipped entirely; prePaintWindow marks
        // it translucent on those passes so its opaque region cannot cull the
        // background repaint — a skipped-but-occluding window leaves the
        // last-presented pixels behind it frozen, which reads as a ghost copy
        // of the window on the neighbouring monitor. On its OWN pass nothing
        // needs clipping: each output renders into its own framebuffer, so
        // the overhang past the edge is clipped by the hardware.
        //
        // m_currentPassOutput is null only outside any prePaintScreen bracket
        // (defensive bootstrap, test harness, a null data.screen) — NOT for
        // offscreen captures, which run inside a pass and keep its output;
        // the window-snapshot captures are exempted by m_capturingSnapshot
        // above. With the latch null the suppression fails open, matching
        // the permissive treatment every other null-screen branch in this
        // file applies.
        if (const KWin::LogicalOutput* managed = scrollManagedOutputFor(w); managed && managed != m_currentPassOutput) {
            return;
        }
    }

    // Read the cached per-frame clock pinned by prePaintScreen. Multiple
    // paintWindow calls within one OUTPUT PASS (multi-pass, back-to-back
    // paint cycles driven by our addRepaint) would otherwise each see a
    // slightly later `shaderClockNowMs()` and paint the surface-extent quad
    // at a slightly different progress — visible as staggered ghost copies
    // of the in-flight window. (The pin is per output pass, not per vsync;
    // see the prePaintScreen comment for the multi-output scope.) Fall back
    // to a live read if prePaintScreen hasn't pinned the clock yet (test
    // harness, defensive bootstrap path).
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
    // walk the target holds the content painted below this window WITHIN the
    // frame's damage region (outside it the buffer still holds the previous
    // presented frame, which is why the capture clips to deviceRegion).
    // Live windows only: a closing window's decoration reuses its frozen
    // composite (renderSurfaceChain) and must never re-capture. Covers both
    // fold sites (the rest-path composite further down AND the transition
    // branch's renderSurfaceChain), hence the shaderApplied-or-transition
    // gate rather than shaderApplied alone.
    if (w && !w->isDeleted() && !m_capturingSnapshot && !m_windowDecorations.isEmpty()) {
        const auto backIt = m_windowDecorations.constFind(getWindowId(w));
        // Skip frames restore-suppression will withhold anyway: the early
        // return below paints nothing on those frames, so a capture taken
        // here is thrown away — and the suppression loop repaints every
        // vsync, so a suppressed frost-decorated opener paid a full-canvas
        // blit per frame for up to 250 ms. Only the WITHHELD frames skip; the
        // release frame (deadline reached) erases the entry and folds in this
        // same call, so it must still capture or its first visible fold has
        // no backdrop. Computed lazily — the undecorated common case never
        // pays the suppression-map probe.
        const auto isWithheldThisFrame = [this, w, frameNowMs]() {
            const auto supLook = m_restoreSuppress.constFind(w);
            return supLook != m_restoreSuppress.constEnd() && frameNowMs < supLook->deadlineMs;
        };
        // needsBackdrop is METADATA and can over-report (pack declares it but
        // the linker dropped every backdrop uniform, or the pack failed to
        // compile). The fold for such a chain takes the all-static early
        // return and discards the capture every frame, so gate the capture on
        // the same linked-uniform evidence packVariesPerFrame uses — resolved
        // through the SAME lazy compile the fold uses, so the gate and the
        // fold agree within one frame. A raw cache probe here skipped the
        // capture on a fresh frost window's first paint (the fold compiled
        // the pack moments later and read a backdrop this gate had called
        // absent) and answered false for a frame after every registry reload
        // cleared the cache while a pre-reload backdropRect still claimed
        // validity.
        const auto chainReadsBackdrop = [this](const WindowDecoration& deco, const QString& windowId) {
            std::optional<PhosphorSurfaceShaders::DecorationProfile> profile;
            for (const QString& packId : deco.chain) {
                CompiledSurfacePack* pk = nullptr;
                if (const auto cacheIt = m_compiledPacks.find(packId); cacheIt != m_compiledPacks.end()) {
                    pk = &cacheIt->second;
                } else {
                    if (!profile) {
                        profile = m_decorationTree.resolve(resolveSurfacePathFor(windowId));
                    }
                    pk = compiledPack(packId, *profile);
                }
                if (!pk || !pk->shader) {
                    continue;
                }
                if (linksBackdropUniforms(pk->uBackdropLoc, pk->uHasBackdropLoc, pk->uBackdropRectLoc)) {
                    return true;
                }
                for (const CompiledSurfaceBufferPass& bp : pk->bufferPasses) {
                    if (linksBackdropUniforms(bp.uBackdropLoc, bp.uHasBackdropLoc, bp.uBackdropRectLoc)) {
                        return true;
                    }
                }
            }
            return false;
        };
        if (backIt != m_windowDecorations.constEnd() && backIt->needsBackdrop
            && (backIt->shaderApplied || m_shaderManager.findTransition(w)) && !isWithheldThisFrame()
            && chainReadsBackdrop(*backIt, backIt.key())) {
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
            captureWindowBackdrop(renderTarget, viewport, w, *backIt, deviceRegion, animatedFrame);
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
        // Scrolling-strip view offset, ADDED to whatever the window animator
        // just applied rather than replacing it. The two describe different
        // things and compose: the view says where the whole strip is, the
        // per-window animation says how this one column differs from riding it
        // (an edge column whose width changed in the same batch has both). A
        // pure scroll has no per-window animation at all, which is the point.
        //
        // Applied even when a shader owns the geometry. A morph shader
        // interpolates between two COMMITTED rects and knows nothing about the
        // view, so the strip sliding underneath it is not something it can
        // double-count — unlike the animator transform above, which describes
        // the same motion the shader is already drawing.
        if (KWin::LogicalOutput* managed = scrollManagedOutputFor(w)) {
            // A parked column is committed below the union of all outputs, so
            // relocate the drawing to where it really sits on the strip BEFORE
            // the view offset goes on. The two together put it exactly where a
            // never-parked column would be, which is what lets it be seen
            // travelling past during a scroll rather than blinking out the
            // moment it leaves the viewport.
            if (!m_scrollVisualPos.isEmpty()) {
                const auto vit = m_scrollVisualPos.constFind(getWindowId(w));
                if (vit != m_scrollVisualPos.constEnd()) {
                    const KWin::RectF committed = w->frameGeometry();
                    data += QPointF(vit->x() - committed.x(), vit->y() - committed.y());
                }
            }
            const qreal viewOffset = m_stripViewAnimator->offsetFor(managed);
            if (!qFuzzyIsNull(viewOffset)) {
                data += QPointF(viewOffset, 0.0);
            }
        } else if (KWin::LogicalOutput* out = w->screen();
                   out && m_stripViewAnimator->isAnimatingOn(out) && isScrollTabIndicatorSurface(w)) {
            // The tab indicators take the SAME offset as the columns they
            // label, from the same spring, inside the same paint pass. That is
            // the whole reason they were given a surface of their own: a second
            // spring in the daemon could never catch this one, because the
            // daemon renders and commits its surface for the compositor to
            // composite a frame or more later.
            //
            // The daemon pushes each indicator at its post-scroll rect, exactly
            // as the apply path commits each column's post-scroll geometry, so
            // one shared offset puts both back where they were and slides them
            // in step.
            //
            // The cheap map lookup is deliberately first: the scope test behind
            // it walks a cast and a string, and no surface needs an offset on
            // an output whose strip is at rest.
            data += QPointF(m_stripViewAnimator->offsetFor(out), 0.0);
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
