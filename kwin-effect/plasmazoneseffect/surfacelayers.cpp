// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include "shader_internal.h"
#include "surface_fold.h"
#include "types.h"

#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>
#include <opengl/glvertexbuffer.h>
#include <scene/item.h>
#include <scene/windowitem.h>

#include <PhosphorSurface/SurfaceShaderContract.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>

#include <QMatrix4x4>
#include <QPoint>
#include <QRectF>
#include <QScopeGuard>
#include <QSize>
#include <QVector2D>
#include <QVector4D>

#include <array>
#include <cmath>
#include <optional>
#include <epoxy/gl.h>

namespace PlasmaZones {

namespace {

/// Draw a fullscreen unit quad (NDC -1..1, texcoord 0..1) through the
/// already-bound shader as a triangle strip. texcoord [0,1] maps to the bound
/// FBO directly (bottom-origin), so a passthrough buffer reproduces uTexture0's
/// layout. This is the SOLE buffer-pass draw site; the matching vertex SOURCE
/// (kFullscreenQuadVertexSource) lives in surface_compile.cpp, which only
/// compiles the buffer-pass shaders. Kept file-local — under the Unity build a
/// duplicate anonymous-namespace definition elsewhere would collide.
/// The caller owns the ShaderBinder.
void drawFullscreenQuad()
{
    const std::array<KWin::GLVertex2D, 4> verts = {{
        {QVector2D(-1.0f, -1.0f), QVector2D(0.0f, 0.0f)}, // bottom-left
        {QVector2D(1.0f, -1.0f), QVector2D(1.0f, 0.0f)}, // bottom-right
        {QVector2D(-1.0f, 1.0f), QVector2D(0.0f, 1.0f)}, // top-left
        {QVector2D(1.0f, 1.0f), QVector2D(1.0f, 1.0f)}, // top-right
    }};
    KWin::GLVertexBuffer* const vbo = KWin::GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setVertices(verts);
    vbo->render(GL_TRIANGLE_STRIP);
}

} // namespace

// Render the window's active surface-layer stack for an in-flight transition
// and return the texture holding the final composited surface, or nullptr when
// the window has no decoration (the caller then animates the bare uTexture0).
//
// Delegates to renderSurfaceChainComposite — the SAME padded full-chain fold
// the rest path uses — so a transition composites the ENTIRE chain (border +
// glow + …) over the padded canvas instead of the old bespoke chain[0]-only
// unpadded blit (which silently dropped every pack after the base and clipped
// the outer margin for the whole animation). The animation samples the result
// as uSurfaceLayer through surfaceColor(), whose iLayerRectInTexture remap
// addresses the padded canvas. renderSurfaceChainComposite always folds the
// full chain here: mid-animation there is no OffscreenData blit to fall back on
// even for a single unpadded pack, and the capture hands the OffscreenEffect
// slot back to the ANIMATION shader (not the rest path's present passthrough).
KWin::GLTexture* PlasmaZonesEffect::renderSurfaceChain(ShaderTransition& transition, KWin::EffectWindow* w, qreal scale)
{
    if (!w || !transition.cached) {
        return nullptr;
    }
    // CLOSING (deleted) windows: NEVER capture — reuse the rest path's last
    // composite. Re-drawing a deleted window (effects->drawWindow + the
    // setShader redirect swap on a corpse) is the UB class the teardown paths
    // avoid, and once the client buffer is released it composites an
    // opaque/empty padded canvas that flashes the whole expanded box — even a
    // single first-frame capture flashed (the buffer can be gone by then). A
    // padded/multi-pack window doesn't need one: renderSurfaceChainComposite
    // ran on every ALIVE paint frame and m_surfaceMultipass still holds the
    // final pre-close decorated composite (the entry outlives close because
    // slotWindowClosed defers removeWindowDecoration), which is exactly the frozen
    // frame the close animation should carry. A plain single-pack unpadded
    // window has no rest composite and animates the bare frozen uTexture0
    // (its border does not ride the close — strictly better than the flash;
    // a future rest-path composite for that class would restore it).
    if (w->isDeleted()) {
        const auto sIt = m_surfaceMultipass.find(getWindowId(w));
        if (sIt != m_surfaceMultipass.end()) {
            KWin::GLTexture* const frozen = sIt->second.compositeTex[sIt->second.finalSlot].get();
            if (frozen) {
                return frozen;
            }
        }
        return nullptr;
    }
    return renderSurfaceChainComposite(w, scale, transition.cached->shader.get());
}

// This TU owns the composite fold and nothing else. Its neighbours, all split out
// to keep it under the 800-line limit:
//   surface_capture.cpp    — the fold's INPUT side: target (re)allocation and the raw
//                            window capture
//   surface_backdrop.cpp   — captureWindowBackdrop (the scene behind the window)
//   surface_audio.cpp      — the CAVA spectrum binding
//   surface_compile.cpp    — pack compilation
//   surface_gating.cpp     — whether a chain is driven to repaint at all
//                            (Decorations.Performance), plus windowSurfaceAnimates
//                            and the shared surface shader clock
//   decoration_render.cpp  — surfacePresentShader, alongside the two paths that
//                            consume the program it compiles

KWin::GLTexture* PlasmaZonesEffect::renderSurfaceChainComposite(KWin::EffectWindow* w, qreal scale,
                                                                KWin::GLShader* captureRestoreShader)
{
    if (!w) {
        return nullptr;
    }
    const QString windowId = getWindowId(w);
    const auto found = m_windowDecorations.constFind(windowId);
    if (found == m_windowDecorations.constEnd()) {
        return nullptr;
    }
    // COPY the record — do not hold the iterator. m_windowDecorations is a QHash,
    // whose iterators are invalidated by any insert, and this function makes a
    // NESTED effects->drawWindow call (the capture below) that re-enters the effect
    // chain. Anything reached from inside that draw which touches
    // updateWindowDecoration would rehash and leave a dangling `bit` for the
    // dereferences that follow the capture — a use-after-free in the compositor.
    // The chain was already being copied for exactly this reason; the other fields
    // needed the same treatment.
    const WindowDecoration deco = *found;
    const WindowDecoration* const bit = &deco;
    const QStringList chain = deco.chain;
    if (chain.isEmpty()) {
        return nullptr;
    }
    // Restore blend/viewport/clear/active-texture on every exit path — the
    // fold runs right before KWin's on-screen draw of this same frame, which
    // must not inherit our offscreen state (see ScopedGlState).
    const ShaderInternal::ScopedGlState glStateGuard;
    // Disable scissor for the whole fold: the FBO clears (capture, buffer, and
    // main passes below) and the offscreen draws must not be clipped to a
    // scissor box the scene walk left enabled — a scissored clear would leave
    // stale/undefined texels in the composite. The guard restores scissor on
    // exit (matching the captureWindowBackdrop and paint_pipeline siblings).
    glDisable(GL_SCISSOR_TEST);
    namespace SC = PhosphorSurfaceShaders::SurfaceShaderContract;

    // Upload the audio spectrum ONCE per fold, up front, before any pass binds
    // its textures. GLTexture::upload binds the new texture to the active unit,
    // so doing it mid-pass (inside bindSurfaceAudio) would clobber uTexture0's
    // binding on unit 0. audioActive() gates the work; the dirty flag makes the
    // repeat calls this frame (other windows, or the transition path) no-ops.
    // The ScopedGlState guard above restores the active unit on exit regardless.
    if (audioActive()) {
        ensureAudioSpectrumTexture();
    }

    // Size the targets to the window's expanded geometry × screen scale, with the
    // same defensive cap as captureWindowBackdrop / renderSurfaceChain.
    //
    // PADDED CANVAS: when the chain requests an outer margin (a pack with a
    // paddingParam, e.g. glow), inflate the capture rect by that margin. The
    // capture viewport below maps the inflated rect onto the FBO, so the
    // window renders inset with real transparent margin on every side — an
    // outer effect gets room to draw even when the window has no
    // decoration-shadow margin of its own (borderless windows). apply()
    // presents the composite on a matching padded quad.
    const qreal pad = bit->outerPadding;
    QRectF windowRect = w->expandedGeometry();
    if (windowRect.isEmpty()) {
        windowRect = w->frameGeometry();
    }
    const SurfaceCanvas canvas = surfaceCanvasFor(windowRect, pad, scale);
    const QRectF logicalGeometry = canvas.logicalGeometry;
    const qreal captureScale = canvas.captureScale;
    const QSize textureSize = canvas.textureSize;
    if (textureSize.isEmpty()) {
        return nullptr;
    }

    // Resolve the decoration profile LAZILY: compiledPack reads it only on a
    // compile-cache miss, and this composite fold runs every frame for an
    // animated / audio decoration. In the steady state (all packs already
    // compiled) resolving the tree every frame would be pure per-frame waste.
    // Mirrors the lazy resolve in windowSurfaceAnimates(); materialised at most
    // once even when several packs miss.
    std::optional<PhosphorSurfaceShaders::DecorationProfile> profile;
    auto compiledPackLazy = [&](const QString& packId) -> CompiledSurfacePack* {
        if (const auto cacheIt = m_compiledPacks.find(packId); cacheIt != m_compiledPacks.end()) {
            return &cacheIt->second;
        }
        if (!profile) {
            profile = m_decorationTree.resolve(resolveSurfacePathFor(windowId));
        }
        return compiledPack(packId, *profile);
    };

    SurfaceMultipassState& state = m_surfaceMultipass[windowId];
    state.canvasGeo = logicalGeometry;

    // (Re)allocate every GL target this fold draws into, for the current size, scale
    // and chain, dropping the caches each allocation makes stale. On failure the whole
    // surface state is gone (and `state` with it), so there is nothing left to fold.
    if (!ensureSurfaceTargets(windowId, state, chain, textureSize, captureScale, compiledPackLazy)) {
        // Out of VRAM. Hand the window back to KWin rather than leaving it redirected
        // with a present shader whose uFinal sampler now points at nothing: the fold has
        // no composite to bind, so the window would render as transparent black — it
        // would VANISH. Undecorated is a far better answer to an allocation failure than
        // invisible. Not during a transition, which owns the shader slot and would be
        // torn out mid-animation; it re-captures every frame anyway and gets its own
        // chance to recover next one.
        if (!captureRestoreShader) {
            // Route through the real teardown rather than half-undoing the decoration by
            // hand. Doing it by hand left the entry's two connections live — and the
            // padded-geometry one keeps damaging a margin band for a decoration that no
            // longer exists — and left releaseDecorationGl's addRepaintFull unflagged,
            // which only failed to invalidate the capture cache because the state had
            // already been erased one line earlier. That is an accident, not a design.
            // removeWindowDecoration disconnects both, releases the GL, and takes the
            // self-repaint scope for us.
            const auto selfRepaint = selfRepaintScope();
            removeWindowDecoration(windowId, w);
        }
        return nullptr;
    }

    // Decide what this fold can REUSE before it does any work: whether the chain may
    // animate right now, what clock it runs on, how much of its head is cacheable, and
    // whether the state it was last folded with has moved. Everything below reads the
    // plan; nothing below decides any of it.
    const SurfaceFoldPlan plan =
        planSurfaceFold(w, windowId, *bit, chain, state, compiledPackLazy, captureRestoreShader != nullptr);
    const bool mayAnimate = plan.mayAnimate;
    const float foldTime = plan.foldTime;
    const bool captureCacheable = plan.captureCacheable;
    const int foldablePacks = plan.foldablePacks;
    const int staticPrefix = plan.staticPrefix;
    const int lastStaticDraw = plan.lastStaticDraw;
    const bool usePrefix = plan.usePrefix;
    const bool allStatic = plan.allStatic;

    // The capture's DESTINATION is part of the cache key, not just its freshness. A
    // chain with no compilable pack captures straight into compositeTex[0] (there is
    // nothing to fold, so the capture IS the composite); every other chain captures into
    // captureTex. A chain can cross that line while a window is open — the decoration
    // tree changes, or a broken pack is edited and recompiles — and a capture cached on
    // the wrong side of it is not a capture at all: the fold would read a texture that
    // was allocated and never written. Re-capture on the flip, which costs exactly one
    // frame and happens almost never.
    if (!state.captureValid) {
        // The capture is RAW (opacity 1.0), with ONE fail-safe exception: an opacity-baking
        // chain (the plain opacity-tint layer) whose opacity-tint pack has no compiled shader
        // would apply the window's resolved opacity nowhere, silently rendering a SetOpacity'd
        // window fully opaque. Dim the capture by the folded value there instead — the nested
        // draw runs KWin's default modulating shader and the pack that owns the value never
        // runs, so single-apply holds. Any other chain captures raw and dims downstream as a
        // pack-param concern (frost/glass contentOpacity, the tint layer's own param). The
        // opacity is part of the capture cache key (plan.foldOpacity), so a change re-captures.
        qreal captureOpacity = 1.0;
        if (deco.chainBakesOpacity && !qFuzzyCompare(deco.foldedOpacity + 1.0, 2.0)) {
            const CompiledSurfacePack* const otPack = compiledPackLazy(QStringLiteral("opacity-tint"));
            if (!otPack || !otPack->shader) {
                captureOpacity = qBound(0.0, deco.foldedOpacity, 1.0);
                // Latched: pack-level condition, and this runs per window per frame — unlatched
                // it would spam at vsync. Cleared with the compile cache on registry reload.
                if (!m_opacityTintFallbackWarned) {
                    m_opacityTintFallbackWarned = true;
                    qCWarning(lcEffect) << "opacity-tint pack unavailable — applying window opacity" << captureOpacity
                                        << "at capture time for" << windowId;
                }
            }
        }
        captureWindowSurface(w, state, logicalGeometry, captureScale,
                             /*intoCaptureTex=*/!plan.captureInComposite, captureCacheable, captureOpacity);
    }
    // Hand the OffscreenEffect slot back: to the passthrough present on the rest
    // path, or to the caller's animation shader mid-transition. Runs on the
    // capture-skipped path too — the slot is a per-window binding the transition
    // path relies on being (re)asserted every fold, not a side effect of capturing.
    setShader(w, captureRestoreShader ? captureRestoreShader : surfacePresentShader());

    // A chain with no animated pack in it produces a composite that is a pure
    // function of the capture — so once folded, it stays correct for exactly as long
    // as the capture does, and there is nothing to re-fold. Skipping the whole of
    // step 2 matters because paintWindow fires whenever ANYTHING on screen damages a
    // region overlapping this window, not only when the window's own content
    // changes: without this a plain bordered window re-ran its entire chain on
    // essentially every frame of any activity anywhere on the desktop.
    //
    // Placed after the setShader above, not before it: the slot is a per-window
    // binding the transition path relies on being re-asserted on every fold, and an
    // early return that skipped it would strand the window on the wrong shader.
    if (allStatic && state.compositeValid && state.compositeTex[state.finalSlot]) {
        // Stamp the fold time even though nothing was folded: the composite IS current as
        // of now, which is what the field means.
        state.lastFoldMs = ShaderInternal::shaderClockNowMs();
        // LEAVE backdropRepaintPending SET. Reaching here proves no pack in this chain
        // varies per frame — which means none of them samples uBackdrop, whatever the
        // metadata's needsBackdrop says (a pack whose shader failed to compile, or whose
        // uBackdrop uses were optimised out of the link, lands here with needsBackdrop
        // still true). A backdrop refold therefore cannot change a single pixel, so the
        // driver must not ask for another one.
        //
        // Clearing the flag here is what an earlier pass did, and it converted a benign
        // one-shot stall into a permanent 30Hz wake-up: the driver armed, this path cleared,
        // 33ms later it armed again, forever, for a window whose composite is provably
        // byte-identical. The flag stays set until a fold that actually folds something
        // clears it — and a chain that starts varying per frame (a recompile lands, a pack
        // starts animating) stops taking this path and clears it on its next real fold.
        //
        // The HOVER flag is the opposite, and the two are not symmetric. Reaching here proves
        // compositeValid survived planSurfaceFold, and planSurfaceFold clears compositeValid
        // whenever the fold cursor differs from the folded one — so the composite provably
        // already reflects the live cursor and the hover repaint HAS been serviced. Leaving
        // it set deadlocked the hover driver outright: it hard-skips any window whose flag is
        // set, so a pure iMouse chain (classified static, because iMouse is state and not a
        // per-frame input) armed the flag once, took this path, and never hover-updated again
        // for the rest of the session.
        state.hoverRepaintPending = false;
        return state.compositeTex[state.finalSlot].get();
    }

    // ── Step 2: fold each pack over the running composite ────────────────────
    // Invariant: each pk is consumed entirely within its own iteration and
    // re-fetched from compiledPack() every pass. std::unordered_map keeps
    // element pointers/references stable across insert/rehash (only iterators
    // are invalidated), so the per-iteration re-fetch is a cheap defensive
    // habit rather than a strict requirement here — but it would become
    // load-bearing if m_compiledPacks were ever swapped for a node-relocating
    // container, so keep it.
    // The running composite is a TEXTURE POINTER, not a slot index: the first
    // pack folds out of captureTex (which the ping-pong must never overwrite),
    // and every pack after that folds out of whichever composite slot the
    // previous one wrote. `dstSlot` alternates 0/1 across the composite pair and
    // `lastDst` records where the final fold landed — finalSlot must name a
    // compositeTex slot, which foldablePacks > 0 guarantees.
    KWin::GLTexture* srcTex = foldablePacks > 0 ? state.captureTex.get() : state.compositeTex[0].get();
    int dstSlot = 0;
    int lastDst = 0;
    // A live prefix means the static head already sits in prefixTex, folded over
    // the still-valid capture: start the animated packs straight off it.
    int firstPack = 0;
    // prefixValid implies prefixTex today (it is only ever set under writesPrefix,
    // which requires a live prefixTex, and every path that releases the texture clears
    // the flag). Check both anyway: if that ever came apart, srcTex would be null and
    // the bind below would be a null deref inside the compositor — a far worse outcome
    // than the one frame of cold fold this guard costs. The chainBufferTex bounds check
    // in the loop below is defensive for exactly the same reason.
    if (state.prefixValid && !state.prefixTex) {
        // Half a guard is worse than none: skipping the read but leaving the flag set means
        // writesPrefix (which requires !prefixValid) never fires again, so the cache is dead
        // for this window forever while the flag goes on claiming it is live.
        state.prefixValid = false;
    }
    if (state.prefixValid) {
        srcTex = state.prefixTex.get();
        firstPack = staticPrefix;
    }
    for (int k = firstPack; k < chain.size(); ++k) {
        CompiledSurfacePack* const pk = compiledPackLazy(chain.at(k));
        if (!pk || !pk->shader) {
            continue; // skip a failed pack; the composite carries through unchanged
        }
        // While rebuilding, the last pack of the cacheable run that actually DRAWS
        // folds into prefixTex rather than a ping-pong slot, so the run is cached in
        // place with no extra copy. The ping-pong then resumes from there for the
        // animated packs (which never write prefixTex, so it survives as their source
        // next frame). Keyed on lastStaticDraw, not on staticPrefix - 1: the run can
        // end with a pack that failed to compile, and that pack never reaches here.
        const bool writesPrefix = usePrefix && !state.prefixValid && k == lastStaticDraw;
        // Defensive: chainBufferTex is sized to chain.size() in the realloc block
        // above and stays in lockstep with `chain`, but guard the unchecked
        // operator[] in case a future edit decouples the two (out-of-bounds [] is
        // UB; the bounds-correct outcome is to skip the pack's buffer passes).
        if (k >= static_cast<int>(state.chainBufferTex.size())) {
            continue;
        }
        const std::vector<std::unique_ptr<KWin::GLTexture>>& bufs = state.chainBufferTex[k];
        const std::vector<std::unique_ptr<KWin::GLFramebuffer>>& bufFbos = state.chainBufferFbo[k];

        // 2a: pack k's buffer passes, sampling the running composite as uTexture0.
        // bufs/bufFbos are allocated together and cleared together, so they are the
        // same length; qMin over both keeps the indexing safe regardless.
        const size_t passCount = qMin(qMin(bufs.size(), bufFbos.size()), pk->bufferPasses.size());
        for (size_t i = 0; i < passCount; ++i) {
            const CompiledSurfaceBufferPass& pass = pk->bufferPasses[i];
            KWin::GLTexture* const target = bufs[i].get();
            KWin::GLFramebuffer& fbo = *bufFbos[i];
            KWin::GLFramebuffer::pushFramebuffer(&fbo);
            glViewport(0, 0, target->width(), target->height());
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            // "A capture exists this frame" drives the uHasBackdrop VALUE for
            // every stage; whether THIS stage samples it (the linker kept its
            // uBackdrop) only drives the bind. A pack's main pass typically
            // reads the blurred buffers rather than uBackdrop itself, so its
            // own sampler location being -1 must not zero the gate.
            const bool backdropAvailable = backdropUsable(state);
            const bool passHasBackdrop = pass.uBackdropLoc >= 0 && backdropAvailable;
            bool passBackdropUnitBound = false; ///< the transparent fallback went to unit 5
            bool passAudioBound = false;
            {
                KWin::ShaderBinder binder(pass.shader.get());
                glActiveTexture(GL_TEXTURE0);
                srcTex->bind();
                if (pass.uTexture0Loc >= 0) {
                    pass.shader->setUniform(pass.uTexture0Loc, 0);
                }
                if (pass.uTimeLoc >= 0) {
                    pass.shader->setUniform(pass.uTimeLoc, foldTime);
                }
                // Canvas geometry for passes that translate logical lengths
                // (blur radii) into canvas UV steps. uSurfaceSize is the
                // composite canvas extent — NOT this buffer's own size — so a
                // radius maps identically at any bufferScale.
                if (pass.uSurfaceSizeLoc >= 0) {
                    pass.shader->setUniform(pass.uSurfaceSizeLoc,
                                            QVector2D(static_cast<float>(state.compositeSize.width()),
                                                      static_cast<float>(state.compositeSize.height())));
                }
                if (pass.uScaleLoc >= 0) {
                    pass.shader->setUniform(pass.uScaleLoc, static_cast<float>(captureScale));
                }
                // Backdrop (needsBackdrop packs) on unit 5 — units 1..4
                // belong to the pass's iChannel bindings below.
                if (passHasBackdrop) {
                    pass.shader->setUniform(pass.uBackdropLoc, 5);
                    glActiveTexture(GL_TEXTURE5);
                    state.backdropTex->bind();
                    glActiveTexture(GL_TEXTURE0);
                } else if (pass.uBackdropLoc >= 0) {
                    // DECLARED but with nothing to supply (first frame, a failed blit, a
                    // window entirely off its output). An unset sampler2D reads unit 0, and
                    // unit 0 holds the RUNNING COMPOSITE — so the pack would sample the
                    // window back into itself. Every other declared-but-unbacked sampler in
                    // this fold gets the transparent fallback and says why; uBackdrop was the
                    // one exception, safe only because every bundled pack happens to gate on
                    // uHasBackdrop first. A third-party pack that does not is not a bug in
                    // the pack.
                    pass.shader->setUniform(pass.uBackdropLoc, 5);
                    // Set the uniform EVEN IF the fallback texture is null. An explicitly set sampler
                    // pointing at an unbound unit reads black, which is the safe answer; an UNSET one
                    // reads unit 0, the running composite. The lazy 1x1 upload can fail (OOM, context
                    // loss), and every other consumer of this texture null-checks it — these two were
                    // the only ones dereferencing it blind, which is a segfault in the compositor.
                    if (KWin::GLTexture* const fallback = transparentFallbackTexture()) {
                        glActiveTexture(GL_TEXTURE5);
                        fallback->bind();
                        glActiveTexture(GL_TEXTURE0);
                        passBackdropUnitBound = true;
                    }
                }
                if (pass.uBackdropRectLoc >= 0) {
                    pass.shader->setUniform(pass.uBackdropRectLoc, state.backdropRect);
                }
                if (pass.uHasBackdropLoc >= 0) {
                    pass.shader->setUniform(pass.uHasBackdropLoc, backdropAvailable ? 1.0f : 0.0f);
                }
                for (size_t j = 0; j < i && j < 4; ++j) {
                    glActiveTexture(GL_TEXTURE1 + static_cast<int>(j));
                    bufs[j]->bind();
                    if (pass.iChannelLoc[j] >= 0) {
                        pass.shader->setUniform(pass.iChannelLoc[j], 1 + static_cast<int>(j));
                    }
                    if (pass.iChannelResolutionLoc[j] >= 0) {
                        const QVector4D res(static_cast<float>(bufs[j]->width()), static_cast<float>(bufs[j]->height()),
                                            0.0f, 0.0f);
                        pass.shader->setUniform(pass.iChannelResolutionLoc[j], res);
                    }
                }
                // Bind a transparent fallback to every channel this pass DECLARES but has
                // no prior buffer output for — which for the FIRST buffer pass is all of
                // them, since there is nothing before it. An unset sampler2D reads texture
                // unit 0, and unit 0 holds the RUNNING COMPOSITE: a pack whose first buffer
                // pass samples iChannel0 was reading the window back into itself. The main
                // pass, the audio slot and the user-texture slots all bind a fallback for
                // exactly this reason; the buffer passes were left out of it.
                for (size_t j = i; j < 4; ++j) {
                    if (pass.iChannelLoc[j] < 0) {
                        continue; // the pass never samples this channel
                    }
                    // Park the destination unit BEFORE the call: the first-ever call
                    // creates the texture, and GLTexture::upload binds it on the ACTIVE
                    // unit, which is TEXTURE0 here.
                    glActiveTexture(GL_TEXTURE1 + static_cast<int>(j));
                    KWin::GLTexture* const fallback = transparentFallbackTexture();
                    if (!fallback) {
                        glActiveTexture(GL_TEXTURE0);
                        continue; // allocation failed — keep the old omit behaviour
                    }
                    fallback->bind();
                    pass.shader->setUniform(pass.iChannelLoc[j], 1 + static_cast<int>(j));
                    if (pass.iChannelResolutionLoc[j] >= 0) {
                        pass.shader->setUniform(pass.iChannelResolutionLoc[j],
                                                QVector4D(static_cast<float>(fallback->width()),
                                                          static_cast<float>(fallback->height()), 0.0f, 0.0f));
                    }
                    glActiveTexture(GL_TEXTURE0);
                }
                // Per-window param values for THIS chain pack; the compiled
                // pack's baked baseline is the fallback (same seeding as the
                // main-pass pushBorderUniforms below).
                const SurfaceParamValues* packVals = nullptr;
                if (const auto pvIt = bit->packParamValues.constFind(chain.at(k));
                    pvIt != bit->packParamValues.constEnd()) {
                    packVals = &*pvIt;
                }
                for (int slot = 0; slot < SC::kMaxCustomParams; ++slot) {
                    if (pass.customParamsLoc[slot] >= 0) {
                        pass.shader->setUniform(pass.customParamsLoc[slot],
                                                packVals ? packVals->params[static_cast<size_t>(slot)]
                                                         : pk->customParamsValues[slot]);
                    }
                }
                for (int slot = 0; slot < SC::kMaxCustomColors; ++slot) {
                    if (pass.customColorsLoc[slot] >= 0) {
                        pass.shader->setUniform(pass.customColorsLoc[slot],
                                                packVals ? packVals->colors[static_cast<size_t>(slot)]
                                                         : pk->customColorsValues[slot]);
                    }
                }
                // Audio spectrum (unit kSurfaceAudioUnit) for a buffer pass that
                // reads surface_audio.glsl — same contract as the main pass.
                passAudioBound =
                    bindSurfaceAudio(pass.shader.get(), pass.iAudioSpectrumSizeLoc, pass.uAudioSpectrumLoc, mayAnimate);
                drawFullscreenQuad();
            }
            // Unbind all four channel units, not just the prior-buffer ones: the loop
            // above also binds a transparent fallback to every channel the pass declares
            // but has no buffer for, and leaving those bound would leak this pass's state
            // into the next one.
            for (size_t j = 0; j < 4; ++j) {
                glActiveTexture(GL_TEXTURE1 + static_cast<int>(j));
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            if (passHasBackdrop || passBackdropUnitBound) {
                glActiveTexture(GL_TEXTURE5);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            if (passAudioBound) {
                glActiveTexture(GL_TEXTURE0 + ShaderInternal::kSurfaceAudioUnit);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            glActiveTexture(GL_TEXTURE0);
            KWin::GLFramebuffer::popFramebuffer();
        }

        // 2b: pack k's MAIN as a fullscreen FBO pass → the next composite slot, or
        // into prefixTex when this is the last static pack of a rebuild.
        const int dst = dstSlot;
        KWin::GLTexture* const target = writesPrefix ? state.prefixTex.get() : state.compositeTex[dst].get();
        KWin::GLFramebuffer& fbo = writesPrefix ? *state.prefixFbo : *state.compositeFbo[dst];
        KWin::GLFramebuffer::pushFramebuffer(&fbo);
        glViewport(0, 0, target->width(), target->height());
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        // Same split as the buffer passes: the gate VALUE reflects capture
        // availability; the main pass's own sampler location only gates the
        // bind (frost's main reads the blurred buffers, not uBackdrop).
        const bool backdropAvailable = backdropUsable(state);
        const bool mainHasBackdrop = pk->uBackdropLoc >= 0 && backdropAvailable;
        bool mainBackdropUnitBound = false; ///< the transparent fallback went to unit 5
        bool mainAudioBound = false;
        bool mainUserTexturesBound = false;
        // Highest iChannel unit bound by the main pass, +1. Tracked rather than
        // recomputed from passCount, because a declared-but-unrendered channel is
        // also bound (to the transparent fallback) and must be unbound too.
        int mainChannelsBound = 0;
        {
            KWin::ShaderBinder binder(pk->shader.get());
            // Identity MVP so the NDC fullscreen quad from drawFullscreenQuad maps
            // 1:1 through the pack's default (MVP) vertex stage.
            pk->shader->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix, QMatrix4x4());
            glActiveTexture(GL_TEXTURE0);
            srcTex->bind();
            if (pk->uTexture0Loc >= 0) {
                pk->shader->setUniform(pk->uTexture0Loc, 0);
            }
            // Bind only the buffers step 2a actually rendered (passCount), not
            // every allocated slot: if bufs ever outnumbers the pack's buffer
            // passes, the surplus textures are unwritten and must not be sampled.
            const int n = qMin(static_cast<int>(passCount), 4);
            for (int i = 0; i < n; ++i) {
                glActiveTexture(GL_TEXTURE1 + i);
                bufs[i]->bind();
                mainChannelsBound = i + 1;
                if (pk->iChannelLoc[i] >= 0) {
                    pk->shader->setUniform(pk->iChannelLoc[i], 1 + i);
                }
                if (pk->iChannelResolutionLoc[i] >= 0) {
                    const QVector4D res(static_cast<float>(bufs[i]->width()), static_cast<float>(bufs[i]->height()),
                                        0.0f, 0.0f);
                    pk->shader->setUniform(pk->iChannelResolutionLoc[i], res);
                }
            }
            // Slots the pack DECLARES but this fold rendered no buffer for — a
            // buffer allocation failed (collapsing passCount to 0), or the pack
            // declares more iChannel slots than it has buffer passes.
            //
            // Leaving them alone does NOT make them sample transparent black. The
            // linker kept iChannelLoc, and an UNSET sampler2D reads texture unit
            // 0 — which at this moment holds the RUNNING COMPOSITE. A frost pack
            // would sample the unblurred window as its own blur buffer. The
            // program is also shared across windows and frames, so the sampler can
            // instead retain a unit index set during a PREVIOUS window's
            // successful fold, whose units now hold unrelated texels. Bind the
            // transparent fallback so an unrendered channel really does read as 0,
            // which is what the allocation-failure path claims to deliver.
            for (int i = n; i < 4; ++i) {
                if (pk->iChannelLoc[i] < 0) {
                    continue; // the pack never samples this channel
                }
                // Park the destination unit BEFORE the call, mirroring the
                // user-texture fallback below: the first-ever call creates the
                // texture, and GLTexture::upload binds it on the CURRENTLY ACTIVE
                // unit — which is TEXTURE0 here, holding the running composite.
                glActiveTexture(GL_TEXTURE1 + i);
                KWin::GLTexture* const fallback = transparentFallbackTexture();
                if (!fallback) {
                    glActiveTexture(GL_TEXTURE0);
                    continue; // allocation failed — keep the old omit behaviour
                }
                fallback->bind();
                mainChannelsBound = i + 1;
                pk->shader->setUniform(pk->iChannelLoc[i], 1 + i);
                if (pk->iChannelResolutionLoc[i] >= 0) {
                    pk->shader->setUniform(pk->iChannelResolutionLoc[i],
                                           QVector4D(static_cast<float>(fallback->width()),
                                                     static_cast<float>(fallback->height()), 0.0f, 0.0f));
                }
                glActiveTexture(GL_TEXTURE0);
            }
            // Backdrop (needsBackdrop packs) on unit 5 — units 1..n above
            // hold this pack's buffer outputs.
            if (mainHasBackdrop) {
                pk->shader->setUniform(pk->uBackdropLoc, 5);
                glActiveTexture(GL_TEXTURE5);
                state.backdropTex->bind();
                glActiveTexture(GL_TEXTURE0);
            } else if (pk->uBackdropLoc >= 0) {
                // Declared with nothing behind it — the transparent fallback, for the reason
                // given on the buffer-pass sibling above. Unit 0 is the running composite.
                pk->shader->setUniform(pk->uBackdropLoc, 5);
                // Set the uniform EVEN IF the fallback texture is null. An explicitly set sampler
                // pointing at an unbound unit reads black, which is the safe answer; an UNSET one
                // reads unit 0, the running composite. The lazy 1x1 upload can fail (OOM, context
                // loss), and every other consumer of this texture null-checks it — these two were
                // the only ones dereferencing it blind, which is a segfault in the compositor.
                if (KWin::GLTexture* const fallback = transparentFallbackTexture()) {
                    glActiveTexture(GL_TEXTURE5);
                    fallback->bind();
                    glActiveTexture(GL_TEXTURE0);
                    mainBackdropUnitBound = true;
                }
            }
            if (pk->uBackdropRectLoc >= 0) {
                pk->shader->setUniform(pk->uBackdropRectLoc, state.backdropRect);
            }
            if (pk->uHasBackdropLoc >= 0) {
                pk->shader->setUniform(pk->uHasBackdropLoc, backdropAvailable ? 1.0f : 0.0f);
            }
            // Audio spectrum (unit kSurfaceAudioUnit) for a pack that reads
            // surface_audio.glsl. Pushes iAudioSpectrumSize (0 when audio is
            // off, so getBass* reads 0 and the pack renders static) and binds
            // the spectrum texture only when live.
            mainAudioBound =
                bindSurfaceAudio(pk->shader.get(), pk->iAudioSpectrumSizeLoc, pk->uAudioSpectrumLoc, mayAnimate);
            // User-declared image textures (metadata `textures`), units
            // kSurfaceUserTextureBaseUnit.. — loaded once at compile time onto
            // the pack state. Every slot the shader REFERENCES gets a bind: a
            // loaded texture, or the shared 1x1 transparent fallback when the
            // metadata file failed to load (or the pack referenced a sampler
            // it never declared). Without the fallback a referenced classic
            // default-block sampler reads unit 0 — the running composite —
            // instead of the contract's transparent black. iTextureResolution
            // is pushed only for real textures (stays 0 for the fallback).
            for (int t = 0; t < PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots; ++t) {
                if (pk->userTextureLoc[t] < 0) {
                    continue;
                }
                KWin::GLTexture* userTex = pk->userTextures[t].get();
                if (!userTex) {
                    // Park the destination unit BEFORE the call: the
                    // first-ever call creates the texture, and
                    // GLTexture::upload binds it on the CURRENTLY ACTIVE
                    // unit — which is TEXTURE0 here, holding the running
                    // composite this pass samples.
                    glActiveTexture(GL_TEXTURE0 + ShaderInternal::kSurfaceUserTextureBaseUnit + t);
                    userTex = transparentFallbackTexture();
                    glActiveTexture(GL_TEXTURE0);
                    if (!userTex) {
                        continue; // allocation failed — keep the old omit behaviour
                    }
                } else if (pk->iTextureResolutionLoc[t] >= 0) {
                    pk->shader->setUniform(pk->iTextureResolutionLoc[t],
                                           QVector4D(static_cast<float>(userTex->width()),
                                                     static_cast<float>(userTex->height()), 0.0f, 0.0f));
                }
                const int unit = ShaderInternal::kSurfaceUserTextureBaseUnit + t;
                pk->shader->setUniform(pk->userTextureLoc[t], unit);
                glActiveTexture(GL_TEXTURE0 + unit);
                userTex->bind();
                glActiveTexture(GL_TEXTURE0);
                mainUserTexturesBound = true;
            }
            // Contract uniforms + pack params. windowId threaded in so the
            // focus-fade ramp doesn't recompute getWindowId(w) per pack.
            pushBorderUniforms(w, *bit, chain.at(k), *pk, captureScale, foldTime, plan.foldCursor, pad, windowId);
            drawFullscreenQuad();
        }
        for (int i = 0; i < mainChannelsBound; ++i) {
            glActiveTexture(GL_TEXTURE1 + i);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (mainHasBackdrop || mainBackdropUnitBound) {
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (mainAudioBound) {
            glActiveTexture(GL_TEXTURE0 + ShaderInternal::kSurfaceAudioUnit);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (mainUserTexturesBound) {
            for (int t = 0; t < PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots; ++t) {
                glActiveTexture(GL_TEXTURE0 + ShaderInternal::kSurfaceUserTextureBaseUnit + t);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
        }
        glActiveTexture(GL_TEXTURE0);
        KWin::GLFramebuffer::popFramebuffer();
        if (writesPrefix) {
            // The static run is now cached. The animated packs fold out of it and
            // ping-pong through the composite pair, leaving prefixTex untouched so
            // it can serve as their source again next frame.
            srcTex = state.prefixTex.get();
            state.prefixValid = true;
            state.prefixPackCount = staticPrefix;
        } else {
            srcTex = state.compositeTex[dst].get();
            lastDst = dst;
            dstSlot = 1 - dst;
        }
    }

    state.finalSlot = lastDst;
    // The composite just folded is reusable verbatim only when nothing in the chain
    // varies PER FRAME (iTime, audio, backdrop). Focus, opacity and the cursor are baked
    // in as keys instead, so record what this fold used — the next one refuses the
    // cache if either has moved.
    state.compositeValid = allStatic;
    state.foldedFocus = plan.foldFocus;
    state.foldedOpacity = plan.foldOpacity;
    state.foldedCursor = plan.foldCursor;
    state.lastFoldMs = ShaderInternal::shaderClockNowMs();
    // Both drivers' repaints have landed: this fold actually folded, so the composite
    // reflects the scene behind the window AND the live cursor.
    state.backdropRepaintPending = false;
    state.hoverRepaintPending = false;
    return state.compositeTex[lastDst].get();
}

} // namespace PlasmaZones
