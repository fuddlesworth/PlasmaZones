// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"
#include "../compositorclock.h"
#include "shader_internal.h"
#include "shader_resolve.h"
#include "window_query.h"

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

    // Cache cursor pos once per frame for shader-transition iMouse uniform.
    // paintWindow runs once per active transition (and may run multiple
    // times across outputs); reading KWin::effects->cursorPos() at every
    // call multiplies up. Caching here also guarantees every transition
    // this frame reads an identical iMouse, eliminating sub-frame jitter.
    if (KWin::effects && !m_shaderManager.empty()) {
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

void PlasmaZonesEffect::postPaintScreen()
{
    // Schedule targeted repaints for active animations instead of full-screen
    m_windowAnimator->scheduleRepaints();
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
            if (!w || w->isDeleted()) {
                continue;
            }
            const bool timeBasedActive =
                transition.durationMs > 0 && (now - transition.startTimeMs) <= transition.durationMs;
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
                // output as dirty every frame the transition is live.
                if ((timeBasedActive || transition.durationMs == 0) && KWin::effects) {
                    if (const auto* output = w->screen()) {
                        KWin::effects->addRepaint(output->geometry());
                    } else {
                        KWin::effects->addRepaintFull();
                    }
                }
            } else if (timeBasedActive) {
                // Damage the whole output every frame an anchor-extent
                // time-based shader is live. The vertex stage translates
                // the redirected quad past the window's natural rect
                // (bounce drops it in from above, fly-in slides it from
                // the edge); the band it sweeps — both the off-frame
                // destination and the vacated origin — must be marked
                // dirty so the compositor recomposites it each frame.
                // Without this the swept band keeps stale pixels.
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
    if (KWin::effects && !m_windowBorders.isEmpty()) {
        for (auto it = m_windowBorders.cbegin(); it != m_windowBorders.cend(); ++it) {
            if (!it->shaderApplied) {
                continue;
            }
            KWin::EffectWindow* const sw = findWindowById(it.key());
            if (!sw || sw->isDeleted() || !sw->isOnCurrentDesktop()) {
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
                backdropDue = stateIt == m_surfaceMultipass.end() || stateIt->second.lastFoldMs < 0
                    || (ShaderInternal::shaderClockNowMs() - stateIt->second.lastFoldMs) >= kBackdropRefoldIntervalMs;
            }
            if (windowSurfaceAnimates(it.key()) || backdropDue) {
                sw->addRepaintFull();
                // A padded chain's margin band sits OUTSIDE the window item;
                // per-window repaints clip to it, so damage the band at
                // screen level (the documented addLayerRepaint pitfall).
                if (it->outerPadding > 0) {
                    QRectF padded = sw->expandedGeometry();
                    if (padded.isEmpty()) {
                        padded = sw->frameGeometry();
                    }
                    const int pad = it->outerPadding;
                    KWin::effects->addRepaint(KWin::RectF(padded.adjusted(-pad, -pad, pad, pad)));
                }
            }
        }
    }
    KWin::effects->postPaintScreen();
    // Unpin the per-frame clock. Any paintWindow() invocation outside
    // the prePaintScreen→postPaintScreen bracket (defensive bootstrap,
    // future test harness, an unexpected mid-cycle paint) then falls
    // back to the live `shaderClockNowMs()` via the -1 sentinel branch
    // in shader_resolve.cpp's resolveShaderClock(), instead of reading
    // a stale pinned timestamp from this cycle.
    m_shaderManager.setCurrentFrameClockMs(-1);
    // Drop the per-frame SetOpacity cache so next frame's prePaintWindow
    // re-resolves against any rule-set or window-metadata changes that
    // landed between frames. See ShaderTransitionManager's cache-block
    // comment for the per-frame contract rationale.
    m_shaderManager.clearFrameOpacityCache();
}

void PlasmaZonesEffect::prePaintWindow(KWin::RenderView* view, KWin::EffectWindow* w, KWin::WindowPrePaintData& data)
{
    const bool transformDriven =
        w && (m_windowAnimator->hasAnimation(w) || m_shaderManager.hasTransition(w) || m_restoreSuppress.contains(w));
    if (transformDriven) {
        // Mark as transformed so paintWindow applies our translate+scale, and
        // so the OffscreenEffect redirect drives full-window repaints for the
        // shader leg's iTime advance even when the underlying window content
        // hasn't changed (lifecycle-event shaders need this; without the
        // transformed flag, paintWindow only fires on actual window damage).
        //
        // Damage-region expansion for actor-expansion transitions lives
        // in `postPaintScreen`'s addLayerRepaint loop (see the ringRect
        // block there). prePaintWindow doesn't drive that on KWin 6;
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
    } else if (w && !m_windowBorders.isEmpty()) {
        // Padded decoration chains (WindowBorder::outerPadding) present on a
        // quad LARGER than the window's natural rect (see apply()); mark the
        // window transformed so KWin paints the padded quad unclipped. The
        // opaque region stays — the window's own content still covers it, so
        // occlusion culling underneath remains valid.
        const auto bit = m_windowBorders.constFind(getWindowId(w));
        if (bit != m_windowBorders.constEnd() && bit->shaderApplied && bit->outerPadding > 0) {
            data.setTransformed();
        }
    }

    // Resolve + cache the rule-resolved opacity for ANY window with
    // SetOpacity rules, independent of whether a transition is in flight.
    // Two consumers in paintWindow read this cache: the `data.setOpacity()`
    // path (plain dimmed window) and the shader-transition draw, which
    // pushes the value into the `iWindowOpacity` uniform so surfaceColor()
    // dims the surface for the whole animation — a MapTexture-only custom
    // shader can't see `data.opacity()`. This was previously an `else if`
    // gated off the transform-driven branch above, so a window mid-
    // transition never cached its opacity and the transition shader
    // rendered it fully opaque until the animation settled, then snapped to
    // the rule opacity. Resolve once per frame and cache (the cascade walk
    // is the hot-path cost the project tracks); the cache drops at
    // postPaintScreen.
    if (w && m_shaderManager.hasOpacityRules()) {
        const QString winClass = w->windowClass();
        if (!isOwnOverlayClass(winClass) && !isPlasmaShellSurface(winClass)) {
            const auto opacity = resolveWindowOpacity(resolveRuleActions(w, getWindowId(w)));
            m_shaderManager.cacheFrameOpacity(w, opacity);
            // Clear the deviceOpaque region so KWin recomposites whatever
            // sits behind a dimmed window — without it, stale background
            // pixels show through the moment anything behind moves. The
            // transform-driven branch already did this for windows mid-
            // transition; only the SetOpacity-only case needs it here.
            // Geometry is untouched (no setTransformed) — SetOpacity is a
            // pure alpha change.
            if (!transformDriven && opacity && *opacity < 1.0) {
                data.setTranslucent();
            }
        }
    }

    // Keep a static bordered window in KWin's paint set so the drawWindow
    // override keeps getting called for it on idle frames. KWin's simple-screen
    // path culls a fully-opaque, undamaged window out of the paint cycle
    // entirely (its opaque region is occluded / unchanged), which would drop
    // drawWindow for it and the passive border blit would only run while the
    // window happens to be animating or damaged. setTranslucent() clears the
    // window's opaque region so KWin always recomposites it (and thus calls
    // drawWindow), letting the border shader re-blit the redirected FBO every
    // frame with no FBO re-render. Gated on the cheap isEmpty() hot-path check
    // and shaderApplied so only windows we actually border pay the cost; the
    // transform-driven branch above already set translucent for transitioning
    // windows, so this only adds the idle bordered case.
    if (w && !transformDriven && !m_windowBorders.isEmpty()) {
        const auto bit = m_windowBorders.constFind(getWindowId(w));
        if (bit != m_windowBorders.constEnd() && bit->shaderApplied) {
            data.setTranslucent();
        }
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
    if (w && !w->isDeleted() && !m_capturingSnapshot && !m_windowBorders.isEmpty()) {
        const auto backIt = m_windowBorders.constFind(getWindowId(w));
        if (backIt != m_windowBorders.constEnd() && backIt->needsBackdrop
            && (backIt->shaderApplied || m_shaderManager.findTransition(w))) {
            // While an animation is drawing the window somewhere other than
            // its resting rect, capture the backdrop where the quad actually
            // IS this frame, or the pane shows the wrong slice of the scene
            // for the whole animation (every snap/zone change here). Two of
            // the three animation classes expose exact geometry:
            //   1. C++ WindowAnimator translate+scale — its current rect.
            //   2. Geometry-morph transitions — lerp(from, to, progress)
            //      with the same time-based progress the draw uses.
            //   3. Non-morph shader transitions: anchor-extent packs warp in
            //      place (rest rect is already right); surface-extent movers
            //      (fly-in / bounce) place pixels only the shader knows, so
            //      they keep the rest-rect capture — stale-position frost
            //      that rides the quad, which motion masks. The reference
            //      blur effects disable blur outright on transformed
            //      windows; this degrades strictly less.
            QRectF animatedFrame = m_windowAnimator->currentValue(w, QRectF());
            if (!animatedFrame.isValid()) {
                if (const ShaderTransition* st = m_shaderManager.findTransition(w);
                    st && st->fromGeometry.isValid() && st->toGeometry.isValid() && st->durationMs > 0) {
                    const qreal t = qBound(0.0, qreal(frameNowMs - st->startTimeMs) / qreal(st->durationMs), 1.0);
                    const QRectF& f = st->fromGeometry;
                    const QRectF& g = st->toGeometry;
                    animatedFrame =
                        QRectF(f.x() + (g.x() - f.x()) * t, f.y() + (g.y() - f.y()) * t,
                               f.width() + (g.width() - f.width()) * t, f.height() + (g.height() - f.height()) * t);
                }
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

    // SetOpacity rule consumer — sets the window's painted opacity before
    // downstream transforms run. Absolute set (not multiplyOpacity) because
    // SetOpacity rules describe a target opacity, not a scale factor —
    // "make Firefox 80% opaque" should land at 0.8 regardless of whatever
    // opacity another effect previously composed. Uses the shader-manager's
    // window-rule evaluator because the SetOpacity action lives in the same
    // effect-side rule bag as OverrideAnimation{Shader,Curve,Timing} —
    // sharing the evaluator also shares its per-window cache, so the paint
    // hot path pays at most one cascade walk per window per rule-set
    // revision.
    //
    // Skip our own overlay/editor/snap-assist surfaces and plasma-shell
    // panels — a user-authored rule like `WindowClass contains "plasmashell"`
    // would otherwise dim our own UI or the system panel. This is a
    // narrower exclusion than `shouldHandleWindow` (which also rejects
    // xdg-desktop-portal surfaces) — deliberately so. SetOpacity is a
    // user-visible cosmetic effect that the user opts into by authoring
    // a class-matching rule; we exclude only the surfaces whose dimming
    // would feel like a daemon bug (our own UI, panels), and let the
    // user-authored rule reach everything else the rule explicitly
    // names.
    //
    // Short-circuit on an empty rule set — the common case for users without
    // any effect-side rules. The animation-cascade sister consumers
    // (window_filtering.cpp, drag_snap.cpp) follow the same `isEmpty()` gate
    // to keep default-state cost to two pointer reads; without it, every
    // paintWindow call churns the rule evaluator's per-window cache for
    // zero benefit.
    if (m_shaderManager.hasOpacityRules()) {
        const QString winClass = w->windowClass();
        if (!isOwnOverlayClass(winClass) && !isPlasmaShellSurface(winClass)) {
            // Reuse the per-frame cache populated by prePaintWindow above.
            // Falls back to a live resolve only when prePaintWindow wasn't
            // called this frame (defensive bootstrap, test harness — KWin
            // normally guarantees prePaintWindow→paintWindow ordering).
            std::optional<qreal> opacity;
            if (m_shaderManager.frameOpacityCached(w)) {
                opacity = m_shaderManager.cachedFrameOpacity(w);
            } else {
                opacity = resolveWindowOpacity(resolveRuleActions(w, getWindowId(w)));
                m_shaderManager.cacheFrameOpacity(w, opacity);
            }
            if (opacity) {
                data.setOpacity(*opacity);
            }
        }
    }

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
        // ShaderTransition's docstring). Lifecycle events (window.*)
        // started via tryBeginShaderForEvent set durationMs > 0 and drive
        // progress from monotonic steady-clock elapsed; zone.* events flowed
        // through applyWindowGeometry leave durationMs = 0 and ride the
        // m_windowAnimator timeline so the shader matches the geometry
        // animation.
        qreal progress = 0.0;
        bool active = false;
        if (transition.durationMs > 0) {
            const qint64 elapsed = frameNowMs - transition.startTimeMs;
            if (elapsed >= 0 && elapsed <= transition.durationMs) {
                progress = qreal(elapsed) / qreal(transition.durationMs);
                active = true;
            }
        } else {
            const auto* anim = m_windowAnimator->animationFor(w);
            if (anim && anim->isAnimating()) {
                progress = qBound(0.0, anim->state().value, 1.0);
                active = true;
            }
        }
        // Flip the timeline for `going-away` events so a single user-
        // assigned shader covers both directions of a paired event:
        // window.open plays 0→1, window.close plays the same shader 1→0;
        // going-to-minimized plays 1→0 while unminimize plays 0→1; same
        // for maximize/unmaximize. Both progress sources are guaranteed
        // to be in [0, 1] above, so the flip is bound-preserving.
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
            // unpopulated on the kwin path — those need C++ wiring
            // (CAVA subscription, FBO chain, texture cache) that is out
            // of scope for this commit.
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
            const float iTimeDelta = (transition.lastPaintTimeMs < 0)
                ? 0.0f
                : static_cast<float>(nowMs - transition.lastPaintTimeMs) / 1000.0f;
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
            if (const auto lbIt = m_windowBorders.constFind(getWindowId(w)); lbIt != m_windowBorders.constEnd()) {
                layerPad = lbIt->outerPadding;
            }
            // Prefer the composite's RECORDED canvas rect over recomputing
            // from live geometry: it describes the texture that actually
            // exists, which matters for a CLOSING (deleted) window whose
            // frozen composite is reused while its live geometry may drift.
            QRectF layerCanvasGeo = expandedGeo.adjusted(-layerPad, -layerPad, layerPad, layerPad);
            if (const auto lsIt = m_surfaceMultipass.find(getWindowId(w));
                lsIt != m_surfaceMultipass.end() && lsIt->second.canvasGeo.isValid()) {
                layerCanvasGeo = lsIt->second.canvasGeo;
            }
            const QVector4D layerRectInTexture = ShaderInternal::computeTextureSubRect(
                transition.surfaceExtent ? anchorGeo : expandedGeo, layerCanvasGeo);
            // Surface-layer stack (border / rounded corners, ...): render the
            // window's active layers into an FBO so the animation composites
            // OVER the layered surface and the border stays visible for the
            // whole transition instead of vanishing when the animation shader
            // takes the draw slot. Runs BEFORE the animation shader is bound —
            // it briefly swaps the redirect's bound shader to the border and
            // back, so it must not sit inside the ShaderBinder scope below.
            // Null when the window has no surface layers (the common no-border
            // case), in which case surfaceColor() samples the bare uTexture0.
            KWin::GLTexture* const surfaceLayerTex = renderSurfaceChain(transition, w, viewport.scale());
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
                    // form). Matches `shadernoderhiuniforms.cpp:51-52`
                    // exactly so a shader that reads iDate.w sees the
                    // same value on both runtimes.
                    // 1Hz cache: re-decompose the QDateTime only when at
                    // least 1000 ms have elapsed since the last refresh
                    // (or this is the first paint to read iDate). Mirrors
                    // shadernoderhiuniforms.cpp:42-53 — sub-second iDate
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
                    // same clock sample.
                    if (nowMs - m_shaderManager.m_lastIDateRefreshMs >= 1000) {
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
                    // Feed the rule-resolved opacity so surfaceColor() dims
                    // the surface for the entire transition. prePaintWindow
                    // cached it this frame for any window with SetOpacity
                    // rules; a nullopt entry (or no rules at all) means "no
                    // rule matched this window" → full opacity. The custom
                    // shader is compiled MapTexture-only and can't observe
                    // data.opacity(), so this uniform is the only path that
                    // applies the dim while the transition runs — without it
                    // the window flashes fully opaque until the animation
                    // settles, then snaps to the rule opacity.
                    float winOpacity = 1.0f;
                    if (m_shaderManager.frameOpacityCached(w)) {
                        if (const auto cachedOpacity = m_shaderManager.cachedFrameOpacity(w)) {
                            winOpacity = static_cast<float>(*cachedOpacity);
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
                    constexpr int kOldSnapshotUnit =
                        1 + PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots;
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
                if (surfaceLayerTex && cached->uSurfaceLayerLoc >= 0) {
                    constexpr int kSurfaceLayerUnit =
                        2 + PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots;
                    shader->setUniform(cached->uSurfaceLayerLoc, kSurfaceLayerUnit);
                    glActiveTexture(GL_TEXTURE0 + kSurfaceLayerUnit);
                    surfaceLayerTex->bind();
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
            // whatever) when it expects a default-empty unit.
            for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots; ++slot) {
                if (!transition.userTextures[slot]) {
                    continue;
                }
                glActiveTexture(GL_TEXTURE1 + slot);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            // Same hygiene for the old-content snapshot unit (uOldWindow),
            // bound just past the user-texture slots above for morph
            // transitions — don't leave it dangling for the next effect.
            if (cached->iOldWindowLoc >= 0 && transition.oldSnapshot) {
                constexpr int kOldSnapshotUnit =
                    1 + PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots;
                glActiveTexture(GL_TEXTURE0 + kOldSnapshotUnit);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            // Same hygiene for the surface-layer unit (uSurfaceLayer), bound one
            // unit past the old-snapshot slot — don't leave the layered surface
            // dangling on its unit for the next effect in the chain.
            if (surfaceLayerTex && cached->uSurfaceLayerLoc >= 0) {
                constexpr int kSurfaceLayerUnit =
                    2 + PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots;
                glActiveTexture(GL_TEXTURE0 + kSurfaceLayerUnit);
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

    // Border rendering is NOT handled here. A static bordered window is rendered
    // through the offscreen border shader passively in the drawWindow override
    // (the KDE-Rounded-Corners model), which applies the shader on every
    // composite including idle frames. paintWindow only drives the animation
    // transition path above; everything else falls through unchanged.
    //
    // EXCEPTION — multipass surface packs: render the buffer passes HERE, before
    // the draw-chain walk below, when the (non-transition) window has a multipass
    // border. renderSurfaceBufferPasses captures the raw surface via
    // effects->drawWindow; that nested draw-chain walk MUST run from paintWindow
    // (a fresh draw-window iterator) — doing it from inside the drawWindow
    // override re-enters KWin's iterator mid-walk and corrupts it (crash in the
    // following OffscreenEffect::drawWindow). The override then only BINDS the
    // per-window buffer textures this prepared. Single-pass packs (border) have
    // no compiled buffer passes → renderSurfaceBufferPasses cheap-early-outs (it
    // resolves the window's base pack from the cache and returns when its
    // bufferPasses are empty), no capture. The pre-gate here just skips windows
    // with no applied border (no decoration at all) without a map+cache lookup.
    if (!m_capturingSnapshot && !m_windowBorders.isEmpty() && !m_shaderManager.findTransition(w)) {
        const auto bit = m_windowBorders.constFind(getWindowId(w));
        if (bit != m_windowBorders.constEnd() && bit->shaderApplied) {
            if (bit->chain.size() > 1 || bit->outerPadding > 0 || bit->needsBackdrop || bit->ruleOpacity < 1.0) {
                // MULTI-PACK: composite the whole chain into a per-window FBO here
                // (each pack's main runs as an FBO pass); drawWindow then presents
                // the final FBO through the passthrough present shader.
                renderSurfaceChainComposite(w, viewport.scale());
            } else {
                renderSurfaceBufferPasses(w, viewport.scale());
            }
        }
    }

    // Route through the draw chain (not a direct OffscreenEffect::drawWindow
    // call) so KWin's `m_currentDrawWindowIterator` is advanced past us
    // before any redirected window's `OffscreenData::maybeRender` re-enters
    // the chain to capture — see the detailed rationale at the shader-branch
    // drawWindow call above. This path also covers redirected windows in
    // their post-transition expiry frame, which are still offscreen-backed.
    KWin::effects->drawWindow(renderTarget, viewport, w, mask, deviceRegion, data);
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
    // A chain with an outer margin (WindowBorder::outerPadding) composited
    // into a canvas LARGER than the redirect texture
    // (renderSurfaceChainComposite inflated the capture rect). Present it on
    // a matching padded quad so the margin band reaches the screen — the
    // same quad-rewrite mechanism the surface-extent transitions use (they
    // stretch to the whole output; this stretches by the padding). Gated on
    // no live transition: a transition owns the shader slot (shaderApplied
    // false) and its own quad handling wins.
    if (!st && !quads.isEmpty() && !m_windowBorders.isEmpty()) {
        const auto bit = m_windowBorders.find(getWindowId(window));
        if (bit != m_windowBorders.end() && bit->shaderApplied && bit->outerPadding > 0) {
            QRectF textureGeo = window->expandedGeometry();
            if (textureGeo.isEmpty()) {
                textureGeo = window->frameGeometry();
            }
            if (textureGeo.isEmpty()) {
                return;
            }
            const qreal pad = bit->outerPadding;
            const QRectF padded = textureGeo.adjusted(-pad, -pad, pad, pad);

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
    if (st && !st->surfaceExtent && !quads.isEmpty() && !m_windowBorders.isEmpty()) {
        const auto bit = m_windowBorders.find(getWindowId(window));
        if (bit != m_windowBorders.end() && bit->outerPadding > 0) {
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
        QRectF dst = st->toGeometry;
        if (!dst.isValid() || dst.isEmpty()) {
            dst = window->frameGeometry();
        }
        if (dst.isEmpty()) {
            return;
        }
        // quad-space <-> screen-space is a pure translation at 1:1 logical
        // scale; qLeft/qTop is the captured texture's top-left in quad
        // space and textureGeo its top-left in screen space.
        const double qOffX = qLeft - textureGeo.x();
        const double qOffY = qTop - textureGeo.y();
        const int n = st->gridSubdivisions;
        quads.clear();
        quads.reserve(n * n);
        for (int gy = 0; gy < n; ++gy) {
            const double v0 = static_cast<double>(gy) / n;
            const double v1 = static_cast<double>(gy + 1) / n;
            const double y0 = dst.y() + v0 * dst.height() + qOffY;
            const double y1 = dst.y() + v1 * dst.height() + qOffY;
            for (int gx = 0; gx < n; ++gx) {
                const double u0 = static_cast<double>(gx) / n;
                const double u1 = static_cast<double>(gx + 1) / n;
                const double x0 = dst.x() + u0 * dst.width() + qOffX;
                const double x1 = dst.x() + u1 * dst.width() + qOffX;
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
