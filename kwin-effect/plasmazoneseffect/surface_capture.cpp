// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The composite fold's INPUT side, split out of surfacelayers.cpp (which owns the
// fold itself) to keep that TU under the 800-line limit.
//
// Two steps, in the order the fold runs them:
//   ensureSurfaceTargets  — (re)allocate the per-window GL targets the fold draws
//                           into, and invalidate exactly the caches an allocation
//                           invalidates.
//   captureWindowSurface  — the raw window capture, which is the single most
//                           expensive step of the whole fold (it re-enters KWin's
//                           draw chain) and the reason the capture cache exists.

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
#include <opengl/gltexture.h>
#include <scene/item.h>
#include <scene/windowitem.h>

#include <PhosphorSurface/SurfaceShaderEffect.h>

#include <QPoint>
#include <QRectF>
#include <QScopeGuard>
#include <QSize>

#include <epoxy/gl.h>

namespace PlasmaZones {

// (Re)allocate this window's composite / capture / per-pack buffer targets for the
// current size, scale and chain, and drop every cache an allocation makes stale.
//
// Returns false when an allocation FAILED, in which case the window's whole surface
// state has been erased and @p state is dangling — the caller must abandon the fold.
bool PlasmaZonesEffect::ensureSurfaceTargets(const QString& windowId, SurfaceMultipassState& state,
                                             const QStringList& chain, const QSize& textureSize, qreal captureScale,
                                             const CompiledPackResolver& compiledPackLazy)
{
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
            return false;
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
    return true;
}

// Capture the raw window surface into the target the fold will read as uTexture0.
//
// THE expensive step: KWin::effects->drawWindow() re-enters the entire draw chain for
// this window. Its only input is the window's own content, so the fold caches the
// result and re-runs this only when the window actually damages — which is what the
// whole capture cache is for. @p intoCaptureTex is false only for the degenerate chain
// where no pack compiled, which folds nothing and presents the capture directly out of
// compositeTex[0].
void PlasmaZonesEffect::captureWindowSurface(KWin::EffectWindow* w, SurfaceMultipassState& state,
                                             const QRectF& logicalGeometry, qreal captureScale, bool intoCaptureTex,
                                             bool captureCacheable)
{
    KWin::GLFramebuffer& fbo = intoCaptureTex ? *state.captureFbo : *state.compositeFbo[0];
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

} // namespace PlasmaZones
