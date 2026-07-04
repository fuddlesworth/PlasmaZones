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
        // Final KWin-style opacity modulation (premultiplied multiply): the
        // WHOLE decorated output ghosts uniformly, exactly like KWin's own
        // Modulate trait would. Pushed per frame in drawWindow's present
        // branch; forced to 1.0 when a chain pack declares handlesOpacity
        // (frost applies the window's opacity to its content sample itself
        // so its slab stays solid).
        "uniform float uOpacity;\n\n"
        "void main() {\n"
        "    fragColor = texture(uFinal, vTexCoord) * uOpacity;\n"
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
    m_surfacePresentOpacityLoc = shader->uniformLocation("uOpacity");
    m_surfacePresentShader = std::move(shader);
    m_surfacePresentFailed = false;
    return m_surfacePresentShader.get();
}

// Blit the scene behind @p w into its backdrop texture (see the header).
// MUST run from paintWindow, while the live render target still holds only
// the content painted below this window.
void PlasmaZonesEffect::captureWindowBackdrop(const KWin::RenderTarget& renderTarget,
                                              const KWin::RenderViewport& viewport, KWin::EffectWindow* w,
                                              const WindowBorder& wb, const QRectF& animatedFrame)
{
    // Mirror renderSurfaceChainComposite's canvas math EXACTLY (padded
    // logical rect, capped capture scale, derived texture size) so uBackdrop
    // and the composite canvas are texel-aligned — a pack samples both with
    // the same uv (backdropTexel / surfaceTexel).
    const qreal pad = wb.outerPadding;
    QRectF logicalGeometry = w->expandedGeometry();
    if (logicalGeometry.isEmpty()) {
        logicalGeometry = w->frameGeometry();
    }
    logicalGeometry.adjust(-pad, -pad, pad, pad);
    qreal captureScale = viewport.scale();
    constexpr qreal kMaxSurfaceDim = 8192.0;
    const qreal longestPx = qMax(logicalGeometry.width(), logicalGeometry.height()) * captureScale;
    if (longestPx > kMaxSurfaceDim) {
        captureScale *= kMaxSurfaceDim / longestPx;
    }
    const QSize textureSize = (logicalGeometry.size() * captureScale).toSize();
    if (textureSize.isEmpty()) {
        return;
    }
    SurfaceMultipassState& state = m_surfaceMultipass[getWindowId(w)];
    // ── Multi-output accumulation ───────────────────────────────────────
    // paintWindow runs once per OUTPUT, and each output can only blit the
    // slice of the canvas its viewport covers. OUTPUTS HAVE INDEPENDENT
    // FRAME CLOCKS, so "same frame" is undecidable from the pinned clock —
    // keying a clear on it made the neighbour output (whose cycle always
    // looks like a new frame) wholesale-clear the texture and leave only
    // its overhang sliver, which the pixel probes showed as a transparent
    // backdrop exactly while animations kept both outputs cycling. So:
    // NEVER clear on capture (stale pixels outside the valid rect are
    // never sampled — backdropTexel clamps into it; the texture is cleared
    // once at allocation), and treat captures within a short window as one
    // accumulation generation: their dest slices overwrite in place and
    // the valid rect grows to the union. A stale generation restarts the
    // rect at this capture's slice.
    const qint64 pinned = m_shaderManager.currentFrameClockMs();
    const qint64 frameStamp = pinned >= 0 ? pinned : ShaderInternal::shaderClockNowMs();
    constexpr qint64 kAccumulationWindowMs = 50;
    if (state.backdropSize != textureSize || !state.backdropTex) {
        state.backdropTex = KWin::GLTexture::allocate(GL_RGBA8, textureSize);
        if (!state.backdropTex) {
            state.backdropSize = QSize();
            return;
        }
        state.backdropTex->setFilter(GL_LINEAR);
        state.backdropTex->setWrapMode(GL_CLAMP_TO_EDGE);
        state.backdropSize = textureSize;
        state.backdropRect = QVector4D();
        state.backdropFrameMs = -1;
        // The only clear the texture ever gets: uncovered regions read
        // transparent until a capture lands there, and are never sampled
        // (backdropTexel clamps into the valid rect).
        KWin::GLFramebuffer clearFbo(state.backdropTex.get());
        if (clearFbo.valid()) {
            KWin::GLFramebuffer::pushFramebuffer(&clearFbo);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            KWin::GLFramebuffer::popFramebuffer();
        }
    }
    // Where to READ the scene from. At rest that is the canvas rect itself;
    // while an animation draws the window elsewhere (animatedFrame valid),
    // map the canvas rect through the frame -> animatedFrame transform so
    // the pane shows the scene behind the MOVING quad. The canvas/texture
    // stays rest-rect sized either way (it must match the composite); a
    // moving source of a different size is scaled by the blit.
    QRectF sourceRect = logicalGeometry;
    if (animatedFrame.isValid()) {
        QRectF frame = w->frameGeometry();
        if (frame.isEmpty()) {
            frame = animatedFrame;
        }
        const qreal sx = animatedFrame.width() / qMax(frame.width(), 1.0);
        const qreal sy = animatedFrame.height() / qMax(frame.height(), 1.0);
        sourceRect = QRectF(animatedFrame.x() + (logicalGeometry.x() - frame.x()) * sx,
                            animatedFrame.y() + (logicalGeometry.y() - frame.y()) * sy, logicalGeometry.width() * sx,
                            logicalGeometry.height() * sy);
        if (sourceRect.isEmpty()) {
            sourceRect = logicalGeometry;
        }
    }
    // The source can hang off the output (a padded canvas at a screen edge,
    // a window half-dragged off, an animation passing the edge): blit only
    // the part the render target covers and record the valid sub-rect so
    // packs clamp samples into it rather than reading the cleared margin.
    const QRectF sourceLogicalF = sourceRect & viewport.renderRect();
    if (sourceLogicalF.isEmpty()) {
        return; // keep whatever another output captured
    }
    const bool sameGeneration =
        state.backdropFrameMs >= 0 && qAbs(frameStamp - state.backdropFrameMs) < kAccumulationWindowMs;
    // Restore scissor/blend/viewport state — the clear below and the blit
    // both honour scissor, and this runs mid scene-walk (see ScopedGlState).
    const ShaderInternal::ScopedGlState glStateGuard;
    glDisable(GL_SCISSOR_TEST);
    KWin::GLFramebuffer fbo(state.backdropTex.get());
    if (!fbo.valid()) {
        state.backdropTex.reset();
        state.backdropSize = QSize();
        return;
    }
    // No clear here — see the accumulation note above.
    const KWin::Rect source(qRound(sourceLogicalF.x()), qRound(sourceLogicalF.y()), qRound(sourceLogicalF.width()),
                            qRound(sourceLogicalF.height()));
    // Destination: the source's PROPORTIONAL position within sourceRect,
    // mapped onto the texture — reduces to the plain scaled copy at rest
    // (sourceRect == logicalGeometry) and scales a moving/resizing source
    // into the rest-sized canvas during animations.
    const qreal texW = textureSize.width();
    const qreal texH = textureSize.height();
    const QRectF destF((source.x() - sourceRect.x()) / sourceRect.width() * texW,
                       (source.y() - sourceRect.y()) / sourceRect.height() * texH,
                       source.width() / sourceRect.width() * texW, source.height() / sourceRect.height() * texH);
    const KWin::Rect destination(qRound(destF.x()), qRound(destF.y()), qRound(destF.width()), qRound(destF.height()));
    if (!fbo.blitFromRenderTarget(renderTarget, viewport, source, destination)) {
        state.backdropRect = QVector4D();
        return;
    }
    // Valid sub-rect in TOP-DOWN normalized coords — matches backdropTexel's
    // clamp space. Same-frame captures UNION with the slices other outputs
    // already blitted (adjacent outputs tile the canvas, so the bounding box
    // is exact for the straddling case).
    QVector4D destNorm(static_cast<float>(destF.x() / textureSize.width()),
                       static_cast<float>(destF.y() / textureSize.height()),
                       static_cast<float>(destF.width() / textureSize.width()),
                       static_cast<float>(destF.height() / textureSize.height()));
    if (sameGeneration && state.backdropRect.z() > 0.0f && state.backdropRect.w() > 0.0f) {
        const float x0 = qMin(state.backdropRect.x(), destNorm.x());
        const float y0 = qMin(state.backdropRect.y(), destNorm.y());
        const float x1 = qMax(state.backdropRect.x() + state.backdropRect.z(), destNorm.x() + destNorm.z());
        const float y1 = qMax(state.backdropRect.y() + state.backdropRect.w(), destNorm.y() + destNorm.w());
        destNorm = QVector4D(x0, y0, x1 - x0, y1 - y0);
    }
    state.backdropRect = destNorm;
    state.backdropFrameMs = frameStamp;
}

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
    if (chain.isEmpty()) {
        return nullptr;
    }
    // Restore blend/viewport/clear/active-texture on every exit path — the
    // fold runs right before KWin's on-screen draw of this same frame, which
    // must not inherit our offscreen state (see ScopedGlState).
    const ShaderInternal::ScopedGlState glStateGuard;
    namespace SC = PhosphorSurfaceShaders::SurfaceShaderContract;

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
            // "A capture exists this frame" drives the uHasBackdrop VALUE for
            // every stage; whether THIS stage samples it (the linker kept its
            // uBackdrop) only drives the bind. A pack's main pass typically
            // reads the blurred buffers rather than uBackdrop itself, so its
            // own sampler location being -1 must not zero the gate.
            const bool backdropAvailable = state.backdropTex && state.backdropRect.z() > 0.0f;
            const bool passHasBackdrop = pass.uBackdropLoc >= 0 && backdropAvailable;
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
        // Temporary transition-blur diagnostic: log the first few TRANSITION
        // folds (force = renderSurfaceChain path) so "blur vanishes during
        // animations" can be attributed from journalctl.
        if (force && (pk->uHasBackdropLoc >= 0 || pk->uBackdropLoc >= 0)) {
            static int transDbgCount = 0;
            if (transDbgCount < 6) {
                ++transDbgCount;
                qCWarning(lcEffect) << "PZDBG transition-fold:" << chain.at(k) << "available" << backdropAvailable
                                    << "rect" << state.backdropRect << "bufferPasses" << pk->bufferPasses.size()
                                    << "bufs"
                                    << (k < static_cast<int>(state.chainBufferTex.size())
                                            ? state.chainBufferTex[k].size()
                                            : size_t(999))
                                    << "canvas" << state.canvasGeo << "texSize" << state.compositeSize;
            }
        }
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
            // Contract uniforms + pack params (shared with the OffscreenData path).
            pushBorderUniforms(w, *bit, chain.at(k), *pk, captureScale, pad);
            drawFullscreenQuad();
        }
        for (int i = 0; i < qMin(static_cast<int>(passCount), 4); ++i) {
            glActiveTexture(GL_TEXTURE1 + i);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        if (mainHasBackdrop) {
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, 0);
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
