// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include "shader_internal.h"
#include "surface_fold.h"
#include "types.h"
#include "window_query.h"

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
//   surface_backdrop.cpp   — captureWindowBackdrop (the scene behind the window)
//   surface_audio.cpp      — the CAVA spectrum binding
//   surface_compile.cpp    — pack compilation
//   surface_gating.cpp     — whether a chain is driven to repaint at all
//                            (Decorations.Performance), plus windowSurfaceAnimates
//                            and the shared surface shader clock
//   decoration_render.cpp  — surfacePresentShader, alongside the two paths that
//                            consume the program it compiles

// captureWindowBackdrop lives in surface_backdrop.cpp (split out to keep this
// TU under the 800-line limit, mirroring the surface_audio.cpp split).

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
    QRectF logicalGeometry = w->expandedGeometry();
    if (logicalGeometry.isEmpty()) {
        logicalGeometry = w->frameGeometry();
    }
    logicalGeometry.adjust(-pad, -pad, pad, pad);
    qreal captureScale = scale;
    constexpr qreal kMaxSurfaceDim = 8192.0;
    const qreal longestPx = qMax(logicalGeometry.width(), logicalGeometry.height()) * captureScale;
    if (longestPx > kMaxSurfaceDim) {
        captureScale *= kMaxSurfaceDim / longestPx;
    }
    const QSize textureSize = (logicalGeometry.size() * captureScale).toSize();
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

    // (Re)allocate the composite ping-pong pair on a size change — or on a SCALE
    // change that the size does not reflect. Past the kMaxSurfaceDim cap the texture
    // is pinned to the cap on its long axis whatever the input scale, so a huge
    // window crossing between outputs of different scale keeps the same
    // compositeSize while uSurfaceScale (which packs multiply logical-px border
    // widths and radii by) moves under it.
    if (state.compositeSize != textureSize || !qFuzzyCompare(state.captureScaleKey, captureScale)
        || !state.compositeTex[0] || !state.compositeTex[1]) {
        bool allocFailed = false;
        for (size_t i = 0; i < state.compositeTex.size(); ++i) {
            auto& t = state.compositeTex[i];
            t = KWin::GLTexture::allocate(GL_RGBA8, textureSize);
            if (!t) {
                allocFailed = true;
                break;
            }
            t->setFilter(GL_LINEAR);
            t->setWrapMode(GL_CLAMP_TO_EDGE);
            // Wrap each composite target once, here, rather than per pass per
            // frame in the fold below.
            auto fbo = std::make_unique<KWin::GLFramebuffer>(t.get());
            if (!fbo->valid()) {
                allocFailed = true;
                break;
            }
            state.compositeFbo[i] = std::move(fbo);
        }
        // The capture target lives alongside the ping-pong pair and is sized
        // identically; a stale one at the old size must never be presented, so the
        // realloc invalidates every cache keyed on it.
        //
        // The static-prefix target is NOT allocated here. It is only ever written
        // when a chain has a cacheable run followed by a per-frame pack, which the
        // most common chains do not — the default ["border"] has no per-frame pack at
        // all — so allocating it eagerly meant a full-canvas RGBA8 (a fifth of the
        // decoration's whole VRAM budget, ~8 MB on a 4K window) that was never
        // written and never read. It is allocated lazily below, once the fold knows
        // the chain actually needs it.
        if (!allocFailed) {
            state.captureValid = false;
            state.prefixValid = false;
            state.compositeValid = false;
            state.prefixPackCount = -1;
            state.captureFbo.reset();
            state.prefixTex.reset();
            state.prefixFbo.reset();
            if (!allocSurfaceTarget(state.captureTex, state.captureFbo, textureSize)) {
                allocFailed = true;
            }
        }
        if (allocFailed) {
            // Drop the half-allocated state. Erase AFTER the loop has ended so
            // we never destroy the container mid-iteration (state is a reference
            // into the map being erased).
            m_surfaceMultipass.erase(windowId);
            return nullptr;
        }
        state.compositeSize = textureSize;
        state.captureScaleKey = captureScale;
        state.chainKey.clear(); // force the per-pack buffers to reallocate at the new size
    }

    // (Re)allocate the cached per-pack buffer textures when the chain or size
    // changes. chainBufferTex[k] holds one texture per pack k's buffer passes,
    // downscaled by that pack's bufferScale; a pack that fails to compile (or has
    // no buffers) leaves an empty inner vector and renders single-pass in the fold.
    if (state.chainKey != chain) {
        state.chainBufferTex.clear();
        state.chainBufferTex.resize(chain.size());
        state.chainBufferFbo.clear();
        state.chainBufferFbo.resize(chain.size());
        for (int k = 0; k < chain.size(); ++k) {
            CompiledSurfacePack* const pk = compiledPackLazy(chain.at(k));
            if (!pk || !pk->shader || pk->bufferPasses.empty()) {
                continue;
            }
            const PhosphorSurfaceShaders::SurfaceShaderEffect eff = m_surfaceShaderRegistry.effect(chain.at(k));
            const qreal bufferScale =
                qBound(PhosphorSurfaceShaders::SurfaceShaderEffect::kMinBufferScale, eff.bufferScale,
                       PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferScale);
            const QSize bufferSize(qMax(1, qRound(textureSize.width() * bufferScale)),
                                   qMax(1, qRound(textureSize.height() * bufferScale)));
            auto& bufs = state.chainBufferTex[k];
            auto& fbos = state.chainBufferFbo[k];
            bufs.reserve(pk->bufferPasses.size());
            fbos.reserve(pk->bufferPasses.size());
            for (size_t i = 0; i < pk->bufferPasses.size(); ++i) {
                std::unique_ptr<KWin::GLTexture> bt = KWin::GLTexture::allocate(GL_RGBA8, bufferSize);
                if (!bt) {
                    // Pack k degrades to no buffers. The fold's main pass then
                    // binds the transparent fallback to every iChannel the pack
                    // still declares, so they genuinely sample 0 — an unset
                    // sampler2D would otherwise read unit 0, i.e. the running
                    // composite.
                    bufs.clear();
                    fbos.clear(); // the framebuffers pooled beside them go too
                    break;
                }
                bt->setFilter(GL_LINEAR);
                bt->setWrapMode(GL_CLAMP_TO_EDGE);
                // Wrap the buffer target once here; the fold reuses it every frame.
                // Keep bufs/fbos strictly in lockstep — the fold indexes both by i.
                auto bfbo = std::make_unique<KWin::GLFramebuffer>(bt.get());
                if (!bfbo->valid()) {
                    bufs.clear();
                    fbos.clear();
                    break;
                }
                bufs.push_back(std::move(bt));
                fbos.push_back(std::move(bfbo));
            }
        }
        // A different chain folds to a different composite, so neither the whole-
        // chain cache nor the static-prefix cache survives it.
        state.compositeValid = false;
        state.prefixValid = false;
        state.prefixPackCount = -1;
        state.chainKey = chain;
    }

    // ── Step 1: capture the raw window surface into captureTex ────────────────
    // Raw window capture (bypass the redirect shader so the
    // capture is the raw composited window), then restore the present passthrough.
    //
    // SKIPPED when the window has not damaged since the last capture. This is
    // the single most expensive step of the fold: KWin::effects->drawWindow()
    // re-enters the entire draw chain. Its only input is the window's own
    // content, so on a window that is idle — but whose chain still animates
    // (motes drifting, a border gleam orbiting) — the previous capture is still
    // exactly correct and the iTime packs below fold over it unchanged.
    // windowDamaged clears captureValid, so a window that genuinely repaints
    // every frame (video, a busy terminal) re-captures every frame as before.
    // Does ANY pack in the chain actually fold? If none does (every pack failed
    // to compile), the presented composite is the bare capture — and finalSlot
    // must index compositeTex, so in that degenerate case capture straight into
    // compositeTex[0] and don't cache. compiledPackLazy is memoised, so this
    // pre-pass costs a hash lookup per pack and the fold below re-reads it free.
    int foldablePacks = 0;
    for (int k = 0; k < chain.size(); ++k) {
        const CompiledSurfacePack* const pk = compiledPackLazy(chain.at(k));
        if (pk && pk->shader && k < static_cast<int>(state.chainBufferTex.size())) {
            ++foldablePacks;
        }
    }
    // A transition supplies its own restore shader and drives the window's
    // geometry frame by frame; don't trust a cached capture across it.
    const bool captureCacheable = foldablePacks > 0 && captureRestoreShader == nullptr;
    if (!captureCacheable) {
        state.captureValid = false;
    }

    // How many packs at the head of the chain are cacheable? Their fold is a pure
    // function of the capture and the folded STATE (focus / opacity), so it can be
    // cached across frames while the per-frame packs behind them keep re-folding.
    //
    // This is a leading RUN, not a set: once a per-frame pack has folded, its output
    // differs every frame, so every pack downstream is fed a different input every
    // frame no matter how simple it is. Stop at the first pack that varies per frame
    // — and at the first that failed to compile, whose skip would otherwise desync
    // the count from the packs actually folded below.
    int staticPrefix = 0;
    while (staticPrefix < chain.size()) {
        const CompiledSurfacePack* const pk = compiledPackLazy(chain.at(staticPrefix));
        if (!pk || !pk->shader || staticPrefix >= static_cast<int>(state.chainBufferTex.size())
            || packVariesPerFrame(*pk)) {
            break;
        }
        ++staticPrefix;
    }

    // The STATE the fold is about to bake in. Focus and rule-opacity are constant
    // between events (the ramp clamps to exactly 0/1 at its ends), so they are cache
    // keys rather than disqualifiers — which is what lets the default `border` chain
    // cache at all, since that pack mixes its colours on uSurfaceFocused.
    //
    // advanceFocusFade reads the PINNED per-frame clock, so calling it here and again
    // from pushBorderUniforms within the same fold is an exact no-op the second time:
    // the ramp still advances at most once per frame.
    const bool focusedNow = KWin::effects && w == KWin::effects->activeWindow();
    const float foldFocus = advanceFocusFade(windowId, focusedNow);
    float foldOpacity = static_cast<float>(bit->ruleOpacity);
    if (m_shaderManager.frameOpacityCached(w)) {
        const auto frameOpacity = m_shaderManager.cachedFrameOpacity(w);
        foldOpacity = frameOpacity ? static_cast<float>(qBound(0.0, *frameOpacity, 1.0)) : 1.0f;
    }
    const bool stateMoved =
        !qFuzzyCompare(state.foldedFocus, foldFocus) || !qFuzzyCompare(state.foldedOpacity, foldOpacity);

    // The PREFIX cache pays only when something per-frame follows the cacheable run:
    // it exists so those packs can fold over a run that does not need re-folding.
    bool usePrefix = captureCacheable && staticPrefix > 0 && staticPrefix < foldablePacks;
    // Allocate its target lazily, and only for a chain that will actually use it. A
    // chain with no per-frame pack (the default ["border"]) never writes it, so an
    // eager allocation was a full-canvas RGBA8 held for nothing. Release it again if
    // the chain changes to a shape that no longer needs it.
    if (usePrefix) {
        if (!state.prefixTex && !allocSurfaceTarget(state.prefixTex, state.prefixFbo, state.compositeSize)) {
            // Out of VRAM for the optional cache: fold the chain the long way rather
            // than failing the whole paint.
            usePrefix = false;
        }
    } else if (state.prefixTex) {
        state.prefixTex.reset();
        state.prefixFbo.reset();
    }
    // When NOTHING in the chain varies per frame there is no such split — the whole
    // composite is a pure function of (capture, state), so it is cached entire.
    const bool allStatic = captureCacheable && foldablePacks > 0 && staticPrefix == foldablePacks;
    // Both caches sit downstream of the capture and of the folded state.
    if (!state.captureValid || stateMoved) {
        state.prefixValid = false;
        state.compositeValid = false;
    }
    if (!usePrefix || state.prefixPackCount != staticPrefix) {
        state.prefixValid = false;
    }
    if (!allStatic) {
        state.compositeValid = false;
    }

    if (!state.captureValid) {
        KWin::GLFramebuffer& fbo = foldablePacks > 0 ? *state.captureFbo : *state.compositeFbo[0];
        setShader(w, nullptr);
        m_capturingSnapshot = true;
        // Guard the re-entrancy flag against a throw from the draw chain — a
        // leaked m_capturingSnapshot would corrupt every subsequent paint.
        // Same pattern as renderSurfaceChain.
        auto resetCapture = qScopeGuard([this] {
            m_capturingSnapshot = false;
        });
        {
            KWin::RenderTarget renderTarget(&fbo);
            KWin::RenderViewport viewport(logicalGeometry, captureScale, renderTarget, QPoint());
            KWin::GLFramebuffer::pushFramebuffer(&fbo);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            KWin::ItemEffect keepRenderable(w->windowItem());
            KWin::WindowPaintData captureData;
            // Capture RAW (opacity 1.0). Rule opacity is applied downstream
            // as a shader concern: the present passthrough modulates the
            // final composite (KWin-style uniform ghosting), unless a chain
            // pack declares handlesOpacity and applies uSurfaceOpacity to
            // its own content sample instead (frost). Dimming the capture
            // here would double-apply against either.
            captureData.setOpacity(1.0);
            const int captureMask = PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_TRANSLUCENT;
            KWin::effects->drawWindow(renderTarget, viewport, w, captureMask, KWin::Region::infinite(), captureData);
            KWin::GLFramebuffer::popFramebuffer();
        }
        resetCapture.dismiss();
        m_capturingSnapshot = false;
        state.captureValid = captureCacheable;
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
        // Stamp the fold time even though nothing was folded: the composite IS
        // current as of now, which is what the field means. Without this, a chain
        // whose metadata declares needsBackdrop but whose compiled shaders never
        // actually reference uBackdrop is classified cacheable, early-returns
        // forever, and lastFoldMs stays frozen — so postPaintScreen's backdropDue
        // test reads permanently true and drives an addRepaintFull every ~33ms for
        // the rest of the session, and the desktop can never idle.
        state.lastFoldMs = ShaderInternal::shaderClockNowMs();
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
    if (state.prefixValid) {
        srcTex = state.prefixTex.get();
        firstPack = staticPrefix;
    }
    // Did a pack that ACTUALLY DREW take ownership of the window's rule alpha? The
    // metadata flag alone cannot answer this: a handlesOpacity pack that fails to
    // compile is skipped below, so nothing applies uSurfaceOpacity — and if both
    // consumers stood down on the metadata, the window would render fully opaque and
    // silently drop the user's SetOpacity rule. See SurfaceMultipassState::handledOpacity.
    //
    // The packs SKIPPED by a live prefix count too. Their fold is baked into
    // prefixTex, so a handlesOpacity pack in the static head has already applied the
    // alpha even though the loop below never visits it. Starting from false there
    // would report the fold as not owning an alpha it very much baked in, and the
    // present pass would modulate it a second time. A pack that compiled is a pack
    // that drew, which is exactly what the loop concludes for the packs it does run.
    bool foldAppliedOpacity = false;
    for (int k = 0; k < firstPack; ++k) {
        const CompiledSurfacePack* const pk = compiledPackLazy(chain.at(k));
        if (pk && pk->shader && pk->handlesOpacity) {
            foldAppliedOpacity = true;
            break;
        }
    }
    for (int k = firstPack; k < chain.size(); ++k) {
        CompiledSurfacePack* const pk = compiledPackLazy(chain.at(k));
        if (!pk || !pk->shader) {
            continue; // skip a failed pack; the composite carries through unchanged
        }
        // While rebuilding, the LAST static pack folds into prefixTex rather than a
        // ping-pong slot, so the run is cached in place with no extra copy. The
        // ping-pong then resumes from there for the animated packs (which never
        // write prefixTex, so it survives as their source next frame).
        const bool writesPrefix = usePrefix && !state.prefixValid && k == staticPrefix - 1;
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
            const bool backdropAvailable = state.backdropTex && state.backdropRect.z() > 0.0f;
            const bool passHasBackdrop = pass.uBackdropLoc >= 0 && backdropAvailable;
            bool passAudioBound = false;
            {
                KWin::ShaderBinder binder(pass.shader.get());
                glActiveTexture(GL_TEXTURE0);
                srcTex->bind();
                if (pass.uTexture0Loc >= 0) {
                    pass.shader->setUniform(pass.uTexture0Loc, 0);
                }
                if (pass.uTimeLoc >= 0) {
                    pass.shader->setUniform(pass.uTimeLoc, surfaceShaderTimeSeconds());
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
                    bindSurfaceAudio(pass.shader.get(), pass.iAudioSpectrumSizeLoc, pass.uAudioSpectrumLoc);
                drawFullscreenQuad();
            }
            for (size_t j = 0; j < i && j < 4; ++j) {
                glActiveTexture(GL_TEXTURE1 + static_cast<int>(j));
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            if (passHasBackdrop) {
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
        const bool backdropAvailable = state.backdropTex && state.backdropRect.z() > 0.0f;
        const bool mainHasBackdrop = pk->uBackdropLoc >= 0 && backdropAvailable;
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
            mainAudioBound = bindSurfaceAudio(pk->shader.get(), pk->iAudioSpectrumSizeLoc, pk->uAudioSpectrumLoc);
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
            pushBorderUniforms(w, *bit, chain.at(k), *pk, captureScale, pad, windowId);
            drawFullscreenQuad();
        }
        for (int i = 0; i < mainChannelsBound; ++i) {
            glActiveTexture(GL_TEXTURE1 + i);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (mainHasBackdrop) {
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
        // Latch AFTER the main pass has actually drawn. Setting it at the top of the
        // loop — as this first did — claims the alpha was applied on paths where the
        // pack's main pass never runs (a chainBufferTex size mismatch, an invalid
        // FBO). pushBorderUniforms is the ONLY site that pushes uSurfaceOpacity and
        // it lives inside that main pass, so the flag would say the alpha was handled,
        // both consumers would stand down, and the window would render fully opaque —
        // the exact fail-open this flag exists to remove, relocated rather than fixed.
        if (pk->handlesOpacity && pk->uOpacityLoc >= 0) {
            foldAppliedOpacity = true;
        }
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
    // varies PER FRAME (iTime, audio, backdrop, mouse). Focus and opacity are baked
    // in as keys instead, so record what this fold used — the next one refuses the
    // cache if either has moved.
    state.compositeValid = allStatic;
    state.handledOpacity = foldAppliedOpacity;
    state.foldedFocus = foldFocus;
    state.foldedOpacity = foldOpacity;
    state.lastFoldMs = ShaderInternal::shaderClockNowMs();
    return state.compositeTex[lastDst].get();
}

} // namespace PlasmaZones
