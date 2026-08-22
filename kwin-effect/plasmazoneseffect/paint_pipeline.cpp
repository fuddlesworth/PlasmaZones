// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// FILE-SIZE EXCEPTION (sanctioned): the paint pipeline is one temporal
// sequence — prePaintScreen through postPaintScreen with paintWindow and
// drawWindow between — whose stages share per-pass latches
// (m_currentPassOutput, the frame-clock pin, the strip-capture exclusion)
// that only hold within one bracket. Splitting the bracket across TUs would
// scatter the latch discipline that most of this file's comments exist to
// defend. Over the 1150 ceiling before PR #891 and accepted as such.

#include "plasmazoneseffect.h"
#include "compositor/compositorclock.h"
#include "handlers/navigationhandler.h"
#include "tilinghandler/tilinghandler.h"
#include "shader_internal.h"
#include "surface_fold.h"
#include "shader_resolve.h"
#include "window_query.h"

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>

#include <QTimer>

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
    // Either the daemon has resolved at least one screen to cropping (the
    // per-context SetScrollCropStraddlers rule folded with the setting), or
    // the map has not arrived yet and the global setting says crop. The
    // fallback is SEEDED-GATED, not a plain OR: an empty resolved map is
    // ambiguous on its own — "no screen crops" and "no reply yet" look
    // identical — and while the fallback applied to both, a rule resolving
    // every screen to false could never hand direct scanout back while the
    // global setting stayed on. Gating on the seeded flag keeps the bring-up
    // window no worse than the old global-flag test while making the resolved
    // map authoritative the moment it exists.
    if (m_tilingHandler && m_tilingHandler->hasScrollingScreens()
        && (m_tilingHandler->anyScreenCropsStraddlers()
            || (!m_tilingHandler->scrollEffectBehaviourSeeded() && m_cachedScrollCropStraddlers))) {
        return true;
    }
    // A live view spring translates every strip column in the COMPOSITE
    // path; a surface presented directly on a hardware plane bypasses the
    // effect chain, so a scanout-eligible fullscreen column would sit
    // un-translated while its neighbours slide. The same reasoning covers
    // the strip shader pass, which replaces the output with its decorated
    // capture — and the pass clause additionally holds through the settle
    // fade, which outlives the spring. Both costs are bounded by the leg
    // (plus the fade tail) rather than by mode.
    //
    // NOTE the RETURN VALUE is global (the API takes no output), but KWin
    // asks once per output, inside that output's paint bracket: composite()
    // runs prePaint, then layerCandidates -> blocksDirectScanout, then the
    // scene and postPaint, per RenderLoop. So m_currentPassOutput names
    // exactly the output whose scanout candidacy is being decided, and a
    // clause can be scoped to it. The spring and pass clauses above keep
    // their session-wide breadth (their state is not per-output here); a
    // scroll leg on one monitor still forces composition on every monitor
    // for its bounded duration.
    //
    // The pills take the per-output form. A pill band CAN share an output
    // with a scanout-eligible column: place-within-column is off by default
    // (nothing is reserved out of the column), and the gap floor is -64 by
    // design (niri parity pulls the indicator onto the window), so a
    // full-extent tabbed column with a negative gap presents an opaque
    // output-filling surface with the band painted over it. A frame that
    // went to a hardware plane would drop that band, so composition is
    // forced — but only on outputs that actually carry indicators.
    if (m_scrollTabPainter) {
        if (m_currentPassOutput) {
            if (m_scrollTabPainter->hasIndicators(m_currentPassOutput)) {
                return true;
            }
        } else if (m_scrollTabPainter->hasAnyIndicators()) {
            // Asked outside any bracket (a path this effect has not seen):
            // fall back to the conservative global answer rather than a
            // wrong per-output one.
            return true;
        }
    }
    // Deliberately NO clause for m_scrollVisualDelta (parked columns
    // relocated with no spring live, the edge auto-scroll's immediate path).
    // A relocated parked column cannot be lost to scanout: for scanout the
    // opaque candidate must be the output's TOP item, so a relocation
    // stacked above it un-tops the candidate and scanout is refused anyway,
    // while a relocation stacked below it is occluded and correct to drop.
    // Adding a clause keyed on the map would instead block scanout for as
    // long as any column stays parked, which is the steady state of every
    // overflowing strip.
    return m_stripViewAnimator->hasActiveAnimations() || m_stripTransition.isRunning();
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

    // Tab-indicator paint slotting, computed once per OUTPUT PASS — which
    // is N times per vsync on an N-monitor desktop, each pass walking the full
    // stacking order for its own output. It has to be per pass rather than per
    // frame: the anchor is the topmost strip member ON THIS OUTPUT, so the
    // answer differs per output by construction. The has-indicators gate
    // below is what keeps that walk off every desktop with no tabbed column.
    //
    // The pills are painted by this effect (ScrollTabIndicatorPainter), so
    // "where in the z-order" is entirely this file's decision: paintWindow
    // blits them right after the ANCHOR (the topmost strip window this pass
    // will draw), which puts them above every column and below whatever the
    // stacking puts over the strip. The filter drops the windows KWin plainly
    // will not draw (minimized, hidden, off-desktop); it CANNOT rule out an
    // anchor the scene culls later as fully occluded, and it does not try.
    // That case is covered by the SECOND trigger instead: the walk also
    // collects the windows above the final anchor (m_scrollTabAboveAnchor),
    // and paintWindow blits just before the first of those that paints — an
    // occluded anchor implies a visible occluder above it, so the blit runs
    // either way. A pass where NEITHER fires is still possible: a damage
    // region that touches the pill band but no strip window and no occluder
    // (a hover-only repaint of a pill reserved OUTSIDE its column, a client
    // repaint under the band). paintScreen's post-walk fallback blits then,
    // on top of what the pass painted — correct, because nothing stacked
    // above the strip painted in that region (it would have been an
    // above-anchor trigger), so the pills land where the anchor slot would
    // have put them. Inside a STRIP PASS capture only the anchor trigger is
    // reachable (the capture's above-strip exclusion returns before the
    // second trigger), which is fine: the capture runs under
    // PAINT_SCREEN_TRANSFORMED, the generic no-culling path, so the anchor
    // always paints there.
    //
    // KWin::effects IS NOT GUARDED ANYWHERE IN THIS FUNCTION, and that is the
    // file's rule rather than an omission here. prePaintScreen ends in an
    // unconditional `KWin::effects->prePaintScreen(data)` and postPaintScreen
    // in an unconditional `KWin::effects->postPaintScreen()`, so neither entry
    // point can survive a null global at all — a per-site guard inside them
    // buys nothing but the illusion of one. The guards that remain in this
    // tree sit on paths that genuinely CAN dispatch after teardown (a late
    // D-Bus reply, the park-reap timer's fire), not on the paint bracket.
    m_scrollTabPaintAnchor = nullptr;
    m_scrollTabPainted = false;
    m_scrollTabBlitIssued = false;
    m_scrollTabAboveAnchor.clear();
    if (data.screen && m_scrollTabPainter->hasIndicators(data.screen)) {
        const QRectF passOutputGeo = QRect(data.screen->geometry());
        for (KWin::EffectWindow* sw : KWin::effects->stackingOrder()) {
            // The paintability terms StripTransitionManager's above-strip
            // election uses, plus the activity half of "on the current
            // workspace": scrollManagedOutputFor applies neither a desktop
            // nor an activity term, so an off-activity column stays
            // scroll-managed exactly like an off-desktop one does (#808), and
            // a window KWin will not draw must not be elected as anchor.
            //
            // isDeleted is deliberately NOT among them: a closing window is
            // painted for its whole close animation (grab-held), and one over
            // the strip must be able to fire the above-anchor trigger so the
            // pills land under it rather than over it via the fallback. It
            // can still never anchor — scrollManagedOutputFor rejects deleted
            // windows — and a dead member that is never painted never reaches
            // paintWindow, so it sits in the set inert.
            if (!sw || sw->isMinimized() || sw->isHidden() || sw->isHiddenByShowDesktop() || !sw->isOnCurrentDesktop()
                || !sw->isOnCurrentActivity()) {
                continue;
            }
            if (scrollManagedOutputFor(sw) == data.screen) {
                // A parked-offscreen column cannot anchor: paintWindow culls it
                // (scrollParkedOffscreen), so an anchor picked here would never
                // paint and the blit would never run — the same
                // anchor-never-painted case the filter note above calls out.
                // Skip it and let a visible strip member win.
                //
                // Read as ONE TICK STALE, not as deterministic. This election
                // runs BEFORE advanceAnimations below, so the view offset the
                // predicate folds in is the one the PREVIOUS tick left behind.
                // At a leg boundary the two can disagree for a single frame,
                // both ways: a column that unparks this tick is skipped here
                // though the draw will paint it, and a column that parks this
                // tick is elected here though the draw will cull it. Neither
                // costs more than one frame with the pills under the second
                // trigger instead of the first. Moving the election after
                // advanceAnimations would tighten it, but the anchor must also
                // survive the scene's own occlusion culling, which nothing here
                // can predict, so the fallback has to stay correct regardless.
                if (scrollParkedOffscreen(sw, getWindowId(sw))) {
                    continue;
                }
                m_scrollTabPaintAnchor = sw; // topmost strip member wins
                // Everything collected so far sits BELOW the new anchor, so it
                // cannot be this pass's blit trigger.
                m_scrollTabAboveAnchor.clear();
            } else if (sw->screen() == data.screen || QRectF(sw->expandedGeometry()).intersects(passOutputGeo)) {
                // Candidate above-anchor trigger (see the member's doc): a
                // non-strip window on this output stacked over the current
                // anchor. Provisional while the walk runs — a later strip
                // member resets it above — so what survives is exactly the
                // windows above the FINAL anchor. paintWindow blits the pills
                // just before the first of these that paints, which is what
                // keeps a culled anchor from losing the pills under the very
                // dialog or OSD covering the strip (and from flickering
                // against it as occlusion comes and goes).
                //
                // screen() OR rect intersection, per the transition-relevance
                // convention above: a window straddling outputs but ASSIGNED to
                // the neighbour still paints in this pass and its opaque region
                // can cull the anchor exactly like a same-screen occluder, so
                // keying on screen() alone left that configuration with neither
                // trigger firing. A foreign-MANAGED strip column (crop-mode
                // straddler) can land here too now; it is inert — paintWindow's
                // foreign-output cull returns before the trigger — and it could
                // not have culled the anchor anyway, since prePaintWindow marks
                // it translucent on foreign passes. Over-inclusion is harmless:
                // a member this pass never paints simply never triggers.
                m_scrollTabAboveAnchor.append(sw);
            }
        }
        // No column on this output — the mode-teardown race (indicators
        // outliving their strip by a frame), or every strip column parked
        // off the viewport at once (short strip flung past its extent, or
        // transiently mid-leg): nothing to stack above, so no pills this pass.
        if (!m_scrollTabPaintAnchor) {
            m_scrollTabAboveAnchor.clear();
        }
    }
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
    //
    // The strip term: a scroll leg translates decorated columns by a qreal
    // view offset through their permanently-redirected present quads, and
    // Round mode would quantize exactly those translates to device pixels
    // while undecorated columns in the same strip keep sub-pixel precision —
    // a per-column desync that reads as shear at fractional scale, worst
    // near settle where the residual motion is sub-pixel. Same
    // decorations-exist gate as the probe: with no decorated window there is
    // no redirected quad to protect.
    const bool redirectedAnimating = !m_shaderManager.empty()
        || (!m_windowDecorations.isEmpty() && m_stripViewAnimator->hasActiveAnimations())
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

    // Same arm for the strip pass: while this output's view spring is live
    // with a pack armed, paintScreen replaces the scene with the decorated
    // capture, and the capture itself relies on this mask routing the scene
    // through the generic infinite-region path (see captureLiveScene's
    // region note — a damage-clipped capture goes black on any secondary
    // monitor).
    const bool stripOnThisOutput =
        data.screen ? m_stripTransition.isRunningForOutput(data.screen) : m_stripTransition.isRunning();
    if (stripOnThisOutput) {
        data.mask |= PAINT_SCREEN_TRANSFORMED;
    }

    // Cache cursor pos once per frame for the iMouse uniforms. paintWindow
    // runs once per active transition (and may run multiple times across
    // outputs); reading KWin::effects->cursorPos() at every call multiplies
    // up. Caching here also guarantees every consumer this frame reads an
    // identical iMouse, eliminating sub-frame jitter. Decorated windows read
    // it too (hover-reactive surface packs via pushBorderUniforms), so the
    // refresh also runs while any decoration exists, not only mid-transition.
    if (!m_shaderManager.empty() || !m_windowDecorations.isEmpty()) {
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
    // GL-current point reached on every pass the effect takes part in,
    // including the transition-owned ones below: textures retired by a strip
    // that went away are deleted here. The clear sites drain too (under a
    // made-current context), because once the last indicator is gone the
    // effect can leave the paint chain and this is never reached again; this
    // covers a retire that happens between a clear and the next pass.
    m_scrollTabPainter->drainRetiredTextures();
    // While a desktop-switch transition is live for this output, paintOutput
    // draws the two-desktop blend into the screen target and returns true, so we
    // skip the normal scene paint. Otherwise (no transition, or it just settled)
    // chain straight through to the standard scene — this override is a no-op for
    // every non-transitioning frame.
    if (m_desktopTransition.paintOutput(renderTarget, viewport, mask, deviceRegion, screen)) {
        return;
    }
    // The strip pass sits BELOW the desktop transition on purpose: a desktop
    // switch replaces the scene wholesale, so a strip pass under it would
    // decorate a frame nobody sees. When the strip pass paints (captures the
    // scene, runs the pack, returns true) the normal scene paint is skipped
    // the same way.
    if (m_stripTransition.paintOutput(renderTarget, viewport, mask, deviceRegion, screen)) {
        return;
    }
    // The scene walk's own damage region is the clip for the pill blit, for
    // the whole walk (see m_scrollTabWalkRegion): every trigger inside
    // paintWindow reads it, and so does the fallback below.
    const ScrollTabWalkScope walkScope(*this, deviceRegion, /*resetPaintedLatch=*/false);
    KWin::effects->paintScreen(renderTarget, viewport, mask, deviceRegion, screen);
    // Post-walk fallback: the pills' damage touched neither a strip window
    // nor anything stacked above the strip, so no paintWindow trigger fired
    // and the pass would otherwise have repainted the band from underneath
    // and erased them. Nothing above the anchor painted in this region
    // (that would have been the second trigger — the collection admits
    // closing windows too, so a grab-held corpse over the strip triggers
    // like any live occluder), so blitting last is the same stacking as
    // blitting at the anchor's slot. Still requires an
    // anchor: with no strip column on the output there is nothing the pills
    // belong to this pass.
    if (screen && m_scrollTabPaintAnchor && !m_scrollTabPainted && !m_capturingSnapshot
        && m_scrollTabPainter->hasIndicators(screen)) {
        paintScrollTabIndicators(renderTarget, viewport, deviceRegion);
    }
}

void PlasmaZonesEffect::postPaintScreen()
{
    // KWin::effects is dereferenced unguarded throughout, per the rule stated
    // in prePaintScreen: this function ends in an unconditional
    // `KWin::effects->postPaintScreen()`, so it cannot complete with a null
    // global under any circumstances and a per-site guard would only hide that
    // from the reader. The ONE guard below sits in the park-reap timer's
    // callback, which is not part of this bracket — it fires later, from the
    // event loop, and can genuinely land after compositor teardown.
    //
    // Pass over. Defensive hygiene: every capture path in this tree reaches
    // paintWindow from INSIDE the pass (before this runs), so the clear only
    // protects a hypothetical paintWindow outside any bracket. Recorded
    // first, then cleared: notePassOutcome below is the ONE reader that
    // needs the bracket's output, and nothing after it does, so a future
    // reader added lower down sees the bracket as already closed.
    //
    // Whether this pass issued the pill blit is what the input side gates on
    // (a model with nothing blitted must answer nothing). A pass that had
    // indicators but no anchor (every column parked) records false.
    if (m_currentPassOutput && m_scrollTabPainter->hasIndicators(m_currentPassOutput)) {
        m_scrollTabPainter->notePassOutcome(m_currentPassOutput, m_scrollTabBlitIssued);
    }
    m_currentPassOutput = nullptr;
    // The re-slotting state holds raw EffectWindow pointers, and between
    // passes a window can die; the next prePaintScreen recomputes them, but
    // clearing here means no dangling pointer ever survives the bracket.
    m_scrollTabPaintAnchor = nullptr;
    m_scrollTabPainted = false;
    m_scrollTabBlitIssued = false;
    m_scrollTabAboveAnchor.clear();
    // Same reasoning, one map further: the per-pass resolve memo keys on raw
    // EffectWindow* and stores raw LogicalOutput*, and either can die between
    // passes. Every read is gated on the in-pass latch cleared just above, and
    // prePaintScreen clears this before the pass's first read, so the stale
    // entries were never reachable — but that safety rested on an argument
    // spanning two files. Clearing here makes it local to the bracket.
    m_scrollManagedCache.clear();
    // Schedule targeted repaints for active animations instead of full-screen
    m_windowAnimator->scheduleRepaints();
    // Keep the desktop-switch transition ticking (per-output repaints) while live.
    m_desktopTransition.scheduleRepaints();
    // Free strip-pass entries whose view spring has settled (the spring's own
    // repaint pump drives live legs; this is resource hygiene, not a ticker).
    m_stripTransition.reapSettled();
    // A view leg slides the pills under a stationary pointer, and hover is
    // otherwise motion-driven only: on the active→settled edge re-evaluate
    // it at the live pointer, off the paint path (the hover update can start
    // or stop the mouse interception, which is not a paint-time call).
    const bool legActive = m_stripViewAnimator->hasActiveAnimations();
    if (m_scrollTabLegWasActive && !legActive && m_scrollTabPainter->hasAnyIndicators()) {
        QTimer::singleShot(0, this, [this] {
            if (KWin::effects) {
                m_tilingHandler->updateScrollTabHover(KWin::effects->cursorPos());
            }
        });
    }
    m_scrollTabLegWasActive = legActive;
    // Per-output damage dedup shared by the two window loops below: K live
    // transitions (or suppressed windows) on one output otherwise issue K
    // identical full-output addRepaint calls per frame. One repaint per
    // output per postPaintScreen is the whole point of screen-level damage.
    QSet<const KWin::LogicalOutput*> damagedOutputs;
    bool damagedAll = false;
    const auto damageOutputOnce = [&](const KWin::LogicalOutput* output) {
        if (damagedAll) {
            return;
        }
        if (!output) {
            KWin::effects->addRepaintFull();
            damagedAll = true;
            return;
        }
        if (!damagedOutputs.contains(output)) {
            damagedOutputs.insert(output);
            KWin::effects->addRepaint(output->geometry());
        }
    };
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
            // paintWindow, and paintWindow is the only teardown for a durationMs == 0
            // (animator-driven) leg reachable from the PAINT path — it has no timer.
            // (The animator's own completion callback tears the same leg down
            // independently of painting, so the runaway below needs the animator to
            // never complete, not merely paintWindow to be skipped.) Without this,
            // snapping a window and then switching virtual desktop mid-morph leaves
            // the arm below requesting a FULL-OUTPUT repaint every vsync.
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
                if (timeBasedActive || animatorActive || heldActive) {
                    damageOutputOnce(w->screen());
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
                damageOutputOnce(w->screen());
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
    //
    // The clock prePaintScreen pinned for this cycle (it is unpinned at the
    // end of this function, so it is still live here). Read ONCE for every
    // consumer below: a live per-window sample would let two windows in the
    // same frame disagree about the time.
    const qint64 pinnedMs = m_shaderManager.currentFrameClockMs();
    const qint64 frameClockMs = pinnedMs >= 0 ? pinnedMs : ShaderInternal::shaderClockNowMs();
    for (auto it = m_restoreSuppress.cbegin(); it != m_restoreSuppress.cend(); ++it) {
        KWin::EffectWindow* sw = it.key();
        if (!sw || sw->isDeleted()) {
            continue;
        }
        // Past its own deadline, an entry must stop driving. paintWindow
        // enforces the 250 ms deadline and erases the entry — but only for a
        // window paintWindow actually reaches, and an OFF-DESKTOP window never
        // does. Without this test such an entry pumped a full-output repaint
        // every frame, forever, for a window nobody can see: the release
        // branch that would clear it is precisely the one that never runs.
        // A LIVE suppression is unaffected — it is still inside its deadline,
        // still driven here, and still erased by paintWindow's release branch
        // on the frame the deadline passes (or by the settle hook before it).
        // The skipped entry is not erased here, only unpumped: it stays in the
        // map until the window closes, or until it returns to the current
        // desktop and paintWindow's release branch takes it. Deliberate — the
        // window is the one thing that can tell us it is visible again. The
        // readers that are not themselves deadline-gated either clear the
        // residual or are startup one-shots; the paint-path one is
        // prePaintWindow's transformDriven probe, which self-clears on the
        // first paint that reaches the window — the same frame it becomes
        // visible again. So the residual entry costs a hash slot and nothing
        // else.
        if (frameClockMs >= it->deadlineMs) {
            continue;
        }
        damageOutputOnce(sw->screen());
    }
    // Drive continuous repaints for windows whose surface decoration animates
    // (a pack in the chain references iTime). Without content damage their
    // paintWindow would not fire and iTime would stall, so damage each such
    // window's full area every frame while the border owns the slot (idle — a
    // live transition drives its own repaints in the loop above and the surface
    // composite degrades to single-pass there anyway). A purely static
    // decoration (border-only) is not matched, so this is a no-op in the common
    // case. windowSurfaceAnimates is per-pack-cache hash lookups.
    //
    // Earliest pending park reap seen this pass (-1 = none). Declared outside
    // the decorations gate so the arm/stop block below runs even when the
    // last decoration was just removed — a pending timer must be stopped in
    // that case too, or its stray fire composites an idle desktop.
    qint64 nextParkReapInMs = -1;
    if (!m_windowDecorations.isEmpty()) {
        // `frameClockMs` is the pinned per-frame clock read above the
        // suppression loop, shared by every consumer in this function.
        //
        // PER OUTPUT PASS, not per frame. postPaintScreen closes ONE output's
        // bracket, so on an N-output desktop this whole loop — the decoration
        // walk, the park probe and the reap arithmetic — runs N times per
        // vsync over the SAME window set (the predicates are output-agnostic,
        // which is what lets the timer arm/stop below not flap between
        // passes). The repeats are idempotent and the damage is deduped per
        // output by damageOutputOnce, so the cost is a re-walk rather than
        // duplicated work; it is stated here so nobody reads "once a frame"
        // into the loop when sizing it.
        //
        // The park cull below removes the very repaint driver that used to
        // keep this loop running, so once the desktop goes idle nothing
        // composites and the reap threshold is never re-evaluated — the
        // timer armed after the loop forces one frame at the deadline so
        // the reap actually fires.
        for (auto it = m_windowDecorations.cbegin(); it != m_windowDecorations.cend(); ++it) {
            KWin::EffectWindow* const sw = findWindowByIdExact(it.key());
            // Exact-id discipline (mirrors reconcileDecorationOnPlacementFlip and
            // the teardown paths): findWindowById's fuzzy appId fallback can
            // return a same-app sibling for a stale id, and repainting the
            // sibling would be wrong. Skip unless it re-derives to this exact id.
            if (!sw || getWindowId(sw) != it.key() || sw->isDeleted() || !sw->isOnCurrentDesktop()) {
                continue;
            }
            // The shaderApplied gate is deliberately NOT the loop's first test,
            // and the resolve above deliberately precedes it.
            //
            // shaderApplied means "we own this window's decoration slot", and
            // reconcileDecorationShader CEDES that ownership — clears the flag —
            // for the whole time a shader transition holds the window. Skipping a
            // ceded window outright meant a column parked while a transition owned
            // it never reached the park block below, so parkedSinceMs was never
            // stamped, nextParkReapInMs never accounted for it, and the arm/stop
            // block then STOPPED the timer. On an idle desktop nothing composites
            // after that (the park cull removed the only driver), so the
            // full-canvas composite, capture and backdrop stayed resident for the
            // rest of the session — the precise cost this reap exists to avoid,
            // closed on one entry path and wide open on the other.
            //
            // The chain is ordinary rather than exotic: every tile batch ends in
            // updateAllDecorations, which walks the whole stacking order, parked
            // columns included. A transition live on one of them at that moment
            // cedes the flag, and on an idle desktop nothing calls reconcile again
            // to give it back.
            //
            // A ceded window is therefore let through to the park/reap half. It is
            // still held back from the backdrop driver further down, which
            // genuinely does require ownership — releaseSurfaceState declines
            // while the transition is live and the retry below re-arms, so the
            // reap resumes on its own once the transition drops.
            const bool cededToTransition = !it->shaderApplied && m_shaderManager.findTransition(sw) != nullptr;
            if (!it->shaderApplied && !cededToTransition) {
                continue;
            }
            // The repaint-driver site of scrollParkedOffscreen's contract: stop
            // driving a column parked off the viewport. Without this the
            // ~30fps backdrop pump below re-armed for every parked glass
            // column forever (backdropDue takes the focus exemption, so
            // m_animateFocusedOnly never saved it), and each repaint forced a
            // full invisible fold. The next tile batch that scrolls the
            // column back damages via the strip-relocation change (tiling.cpp
            // pairs every change with addRepaintFull), so waking needs no
            // driver.
            if (scrollParkedOffscreen(sw, it.key())) {
                // Long-parked columns also surrender their GL targets — the
                // composite pair, capture and backdrop are full-canvas RGBA8
                // (~100+ MB per 4K window) held for pixels nobody can see.
                // The threshold keeps neighbour-to-neighbour scrolling warm:
                // a column that returns within it still has its capture, so
                // waking is a refold, not a realloc + drawWindow re-entry.
                // Past it, the state is erased whole (releaseSurfaceState
                // guards the transition case and makes the GL context
                // current) and the first paint back rebuilds from scratch —
                // one cold fold, paid mid-scroll when every visible window
                // is repainting anyway. (When a shader transition is live on
                // the window, releaseSurfaceState early-returns WITHOUT
                // erasing, and the stamp survives inside the kept state — that
                // case re-arms the timer on a short retry, see below.) The
                // pinned clock, like every other consumer in this loop; the
                // parked stamp is inside the state so the erase disposes of it
                // with everything else.
                constexpr qint64 kParkReapMs = 10000;
                if (const auto sit = m_surfaceMultipass.find(it.key()); sit != m_surfaceMultipass.end()) {
                    qint64 remainingMs = -1;
                    if (sit->second.parkedSinceMs < 0) {
                        sit->second.parkedSinceMs = frameClockMs;
                        remainingMs = kParkReapMs;
                        // The stamp is the reap's whole clock, and until it exists
                        // nothing can reclaim the window's full-canvas targets. It
                        // is worth a line: a column that parks and never logs this
                        // is a column whose render targets are pinned for the
                        // session, which is invisible from the outside.
                        qCDebug(lcEffect) << "park reap: stamped" << it.key()
                                          << (cededToTransition ? "(slot ceded to a live transition)" : "");
                    } else if (frameClockMs - sit->second.parkedSinceMs >= kParkReapMs) {
                        releaseSurfaceState(it.key(), sw);
                        // The refusal case needs its own wake. The note above
                        // says the retry is "driven by the transition's own
                        // repaints, until the transition drops" — that holds
                        // only while the transition is pumping. An anchor-extent
                        // leg issues no damage when it ends (only surfaceExtent
                        // legs repaint on teardown) and the postPaintScreen
                        // transition pump has already gone false by then, so on
                        // an idle desktop this loop would never run again. With
                        // remainingMs left at -1 the arm below then STOPS the
                        // timer, and the full-canvas composite, capture and
                        // backdrop stay resident for the rest of the session —
                        // the exact cost the reap exists to avoid.
                        //
                        // So re-probe: if the state survived the refusal, keep
                        // the timer armed on a short retry. Deliberately NOT
                        // resetting parkedSinceMs, so the entry stays elapsed
                        // and the next attempt fires immediately once the
                        // transition drops. The successful erase invalidates
                        // sit, so this goes by key rather than through it.
                        constexpr qint64 kParkReapRetryMs = 1000;
                        const bool refused = m_surfaceMultipass.find(it.key()) != m_surfaceMultipass.end();
                        if (refused) {
                            remainingMs = kParkReapRetryMs;
                        }
                        qCDebug(lcEffect) << "park reap:" << (refused ? "refused, retrying" : "released") << it.key();
                    } else {
                        remainingMs = kParkReapMs - (frameClockMs - sit->second.parkedSinceMs);
                    }
                    if (remainingMs >= 0 && (nextParkReapInMs < 0 || remainingMs < nextParkReapInMs)) {
                        nextParkReapInMs = remainingMs;
                    }
                }
                continue;
            }
            if (const auto sit = m_surfaceMultipass.find(it.key());
                sit != m_surfaceMultipass.end() && sit->second.parkedSinceMs >= 0) {
                // Back on (or near) the viewport before the reap fired:
                // clear the stamp so a later park restarts the clock.
                // Runs for a ceded window too — unparking is unparking whoever
                // owns the slot, and leaving a stale stamp would make the next
                // park reap early.
                sit->second.parkedSinceMs = -1;
            }
            // Everything below drives repaints for a slot we own. A ceded window
            // was let past the gate above only for the park/reap half; its
            // transition is pumping its own repaints, so driving it from here
            // would be both redundant and a claim on a slot that is not ours.
            if (!it->shaderApplied) {
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
    // Out-of-band reap trigger: a parked column requests no repaints and
    // nothing else on an idle desktop may either, so without this the GL
    // targets the reap exists to free are held until the next incidental
    // frame ("scrolled a column away and walked off" holds them forever).
    // One composited frame at the deadline re-runs the loop above; the timer
    // is restarted with the recomputed minimum on every pass while anything
    // is pending, and after the last reap no stamp survives to arm it. The
    // else-stop matters as much as the arm: a column that unparks inside the
    // threshold (the designed neighbour-scroll case), or the last decoration
    // being removed, leaves nothing pending — without the stop the previously
    // armed deadline would still fire one full-screen wake on an idle
    // desktop. Every pass computes the same answer over the same window set
    // (the predicate is output-agnostic), so the stop cannot flap between
    // output passes.
    if (nextParkReapInMs >= 0) {
        if (!m_parkReapTimer) {
            m_parkReapTimer = new QTimer(this);
            m_parkReapTimer->setSingleShot(true);
            connect(m_parkReapTimer, &QTimer::timeout, this, []() {
                if (KWin::effects) {
                    KWin::effects->addRepaintFull();
                }
            });
        }
        // Small slack so the frame the timer forces lands past the
        // threshold rather than one clock tick short of it. The value is
        // bounded by kParkReapMs, so the int cast cannot truncate.
        m_parkReapTimer->start(static_cast<int>(nextParkReapInMs) + 50);
    } else if (m_parkReapTimer) {
        m_parkReapTimer->stop();
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
    // Park predicate, derived once for the TWO transformed-flag gates below
    // (padded decoration and scroll-strip): both must withhold the flag for a
    // column parked off the viewport, or KWin keeps it in the paint set at
    // full decoration cost forever. No pre-gates here — the predicate's own
    // cheapest-first ordering (empty map, then the delta probe) already
    // exits early for every non-strip window.
    const bool parkedOffscreen = scrollParkedOffscreen(w, windowId);

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
        // Not for a parked column: this branch is not naturally park-aware,
        // and every bundled backdrop pack is padded — without the gate here
        // the park cull's prePaint third was defeated for exactly the
        // windows it targets (the later strip gate cannot withhold what this
        // one already set).
        if (decorated && decoIt->outerPadding > 0 && !parkedOffscreen) {
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
    // it goes from that rect.
    //
    // Gated on the SAME predicate paintWindow relocates under, not on map
    // membership alone. A window that floats or is dragged to another output
    // stops being scroll-managed, so the relocation stops while its entry
    // lingers — flagging it transformed then would surrender occlusion culling
    // every frame of the drag for a window nothing is moving. windowId is the
    // one derived above rather than a second getWindowId call, which is what
    // the note at the top of this function asks for.
    //
    // ONLY while its visual rect actually touches the viewport. TRANSFORMED
    // does not just relocate — it forces KWin to keep the window in the paint
    // set, so an unconditional flag here kept every parked column (visual rect
    // off every output, invisible by construction) painting at full decoration
    // cost forever. Withholding the flag lets KWin's own culling skip it;
    // this is one of several sites enforcing the same predicate, whose
    // authoritative list lives on scrollParkedOffscreen's declaration. The view
    // offset is part of the tested rect, so a column scrolling back toward the
    // viewport re-earns the flag on the frame it starts to intersect.
    //
    // Term order is deliberate: the cheap hash probe first, the resolve after.
    // `m_scrollVisualDelta.contains(windowId)` is one lookup in the same map
    // the isEmpty() gate just tested, while scrollManagedOutputFor walks the
    // tracked-screen resolution (memoised per pass, but only after the first
    // ask), so a non-strip window on a scrolling desktop now answers on the
    // probe instead of paying the resolve.
    if (w && !m_scrollVisualDelta.isEmpty() && m_scrollVisualDelta.contains(windowId) && scrollManagedOutputFor(w)
        && !parkedOffscreen) {
        data.setTransformed();
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
    // is not its managed screen. The predicate lives in scrollManagedOutputFor
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
        //
        // `w` IS TREATED AS NULLABLE THROUGHOUT THIS FUNCTION, here included.
        // Both scroll predicates and getWindowId tolerate a null window, so
        // the guard is not there to stop a crash — it is there so every use of
        // `w` in this function reads the same way, rather than leaving the
        // reader to check three callee contracts to find out which uses are
        // load-bearing. The shared derivation below spells it `w ? ... : ...`
        // for the same reason.
        if (const KWin::LogicalOutput* managed = w ? scrollManagedOutputFor(w) : nullptr;
            managed && managed != m_currentPassOutput) {
            return;
        }
        // Off-viewport park cull: the paintWindow site of scrollParkedOffscreen's
        // contract. The authoritative list of enforcement sites lives on that
        // predicate's declaration — deliberately not restated (nor counted)
        // here, because every copy of the count went stale as sites were added.
        // A parked column's visual rect touches no part of
        // its output, so everything below — the backdrop capture, the decoration
        // fold, the draw itself — is work on pixels nobody can see. Behind the
        // same m_capturingSnapshot exemption as the foreign-output cull above,
        // and for the same reason: a snapshot capture of a parked column (close
        // snapshot, decoration capture) is legitimate offscreen work and builds
        // its viewport from the window's own rect. getWindowId here rather than
        // the shared derivation below, which sits after these early returns by
        // design; the predicate's own cheap gates keep the common case at one
        // empty-map probe.
        if (w && scrollParkedOffscreen(w, getWindowId(w))) {
            return;
        }
    }

    // Strip-pass capture exclusion. ORDER IS LOAD-BEARING: this block must
    // stay BELOW the foreign-output cull above. The cull is what keeps a
    // neighbouring output's strip column from ever reaching this record —
    // hoist this block above it and monitor B's columns get recorded into
    // monitor A's composite set and painted sharp into A's frame.
    //
    // While StripTransitionManager captures the
    // scene for its post-process, every window stacked ABOVE the strip (OSDs,
    // notifications, floating windows, panels, daemon overlays) is skipped
    // here and RECORDED — the manager composites exactly the recorded set,
    // in this same bottom-to-top paint order, sharp on top of the shader
    // output. Without this the capture is the whole scene and a volume OSD
    // popped mid-scroll gets motion-blurred with the columns.
    //
    // Membership in m_stripCaptureAboveStrip IS the predicate: the manager
    // prebuilds it from KWin's stacking order (everything above the topmost
    // strip member that intersects the capture output) right before the
    // capture, so "above the strip" here is a stacking fact, not a role
    // guess. The latch is scoped to the capture's paintScreen call, so the
    // top-composite's own paintWindow re-entry (latch already cleared)
    // paints normally. The !m_capturingSnapshot guard mirrors the foreign-
    // output cull above: a window-rect snapshot capture re-entering inside
    // the strip capture must not be diverted (latent today — an above-strip
    // window returns here before any snapshot is driven for it — but the
    // symmetry keeps it latent).
    //
    // The record is deduplicated by MEMBERSHIP (first-wins), not by comparing
    // against the tail. A tail check only catches back-to-back repeats, while
    // the shape that would defeat it is a window driven through this function
    // twice in one capture walk with other records in between. Recorded twice,
    // the composite would alpha-blend that window twice and draw it at two
    // different depths.
    //
    // POSITION IN THIS LIST IS THE COMPOSITE'S STACKING ORDER — StripTransition
    // Manager replays it bottom to top — so which record survives decides the
    // window's depth, and first-wins keeps the depth its first (natural) slot
    // gave it rather than a later re-entry's.
    //
    // Insurance rather than an observed fault: nothing drives a window through
    // this function twice inside one strip capture today. Cheap to keep correct
    // either way.
    if (!m_capturingSnapshot && m_stripCaptureExclusionOutput && m_stripCaptureAboveStrip.contains(w)) {
        if (!m_stripCaptureSkippedWindows.contains(w)) {
            m_stripCaptureSkippedWindows.append(w);
        }
        return;
    }

    // Compositor-drawn tab indicators: blit them at the ANCHOR's slot — right
    // after the topmost strip window this pass draws — so they sit above every
    // column and below whatever the stacking puts over the strip. A scope
    // guard rather than a call at the tail because the anchor may exit through
    // any of the branch returns (shader transition, direct-capture, redirected
    // present). Painted at most once per output pass (m_scrollTabPainted):
    // prePaintScreen resets the latch, and the two triggers below are mutually
    // exclusive through it.
    //
    // The latch is scoped to the PASS, not to a scene walk, because the blit
    // is not a window: the capture walks (desktop transition, strip pass)
    // swap their own window-drawn bookkeeping around a nested paintScreen,
    // but a pill blit inside a capture is simply part of what the capture
    // sees — exactly where the columns are — and re-blitting it again on the
    // presented walk would double it. One blit per pass is right either way.
    const bool blitTabsAfterThisWindow =
        !m_capturingSnapshot && !m_directPaintCapture && w && w == m_scrollTabPaintAnchor && !m_scrollTabPainted;
    const auto tabBlitGuard = qScopeGuard([&]() {
        if (blitTabsAfterThisWindow && !m_scrollTabPainted) {
            paintScrollTabIndicators(renderTarget, viewport, deviceRegion);
        }
    });
    // Second trigger: the anchor's paint alone is not reliable. The scene
    // culls a fully occluded anchor — a dialog or a raised floating window
    // covering the column — and then the guard above never arms. So ALSO blit
    // just before the first window stacked above the anchor paints — an
    // occluded anchor implies a visible occluder above it, so one of the two
    // triggers always fires, and the pills land under that occluder rather
    // than flickering over it as the anchor's culling comes and goes.
    if (!m_capturingSnapshot && !m_directPaintCapture && w && m_scrollTabPaintAnchor && !m_scrollTabPainted
        && m_scrollTabAboveAnchor.contains(w)) {
        paintScrollTabIndicators(renderTarget, viewport, deviceRegion);
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

    // Derived ONCE for the rest of this function, matching prePaintWindow's
    // stated convention: four consumers below (backdrop probe, scroll
    // relocation, parked-column offset, decoration fold) each re-derived it,
    // and while getWindowId is cached, four hash probes plus refcounts per
    // window per output pass is avoidable hot-path work. Sits after the
    // early returns so the common skipped paths pay nothing.
    const QString windowId = w ? getWindowId(w) : QString();

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
        const auto backIt = m_windowDecorations.constFind(windowId);
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
        // 0.0 = no compiled pack reads the backdrop (skip the capture);
        // 1.0 = some MAIN pass samples it sharp (full-density capture);
        // otherwise the largest bufferScale among the buffer passes that link
        // it — a blur pyramid reads the capture at bufferScale resolution
        // through normalized uvs, so capturing past that density stores and
        // re-blits texels the samplers stride over. Max, not min: a chain
        // with two blur packs must satisfy the denser reader.
        // The id parameter is named apart from the `windowId` derived once for
        // this function: the lambda is handed `backIt.key()`, which is the same
        // string, and a shadowing name made that look like a coincidence to be
        // checked rather than the identity it is.
        const auto chainBackdropScale = [this, w](const WindowDecoration& deco, const QString& decoWindowId) -> qreal {
            std::optional<PhosphorSurfaceShaders::DecorationProfile> profile;
            qreal scale = 0.0;
            for (const QString& packId : deco.chain) {
                CompiledSurfacePack* pk = nullptr;
                if (const auto cacheIt = m_compiledPacks.find(packId); cacheIt != m_compiledPacks.end()) {
                    pk = &cacheIt->second;
                } else {
                    if (!profile) {
                        // Pass the window: the id-only overload re-derives through
                        // findWindowByIdExact, which is a wasted lookup when the
                        // caller already holds the very window it would find. Same
                        // form the surfacelayers.cpp sibling uses.
                        profile = m_decorationTree.resolve(resolveSurfacePathFor(decoWindowId, w));
                    }
                    pk = compiledPack(packId, *profile);
                }
                if (!pk || !pk->shader) {
                    continue;
                }
                if (linksBackdropUniforms(pk->uBackdropLoc, pk->uHasBackdropLoc, pk->uBackdropRectLoc)) {
                    return 1.0; // a sharp main-pass read caps every other answer
                }
                for (const CompiledSurfaceBufferPass& bp : pk->bufferPasses) {
                    if (linksBackdropUniforms(bp.uBackdropLoc, bp.uHasBackdropLoc, bp.uBackdropRectLoc)) {
                        // Same clamp ensureSurfaceTargets applies when sizing
                        // the buffer targets themselves, so capture density
                        // and sampler density agree by construction. The
                        // clamped value is cached per pack: bufferScale is
                        // pack METADATA (unlike the linked-uniform probes
                        // above, which are compile state and MUST resolve
                        // through the lazy compile — see the comment above
                        // this lambda), and the registry lookup copies a
                        // whole SurfaceShaderEffect by value, which this
                        // per-frame path must not pay per pack. The cached
                        // value is the multiplier-folded PRODUCT, so it has
                        // three invalidators: the two m_compiledPacks clears
                        // (a registry hot-reload can change the metadata) and
                        // the blur-scale-multiplier loader in
                        // daemon_settings.cpp.
                        qreal packScale = 0.0;
                        if (const auto bsIt = m_packBufferScaleCache.find(packId);
                            bsIt != m_packBufferScaleCache.end()) {
                            packScale = bsIt->second;
                        } else {
                            packScale = clampedBufferScale(m_surfaceShaderRegistry.effect(packId).bufferScale);
                            m_packBufferScaleCache.emplace(packId, packScale);
                        }
                        scale = qMax(scale, packScale);
                        break; // one linked buffer pass answers for the pack
                    }
                }
                // A buffer pass at the ceiling is already the maximum
                // possible answer (the main-pass branch above returns the
                // same value), so stop walking the chain — restores the
                // short-circuit the pre-scale code had for every pack.
                if (scale >= PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferScale) {
                    return PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferScale;
                }
            }
            return scale;
        };
        qreal backdropScale = 0.0;
        if (backIt != m_windowDecorations.constEnd() && backIt->needsBackdrop
            && (backIt->shaderApplied || m_shaderManager.findTransition(w)) && !isWithheldThisFrame()
            && (backdropScale = chainBackdropScale(*backIt, backIt.key())) > 0.0) {
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
            // Fold in the two scroll displacements the draw applies further
            // down, which neither term above accounts for. Without this a
            // decorated scrolling column samples the scene slice at its
            // COMMITTED rect while being drawn somewhere else: a view offset
            // away for the length of every leg, and for a parked column at a
            // rect that intersects no output at all, which is a garbage
            // capture re-blitted every frame rather than a slightly-off one.
            //
            // Order matches the draw: relocate to the strip position first,
            // then add the view offset. The relocation is ADDITIVE (a
            // translate by the stored strip-minus-park delta), mirroring the
            // draw's `data += delta` — an absolute moveTopLeft here discarded
            // whatever the animator term above contributed, so a parked column
            // with a live per-window leg sampled its backdrop slice at the
            // wrong x for the leg's duration.
            if (KWin::LogicalOutput* scrollOut = scrollManagedOutputFor(w)) {
                if (!animatedFrame.isValid()) {
                    animatedFrame = w->frameGeometry();
                }
                if (const auto visualIt = m_scrollVisualDelta.constFind(windowId);
                    visualIt != m_scrollVisualDelta.constEnd()) {
                    // The stored strip-minus-park delta, matching the draw.
                    animatedFrame.translate(visualIt->x(), visualIt->y());
                }
                animatedFrame.translate(m_stripViewAnimator->offsetFor(scrollOut));
            }
            captureWindowBackdrop(renderTarget, viewport, w, *backIt, deviceRegion, animatedFrame, backdropScale);
        }
    }

    // First-frame open suppression: a window repositioned on open
    // (snap-restore / autotile) is withheld from compositing until its
    // moveResize configure lands, so it never flashes at KWin's centred
    // placement. Paint nothing until then. The deadline is the safety net
    // — if the reposition never lands, release and paint normally rather
    // than risk a permanently invisible window.
    // The !m_capturingSnapshot term matches every sibling preamble block, and
    // here it also guards a MUTATION: this block re-stamps the transition
    // clock and returns without drawing, which inside a capture would latch a
    // cleared FBO as the snapshot. Unreachable today (the closed draw chain,
    // see the guard above), so this is symmetry plus insurance.
    if (auto supIt = m_restoreSuppress.find(w); !m_capturingSnapshot && supIt != m_restoreSuppress.end()) {
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

    // Capture-pass guard. Under KWin's current closed draw chain a capture's
    // effects->drawWindow cannot re-enter paintWindow at all (the draw chain
    // terminates in finalDrawWindow; only apply() and our drawWindow override
    // are genuinely re-entered, and their guards are load-bearing) — this arm
    // is insurance against a third-party effect whose drawWindow override
    // calls effects->paintWindow, which nothing in KWin forbids. If it ever
    // runs, continue the chain plainly with no transform or morph processing.
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
        if (KWin::LogicalOutput* managed = w ? scrollManagedOutputFor(w) : nullptr) {
            // A parked column is committed below the union of all outputs, so
            // relocate the drawing to where it really sits on the strip BEFORE
            // the view offset goes on. The two together put it exactly where a
            // never-parked column would be, which is what lets it be seen
            // travelling past during a scroll rather than blinking out the
            // moment it leaves the viewport.
            if (!m_scrollVisualDelta.isEmpty()) {
                const auto vit = m_scrollVisualDelta.constFind(windowId);
                if (vit != m_scrollVisualDelta.constEnd()) {
                    // The stored strip-minus-park delta, on top of wherever the
                    // window is committed — see the member's contract for why
                    // this is a delta and not an absolute position.
                    data += QPointF(vit->x(), vit->y());
                }
            }
            const QPointF viewOffset = m_stripViewAnimator->offsetFor(managed);
            if (!viewOffset.isNull()) {
                data += viewOffset;
            }
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
        const auto bit = m_windowDecorations.constFind(windowId);
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

    // Direct-drive callers own this latch — TWO of them, and only one is a
    // capture: the desktop transition's compositeWindowsInto (which drives
    // windows that were never in this frame's scene walk into an offscreen
    // capture) and the strip pass's top-composite (which drives the
    // above-strip windows onto the SCREEN target after its quad, once per
    // frame of every scroll leg). Both drive this paintWindow directly with
    // the chain iterator at begin(), so continuing the paint chain below would
    // re-enter this very function (double fold, the animator transform applied
    // twice) and then drive later effects' paintWindow hooks without the
    // prePaintWindow they key off. Terminate with a raw draw instead.
    //
    // The raw draw skips the rest of the paintWindow chain for the driven
    // window, which is the accepted trade at both sites rather than an
    // oversight — see the chain-continuation note below for what that costs.
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

void PlasmaZonesEffect::paintScrollTabIndicators(const KWin::RenderTarget& renderTarget,
                                                 const KWin::RenderViewport& viewport, const KWin::Region& deviceRegion)
{
    // Once per output pass, whichever trigger fired first. Latched BEFORE the
    // paint so a re-entrant trigger during the blit (none exists, but the
    // latch is what makes that true) cannot double it.
    m_scrollTabPainted = true;
    KWin::LogicalOutput* out = m_currentPassOutput;
    if (!out) {
        return;
    }
    // The SAME offset the columns take in the transform block above, read in
    // the same paint pass: that is the entire reason the pills are drawn here
    // rather than by the daemon — they move on exactly the frame the windows
    // do, for a view leg, an edge auto-scroll tick, or anything else that
    // slides the strip.
    const QPointF viewOffset = m_stripViewAnimator->offsetFor(out);
    // Clip to the WALK's region, not the trigger window's. KWin hands each
    // paintWindow the damage intersected with that window's own bounds, so a
    // trigger-bounded clip would cut every pill outside the anchor column
    // (the columns beside it, a band reserved outside the column) off a
    // stock install at rest. The walk region is still exactly the set of
    // pixels that gets recomposited above us this pass, which is the
    // property the clip exists for. The trigger's region is the fallback
    // only for a paintWindow reached outside any paintScreen bracket.
    const KWin::Region& clip = m_scrollTabWalkRegionValid ? m_scrollTabWalkRegion : deviceRegion;
    // The latch above stays unconditional (it is the once-per-pass re-entry
    // guard); whether the blit actually reached the GPU is recorded apart,
    // because the painter can refuse (a latched raster failure) and the pass
    // outcome must then say "nothing on screen" or pill input would answer
    // for invisible pills.
    m_scrollTabBlitIssued = m_scrollTabPainter->paint(out, renderTarget, viewport, clip, viewOffset);
}

} // namespace PlasmaZones
