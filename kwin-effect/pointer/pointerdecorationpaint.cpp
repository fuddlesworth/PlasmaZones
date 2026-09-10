// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// PointerDecorationPass — the DRAW. Runs the engaged chain over the finished
// frame of the one output the pointer is on: per layer, the optional
// multipass buffer stages into their ping-pong targets, then one quad
// covering the damage rect through the pack's main shader, composited
// premultiplied source-over. The compile side lives in
// pointerdecorationshader.cpp; the contract and the reasoning live on the
// header.

#include "pointerdecorationpass.h"

#include "plasmazoneseffect/shader_internal.h"
#include "transitions/transitionpasshelpers.h"
#include "compositor/effectlogging.h"

#include <core/output.h>
#include <core/rect.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <effect/globals.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>
#include <opengl/glvertexbuffer.h>

#include <QImage>
#include <QLoggingCategory>
#include <QMatrix4x4>
#include <QScopeGuard>
#include <QSize>
#include <QVector2D>
#include <QVector4D>

#include <algorithm>
#include <vector>
#include <array>
#include <cmath>

namespace PlasmaZones {

namespace PPS = PhosphorPointerShaders;
namespace PSC = PhosphorPointerShaders::PointerShaderContract;

// ── Geometry primitives ─────────────────────────────────────────────────────

void PointerDecorationPass::drawDamageQuad(const KWin::RenderViewport& viewport, const QRectF& deviceRect,
                                           const QSize& deviceSize)
{
    // Positions live in the viewport's DEVICE coordinate space (y-down, the
    // same space scaledRenderRect reports), projected by the MVP the caller
    // uploaded. That projection is what carries the output transform, so a
    // rotated output needs nothing extra here.
    const KWin::Rect sr = viewport.scaledRenderRect();
    const float x0 = float(sr.left() + deviceRect.left());
    const float y0 = float(sr.top() + deviceRect.top());
    const float x1 = float(sr.left() + deviceRect.right());
    const float y1 = float(sr.top() + deviceRect.bottom());

    // Texcoords are BOTTOM-UP: pointer_lib.glsl's pointerPixel() reconstructs
    // top-down canvas px as `vec2(uv.x, 1.0 - uv.y) * iResolution` under
    // PLASMAZONES_KWIN, so v must be 1 at the TOP of the output. This is the
    // opposite convention to TransitionPass::drawOutputQuad, which pins
    // top-down uv for the transition packs' own Y-flipping samplers — see the
    // header note. Sub-rect texcoords, so the fragment stage runs only over
    // the damage band while every pack still sees the full-canvas uv it
    // expects. Normalised against deviceSize, the very QSize iResolution was
    // pushed from, so uv * iResolution lands on the pixel the quad covers.
    const float w = float(deviceSize.width());
    const float h = float(deviceSize.height());
    const float u0 = w > 0.0f ? float(deviceRect.left()) / w : 0.0f;
    const float u1 = w > 0.0f ? float(deviceRect.right()) / w : 1.0f;
    const float v0 = h > 0.0f ? 1.0f - float(deviceRect.top()) / h : 1.0f;
    const float v1 = h > 0.0f ? 1.0f - float(deviceRect.bottom()) / h : 0.0f;

    const std::array<KWin::GLVertex2D, 4> verts = {{
        {QVector2D(x0, y1), QVector2D(u0, v1)}, // bottom-left
        {QVector2D(x1, y1), QVector2D(u1, v1)}, // bottom-right
        {QVector2D(x0, y0), QVector2D(u0, v0)}, // top-left
        {QVector2D(x1, y0), QVector2D(u1, v0)}, // top-right
    }};
    KWin::GLVertexBuffer* const vbo = KWin::GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setVertices(verts);
    vbo->render(GL_TRIANGLE_STRIP);
}

void PointerDecorationPass::drawFullscreenQuad()
{
    // A buffer stage covers its whole target FBO, so the positions are
    // already clip-space and the caller uploads an IDENTITY MVP. Texcoords
    // run bottom-up with the FBO's own origin, which is the orientation
    // pointerPixel() expects on this runtime — the same convention as the
    // on-screen quad above.
    const std::array<KWin::GLVertex2D, 4> verts = {{
        {QVector2D(-1.0f, -1.0f), QVector2D(0.0f, 0.0f)},
        {QVector2D(1.0f, -1.0f), QVector2D(1.0f, 0.0f)},
        {QVector2D(-1.0f, 1.0f), QVector2D(0.0f, 1.0f)},
        {QVector2D(1.0f, 1.0f), QVector2D(1.0f, 1.0f)},
    }};
    KWin::GLVertexBuffer* const vbo = KWin::GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setVertices(verts);
    vbo->render(GL_TRIANGLE_STRIP);
}

// ── Uniform push ────────────────────────────────────────────────────────────

void PointerDecorationPass::pushFrameUniforms(KWin::GLShader* shader, const PointerUniformLocations& loc,
                                              const CompiledPointerPack& pack, const PPS::PointerFrameState& state,
                                              const QSize& deviceSize, const QRectF& cursorRect, double timeSeconds,
                                              bool hasCursorSprite, float reachDevicePx)
{
    // Every push is gated on a location: a pack that never references a
    // contract uniform links without it, the slot stays -1, and the uniform
    // costs nothing per frame.
    if (loc.iTime >= 0) {
        shader->setUniform(loc.iTime, float(timeSeconds));
    }
    const float w = float(deviceSize.width());
    const float h = float(deviceSize.height());
    if (loc.iResolution >= 0) {
        shader->setUniform(loc.iResolution, QVector2D(w, h));
    }
    if (loc.iMouse >= 0) {
        // The pointer IS the newest trail sample; the contract's .zw lane is
        // that position normalised against the canvas.
        const QVector4D newest = state.trailIsEmpty() ? QVector4D() : state.newestTrail();
        const float px = newest.x();
        const float py = newest.y();
        shader->setUniform(loc.iMouse, QVector4D(px, py, w > 0.0f ? px / w : 0.0f, h > 0.0f ? py / h : 0.0f));
    }
    if (loc.uPointerVelocity >= 0) {
        shader->setUniform(loc.uPointerVelocity,
                           QVector4D(state.velocity.x(), state.velocity.y(), state.velocity.length(),
                                     static_cast<float>(state.filteredSpeed)));
    }
    if (loc.uPointerPress >= 0) {
        shader->setUniform(loc.uPointerPress,
                           QVector4D(float(state.pressPos.x()), float(state.pressPos.y()),
                                     float(state.pressSecondsSince), float(state.pressButton)));
    }
    if (loc.uPointerRelease >= 0) {
        shader->setUniform(loc.uPointerRelease,
                           QVector4D(float(state.releasePos.x()), float(state.releasePos.y()),
                                     float(state.releaseSecondsSince), float(state.releaseButton)));
    }
    // Entries at or past this count read as zero by contract, which is what
    // the zero-fill in the trail loop below guarantees.
    const int filled = std::min(state.trailSize(), PSC::kMaxTrailPoints);
    if (loc.uPointerState >= 0) {
        shader->setUniform(
            loc.uPointerState,
            QVector4D(float(state.buttons), float(state.idleSeconds), float(state.scale), float(filled)));
    }
    if (loc.uCursorRect >= 0) {
        shader->setUniform(loc.uCursorRect,
                           QVector4D(float(cursorRect.x()), float(cursorRect.y()), float(cursorRect.width()),
                                     float(cursorRect.height())));
    }
    if (loc.uPointerFlags >= 0) {
        // .y is the same reach the damage rect is inflated by, so a pack can
        // clamp its own extents to the edge it will actually be clipped at.
        shader->setUniform(loc.uPointerFlags, QVector4D(hasCursorSprite ? 1.0f : 0.0f, reachDevicePx, 0.0f, 0.0f));
    }
    // uPointerTrail[0..31]. Entries past the filled count are pushed as ZERO
    // rather than skipped: the contract promises them zero, and a skipped
    // element would keep whatever the previous frame (or another pack) left
    // in the program's uniform storage, so a shortening trail would grow a
    // ghost tail.
    for (int i = 0; i < PSC::kMaxTrailPoints; ++i) {
        const int location = loc.uPointerTrail[static_cast<size_t>(i)];
        if (location < 0) {
            continue;
        }
        shader->setUniform(location, i < filled ? state.trailAt(i) : QVector4D());
    }
    for (int slot = 0; slot < PSC::kMaxCustomParams; ++slot) {
        const int location = loc.customParams[static_cast<size_t>(slot)];
        if (location >= 0) {
            shader->setUniform(location, pack.customParams[static_cast<size_t>(slot)]);
        }
    }
    for (int slot = 0; slot < PSC::kMaxCustomColors; ++slot) {
        const int location = loc.customColors[static_cast<size_t>(slot)];
        if (location >= 0) {
            shader->setUniform(location, pack.customColors[static_cast<size_t>(slot)]);
        }
    }
}

// ── Texture binding ─────────────────────────────────────────────────────────

int PointerDecorationPass::bindPackTextures(KWin::GLShader* shader, const PointerUniformLocations& loc,
                                            const CompiledPointerPack& pack, int firstUnit)
{
    int unit = firstUnit;
    for (int slot = 0; slot < PSC::kMaxUserTextureSlots; ++slot) {
        const auto& tex = pack.userTextures[static_cast<size_t>(slot)];
        const int samplerLoc = loc.userTextures[static_cast<size_t>(slot)];
        if (loc.iTextureResolution[static_cast<size_t>(slot)] >= 0) {
            const QSize size = tex ? tex->size() : QSize(0, 0);
            shader->setUniform(loc.iTextureResolution[static_cast<size_t>(slot)],
                               QVector4D(float(size.width()), float(size.height()), 0.0f, 0.0f));
        }
        if (samplerLoc < 0 || !tex) {
            continue;
        }
        shader->setUniform(samplerLoc, unit);
        glActiveTexture(GL_TEXTURE0 + unit);
        tex->bind();
        ++unit;
    }
    return unit;
}

void PointerDecorationPass::unbindUnits(int firstUnit, int endUnit)
{
    for (int unit = firstUnit; unit < endUnit; ++unit) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
}

// ── Cursor sprite ───────────────────────────────────────────────────────────

KWin::GLTexture* PointerDecorationPass::cursorSpriteTexture()
{
    if (!KWin::effects) {
        return nullptr;
    }
    const KWin::PlatformCursorImage cursor = KWin::effects->cursorImage();
    if (cursor.isNull()) {
        return nullptr;
    }
    const QImage img = cursor.image();
    // Keyed on the QImage's cacheKey: a stationary pointer re-uses the upload
    // every frame, and a theme or shape change (a resize arrow, a text caret)
    // produces a different key and re-uploads exactly once.
    if (m_cursorSprite && m_cursorSpriteKey == img.cacheKey()) {
        return m_cursorSprite.get();
    }
    std::unique_ptr<KWin::GLTexture> tex = KWin::GLTexture::upload(img);
    if (!tex) {
        qCWarning(lcEffect) << "Pointer pass could not upload the cursor sprite — uPointerFlags.x stays 0";
        return nullptr;
    }
    tex->setFilter(GL_LINEAR);
    tex->setWrapMode(GL_CLAMP_TO_EDGE);
    m_cursorSprite = std::move(tex);
    m_cursorSpriteKey = img.cacheKey();
    return m_cursorSprite.get();
}

// ── Multipass buffer stages ─────────────────────────────────────────────────

bool PointerDecorationPass::runBufferPasses(CompiledPointerPack& pack, const EngagedLayer& layer,
                                            const PPS::PointerFrameState& state, const QSize& deviceSize,
                                            const QRectF& cursorRect, double timeSeconds)
{
    const PPS::PointerShaderEffect& eff = layer.effect;
    const size_t passCount = pack.bufferPasses.size();
    if (passCount == 0) {
        return true;
    }
    const double scale = std::clamp(eff.bufferScale, PPS::PointerShaderEffect::kMinBufferScale,
                                    PPS::PointerShaderEffect::kMaxBufferScale);
    const QSize wantSize(std::max(1, int(std::lround(deviceSize.width() * scale))),
                         std::max(1, int(std::lround(deviceSize.height() * scale))));
    if (pack.bufferAllocFailed) {
        // Abandoned at this size rather than retried per frame: a persistent
        // allocation failure would otherwise cost the texture and FBO attempt
        // plus a warning on every frame of every burst. A size change is a
        // new question and lifts the latch, as the strip pass does for its
        // capture; a registry reload drops the whole entry.
        if (pack.bufferSize == wantSize) {
            return false;
        }
        pack.bufferAllocFailed = false;
    }
    // Every clear below must reach the whole target. The scene walk can leave
    // the scissor test enabled and the guard paintOutput holds restores it,
    // so disabling here cannot leak past the pass.
    glDisable(GL_SCISSOR_TEST);
    // A reset dropped the samples this canvas was accumulated from, so what it
    // holds is the previous burst. The reallocation below clears only when the
    // SIZE changed, and an output crossing between two same-size outputs (or
    // an un-suppress on the same one) changes nothing it looks at — so without
    // this the old glow is read back as feedback on the new burst's first
    // frame, when pointerIdleSeconds() is ~0 and the idle cut does not damp it.
    // Cleared here rather than at the reset because the GL work needs the
    // paint thread's current context.
    const bool feedbackStale = m_bufferFeedbackStale;
    if (pack.bufferSize != wantSize || pack.bufferTex.size() != passCount) {
        // Reallocate on a size change (an output resize, a scale change) or on
        // a first run. Both slots of every pair go, because the feedback
        // history is meaningless at a different resolution.
        pack.bufferTex.clear();
        pack.bufferFbo.clear();
        pack.bufferTex.resize(passCount);
        pack.bufferFbo.resize(passCount);
        pack.bufferSize = wantSize;
        pack.bufferFront = 0;
        // Latch the failure against wantSize (left in bufferSize so the size
        // comparison above can see a change) and warn ONCE, here, not per
        // frame from the caller.
        const auto abandon = [&pack, &layer]() {
            pack.bufferTex.clear();
            pack.bufferFbo.clear();
            pack.bufferAllocFailed = true;
            qCWarning(lcEffect) << "Pointer pack" << layer.effectId << "could not allocate its buffer targets at"
                                << pack.bufferSize << "— layer skipped until the size changes or the pack reloads";
            return false;
        };
        for (size_t i = 0; i < passCount; ++i) {
            for (int slot = 0; slot < 2; ++slot) {
                // GL_RGBA8, not the on-screen target's format: a buffer stage
                // is the pack's own scratch canvas in the pack's own units,
                // never a capture of the scene, so it has no HDR headroom to
                // inherit and no alpha-precision trap to dodge.
                auto tex = TransitionPass::allocateOutputTexture(wantSize, GL_RGBA8);
                if (!tex) {
                    return abandon();
                }
                auto fbo = std::make_unique<KWin::GLFramebuffer>(tex.get());
                if (!fbo->valid()) {
                    return abandon();
                }
                // Fresh targets start transparent, so a feedback pack's first
                // frame reads zero rather than driver garbage.
                KWin::GLFramebuffer::pushFramebuffer(fbo.get());
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                KWin::GLFramebuffer::popFramebuffer();
                pack.bufferTex[i][static_cast<size_t>(slot)] = std::move(tex);
                pack.bufferFbo[i][static_cast<size_t>(slot)] = std::move(fbo);
            }
        }
    }

    if (feedbackStale) {
        // Both slots, because bufferFront only says which one the NEXT read
        // takes; the other is one swap away from being read too.
        for (size_t i = 0; i < passCount; ++i) {
            for (size_t slot = 0; slot < 2; ++slot) {
                KWin::GLFramebuffer* const fbo = pack.bufferFbo[i][slot].get();
                if (!fbo) {
                    continue;
                }
                KWin::GLFramebuffer::pushFramebuffer(fbo);
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                KWin::GLFramebuffer::popFramebuffer();
            }
        }
    }

    const int front = pack.bufferFront;
    const int back = 1 - front;
    QMatrix4x4 identity;

    for (size_t i = 0; i < passCount; ++i) {
        const CompiledBufferPass& stage = pack.bufferPasses[i];
        KWin::GLFramebuffer* const target = pack.bufferFbo[i][static_cast<size_t>(back)].get();
        KWin::GLFramebuffer::pushFramebuffer(target);
        const auto popTarget = qScopeGuard([] {
            KWin::GLFramebuffer::popFramebuffer();
        });
        glViewport(0, 0, wantSize.width(), wantSize.height());
        glDisable(GL_BLEND); // a stage OWNS its target; it writes, it does not composite
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        KWin::ShaderBinder binder(stage.shader.get());
        stage.shader->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix, identity);
        // iResolution is the OUTPUT size for a buffer stage too, not its own
        // (possibly downscaled) target: every position uniform is in output
        // device px and pointerPixel(uv) multiplies the normalised uv by
        // iResolution to reach them, so a stage handed its target size would
        // put its stamps at bufferScale times the pointer's position. The
        // preview shares one UBO across its passes and so already hands the
        // buffer pass the item size; this keeps the two hosts on one rule.
        // The target's own size is what iChannelResolution carries.
        pushFrameUniforms(stage.shader.get(), stage.loc, pack, state, deviceSize, cursorRect, timeSeconds,
                          /*hasCursorSprite=*/false, float(layer.reachLogical * state.scale));
        int unit = bindPackTextures(stage.shader.get(), stage.loc, pack, 0);
        for (size_t ch = 0; ch < passCount && ch < 4; ++ch) {
            const int chLoc = stage.loc.iChannel[ch];
            if (stage.loc.iChannelResolution[ch] >= 0) {
                stage.shader->setUniform(stage.loc.iChannelResolution[ch],
                                         QVector4D(float(wantSize.width()), float(wantSize.height()), 0.0f, 0.0f));
            }
            if (chLoc < 0) {
                continue;
            }
            // Earlier stages hand this one their FRESH output; this stage's
            // OWN channel is its previous frame, which is what `bufferFeedback`
            // means. A later stage's output does not exist yet, so it is left
            // unbound rather than fed a stale frame the contract does not
            // promise.
            KWin::GLTexture* source = nullptr;
            if (ch < i) {
                source = pack.bufferTex[ch][static_cast<size_t>(back)].get();
            } else if (ch == i && eff.bufferFeedback) {
                source = pack.bufferTex[ch][static_cast<size_t>(front)].get();
            }
            if (!source) {
                continue;
            }
            stage.shader->setUniform(chLoc, unit);
            glActiveTexture(GL_TEXTURE0 + unit);
            source->bind();
            ++unit;
        }
        drawFullscreenQuad();
        unbindUnits(0, unit);
    }
    pack.bufferFront = back;
    return true;
}

// ── The pass ────────────────────────────────────────────────────────────────

void PointerDecorationPass::paintOutput(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                                        KWin::LogicalOutput* screen, KWin::RenderDevice* device)
{
    // Cost rule: an unengaged chain, the wrong output, or an output the
    // fullscreen gate covers costs one pointer comparison per output per frame
    // and nothing else. The suppression check is belt and braces on this path —
    // setSuppressedOutputs already emptied the history and released any cursor
    // hide when the gate closed, so the liveness test below would bail anyway —
    // but it keeps the cost rule true by inspection rather than by inference.
    if (!m_engaged || !screen || screen != m_output || suppressedOn(screen) || !KWin::effects) {
        return;
    }
    if (cursorHiddenElsewhere()) {
        // Someone else hid the sprite (a software KVM forwarding this desktop's
        // pointer to another machine, a client that installed a null cursor).
        // Drawing nothing here IS the erase: the compositor is already
        // repainting this region, so the previous frame's trail goes with it.
        // Tested after the output identity check rather than beside it so the
        // cursorImage() read stays one per frame on the pointer's output, not
        // one per output.
        releaseCursorHide(screen);
        return;
    }
    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
    if (!m_history.isLive(nowMs, m_maxTrailSeconds)) {
        // The chain went quiet. Release a hide taken by an `above` layer on a
        // PREVIOUS frame here rather than a frame later: this frame's normal
        // scene has already painted KWin's own cursor, so handing it back now
        // is seamless instead of blinking.
        m_hasTimeOrigin = false;
        updateCursorHiding();
        return;
    }
    const QRectF damage = damageDeviceRect(screen, nowMs);
    if (damage.isEmpty()) {
        return;
    }
    // The ONE canvas size of the frame: pushed as iResolution, used to size
    // the buffer stages, and the divisor drawDamageQuad normalises its
    // texcoords with. Taken from scaledRenderRect because that is the space
    // the quad's positions are placed in; a second source (deviceSize()) that
    // rounded differently would put uv * iResolution a pixel off the quad.
    const KWin::Rect scaledRect = viewport.scaledRenderRect();
    const QSize deviceSize(scaledRect.width(), scaledRect.height());
    if (deviceSize.isEmpty()) {
        return;
    }
    if (!m_hasTimeOrigin) {
        // iTime starts at zero for each burst of pointer activity, so a
        // time-driven pack's phase is reproducible instead of depending on
        // the compositor's uptime.
        m_timeOriginMs = nowMs;
        m_hasTimeOrigin = true;
    }
    const double timeSeconds = double(nowMs - m_timeOriginMs) / 1000.0;

    const PPS::PointerFrameState state = m_history.frameState(nowMs, screen->scale());
    const QRectF cursorRect = cursorCanvasRect(screen);

    // Hand GL state back exactly as found: KWin's convention for an effect
    // that draws inside someone else's frame. Taken BEFORE the framebuffer
    // push so it outlives every draw below.
    const ShaderInternal::ScopedGlState glStateGuard;
    // Both draws below use the no-region vbo->render() overload, so a scissor
    // box the scene walk left enabled would silently clip them (the quad IS
    // the clip here). Same discipline as the surface fold, the backdrop and
    // the capture paths; the guard above restores the test on exit.
    glDisable(GL_SCISSOR_TEST);

    // Draw into the framebuffer KWin handed us, sized to that target — the
    // same shape as the strip and desktop pass tails (see their comments for
    // the rotated-output and HDR-intermediate reasoning).
    KWin::GLFramebuffer* const targetFb = renderTarget.framebuffer();
    if (targetFb) {
        KWin::GLFramebuffer::pushFramebuffer(targetFb);
    }
    const auto popTargetFb = qScopeGuard([targetFb] {
        if (targetFb) {
            KWin::GLFramebuffer::popFramebuffer();
        }
    });
    const QSize targetSize = targetFb ? targetFb->size() : deviceSize;
    // pushFramebuffer already set this viewport for the targetFb branch; the
    // explicit call is load-bearing only on the no-framebuffer fallback (no
    // push happened, so the scene walk's viewport is still current). Kept
    // unconditional so both branches leave identical state, and re-asserted
    // after any buffer stage below, which renders into its own smaller target.
    // Same shape and reasoning as the strip pass tail.
    glViewport(0, 0, targetSize.width(), targetSize.height());

    // The chain composites OVER the finished frame, premultiplied
    // source-over, which is what the contract promises packs. Set explicitly
    // rather than inherited: KWin's item renderer disables blending after
    // every window it draws, so the ambient state here is whatever the scene
    // walk's last window left behind.
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    bool anyLayerDrawn = false;
    // The pack cache is keyed by pack id, so a chain naming the same pack
    // twice (which the decoration tree explicitly allows) hands both layers
    // ONE CompiledPointerPack. Running its buffer stages once per layer would
    // swap the ping-pong twice in a frame: the second run would read the
    // output the first just wrote as if it were the previous frame's, apply
    // the persistence decay a second time, and leave the first layer's main
    // draw sampling a slot the second run had since overwritten. Each pack's
    // stages therefore run at most once per frame, and the later layer reuses
    // the front slot the first run left.
    std::vector<const CompiledPointerPack*> ranBufferPasses;
    for (const EngagedLayer& layer : m_engagedLayers) {
        CompiledPointerPack* const pack = compiledPack(layer);
        if (!pack || !pack->shader) {
            // unknown id or failed compile (latched), or no context yet — that last
            // case caches nothing on purpose, so it IS retried next frame.
            continue;
        }
        // Buffer stages render into their own FBOs, so they run OUTSIDE the
        // on-screen bracket's viewport and blend state and restore both after.
        if (!pack->bufferPasses.empty()
            && std::find(ranBufferPasses.begin(), ranBufferPasses.end(), pack) == ranBufferPasses.end()) {
            if (!runBufferPasses(*pack, layer, state, deviceSize, cursorRect, timeSeconds)) {
                continue; // targets unallocatable; warned and latched in runBufferPasses
            }
            ranBufferPasses.push_back(pack);
            // runBufferPasses pushed and popped its own targets, which
            // restores KWin's framebuffer binding but neither the viewport
            // nor the blend state it set for the (possibly downscaled)
            // buffer.
            glViewport(0, 0, targetSize.width(), targetSize.height());
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }

        KWin::GLTexture* sprite = nullptr;
        // The location test alone is exact: cacheUniformLocations already forces
        // uCursorSprite to -1 for a pack without needsCursor, so a surviving
        // location implies the declaration.
        if (pack->loc.uCursorSprite >= 0) {
            sprite = cursorSpriteTexture();
        }

        KWin::ShaderBinder binder(pack->shader.get());
        pack->shader->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix, viewport.projectionMatrix());
        // The pack authors sRGB and this draws straight into KWin's blending
        // space, which on an HDR or wide-gamut output is not sRGB. The main
        // program was compiled with the pzFinalizeColor conversion spliced in
        // (pointerdecorationshader.cpp), and these are the uniforms it reads.
        // Identity on an SDR sRGB target. Same fix the tab pills and the
        // window-animation present path carry; the buffer stages above do NOT
        // convert, because their targets are intermediate.
        pack->shader->setColorspaceUniforms(KWin::ColorDescription::sRGB, renderTarget.colorDescription(),
                                            KWin::RenderingIntent::Perceptual);
        pushFrameUniforms(pack->shader.get(), pack->loc, *pack, state, deviceSize, cursorRect, timeSeconds,
                          sprite != nullptr, float(layer.reachLogical * state.scale));

        int unit = bindPackTextures(pack->shader.get(), pack->loc, *pack, 0);
        if (sprite) {
            pack->shader->setUniform(pack->loc.uCursorSprite, unit);
            glActiveTexture(GL_TEXTURE0 + unit);
            sprite->bind();
            ++unit;
        }
        for (size_t ch = 0; ch < pack->bufferPasses.size() && ch < 4; ++ch) {
            if (pack->loc.iChannelResolution[ch] >= 0) {
                pack->shader->setUniform(
                    pack->loc.iChannelResolution[ch],
                    QVector4D(float(pack->bufferSize.width()), float(pack->bufferSize.height()), 0.0f, 0.0f));
            }
            const int chLoc = pack->loc.iChannel[ch];
            if (chLoc < 0) {
                continue;
            }
            // bufferFront was swapped to the freshly written slot by
            // runBufferPasses, so this is this frame's output.
            KWin::GLTexture* const source = pack->bufferTex[ch][static_cast<size_t>(pack->bufferFront)].get();
            if (!source) {
                continue;
            }
            pack->shader->setUniform(chLoc, unit);
            glActiveTexture(GL_TEXTURE0 + unit);
            source->bind();
            ++unit;
        }

        drawDamageQuad(viewport, damage, deviceSize);

        // ScopedGlState restores the active-unit ENUM, not the BINDINGS, and a
        // name still bound when a later reset deletes it survives as a
        // dangling reference — the hole the strip and desktop passes document
        // at their own unbinds.
        unbindUnits(0, unit);
        anyLayerDrawn = true;
    }

    // After the whole chain, not inside runBufferPasses: that runs once per
    // pack, so clearing it there would leave every layer after the first
    // reading the stale canvas for this frame.
    //
    // Only when a buffer run actually happened, though. The flag means "the
    // ping-pong pair holds a PREVIOUS burst's content, do not feed it back", and
    // only runBufferPasses clears that content. A frame where every multipass pack
    // was skipped — not compiled yet, or its targets failed to allocate — leaves
    // the pair holding the old burst, so clearing here would let the next
    // bufferFeedback pack read it back at pointerIdleSeconds ~0: the glow bleed
    // across bursts this flag exists to prevent. A chain with no buffer stages at
    // all leaves it set harmlessly, since nothing else reads it and a pack added
    // later gets freshly cleared targets at allocation.
    if (!ranBufferPasses.empty()) {
        m_bufferFeedbackStale = false;
    }

    if (!anyLayerDrawn) {
        // Every layer latched or was skipped. Give the cursor back rather than
        // hold a hide for a chain that draws nothing. Unconditionally for this
        // output: updateCursorHiding keeps a hide while the chain is live,
        // and the chain IS live here (it passed the liveness gate above), so
        // only releaseCursorHide actually hands the cursor back.
        releaseCursorHide(screen);
        return;
    }

    // An `above` chain paints OVER the pointer, so KWin's own cursor has to go
    // and this pass owns drawing it. Taken AFTER the draws, at the point where
    // the frame is otherwise complete. On the frame the hide is FIRST taken
    // KWin's cursor was already composited by the scene walk that ran before
    // us, so the sprite is NOT blitted then: source-over on top of the same
    // sprite doubles the alpha at its antialiased edge for that one frame.
    // The hide takes effect from the next frame, and from then on this pass
    // is the only thing drawing the cursor.
    if (m_anyAboveLayer) {
        const bool newlyTaken = hideCursorForPass(screen);
        if (m_cursorHidden && !newlyTaken) {
            // Last draw of the pass: the cursor, above everything, where
            // KWin's overlay item would have put it.
            TransitionPass::drawSceneCursor(renderTarget, viewport, device);
        }
    }
}

} // namespace PlasmaZones
