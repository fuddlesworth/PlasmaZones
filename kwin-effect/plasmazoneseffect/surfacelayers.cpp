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
    // Same off-paint-caller guard as compiledPack(): reconcileDecorationShader
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
        // Final KWin-style opacity modulation (premultiplied multiply): the
        // WHOLE decorated output ghosts uniformly, exactly like KWin's own
        // Modulate trait would. Pushed per frame in drawWindow's present
        // branch; forced to 1.0 when a chain pack declares handlesOpacity
        // (frost applies the window's opacity to its content sample itself
        // so its slab stays solid).
        "uniform float uOpacity;\n\n"
        // Colour management. Installing our own shader via OffscreenEffect::
        // setShader REPLACES KWin's present shader, which carries
        // ShaderTrait::TransformColorspace and converts the redirected window's
        // sRGB content into the output's colour space (offscreeneffect.cpp does
        // exactly this). Dropping that conversion wrote sRGB values verbatim into
        // the compositor's blending space, so on an HDR output every decorated
        // window rendered dim and desaturated — persistently, not just during an
        // animation.
        //
        // KWin's GLShader::preprocess resolves `#include "…"` against its own
        // `:/opengl/` resource for EVERY source it compiles, custom ones included,
        // so we can pull in its colour-management GLSL rather than reimplement it.
        // The uniforms it declares are what GLShader::setColorspaceUniforms fills.
        //
        // Stage order mirrors KWin's base.frag exactly: encoding → nits, apply the
        // colorimetry transform, THEN modulate, then tonemap, then encode for the
        // destination. Opacity has to land in nits space because that is where
        // base.frag's TRAIT_MODULATE sits, between the colorimetry transform and
        // tonemapping.
        //
        // Deliberately NOT the sourceEncodingToNitsInDestinationColorspace()
        // convenience: it folds doTonemapping() in at the end, which would tonemap
        // BEFORE the modulate and then again at our explicit call — a double
        // compression. Harmless on SDR (that path only clamps, and clamping twice
        // is idempotent) but wrong on HDR, i.e. precisely the case this exists for.
        // base.frag spells the steps out for the same reason.
        //
        // On an SDR output source and destination are both sRGB: the colorimetry
        // transform is identity, the transfer functions round-trip, and
        // doTonemapping degrades to a clamp that cannot bite (for an sRGB source
        // KWin sets maxTonemappingLuminance == the destination reference, so the
        // Reinhard branch is unreachable). So this is a no-op there and can only
        // change HDR.
        //
        // That holds at ANY uOpacity, which is not obvious: modulating in nits
        // space looks like it should brighten a fade versus the old encoded-domain
        // multiply. It does not, because `result *= uOpacity` scales ALPHA too and
        // both encodingToNits and nitsToEncoding un-premultiply before their
        // transfer function and re-premultiply after (by the NEW alpha). The
        // opacity therefore rides entirely on the alpha channel and the gamma
        // cancels: both forms emit E(c)·a·o with alpha a·o. Do not "simplify" the
        // multiply back out of nits space — that is where KWin's TRAIT_MODULATE
        // sits, and moving it would break the HDR path for no SDR gain.
        "#include \"colormanagement.glsl\"\n\n"
        "void main() {\n"
        "    vec4 result = texture(uFinal, vTexCoord);\n"
        "    result = encodingToNits(result, sourceNamedTransferFunction,\n"
        "                            sourceTransferFunctionParams.x, sourceTransferFunctionParams.y);\n"
        "    result.rgb = (colorimetryTransform * vec4(result.rgb, 1.0)).rgb;\n"
        "    result *= uOpacity;\n"
        "    result.rgb = doTonemapping(result.rgb);\n"
        "    fragColor = nitsToDestinationEncoding(result);\n"
        "}\n");

    // Route BOTH stages through injectKwinDefineAfterVersion, exactly like
    // every pack shader: KWin rewrites our #version 450 down to the GL context
    // core version (140), where the `layout(location = N)` qualifiers are
    // illegal without the ARB extensions the inject helper enables. Passing
    // the raw sources here failed the present compile on NVIDIA (error C7548)
    // and latched multi-pack / padded decoration off for the whole session —
    // the same failure mode surface_compile.cpp documents for frag-only packs.
    // TransformColorspace is DECLARATIVE ONLY here. With a custom fragment source
    // `generateCustomShader` uses the traits for nothing but `listDefines`, and
    // colormanagement.glsl does not read TRAIT_TRANSFORM_COLORSPACE at all (only
    // KWin's own base.frag does, to decide whether to include the file). The
    // conversion is explicit in kPresentFragment above and would run with or
    // without this flag. It is declared so the shader's traits describe what it
    // actually does — do not delete the GLSL believing the trait drives it.
    auto shader = KWin::ShaderManager::instance()->generateCustomShader(
        KWin::ShaderTrait::MapTexture | KWin::ShaderTrait::TransformColorspace,
        ShaderInternal::injectKwinDefineAfterVersion(QString::fromUtf8(kPresentVertex)),
        ShaderInternal::injectKwinDefineAfterVersion(QString::fromUtf8(kPresentFragment)));
    // KWin 6.7 removed GLShader::isValid(); generateCustomShader returns nullptr
    // when compilation or linking fails, so a null check is the validity test.
    if (!shader) {
        qCWarning(lcEffect) << "Failed to compile surface present shader — multi-pack decoration disabled this session";
        return nullptr;
    }
    m_surfacePresentFinalLoc = shader->uniformLocation("uFinal");
    m_surfacePresentOpacityLoc = shader->uniformLocation("uOpacity");
    m_surfacePresentShader = std::move(shader);
    m_surfacePresentFailed = false;
    return m_surfacePresentShader.get();
}

// captureWindowBackdrop lives in surface_backdrop.cpp (split out to keep this
// TU under the 800-line limit, mirroring the surface_audio.cpp split).

KWin::GLTexture* PlasmaZonesEffect::renderSurfaceChainComposite(KWin::EffectWindow* w, qreal scale,
                                                                KWin::GLShader* captureRestoreShader)
{
    if (!w) {
        return nullptr;
    }
    const QString windowId = getWindowId(w);
    const auto bit = m_windowDecorations.constFind(windowId);
    if (bit == m_windowDecorations.constEnd()) {
        return nullptr;
    }
    const QStringList chain = bit->chain;
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

    // (Re)allocate the composite ping-pong pair on a size change.
    if (state.compositeSize != textureSize || !state.compositeTex[0] || !state.compositeTex[1]) {
        bool allocFailed = false;
        for (auto& t : state.compositeTex) {
            t = KWin::GLTexture::allocate(GL_RGBA8, textureSize);
            if (!t) {
                allocFailed = true;
                break;
            }
            t->setFilter(GL_LINEAR);
            t->setWrapMode(GL_CLAMP_TO_EDGE);
        }
        if (allocFailed) {
            // Drop the half-allocated state. Erase AFTER the loop has ended so
            // we never destroy the container mid-iteration (state is a reference
            // into the map being erased).
            m_surfaceMultipass.erase(windowId);
            return nullptr;
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
            bufs.reserve(pk->bufferPasses.size());
            for (size_t i = 0; i < pk->bufferPasses.size(); ++i) {
                std::unique_ptr<KWin::GLTexture> bt = KWin::GLTexture::allocate(GL_RGBA8, bufferSize);
                if (!bt) {
                    // Pack k degrades to no buffers. The fold's main pass then
                    // binds the transparent fallback to every iChannel the pack
                    // still declares, so they genuinely sample 0 — an unset
                    // sampler2D would otherwise read unit 0, i.e. the running
                    // composite.
                    bufs.clear();
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
    // Raw window capture (bypass the redirect shader so the
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
        // Hand the OffscreenEffect slot back: to the passthrough present on
        // the rest path, or to the caller's animation shader mid-transition.
        setShader(w, captureRestoreShader ? captureRestoreShader : surfacePresentShader());
    }

    // ── Step 2: fold each pack over the running composite ────────────────────
    // Invariant: each pk is consumed entirely within its own iteration and
    // re-fetched from compiledPack() every pass. std::unordered_map keeps
    // element pointers/references stable across insert/rehash (only iterators
    // are invalidated), so the per-iteration re-fetch is a cheap defensive
    // habit rather than a strict requirement here — but it would become
    // load-bearing if m_compiledPacks were ever swapped for a node-relocating
    // container, so keep it.
    int src = 0;
    for (int k = 0; k < chain.size(); ++k) {
        CompiledSurfacePack* const pk = compiledPackLazy(chain.at(k));
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
                state.compositeTex[src]->bind();
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
            // forward-references a channel a later pass would fill.
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
        src = dst;
    }

    state.finalSlot = src;
    state.lastFoldMs = ShaderInternal::shaderClockNowMs();
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
    const auto it = m_windowDecorations.constFind(windowId);
    if (it == m_windowDecorations.constEnd()) {
        return false;
    }
    // A focus ramp in flight (value strictly between 0 and 1) needs continuous
    // repaints so uSurfaceFocused reaches its target; the ramp clamps to 0/1
    // at the ends, so this self-terminates.
    if (const auto fit = m_focusFade.constFind(windowId);
        fit != m_focusFade.constEnd() && fit->value > 0.001f && fit->value < 0.999f) {
        return true;
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
        // An audio-reactive pack must repaint every frame while a live spectrum
        // flows so getBass* tracks the beat. audioReactiveDriving() self-
        // terminates the loop when the toggle is off, capture stops, OR the
        // spectrum has gone quiet (repeated frames) — so a static border with
        // audio off, and a paused track, both cost nothing.
        if (pack->iAudioSpectrumSizeLoc >= 0 && audioReactiveDriving()) {
            return true;
        }
        for (const CompiledSurfaceBufferPass& bp : pack->bufferPasses) {
            if (bp.uTimeLoc >= 0 || (bp.iAudioSpectrumSizeLoc >= 0 && audioReactiveDriving())) {
                return true;
            }
        }
    }
    return false;
}

} // namespace PlasmaZones
