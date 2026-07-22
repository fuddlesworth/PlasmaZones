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

PlasmaZonesEffect::ShaderBranchOutcome PlasmaZonesEffect::paintShaderTransitionWindow(const PaintWindowContext& ctx,
                                                                                      ShaderTransition* st)
{
    // Re-establish paintWindow's per-call locals from the context so the
    // extracted branch body below reads byte-for-byte as it did inline.
    const KWin::RenderTarget& renderTarget = ctx.renderTarget;
    const KWin::RenderViewport& viewport = ctx.viewport;
    KWin::EffectWindow* const w = ctx.window;
    const int mask = ctx.mask;
    const KWin::Region& deviceRegion = ctx.deviceRegion;
    KWin::WindowPaintData& data = ctx.data;
    const qint64 frameNowMs = ctx.frameNowMs;

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
        return ShaderBranchOutcome::Handled;
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
        if (const auto lbIt = m_windowDecorations.constFind(getWindowId(w)); lbIt != m_windowDecorations.constEnd()) {
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
        const QVector4D layerRectInTexture =
            ShaderInternal::computeTextureSubRect(transition.surfaceExtent ? anchorGeo : expandedGeo, layerCanvasGeo);
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
                if (m_shaderManager.m_lastIDateRefreshMs == 0 || nowMs - m_shaderManager.m_lastIDateRefreshMs >= 1000) {
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
                // .xy is the redirected surface's origin in GLOBAL
                // workspace coordinates — the same anchor rect the
                // anchorRemap uniforms describe, so the minimize-to-icon
                // packs' window-rect reconstruction (iSurfaceScreenPos.xy
                // + iAnchorSize, paired with the same-space iIconRect)
                // stays internally consistent.
                QVector4D surfaceScreenPos(static_cast<float>(anchorGeo.x()), static_cast<float>(anchorGeo.y()), 0.0f,
                                           0.0f);
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
                const QPointF off =
                    transition.holdUntilRelease ? (w->frameGeometry().topLeft() - transition.grabOrigin) : QPointF();
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
            // (genie, phosphor-siphon). Same space and same per-frame push discipline
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
            for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots; ++slot) {
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
        return ShaderBranchOutcome::Handled;
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
    // A reverse (going-to-minimized) leg ends fully swallowed / faded
    // out, and the window is only paintable at all because the
    // transition's visible ref holds it. Falling through to the plain
    // chain continuation below would paint the raw window at its rest
    // geometry for the expiry frame(s) — a one-frame flash of a window
    // the user just watched disappear into its icon. Paint nothing
    // instead; the queued teardown (or the pending one from a prior
    // frame) drops the ref and the window leaves the scene. Guarded on
    // isMinimized rather than the leg direction so an un-minimize leg
    // whose window was re-minimized mid-flight is covered too.
    // Returning without the tail chain continuation is deliberate:
    // later-chained effects have nothing meaningful to paint for a
    // window that is only in the scene via our visible ref (same
    // precedent as the restore-suppression early return above).
    if (w->isMinimized()) {
        return ShaderBranchOutcome::Handled;
    }
    // Expiry fall-through: an installed-but-expired, non-minimized leg. The
    // queued teardown above owns the unredirect; paintWindow continues to the
    // decoration fold and the normal paint-chain continuation.
    return ShaderBranchOutcome::Continue;
}

} // namespace PlasmaZones
