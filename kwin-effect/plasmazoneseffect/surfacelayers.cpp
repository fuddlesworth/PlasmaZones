// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include "shader_internal.h"
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

#include <QLoggingCategory>
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

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

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
// addresses the padded canvas; force=true because mid-animation there is no
// OffscreenData blit to fall back on even for a single unpadded pack, and the
// capture hands the OffscreenEffect slot back to the ANIMATION shader (not the
// rest path's present passthrough).
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
    // slotWindowClosed defers removeWindowBorder), which is exactly the frozen
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
    return renderSurfaceChainComposite(w, scale, transition.cached->shader.get(), /*force=*/true);
}

// Render the MULTIPASS surface pack's buffer passes for @p w into its per-window
// FBO chain (m_surfaceMultipass), so the IDLE drawWindow path can bind the
// buffer outputs as iChannel0..3 for the main pass. Single-pass packs (the
// border) have an empty pack->bufferPasses and never reach here — their cheap
// OffscreenData path stays untouched.
//
// ORIENTATION CONTRACT: surfaceTex and every bufferTex are bottom-origin GL FBOs
// — the SAME layout as KWin's redirected uTexture0. The fullscreen quad maps
// texcoord [0,1] to the FBO directly (v=0 at the bottom), and the buffer
// fragments sample uTexture0 / iChannelN through surface_uniforms.glsl's
// surfaceTexel() helper, which on the KWin branch samples uTexture0 with no
// Y-flip. So a passthrough buffer (fragColor = surfaceTexel(vTexCoord))
// reproduces the captured surface upright, and the main effect.frag samples the
// resulting iChannelN with the same convention it uses for uTexture0 — every
// stage stays in one consistent bottom-origin space.
//
// DURING A TRANSITION this is NOT called: a multipass pack degrades to
// single-pass while a window animates (renderSurfaceChain runs the border via
// the main shader with the iChannels unbound → sampled as 0). Feeding the buffer
// passes into the transition chain is a follow-up.
bool PlasmaZonesEffect::renderSurfaceBufferPasses(KWin::EffectWindow* w, qreal scale)
{
    if (!w) {
        return false;
    }
    const QString windowId = getWindowId(w);
    // Resolve the window's base pack — its compiled buffer passes drive this idle
    // multipass render. nullptr (compile failed/latched) or a single-pass pack
    // (empty bufferPasses) short-circuits: the caller renders single-pass.
    CompiledSurfacePack* const pack = compiledPackForWindow(windowId);
    if (!pack || pack->bufferPasses.empty()) {
        return false;
    }
    // Same rationale as renderSurfaceChainComposite's guard: never leak the
    // offscreen passes' GL state into the on-screen draw that follows.
    const ShaderInternal::ScopedGlState glStateGuard;
    namespace SC = PhosphorSurfaceShaders::SurfaceShaderContract;

    // Size the targets to the window's expanded geometry × screen scale — the
    // redirected FBO covers frame + decoration + shadow, identically to
    // renderSurfaceChain / captureOldWindowSnapshot. The defensive cap keeps a
    // pathological window within GL texture limits (sampled by normalised uv, so
    // a reduced capture scale only costs resolution, never distortion).
    const QRectF logicalGeometry = w->expandedGeometry();
    qreal captureScale = scale;
    constexpr qreal kMaxSurfaceDim = 8192.0;
    const qreal longestPx = qMax(logicalGeometry.width(), logicalGeometry.height()) * captureScale;
    if (longestPx > kMaxSurfaceDim) {
        captureScale *= kMaxSurfaceDim / longestPx;
    }
    const QSize textureSize = (logicalGeometry.size() * captureScale).toSize();
    if (textureSize.isEmpty()) {
        return false;
    }

    // Get / (re)allocate the per-window targets. Reallocate the whole chain when
    // the full size changes — the buffer size is a fixed multiple of it.
    SurfaceMultipassState& state = m_surfaceMultipass[windowId];
    const size_t passCount = pack->bufferPasses.size();
    if (state.size != textureSize || !state.surfaceTex || state.bufferTex.size() != passCount) {
        // The pack's bufferScale downscales the buffer FBOs (the surface capture
        // is always full-resolution). Clamp matches SurfaceShaderEffect::fromJson.
        // Resolve the pack metadata by the window's base pack id (the registry
        // effect backing this window's compiled pack), not a single global
        // selection. The border entry exists — compiledPackForWindow above
        // returned non-null — so reuse an iterator instead of copying the whole
        // WindowBorder for one field. Scoped to this realloc branch (mirroring
        // renderSurfaceChainComposite): the effect() by-value copy is pure waste
        // on the steady-state per-frame path.
        const auto borderIt = m_windowBorders.constFind(windowId);
        const PhosphorSurfaceShaders::SurfaceShaderEffect eff = m_surfaceShaderRegistry.effect(borderIt->basePackId);
        const qreal bufferScale = qBound(PhosphorSurfaceShaders::SurfaceShaderEffect::kMinBufferScale, eff.bufferScale,
                                         PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferScale);
        const QSize bufferSize(qMax(1, qRound(textureSize.width() * bufferScale)),
                               qMax(1, qRound(textureSize.height() * bufferScale)));
        state.surfaceTex = KWin::GLTexture::allocate(GL_RGBA8, textureSize);
        if (!state.surfaceTex) {
            m_surfaceMultipass.erase(windowId);
            return false;
        }
        state.surfaceTex->setFilter(GL_LINEAR);
        state.surfaceTex->setWrapMode(GL_CLAMP_TO_EDGE);
        state.bufferTex.clear();
        state.bufferTex.reserve(passCount);
        for (size_t i = 0; i < passCount; ++i) {
            std::unique_ptr<KWin::GLTexture> bt = KWin::GLTexture::allocate(GL_RGBA8, bufferSize);
            if (!bt) {
                m_surfaceMultipass.erase(windowId);
                return false;
            }
            bt->setFilter(GL_LINEAR);
            bt->setWrapMode(GL_CLAMP_TO_EDGE);
            state.bufferTex.push_back(std::move(bt));
        }
        state.size = textureSize;
    }

    // ── Step 1: capture the RAW window surface into surfaceTex ───────────────
    // Exactly like captureOldWindowSnapshot: bypass our border shader so the
    // capture is the raw composited window (bottom-origin, same layout as
    // uTexture0), then restore the previously-bound shader. setShader(w,nullptr)
    // here, then restore — drawWindow re-binds the border shader for the main
    // blit after this returns.
    {
        KWin::GLFramebuffer fbo(state.surfaceTex.get());
        if (!fbo.valid()) {
            return false;
        }
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
            captureData.setOpacity(1.0);
            const int captureMask = PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_TRANSLUCENT;
            // Route through effects->drawWindow (not OffscreenEffect::drawWindow)
            // so KWin's draw-chain iterator is advanced past us before
            // OffscreenData re-enters — same rationale as renderSurfaceChain and
            // captureOldWindowSnapshot. m_capturingSnapshot short-circuits the
            // passive border bind in our drawWindow override during this capture.
            KWin::effects->drawWindow(renderTarget, viewport, w, captureMask, KWin::Region::infinite(), captureData);
            KWin::GLFramebuffer::popFramebuffer();
        }
        resetCapture.dismiss();
        m_capturingSnapshot = false;
        // The border shader is re-applied by drawWindow's own ShaderBinder after
        // this returns; restore the redirect's bound shader to it so the main
        // blit (OffscreenData::paint) runs the pack's program.
        setShader(w, pack->shader.get());
    }

    // ── Step 2: run each buffer pass into its FBO ────────────────────────────
    // Pass i samples surfaceTex (GL_TEXTURE0) + every earlier bufferTex
    // (GL_TEXTURE1+j as iChannelN) and writes bufferTex[i]. We draw a fullscreen
    // quad with NO MVP (the quad is already NDC), restoring glActiveTexture to
    // GL_TEXTURE0 after each pass for hygiene.
    //
    // Param values: prefer THIS window's resolved set for the base pack
    // (WindowBorder::packParamValues); fall back to the compiled pack's baked
    // baseline when absent. Mirrors pushBorderUniforms' seeding on the main pass.
    const SurfaceParamValues* windowVals = nullptr;
    if (const auto wbIt = m_windowBorders.constFind(windowId); wbIt != m_windowBorders.constEnd()) {
        if (const auto pvIt = wbIt->packParamValues.constFind(wbIt->basePackId);
            pvIt != wbIt->packParamValues.constEnd()) {
            windowVals = &*pvIt;
        }
    }
    for (size_t i = 0; i < passCount; ++i) {
        const CompiledSurfaceBufferPass& pass = pack->bufferPasses[i];
        KWin::GLTexture* const target = state.bufferTex[i].get();
        KWin::GLFramebuffer fbo(target);
        if (!fbo.valid()) {
            return false;
        }
        KWin::GLFramebuffer::pushFramebuffer(&fbo);
        glViewport(0, 0, target->width(), target->height());
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        {
            KWin::ShaderBinder binder(pass.shader.get());

            // uTexture0 — the captured surface on unit 0.
            glActiveTexture(GL_TEXTURE0);
            state.surfaceTex->bind();
            if (pass.uTexture0Loc >= 0) {
                pass.shader->setUniform(pass.uTexture0Loc, 0);
            }
            if (pass.uTimeLoc >= 0) {
                pass.shader->setUniform(pass.uTimeLoc, surfaceShaderTimeSeconds());
            }

            // iChannel0..(i-1) — prior buffer outputs on units 1+j.
            for (size_t j = 0; j < i && j < 4; ++j) {
                glActiveTexture(GL_TEXTURE1 + static_cast<int>(j));
                state.bufferTex[j]->bind();
                if (pass.iChannelLoc[j] >= 0) {
                    pass.shader->setUniform(pass.iChannelLoc[j], 1 + static_cast<int>(j));
                }
                if (pass.iChannelResolutionLoc[j] >= 0) {
                    const QVector4D res(static_cast<float>(state.bufferTex[j]->width()),
                                        static_cast<float>(state.bufferTex[j]->height()), 0.0f, 0.0f);
                    pass.shader->setUniform(pass.iChannelResolutionLoc[j], res);
                }
            }

            // Pack-declared parameter values (same per-window seeding as the
            // main pass — see windowVals above).
            for (int slot = 0; slot < SC::kMaxCustomParams; ++slot) {
                if (pass.customParamsLoc[slot] >= 0) {
                    pass.shader->setUniform(pass.customParamsLoc[slot],
                                            windowVals ? windowVals->params[static_cast<size_t>(slot)]
                                                       : pack->customParamsValues[slot]);
                }
            }
            for (int slot = 0; slot < SC::kMaxCustomColors; ++slot) {
                if (pass.customColorsLoc[slot] >= 0) {
                    pass.shader->setUniform(pass.customColorsLoc[slot],
                                            windowVals ? windowVals->colors[static_cast<size_t>(slot)]
                                                       : pack->customColorsValues[slot]);
                }
            }

            drawFullscreenQuad();
        }
        // Texture hygiene: unbind every channel unit we touched and restore
        // GL_TEXTURE0 as the active unit (mirrors paint_pipeline.cpp).
        for (size_t j = 0; j < i && j < 4; ++j) {
            glActiveTexture(GL_TEXTURE1 + static_cast<int>(j));
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glActiveTexture(GL_TEXTURE0);
        KWin::GLFramebuffer::popFramebuffer();
    }

    return true;
}

// Lazily compile the passthrough present shader. It samples a bound texture
// (uFinal) at vTexCoord and writes it verbatim, ignoring uTexture0 (the
// redirected surface KWin binds to unit 0). Used as a multi-pack window's
// redirect shader so OffscreenData::paint presents the pre-composited final FBO
// at window geometry — the vertex applies modelViewProjectionMatrix (set by
// OffscreenData) exactly like the pack default vertex, so the geometry matches.
KWin::GLShader* PlasmaZonesEffect::surfacePresentShader()
{
    if (m_surfacePresentShader) {
        return m_surfacePresentShader.get();
    }
    if (m_surfacePresentFailed) {
        return nullptr;
    }
    // Same off-paint-caller guard as compiledPack(): reconcileBorderShader
    // reaches here from D-Bus/settings/lifecycle contexts where the GL context
    // is not guaranteed current. A no-context call must return without
    // latching so the next use (at latest the paint cycle) retries.
    if (!KWin::effects || !KWin::effects->makeOpenGLContextCurrent()) {
        qCWarning(lcEffect) << "Surface present shader compile deferred: no current GL context";
        return nullptr;
    }
    m_surfacePresentFailed = true; // pessimistic until the compile succeeds

    static const QByteArray kPresentVertex = QByteArrayLiteral(
        "#version 450\n\n"
        "layout(location = 0) in vec2 position;\n"
        "layout(location = 1) in vec2 texCoord;\n\n"
        "layout(location = 0) out vec2 vTexCoord;\n\n"
        "uniform mat4 modelViewProjectionMatrix;\n\n"
        "void main() {\n"
        "    vTexCoord = texCoord;\n"
        "    gl_Position = modelViewProjectionMatrix * vec4(position, 0.0, 1.0);\n"
        "}\n");
    static const QByteArray kPresentFragment = QByteArrayLiteral(
        "#version 450\n\n"
        "layout(location = 0) in vec2 vTexCoord;\n\n"
        "layout(location = 0) out vec4 fragColor;\n\n"
        "uniform sampler2D uFinal;\n\n"
        "void main() {\n"
        "    fragColor = texture(uFinal, vTexCoord);\n"
        "}\n");

    // Route BOTH stages through injectKwinDefineAfterVersion, exactly like
    // every pack shader: KWin rewrites our #version 450 down to the GL context
    // core version (140), where the `layout(location = N)` qualifiers are
    // illegal without the ARB extensions the inject helper enables. Passing
    // the raw sources here failed the present compile on NVIDIA (error C7548)
    // and latched multi-pack / padded decoration off for the whole session —
    // the same failure mode surface_compile.cpp documents for frag-only packs.
    auto shader = KWin::ShaderManager::instance()->generateCustomShader(
        KWin::ShaderTrait::MapTexture, ShaderInternal::injectKwinDefineAfterVersion(QString::fromUtf8(kPresentVertex)),
        ShaderInternal::injectKwinDefineAfterVersion(QString::fromUtf8(kPresentFragment)));
    // KWin 6.7 removed GLShader::isValid(); generateCustomShader returns nullptr
    // when compilation or linking fails, so a null check is the validity test.
    if (!shader) {
        qCWarning(lcEffect) << "Failed to compile surface present shader — multi-pack decoration disabled this session";
        return nullptr;
    }
    m_surfacePresentFinalLoc = shader->uniformLocation("uFinal");
    m_surfacePresentShader = std::move(shader);
    m_surfacePresentFailed = false;
    return m_surfacePresentShader.get();
}

// Composite a multi-pack decoration chain (chain.size() > 1) into a per-window
// ping-pong FBO and return the slot holding the final fold (drawWindow presents
// it through surfacePresentShader). See the header for the full contract. Like
// renderSurfaceBufferPasses this MUST run from paintWindow — it captures the raw
// surface via effects->drawWindow, which re-enters KWin's draw-window iterator.
KWin::GLTexture* PlasmaZonesEffect::renderSurfaceChainComposite(KWin::EffectWindow* w, qreal scale,
                                                                KWin::GLShader* captureRestoreShader, bool force)
{
    if (!w) {
        return nullptr;
    }
    const QString windowId = getWindowId(w);
    const auto bit = m_windowBorders.constFind(windowId);
    if (bit == m_windowBorders.constEnd()) {
        return nullptr;
    }
    const QStringList chain = bit->chain;
    if (!force && chain.size() < 2 && bit->outerPadding <= 0) {
        return nullptr; // at rest, single unpadded packs take the cheap OffscreenData path
    }
    if (chain.isEmpty()) {
        return nullptr;
    }
    // Restore blend/viewport/clear/active-texture on every exit path — the
    // fold runs right before KWin's on-screen draw of this same frame, which
    // must not inherit our offscreen state (see ScopedGlState).
    const ShaderInternal::ScopedGlState glStateGuard;
    namespace SC = PhosphorSurfaceShaders::SurfaceShaderContract;

    // Size the targets to the window's expanded geometry × screen scale, with the
    // same defensive cap as renderSurfaceBufferPasses / renderSurfaceChain.
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

    // The resolved profile feeds each pack's compiled parameter overrides.
    const PhosphorSurfaceShaders::DecorationProfile profile = m_decorationTree.resolve(resolveSurfacePathFor(windowId));

    SurfaceMultipassState& state = m_surfaceMultipass[windowId];
    state.canvasGeo = logicalGeometry;

    // (Re)allocate the composite ping-pong pair on a size change.
    if (state.compositeSize != textureSize || !state.compositeTex[0] || !state.compositeTex[1]) {
        for (auto& t : state.compositeTex) {
            t = KWin::GLTexture::allocate(GL_RGBA8, textureSize);
            if (!t) {
                m_surfaceMultipass.erase(windowId);
                return nullptr;
            }
            t->setFilter(GL_LINEAR);
            t->setWrapMode(GL_CLAMP_TO_EDGE);
        }
        state.compositeSize = textureSize;
        state.chainKey.clear(); // force the per-pack buffers to reallocate at the new size
    }

    // (Re)allocate the cached per-pack buffer textures when the chain or size
    // changes. chainBufferTex[k] holds one texture per pack k's buffer passes,
    // downscaled by that pack's bufferScale; a pack that fails to compile (or has
    // no buffers) leaves an empty inner vector and renders single-pass in the fold.
    if (state.chainKey != chain) {
        state.chainBufferTex.clear();
        state.chainBufferTex.resize(chain.size());
        for (int k = 0; k < chain.size(); ++k) {
            CompiledSurfacePack* const pk = compiledPack(chain.at(k), profile);
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
            bufs.reserve(pk->bufferPasses.size());
            for (size_t i = 0; i < pk->bufferPasses.size(); ++i) {
                std::unique_ptr<KWin::GLTexture> bt = KWin::GLTexture::allocate(GL_RGBA8, bufferSize);
                if (!bt) {
                    bufs.clear(); // pack k degrades to no buffers (iChannels sampled as 0)
                    break;
                }
                bt->setFilter(GL_LINEAR);
                bt->setWrapMode(GL_CLAMP_TO_EDGE);
                bufs.push_back(std::move(bt));
            }
        }
        state.chainKey = chain;
    }

    // ── Step 1: capture the raw window surface into compositeTex[0] ───────────
    // Same capture as renderSurfaceBufferPasses (bypass the redirect shader so the
    // capture is the raw composited window), then restore the present passthrough.
    {
        KWin::GLFramebuffer fbo(state.compositeTex[0].get());
        if (!fbo.valid()) {
            return nullptr;
        }
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
            captureData.setOpacity(1.0);
            const int captureMask = PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_TRANSLUCENT;
            KWin::effects->drawWindow(renderTarget, viewport, w, captureMask, KWin::Region::infinite(), captureData);
            KWin::GLFramebuffer::popFramebuffer();
        }
        resetCapture.dismiss();
        m_capturingSnapshot = false;
        // Hand the OffscreenEffect slot back: to the passthrough present on
        // the rest path, or to the caller's animation shader mid-transition.
        setShader(w, captureRestoreShader ? captureRestoreShader : surfacePresentShader());
    }

    // ── Step 2: fold each pack over the running composite ────────────────────
    // Invariant: each pk is consumed entirely within its own iteration. Never
    // hoist a pk across iterations — compiledPack() may insert into the
    // m_compiledPacks unordered_map and rehash, invalidating any pointer a
    // prior iteration returned.
    int src = 0;
    for (int k = 0; k < chain.size(); ++k) {
        CompiledSurfacePack* const pk = compiledPack(chain.at(k), profile);
        if (!pk || !pk->shader) {
            continue; // skip a failed pack; the composite carries through unchanged
        }
        // Defensive: chainBufferTex is sized to chain.size() in the realloc block
        // above and stays in lockstep with `chain`, but guard the unchecked
        // operator[] in case a future edit decouples the two (out-of-bounds [] is
        // UB; the bounds-correct outcome is to skip the pack's buffer passes).
        if (k >= static_cast<int>(state.chainBufferTex.size())) {
            continue;
        }
        const std::vector<std::unique_ptr<KWin::GLTexture>>& bufs = state.chainBufferTex[k];

        // 2a: pack k's buffer passes, sampling the running composite as uTexture0.
        const size_t passCount = qMin(bufs.size(), pk->bufferPasses.size());
        for (size_t i = 0; i < passCount; ++i) {
            const CompiledSurfaceBufferPass& pass = pk->bufferPasses[i];
            KWin::GLTexture* const target = bufs[i].get();
            KWin::GLFramebuffer fbo(target);
            if (!fbo.valid()) {
                continue;
            }
            KWin::GLFramebuffer::pushFramebuffer(&fbo);
            glViewport(0, 0, target->width(), target->height());
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            {
                KWin::ShaderBinder binder(pass.shader.get());
                glActiveTexture(GL_TEXTURE0);
                state.compositeTex[src]->bind();
                if (pass.uTexture0Loc >= 0) {
                    pass.shader->setUniform(pass.uTexture0Loc, 0);
                }
                if (pass.uTimeLoc >= 0) {
                    pass.shader->setUniform(pass.uTimeLoc, surfaceShaderTimeSeconds());
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
                drawFullscreenQuad();
            }
            for (size_t j = 0; j < i && j < 4; ++j) {
                glActiveTexture(GL_TEXTURE1 + static_cast<int>(j));
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            glActiveTexture(GL_TEXTURE0);
            KWin::GLFramebuffer::popFramebuffer();
        }

        // 2b: pack k's MAIN as a fullscreen FBO pass → the other composite slot.
        const int dst = 1 - src;
        KWin::GLTexture* const target = state.compositeTex[dst].get();
        KWin::GLFramebuffer fbo(target);
        if (!fbo.valid()) {
            continue;
        }
        KWin::GLFramebuffer::pushFramebuffer(&fbo);
        glViewport(0, 0, target->width(), target->height());
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        {
            KWin::ShaderBinder binder(pk->shader.get());
            // Identity MVP so the NDC fullscreen quad from drawFullscreenQuad maps
            // 1:1 through the pack's default (MVP) vertex stage.
            pk->shader->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix, QMatrix4x4());
            glActiveTexture(GL_TEXTURE0);
            state.compositeTex[src]->bind();
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
                if (pk->iChannelLoc[i] >= 0) {
                    pk->shader->setUniform(pk->iChannelLoc[i], 1 + i);
                }
                if (pk->iChannelResolutionLoc[i] >= 0) {
                    const QVector4D res(static_cast<float>(bufs[i]->width()), static_cast<float>(bufs[i]->height()),
                                        0.0f, 0.0f);
                    pk->shader->setUniform(pk->iChannelResolutionLoc[i], res);
                }
            }
            // Contract uniforms + pack params (shared with the OffscreenData path).
            pushBorderUniforms(w, *bit, chain.at(k), *pk, captureScale, pad);
            drawFullscreenQuad();
        }
        for (int i = 0; i < qMin(static_cast<int>(passCount), 4); ++i) {
            glActiveTexture(GL_TEXTURE1 + i);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glActiveTexture(GL_TEXTURE0);
        KWin::GLFramebuffer::popFramebuffer();
        src = dst;
    }

    state.finalSlot = src;
    return state.compositeTex[src].get();
}

// Continuous seconds for the surface `iTime` uniform, relative to an epoch
// captured on first use so the value starts near 0 (a steady_clock value since
// boot is large enough to lose visible sub-frame precision as a float).
float PlasmaZonesEffect::surfaceShaderTimeSeconds()
{
    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
    if (m_surfaceTimeEpochMs < 0) {
        m_surfaceTimeEpochMs = nowMs;
    }
    return static_cast<float>(static_cast<double>(nowMs - m_surfaceTimeEpochMs) / 1000.0);
}

// True when any pack in the window's resolved chain references iTime (main or a
// buffer pass) — i.e. the decoration animates and must be driven to repaint.
// Uses the per-pack compiled cache (a hit after the first paint compiled it), so
// this is a few hash lookups per call. A window whose packs are not yet compiled
// (or all static) returns false; the first content paint compiles them and the
// next postPaintScreen picks the animation up.
bool PlasmaZonesEffect::windowSurfaceAnimates(const QString& windowId)
{
    const auto it = m_windowBorders.constFind(windowId);
    if (it == m_windowBorders.constEnd()) {
        return false;
    }
    // Resolve the decoration profile LAZILY: compiledPack only reads it on a
    // compile-cache miss, and this runs in the per-frame idle-repaint loop for
    // every shader-applied window — in the common case (all packs compiled,
    // e.g. a static border-only window) the tree walk would be pure per-frame
    // waste. Resolved at most once even when several packs miss.
    std::optional<PhosphorSurfaceShaders::DecorationProfile> profile;
    for (const QString& packId : it->chain) {
        CompiledSurfacePack* pack = nullptr;
        if (const auto cacheIt = m_compiledPacks.find(packId); cacheIt != m_compiledPacks.end()) {
            pack = &cacheIt->second;
        } else {
            if (!profile) {
                profile = m_decorationTree.resolve(resolveSurfacePathFor(windowId));
            }
            pack = compiledPack(packId, *profile);
        }
        if (!pack || !pack->shader) {
            continue;
        }
        if (pack->uTimeLoc >= 0) {
            return true;
        }
        for (const CompiledSurfaceBufferPass& bp : pack->bufferPasses) {
            if (bp.uTimeLoc >= 0) {
                return true;
            }
        }
    }
    return false;
}

} // namespace PlasmaZones
