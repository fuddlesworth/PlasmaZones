// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"
#include "shader_internal.h"

#include <effect/effecthandler.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>

#include <QDate>
#include <QDateTime>
#include <QPointer>
#include <QTime>
#include <QVector2D>
#include <QVector4D>

#include <type_traits>

#include "../windowanimator.h"

namespace PlasmaZones {

using ShaderInternal::shaderClockNowMs;

void PlasmaZonesEffect::prePaintScreen(KWin::ScreenPrePaintData& data, std::chrono::milliseconds presentTime)
{
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

    if (m_windowAnimator->hasActiveAnimations() || !m_shaderManager.m_shaderTransitions.empty()) {
        // Windows have translation transforms that move them outside their
        // frame geometry bounds — force full compositing mode. Shader
        // transitions also need this: without
        // PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS the compositor skips
        // our paintWindow override on stable, undamaged windows (focus,
        // open after the fade settles, minimise, etc.), which means
        // the shader installs and silently expires unrendered.
        data.mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS;
    }

    // Cache cursor pos once per frame for shader-transition iMouse uniform.
    // paintWindow runs once per active transition (and may run multiple
    // times across outputs); reading KWin::effects->cursorPos() at every
    // call multiplies up. Caching here also guarantees every transition
    // this frame reads an identical iMouse, eliminating sub-frame jitter.
    if (KWin::effects && !m_shaderManager.m_shaderTransitions.empty()) {
        m_shaderManager.m_cachedCursorGlobal = KWin::effects->cursorPos();
    }

    KWin::effects->prePaintScreen(data, presentTime);
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
    if (!m_shaderManager.m_shaderTransitions.empty()) {
        const qint64 now = shaderClockNowMs();
        for (const auto& [w, transition] : m_shaderManager.m_shaderTransitions) {
            if (transition.durationMs > 0 && (now - transition.startTimeMs) <= transition.durationMs) {
                if (w && !w->isDeleted()) {
                    // Fall back to frameGeometry when expanded is empty
                    // — a window with no shadow / decoration extents
                    // reports an empty expanded rect, and `addLayerRepaint`
                    // on an empty rect is a silent no-op that would stall
                    // the time-based shader's iTime advance.
                    QRect repaintRect = w->expandedGeometry().toAlignedRect();
                    if (repaintRect.isEmpty()) {
                        repaintRect = w->frameGeometry().toAlignedRect();
                    }
                    // Actor-expansion: when the active transition declares
                    // a non-zero `fboExtentRing`, the apply() override
                    // renders the redirected texture over a region
                    // `(1 + 2·ring) × frame` instead of the natural frame.
                    // Without growing the repaint rect to match, KWin's
                    // damage tracking would clip the visible result back
                    // to the natural expanded rect (shadow extents only)
                    // and the BMW-style shards-past-window-bounds visual
                    // would be cropped flat at the shadow edge.
                    if (transition.fboExtentRing > 0.0) {
                        const QRect ringRect =
                            ShaderInternal::inflatedByRingFraction(w->frameGeometry(), transition.fboExtentRing)
                                .toAlignedRect();
                        repaintRect = repaintRect.united(ringRect);
                    }
                    w->addLayerRepaint(repaintRect);
                }
            }
        }
    }
    KWin::effects->postPaintScreen();
}

void PlasmaZonesEffect::prePaintWindow(KWin::RenderView* view, KWin::EffectWindow* w, KWin::WindowPrePaintData& data,
                                       std::chrono::milliseconds presentTime)
{
    if (w
        && (m_windowAnimator->hasAnimation(w)
            || m_shaderManager.m_shaderTransitions.find(w) != m_shaderManager.m_shaderTransitions.end())) {
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
    }

    OffscreenEffect::prePaintWindow(view, w, data, presentTime);
}

void PlasmaZonesEffect::paintWindow(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                                    KWin::EffectWindow* w, int mask, const KWin::Region& deviceRegion,
                                    KWin::WindowPaintData& data)
{
    m_windowAnimator->applyTransform(w, data);

    auto sit = m_shaderManager.m_shaderTransitions.find(w);
    if (sit != m_shaderManager.m_shaderTransitions.end() && sit->second.cached && sit->second.cached->shader) {
        // Non-const reference because the per-frame book-keeping (`frameCount`,
        // `lastPaintTimeMs`) advances on every paintWindow tick that feeds
        // the transition. Without the mutation, `iFrame` would stay at 0 and
        // `iTimeDelta` would always read 0.
        auto& transition = sit->second;
        // Two progress sources, picked by the transition's mode (see
        // ShaderTransition's docstring). Lifecycle events (window.*)
        // started via tryBeginShaderForEvent set durationMs > 0 and drive
        // progress from monotonic steady-clock elapsed; zone.* events flowed
        // through applySnapGeometry leave durationMs = 0 and ride the
        // m_windowAnimator timeline so the shader matches the geometry
        // animation.
        qreal progress = 0.0;
        bool active = false;
        if (transition.durationMs > 0) {
            const qint64 now = shaderClockNowMs();
            const qint64 elapsed = now - transition.startTimeMs;
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
            const qint64 nowMs = shaderClockNowMs();
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
            {
                KWin::ShaderBinder binder(shader);
                if (cached->iTimeLoc >= 0) {
                    shader->setUniform(cached->iTimeLoc, static_cast<float>(progress));
                }
                if (cached->iResolutionLoc >= 0) {
                    shader->setUniform(cached->iResolutionLoc, QVector2D(geo.width(), geo.height()));
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
                    QVector4D surfaceScreenPos(static_cast<float>(geo.x()), static_cast<float>(geo.y()), 0.0f, 0.0f);
                    if (const auto* output = w->screen()) {
                        const QRect screenGeo = output->geometry();
                        surfaceScreenPos.setZ(static_cast<float>(screenGeo.width()));
                        surfaceScreenPos.setW(static_cast<float>(screenGeo.height()));
                    }
                    shader->setUniform(cached->iSurfaceScreenPosLoc, surfaceScreenPos);
                }
                if (cached->iAnchorSizeLoc >= 0) {
                    // The window's frameGeometry IS the visible "card" on
                    // the kwin path — there's no separate anchor-vs-FBO
                    // distinction here, so iAnchorSize matches iResolution.
                    shader->setUniform(cached->iAnchorSizeLoc,
                                       QVector2D(static_cast<float>(geo.width()), static_cast<float>(geo.height())));
                }
                if (cached->iAnchorPosInFboLoc >= 0) {
                    // Always (0, 0) on the kwin path, even when the active
                    // transition declares `fboExtentRing > 0`. Reason:
                    // actor expansion on kwin is done at quad-construction
                    // time (apply() in paint_pipeline.cpp) by remapping
                    // the expanded quad's `texCoord` to `[-ring, 1+ring]`,
                    // so the fragment shader already receives `vTexCoord`
                    // in anchor-space coordinates. iAnchorSize == iResolution
                    // (both = window frame size) so the shader's unified
                    // remap math collapses to identity:
                    //   anchorTopLeftUv = (0, 0) / frame  = (0, 0)
                    //   anchorSizeUv    = frame  / frame  = (1, 1)
                    //   anchorUv        = vTexCoord - 0   = vTexCoord
                    // and vTexCoord arrives in `[-ring, 1+ring]` from the
                    // expanded quad, giving the BMW-style anchor-space
                    // range morph + broken-glass expect. The daemon path
                    // uses different uniform values (iAnchorPosInFbo =
                    // (padW, padH), iResolution = expanded size) to
                    // produce the same anchor-space range from a `[0, 1]`
                    // vTexCoord; different runtime mechanism, same shader
                    // source.
                    shader->setUniform(cached->iAnchorPosInFboLoc, QVector2D(0.0f, 0.0f));
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
                // Restore TEXTURE0 as the active unit so KWin's
                // OffscreenData::paint binds the redirected surface
                // to the unit it expects (its `m_texture->bind()` runs
                // without a preceding glActiveTexture).
                glActiveTexture(GL_TEXTURE0);
            }
            OffscreenEffect::drawWindow(renderTarget, viewport, w, mask, deviceRegion, data);
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
            // in `tryBeginShaderForEvent` (~line 5744).
            // Iterator `sit` was obtained earlier in this function and no
            // intervening code mutates m_shaderManager.m_shaderTransitions, so the read is
            // safe. The assertion documents that contract for future edits.
            Q_ASSERT(sit != m_shaderManager.m_shaderTransitions.end());
            const quint64 expiringGeneration = sit->second.generation;
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
                    auto liveIt = m_shaderManager.m_shaderTransitions.find(safeWindow.data());
                    if (liveIt != m_shaderManager.m_shaderTransitions.end()
                        && liveIt->second.generation == expiringGeneration) {
                        endShaderTransition(safeWindow.data());
                    }
                    // else: a successor replaced us (last-event-wins) and
                    // owns its own teardown — leave it alone.
                },
                Qt::QueuedConnection);
        }
    }

    OffscreenEffect::drawWindow(renderTarget, viewport, w, mask, deviceRegion, data);
}

void PlasmaZonesEffect::apply(KWin::EffectWindow* window, int mask, KWin::WindowPaintData& data,
                              KWin::WindowQuadList& quads)
{
    // Match BMW's "grow the actor" approach when the active transition's
    // effect declares `fboExtent: "anchor+N"`. The redirected texture
    // (allocated by OffscreenEffect::redirect at window-frame size) gets
    // sampled by an expanded quad with `texCoord` spanning [-ring, 1+ring]
    // The central [0..1] sub-region maps to the captured window content
    // and the surrounding ring lets the shader render past the natural
    // frame. Morph and broken-glass already guard their out-of-anchor
    // samples (boundaryMask / getInputColor override → vec4(0)), so the
    // edge texels don't smear when the redirected texture's clamp-to-edge
    // wrap mode hits.
    //
    // For ring == 0 (53-of-56 default-shader case, plus the fly-in
    // surface-extent case that uses MVP translation instead of actor
    // expansion), pass through to the base implementation which leaves
    // `quads` as the natural single-quad-over-frameGeometry mesh.
    if (!window) {
        OffscreenEffect::apply(window, mask, data, quads);
        return;
    }
    const auto it = m_shaderManager.m_shaderTransitions.find(window);
    if (it == m_shaderManager.m_shaderTransitions.end() || it->second.fboExtentRing <= 0.0) {
        OffscreenEffect::apply(window, mask, data, quads);
        return;
    }

    const qreal ring = it->second.fboExtentRing;
    const QRectF geo = window->frameGeometry();
    if (geo.width() < 1.0 || geo.height() < 1.0) {
        OffscreenEffect::apply(window, mask, data, quads);
        return;
    }

    // Replace the (potentially multi-quad) input mesh with a single quad
    // covering the expanded region. Vertex positions are in window-local
    // coords (KWin's MVP matrix translates to screen position downstream),
    // so subtracting padW/padH from the natural frame origin shifts the
    // top-left into negative window-local space. That's the "render
    // past the frame" semantic. Texture coordinates follow the same
    // ring convention so the captured-window texture (`uTexture0` on
    // KWin) lands in the central [0..1] region, matching what the
    // shader's `anchorUv` remap recovers via the unified
    // `iAnchorPosInFbo / iResolution / iAnchorSize` math.
    //
    // The expanded rect is computed via `inflatedByRingFraction` (single
    // source of truth shared with `postPaintScreen`'s
    // `addLayerRepaint` ring-rect union).
    const QRectF expandedGeo = ShaderInternal::inflatedByRingFraction(geo, ring);
    const qreal leftLocal = expandedGeo.x() - geo.x();
    const qreal topLocal = expandedGeo.y() - geo.y();
    const qreal rightLocal = leftLocal + expandedGeo.width();
    const qreal bottomLocal = topLocal + expandedGeo.height();
    KWin::WindowQuad expanded;
    expanded[0] = KWin::WindowVertex(leftLocal, topLocal, -ring, -ring); // TL
    expanded[1] = KWin::WindowVertex(rightLocal, topLocal, 1.0 + ring, -ring); // TR
    expanded[2] = KWin::WindowVertex(rightLocal, bottomLocal, 1.0 + ring, 1.0 + ring); // BR
    expanded[3] = KWin::WindowVertex(leftLocal, bottomLocal, -ring, 1.0 + ring); // BL
    quads.clear();
    quads.append(expanded);
}

} // namespace PlasmaZones
