// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "striptransitionmanager.h"

#include "compositor/stripviewanimator.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "plasmazoneseffect/shader_internal.h"
#include "shadertransitionmanager.h"
#include "transitionpasshelpers.h"

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include <core/output.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <effect/effecthandler.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/gltexture.h>

#include <QPoint>
#include <QRectF>
#include <QSize>
#include <QVector2D>

#include <cmath>

// The drive part of StripTransitionManager: arm from the tiling batch path,
// capture the live scene, run the pack over it. The pack-source → GLShader
// assembly lives in striptransitionshader.cpp; the GL helpers shared with the
// desktop pass in transitionpasshelpers.cpp. Teardown is small enough (no
// fullscreen claim, no endpoint caches — liveness belongs to the view spring)
// that it lives here rather than in a fourth TU.
namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace {
// Velocity smoothing time constant. One-pole low-pass over the
// finite-difference velocity so a wheel batch's instantaneous retarget (which
// steps the offset without stepping time) reads as a quick ramp instead of a
// one-frame spike in a velocity-driven blur.
constexpr qreal kVelocitySmoothingTauMs = 30.0;
// dt cap for the velocity estimate, matching easeProgress's discipline: a
// paint stall must not turn into a huge dt that swallows the smoothing.
constexpr qreal kMaxVelocityDtMs = 100.0;
} // namespace

StripTransitionManager::StripTransitionManager(PlasmaZonesEffect* effect)
    : m_effect(effect)
{
}

StripTransitionManager::~StripTransitionManager()
{
    // GL resources are released by their unique_ptrs. Do NOT touch
    // KWin::effects here — teardown ordering during plugin unload is not
    // guaranteed. reset() is the explicit-cleanup path while the compositor
    // is live.
}

void StripTransitionManager::notifyLeg(KWin::LogicalOutput* output, const QString& effectId, const QVariantMap& params)
{
    if (!output) {
        return;
    }
    // Empty id — or a pack that is uninstalled / not strip-classed — disarms
    // the output. The erase contract matters as much as the arm: the batch
    // path calls notifyLeg on every seeded leg, so clearing the pack (or
    // disabling animations) mid-flight tears the pass down on the very next
    // wheel tick rather than stranding it until the spring settles.
    bool runnable = !effectId.isEmpty();
    PhosphorAnimationShaders::AnimationShaderEffect eff;
    if (runnable) {
        eff = m_effect->m_shaderManager.shaderRegistry().effect(effectId);
        // Contract-validate against the leaf, exactly as the desktop pass
        // does in prepareTransitionPrototype: a stale override naming a
        // non-strip pack must fall through to the plain translation, not
        // install a pass whose sampler contract the pack never binds.
        if (!eff.isValid()
            || !PhosphorAnimationShaders::shaderEffectAppliesToEventPath(
                eff, PhosphorAnimation::ProfilePaths::ScrollingView)) {
            runnable = false;
        }
    }
    auto it = m_active.find(output);
    if (!runnable) {
        if (it != m_active.end()) {
            // notifyLeg fires from the D-Bus batch path, off the paint
            // thread; the erase frees the entry's capture texture.
            ensureGlContextCurrent();
            m_active.erase(it);
        }
        return;
    }
    if (it == m_active.end()) {
        OutputStripPass pass;
        pass.effectId = effectId;
        TransitionPass::translatePackParams(eff, params, pass.customParams, pass.customColors);
        m_active.emplace(output, std::move(pass));
        return;
    }
    // Refresh only: a batch landing mid-leg is the spring's retarget path,
    // and the pass must ride through it — capture texture, iTime origin and
    // velocity state all persist. Params are re-translated so a settings
    // change mid-scroll applies on the next frame.
    it->second.effectId = effectId;
    TransitionPass::translatePackParams(eff, params, it->second.customParams, it->second.customColors);
}

bool StripTransitionManager::isRunning() const
{
    for (const auto& entry : m_active) {
        if (m_effect->m_stripViewAnimator->isAnimatingOn(entry.first)) {
            return true;
        }
    }
    return false;
}

bool StripTransitionManager::isRunningForOutput(KWin::LogicalOutput* screen) const
{
    // Liveness is the SPRING's: an armed output whose leg has settled is not
    // running (paintOutput falls through to the normal scene the same frame),
    // and reapSettled frees its entry lazily. The spring is also how every
    // kill path folds in for free — animations disabled, the animator's
    // clock reap, forgetOutput — none of them need to know this class exists.
    return m_active.find(screen) != m_active.end() && m_effect->m_stripViewAnimator->isAnimatingOn(screen);
}

bool StripTransitionManager::paintOutput(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                                         int mask, const KWin::Region& deviceRegion, KWin::LogicalOutput* screen)
{
    // As on the desktop pass: the damage region does not participate —
    // prePaintScreen sets PAINT_SCREEN_TRANSFORMED for a running output and
    // the spring's repaint callback damages the full output every frame.
    Q_UNUSED(deviceRegion)
    if (!screen) {
        return false;
    }
    auto it = m_active.find(screen);
    if (it == m_active.end()) {
        return false;
    }
    if (!m_effect->m_stripViewAnimator->isAnimatingOn(screen)) {
        // Settled (or killed) — fall through to the normal scene THIS frame;
        // reapSettled frees the entry from postPaintScreen.
        return false;
    }
    OutputStripPass& pass = it->second;

    // Compile BEFORE capturing, for the desktop pass's reason: a capture
    // renders the entire scene, so doing it first would burn a full-screen
    // pass only to throw it away when the shader turns out not to compile —
    // and the failure sentinel stops re-COMPILES, not re-captures, so that
    // waste would repeat every frame for the rest of the leg.
    CompiledStripShader* cs = compiledShader(pass.effectId);
    if (!cs || !cs->shader) {
        // Compile failed — abandon the pass rather than paint a black
        // output; the normal scene (the plain translation) paints instead,
        // this frame and every later one (the sentinel keeps notifyLeg's
        // re-arms pointless but harmless: we end here again before any
        // capture).
        endOutput(screen);
        return false;
    }

    // Ensure the persistent capture target, revalidated against this frame's
    // device size and on-screen format (output scale/mode change, HDR flip).
    const QSize deviceSize = viewport.deviceSize();
    const GLenum captureFormat = TransitionPass::captureFormatFor(renderTarget);
    if (pass.captureTex
        && (pass.captureTex->size() != deviceSize || pass.captureTex->internalFormat() != captureFormat)) {
        pass.captureFbo.reset();
        pass.captureTex.reset();
    }
    if (!pass.captureTex) {
        pass.captureTex = TransitionPass::allocateOutputTexture(deviceSize, captureFormat);
        if (pass.captureTex) {
            pass.captureFbo = std::make_unique<KWin::GLFramebuffer>(pass.captureTex.get());
            if (!pass.captureFbo->valid()) {
                pass.captureFbo.reset();
                pass.captureTex.reset();
            }
        }
        if (!pass.captureTex) {
            // Allocation failed — abandon rather than retry an output-sized
            // allocation every frame at a size this GPU just refused.
            endOutput(screen);
            return false;
        }
    }

    // Render the live scene into the capture. This is the downstream chain
    // call (effects below us + the scene), NOT a re-entry into our own
    // paintScreen — but it DOES re-enter our paintWindow, which is the point:
    // the strip translation, the parked-column relocation and the
    // tab-indicator offset all apply inside the capture, so the pack
    // decorates exactly the frame the user would otherwise have seen. The
    // pass bracket's m_currentPassOutput is still this output, so the
    // foreign-output cull behaves identically to the on-screen path.
    {
        const ShaderInternal::ScopedGlState glStateGuard;
        KWin::RenderTarget captureTarget(pass.captureFbo.get(), renderTarget.colorDescription());
        KWin::RenderViewport captureViewport(screen->geometryF(), screen->scale(), captureTarget, QPoint());
        KWin::GLFramebuffer::pushFramebuffer(pass.captureFbo.get());
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        // Device-space region rooted at (0, 0) — the FBO's own space, not the
        // output-positioned logical geometry; see captureLiveScene's note.
        KWin::effects->paintScreen(captureTarget, captureViewport, mask,
                                   KWin::Region(KWin::Rect(QPoint(), captureViewport.deviceSize())), screen);
        KWin::GLFramebuffer::popFramebuffer();
    }

    // Motion state, on the PINNED frame clock (one timestamp per output pass;
    // re-sampling per call is the multi-pass ghosting trap the pin exists
    // for). The pin is set in prePaintScreen for this pass's bracket; -1
    // means a caller outside any bracket, which cannot happen from
    // paintScreen, but fall back rather than divide time by a sentinel.
    qint64 nowMs = m_effect->m_shaderManager.currentFrameClockMs();
    if (nowMs < 0) {
        nowMs = ShaderInternal::shaderClockNowMs();
    }
    if (pass.startTimeMs < 0) {
        pass.startTimeMs = nowMs;
    }
    const qreal offsetLogical = m_effect->m_stripViewAnimator->offsetFor(screen);
    if (pass.lastPaintTimeMs >= 0) {
        const qreal dtMs = qBound<qreal>(0.0, qreal(nowMs - pass.lastPaintTimeMs), kMaxVelocityDtMs);
        if (dtMs > 0.0) {
            const qreal rawVelocity = (offsetLogical - pass.lastOffsetPx) / (dtMs / 1000.0);
            const qreal alpha = 1.0 - std::exp(-dtMs / kVelocitySmoothingTauMs);
            pass.smoothedVelocity += alpha * (rawVelocity - pass.smoothedVelocity);
        }
    }
    pass.lastOffsetPx = offsetLogical;
    pass.lastPaintTimeMs = nowMs;

    const qreal scale = screen->scale();
    const float offsetDevice = float(offsetLogical * scale);
    const float velocityDevice = float(pass.smoothedVelocity * scale);
    const float deviceW = float(deviceSize.width() > 0 ? deviceSize.width() : 1);

    // The strip's work area (panels/docks excluded), output-local device px.
    // clientArea(MaximizeArea) is the same panel-excluded rect the daemon's
    // available-geometry report uses (screenchangehandler.cpp). A degenerate
    // rect uploads as zeros, which strip_transition.glsl's stripMask treats
    // as "mask nothing".
    QVector4D stripRect;
    const QRectF workArea = QRectF(KWin::effects->clientArea(KWin::MaximizeArea, screen).toRect());
    const QRectF outputGeo = screen->geometryF();
    if (!workArea.isEmpty()) {
        const QRectF local = workArea.translated(-outputGeo.topLeft());
        stripRect = QVector4D(float(local.x() * scale), float(local.y() * scale), float(local.width() * scale),
                              float(local.height() * scale));
    }

    const ShaderInternal::ScopedGlState glStateGuard;
    // Draw into the framebuffer KWin handed us, sized to that target — same
    // shape as the desktop blend's tail (see its comments for the
    // rotated-output and HDR-intermediate reasoning).
    KWin::GLFramebuffer* const targetFb = renderTarget.framebuffer();
    if (targetFb) {
        KWin::GLFramebuffer::pushFramebuffer(targetFb);
    }
    const QSize targetSize = targetFb ? targetFb->size() : deviceSize;
    glViewport(0, 0, targetSize.width(), targetSize.height());
    glDisable(GL_BLEND); // the decorated scene is itself opaque — replace the output

    KWin::ShaderBinder binder(cs->shader.get());
    cs->shader->setUniform(KWin::GLShader::Mat4Uniform::ModelViewProjectionMatrix, viewport.projectionMatrix());
    if (cs->iTimeLoc >= 0) {
        // SECONDS since this output's pass activated — monotonic, never
        // rewinding on a retarget. NOT progress; see strip_transition.glsl.
        cs->shader->setUniform(cs->iTimeLoc, float(qreal(nowMs - pass.startTimeMs) / 1000.0));
    }
    if (cs->iResolutionLoc >= 0) {
        cs->shader->setUniform(cs->iResolutionLoc, QVector2D(float(deviceSize.width()), float(deviceSize.height())));
    }
    if (cs->iFrameLoc >= 0) {
        cs->shader->setUniform(cs->iFrameLoc, pass.frameCount);
    }
    ++pass.frameCount;
    if (cs->iStripMotionLoc >= 0) {
        cs->shader->setUniform(
            cs->iStripMotionLoc,
            QVector4D(offsetDevice, velocityDevice, offsetDevice / deviceW, velocityDevice / deviceW));
    }
    if (cs->iStripRectLoc >= 0) {
        cs->shader->setUniform(cs->iStripRectLoc, stripRect);
    }
    for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams; ++slot) {
        if (cs->customParamsLoc[slot] >= 0) {
            cs->shader->setUniform(cs->customParamsLoc[slot], pass.customParams[slot]);
        }
    }
    for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors; ++slot) {
        if (cs->customColorsLoc[slot] >= 0) {
            cs->shader->setUniform(cs->customColorsLoc[slot], pass.customColors[slot]);
        }
    }
    if (cs->uStripLoc >= 0) {
        cs->shader->setUniform(cs->uStripLoc, 0);
        glActiveTexture(GL_TEXTURE0);
        pass.captureTex->bind();
    }

    TransitionPass::drawOutputQuad(viewport);

    // Unbind the capture unit: ScopedGlState restores the active-unit ENUM,
    // not the BINDINGS, and a name still bound when a reap later deletes it
    // survives as a dangling reference — the exact hole the desktop pass
    // documents at its own unbind.
    if (cs->uStripLoc >= 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    if (targetFb) {
        KWin::GLFramebuffer::popFramebuffer();
    }
    return true;
}

void StripTransitionManager::reapSettled()
{
    bool contextEnsured = false;
    for (auto it = m_active.begin(); it != m_active.end();) {
        if (!m_effect->m_stripViewAnimator->isAnimatingOn(it->first)) {
            // The settle frame itself needed no repaint — paintOutput
            // returned false and the normal scene painted in that same frame
            // — so this is pure resource hygiene. The erase frees an
            // output-sized GLTexture; postPaintScreen runs on the paint
            // thread, but ensure anyway for parity with the other mutators.
            if (!contextEnsured) {
                ensureGlContextCurrent();
                contextEnsured = true;
            }
            it = m_active.erase(it);
        } else {
            ++it;
        }
    }
}

void StripTransitionManager::endOutput(KWin::LogicalOutput* screen)
{
    m_active.erase(screen);
}

void StripTransitionManager::ensureGlContextCurrent()
{
    if (KWin::effects) {
        KWin::effects->makeOpenGLContextCurrent();
    }
}

void StripTransitionManager::invalidateShaderCache()
{
    // Fires from the AnimationShaderRegistry file watcher between frames,
    // where the compositor GL context is NOT current; the GLShaders' frees
    // want one. Teardown (!effects) reclaims them regardless.
    ensureGlContextCurrent();
    m_shaderCache.clear();
}

void StripTransitionManager::outputRemoved(KWin::LogicalOutput* screen)
{
    // A disconnected output must not linger as a key: paintOutput and
    // reapSettled deref it against the spring map. StripViewAnimator's own
    // forgetOutput runs beside this in the effect's screenRemoved handler.
    const auto it = m_active.find(screen);
    if (it == m_active.end()) {
        return;
    }
    // screenRemoved fires off the paint thread; the erase frees the entry's
    // capture texture.
    ensureGlContextCurrent();
    m_active.erase(it);
}

void StripTransitionManager::reset()
{
    // Teardown path (compositor reset / plugin unload). Clearing the shader
    // cache HERE — not leaving it for the destructor, which deliberately
    // can't make a context current — is what makes this the real "release GL
    // resources" path.
    ensureGlContextCurrent();
    m_active.clear();
    m_shaderCache.clear();
}

} // namespace PlasmaZones
