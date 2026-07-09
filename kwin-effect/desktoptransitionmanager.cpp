// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "desktoptransitionmanager.h"

#include "plasmazoneseffect.h"
#include "shadertransitionmanager.h"
#include "plasmazoneseffect/shader_internal.h"

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <core/output.h>
#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/glshadermanager.h>
#include <opengl/gltexture.h>
#include <opengl/glvertexbuffer.h>
#include <scene/windowitem.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QVector2D>

#include <array>

namespace PlasmaZones {

namespace {
// Output device-pixel size for @p screen (logical geometry scaled to the
// physical framebuffer). The capture textures and the blend viewport both key on
// this, so it is derived in one place.
QSize deviceSizeForOutput(KWin::LogicalOutput* screen)
{
    return (screen->geometryF().size() * screen->scale()).toSize();
}

// Allocate an output-sized RGBA8 texture for @p screen (LINEAR filter,
// CLAMP_TO_EDGE) — the shared preamble of both desktop captures. Returns null
// when the output's device size is empty or GL allocation fails.
std::unique_ptr<KWin::GLTexture> allocateOutputTexture(KWin::LogicalOutput* screen)
{
    const QSize textureSize = deviceSizeForOutput(screen);
    if (textureSize.isEmpty()) {
        return nullptr;
    }
    std::unique_ptr<KWin::GLTexture> tex = KWin::GLTexture::allocate(GL_RGBA8, textureSize);
    if (!tex) {
        return nullptr;
    }
    tex->setFilter(GL_LINEAR);
    tex->setWrapMode(GL_CLAMP_TO_EDGE);
    return tex;
}

// A full-screen NDC quad with 0..1 texcoords. Local copy (unique name) so the
// Unity build cannot ODR-collide with surfacelayers.cpp's drawFullscreenQuad —
// this TU is also excluded from the Unity blob in CMakeLists as belt-and-braces.
void drawDesktopBlendQuad()
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

DesktopTransitionManager::DesktopTransitionManager(PlasmaZonesEffect* effect)
    : m_effect(effect)
{
}

DesktopTransitionManager::~DesktopTransitionManager()
{
    // GL resources (textures/shaders) are released by their unique_ptrs. Do NOT
    // touch KWin::effects here — teardown ordering during plugin unload is not
    // guaranteed. reset() is the explicit-cleanup path while the compositor is live.
}

void DesktopTransitionManager::begin(KWin::VirtualDesktop* from, KWin::VirtualDesktop* to, KWin::LogicalOutput* output,
                                     const QString& effectId, const QVariantMap& params, int durationMs)
{
    if (!from || !to || from == to || effectId.isEmpty()) {
        return; // nothing to animate (no shader assigned, or a no-op switch)
    }
    // A non-positive resolve (corrupt/absent motion-tree node) falls back to the
    // default so the transition can't settle on its first paint (durationMs <= 0
    // would make elapsed >= durationMs true immediately).
    const int effectiveDurationMs = durationMs > 0 ? durationMs : DefaultDurationMs;
    const qint64 nowMs = ShaderInternal::shaderClockNowMs();

    // Resolve p_<name> parameter values into customParams[] slots. Same for
    // every output this begin() touches, so compute once. translateAnimationParams
    // fills the metadata defaults when the profile carries no override — WITHOUT
    // this the shaders run at customParams == 0 (slide has no direction, dissolve
    // no speckle scale, etc.) and appear broken.
    namespace ASC = PhosphorAnimationShaders::AnimationShaderContract;
    std::array<QVector4D, ASC::kMaxCustomParams> customParams{};
    std::array<QVector4D, ASC::kMaxCustomColors> customColors{};
    const PhosphorAnimationShaders::AnimationShaderEffect eff =
        m_effect->m_shaderManager.shaderRegistry().effect(effectId);
    if (!eff.isValid()) {
        // The resolved shader id is not installed (e.g. the profile references an
        // uninstalled pack). Do NOT claim the fullscreen effect or populate
        // m_active — bail so KWin's native desktop switch plays instead of a
        // blank transition that the first paintOutput would only then abandon.
        return;
    }
    if (!PhosphorAnimationShaders::shaderEffectAppliesToEventPath(eff,
                                                                  PhosphorAnimation::ProfilePaths::DesktopSwitch)) {
        // The resolved shader is not a desktop-contract pack — a window/surface
        // or universal shader inherited from a broader profile scope (the desktop
        // settings page only offers appliesTo:["desktop"] packs, but a `window`
        // or `global` scope override cascades here). It has no getFromColor /
        // getToColor, so it would sample the unbound uFromDesktop / uToDesktop and
        // render garbage. Refuse it and let KWin's native switch play. Routes
        // through the same predicate the settings picker filters on, so the
        // runtime and the picker share one opt-in desktop policy.
        return;
    }
    {
        const QVariantMap translated =
            PhosphorAnimationShaders::AnimationShaderRegistry::translateAnimationParams(eff, params);
        for (int slot = 0; slot < ASC::kMaxCustomParams; ++slot) {
            auto pull = [&](char comp) -> float {
                const auto it = translated.constFind(ASC::slotKey(slot, comp));
                if (it == translated.constEnd()) {
                    return 0.0f;
                }
                bool ok = false;
                const float v = it->toFloat(&ok);
                return ok ? v : 0.0f;
            };
            customParams[slot] = QVector4D(pull('x'), pull('y'), pull('z'), pull('w'));
        }
        // Color params resolve into the customColors pool as normalised rgba,
        // exactly as the per-window transition path uploads them (see
        // shader_transitions.cpp). translateAnimationParams coerces every color
        // to a valid QColor (default → Qt::transparent), so the isValid guard is
        // defence-in-depth against a caller that bypasses the registry encoder.
        for (int slot = 0; slot < ASC::kMaxCustomColors; ++slot) {
            const auto it = translated.constFind(ASC::colorKey(slot));
            if (it == translated.constEnd()) {
                continue;
            }
            const QColor c = it->value<QColor>();
            if (!c.isValid()) {
                continue;
            }
            customColors[slot] = QVector4D(c.redF(), c.greenF(), c.blueF(), c.alphaF());
        }
    }

    // Resolve the affected outputs: a specific output for a per-output switch
    // (Plasma 6.7 per-output desktops, #648), otherwise every output for a
    // global all-output switch.
    QList<KWin::LogicalOutput*> outputs;
    if (output) {
        outputs.append(output);
    } else {
        outputs = KWin::effects->screens();
    }

    // insert_or_assign below destroys any prior OutputTransition still mapped to
    // an output (a re-switch inside the transition window), freeing its captured
    // GLTextures. begin() runs from the desktopChanged signal handler, off the
    // paint thread, so make the compositor context current first — the same
    // discipline the shader-registry reload path uses.
    ensureGlContextCurrent();

    for (KWin::LogicalOutput* screen : outputs) {
        if (!screen) {
            continue;
        }
        OutputTransition tr;
        tr.from = from;
        tr.effectId = effectId;
        tr.customParams = customParams;
        tr.customColors = customColors;
        tr.startTimeMs = nowMs;
        tr.durationMs = effectiveDurationMs;
        tr.captured = false; // capture is deferred to the first paintOutput (live GL context)
        m_active.insert_or_assign(screen, std::move(tr));
    }

    if (m_active.empty()) {
        return;
    }

    // Claim the screen so KWin's built-in Slide (and Overview/Cube) bow out —
    // they check activeFullScreenEffect() in their desktopChanged handlers. This
    // is a race against Slide's own handler (its check is signal-time only, not
    // re-validated at paint), so it wins when our handler runs first; disabling
    // KWin's Desktop-Switch animation guarantees a clean single transition.
    if (!m_fullScreenClaimed) {
        KWin::effects->setActiveFullScreenEffect(m_effect);
        m_fullScreenClaimed = true;
    }
    KWin::effects->addRepaintFull();
}

std::unique_ptr<KWin::GLTexture> DesktopTransitionManager::captureDesktop(KWin::VirtualDesktop* desktop,
                                                                          KWin::LogicalOutput* screen)
{
    const qreal scale = screen->scale();
    const QRectF logicalGeometry = screen->geometryF();

    // Never leak the capture's GL state (blend/viewport/clear/active texture)
    // into the on-screen draw that follows in this same frame.
    const ShaderInternal::ScopedGlState glStateGuard;

    std::unique_ptr<KWin::GLTexture> tex = allocateOutputTexture(screen);
    if (!tex) {
        return nullptr;
    }
    KWin::GLFramebuffer fbo(tex.get());
    if (!fbo.valid()) {
        return nullptr;
    }

    // Route every window of this desktop through the raw draw path: set the
    // effect's capturing guard so its own paintWindow/apply short-circuit (no
    // border/morph shader) and we capture the plain composited content. Guard
    // the flag against a throw from the draw chain — a leaked m_capturingSnapshot
    // would corrupt every subsequent paint. Same pattern as the per-window
    // capture in paint_pipeline.cpp and the surface-layer sites.
    m_effect->m_capturingSnapshot = true;
    auto resetCapture = qScopeGuard([this] {
        m_effect->m_capturingSnapshot = false;
    });

    {
        KWin::RenderTarget renderTarget(&fbo);
        KWin::RenderViewport viewport(logicalGeometry, scale, renderTarget, QPoint());
        KWin::GLFramebuffer::pushFramebuffer(&fbo);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Bottom-to-top: stackingOrder() is already bottom→top, so the wallpaper
        // (an on-all-desktops window) lands first and app windows composite over
        // it. Windows outside this output are clipped by the viewport.
        const QList<KWin::EffectWindow*> stack = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* w : stack) {
            if (!w || w->isMinimized()) {
                continue;
            }
            if (!(w->isOnDesktop(desktop) || w->isOnAllDesktops())) {
                continue;
            }
            if (!w->expandedGeometry().intersects(logicalGeometry)) {
                continue;
            }
            KWin::ItemEffect keepRenderable(w->windowItem());
            KWin::WindowPaintData captureData;
            captureData.setOpacity(1.0);
            const int captureMask = KWin::Effect::PAINT_WINDOW_TRANSFORMED | KWin::Effect::PAINT_WINDOW_TRANSLUCENT;
            KWin::effects->drawWindow(renderTarget, viewport, w, captureMask, KWin::Region::infinite(), captureData);
        }

        KWin::GLFramebuffer::popFramebuffer();
    }

    return tex;
}

std::unique_ptr<KWin::GLTexture> DesktopTransitionManager::captureLiveScene(int mask, KWin::LogicalOutput* screen)
{
    const qreal scale = screen->scale();
    const QRectF logicalGeometry = screen->geometryF();

    const ShaderInternal::ScopedGlState glStateGuard;

    std::unique_ptr<KWin::GLTexture> tex = allocateOutputTexture(screen);
    if (!tex) {
        return nullptr;
    }
    KWin::GLFramebuffer fbo(tex.get());
    if (!fbo.valid()) {
        return nullptr;
    }

    {
        KWin::RenderTarget renderTarget(&fbo);
        KWin::RenderViewport viewport(logicalGeometry, scale, renderTarget, QPoint());
        KWin::GLFramebuffer::pushFramebuffer(&fbo);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        // Render the live scene (the now-current INCOMING desktop) into the FBO
        // via KWin's own composite. This is the downstream chain call (effects
        // below us + the scene), NOT a re-entry into our own paintScreen.
        KWin::effects->paintScreen(renderTarget, viewport, mask, KWin::Region(screen->geometry()), screen);
        KWin::GLFramebuffer::popFramebuffer();
    }
    return tex;
}

DesktopTransitionManager::CompiledDesktopShader* DesktopTransitionManager::compiledShader(const QString& effectId)
{
    auto cached = m_shaderCache.find(effectId);
    if (cached != m_shaderCache.end()) {
        return &cached->second; // may hold a null shader sentinel (compile failed) — caller checks
    }

    // Insert a default (null-shader) entry up front so every early-return path
    // caches a sentinel and never recompiles a broken pack every frame.
    CompiledDesktopShader& compiled = m_shaderCache.emplace(effectId, CompiledDesktopShader{}).first->second;

    ShaderTransitionManager& mgr = m_effect->m_shaderManager;
    const PhosphorAnimationShaders::AnimationShaderEffect eff = mgr.shaderRegistry().effect(effectId);
    if (!eff.isValid()) {
        return &compiled; // sentinel: unknown id
    }

    QFile shaderFile(eff.fragmentShaderPath);
    if (!shaderFile.open(QIODevice::ReadOnly)) {
        return &compiled;
    }
    const QString rawSource = QString::fromUtf8(shaderFile.readAll());

    QStringList animIncludePaths;
    for (const QString& sp : mgr.shaderRegistry().searchPaths()) {
        const QString sharedDir = sp + QStringLiteral("/shared");
        if (QDir(sharedDir).exists()) {
            animIncludePaths.append(sharedDir);
        }
    }

    // Reuse the exact per-window assembly: entry-point scaffold -> include
    // expansion -> named-param preamble -> KWin default-block define.
    const QString assembledSource = PhosphorShaders::assembleEntryPoint(
        rawSource, PhosphorAnimationShaders::AnimationShaderRegistry::animationEntryPrologue(),
        PhosphorAnimationShaders::AnimationShaderRegistry::animationEntryCandidates());
    QString includeError;
    const QString currentDir = QFileInfo(eff.fragmentShaderPath).absolutePath();
    QString expanded = PhosphorShaders::ShaderIncludeResolver::expandIncludes(assembledSource, currentDir,
                                                                              animIncludePaths, &includeError);
    if (expanded.isEmpty()) {
        return &compiled;
    }
    expanded = PhosphorShaders::spliceAfterVersion(
        expanded, PhosphorAnimationShaders::AnimationShaderRegistry::paramPreamble(eff));
    const QByteArray fragWithKwinDefine = ShaderInternal::injectKwinDefineAfterVersion(expanded);

    // Full-screen quad vertex stage: position is already clip-space, so there is
    // no modelViewProjectionMatrix — matches the surface buffer-pass primitive.
    static constexpr const char* kDesktopQuadVertexSource =
        "#version 450\n"
        "layout(location = 0) in vec2 position;\n"
        "layout(location = 1) in vec2 texCoord;\n"
        "layout(location = 0) out vec2 vTexCoord;\n"
        "void main() {\n"
        "    vTexCoord = texCoord;\n"
        "    gl_Position = vec4(position, 0.0, 1.0);\n"
        "}\n";
    const QByteArray vertWithKwinDefine =
        ShaderInternal::injectKwinDefineAfterVersion(QString::fromUtf8(kDesktopQuadVertexSource));

    std::unique_ptr<KWin::GLShader> shader = KWin::ShaderManager::instance()->generateCustomShader(
        KWin::ShaderTrait::MapTexture, vertWithKwinDefine, fragWithKwinDefine);
    if (shader) {
        compiled.iFromDesktopLoc = shader->uniformLocation("uFromDesktop");
        compiled.iToDesktopLoc = shader->uniformLocation("uToDesktop");
        compiled.iTimeLoc = shader->uniformLocation("iTime");
        compiled.iResolutionLoc = shader->uniformLocation("iResolution");
        compiled.iFrameLoc = shader->uniformLocation("iFrame");
        for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams; ++slot) {
            compiled.customParamsLoc[slot] = shader->uniformLocation(ShaderInternal::kCustomParamsElementNames[slot]);
        }
        for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors; ++slot) {
            compiled.customColorsLoc[slot] = shader->uniformLocation(ShaderInternal::kCustomColorsElementNames[slot]);
        }
        compiled.shader = std::move(shader);
    }
    return &compiled;
}

bool DesktopTransitionManager::paintOutput(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                                           int mask, const KWin::Region& deviceRegion, KWin::LogicalOutput* screen)
{
    Q_UNUSED(deviceRegion)
    if (!screen) {
        return false;
    }
    auto it = m_active.find(screen);
    if (it == m_active.end()) {
        return false;
    }
    OutputTransition& tr = it->second;

    // Settle check first: once the switch has run its course, tear this output
    // down and let the normal scene (the now-current desktop) paint.
    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
    const qint64 elapsed = nowMs - tr.startTimeMs;
    if (elapsed >= tr.durationMs) {
        endOutput(screen);
        return false;
    }
    const float t = tr.durationMs > 0 ? float(qBound<qreal>(0.0, qreal(elapsed) / qreal(tr.durationMs), 1.0)) : 1.0f;

    // Deferred capture (once). The OUTGOING desktop is no longer current, so its
    // windows are reconstructed via drawWindow. The INCOMING desktop IS the live
    // scene now, so it is captured via effects->paintScreen — drawWindow on the
    // current desktop's already-visible windows renders black.
    if (!tr.captured) {
        tr.fromTex = captureDesktop(tr.from, screen);
        tr.toTex = captureLiveScene(mask, screen);
        tr.captured = true;
    }
    CompiledDesktopShader* cs = compiledShader(tr.effectId);
    if (!cs || !cs->shader || !tr.fromTex || !tr.toTex) {
        // Compile or capture failed — abandon the transition rather than paint a
        // black screen; the normal scene paints the settled desktop.
        endOutput(screen);
        return false;
    }

    const QSize deviceSize = deviceSizeForOutput(screen);

    const ShaderInternal::ScopedGlState glStateGuard;
    // Draw into the framebuffer KWin handed us, sized to that target, instead of
    // assuming the default backbuffer at the device origin. Under a non-default
    // render target (HDR / colour-management intermediate) the target FB is
    // authoritative; on the common path it is the output's device-sized backbuffer,
    // so this matches the previous behaviour there. The fullscreen NDC quad needs no
    // projection matrix, so the viewport is intentionally unused; iResolution below
    // stays the OUTPUT device resolution (what the packs do aspect/texel maths
    // against), independent of the draw target's pixel size.
    KWin::GLFramebuffer* const targetFb = renderTarget.framebuffer();
    if (targetFb) {
        KWin::GLFramebuffer::pushFramebuffer(targetFb);
    }
    const QSize targetSize = targetFb ? targetFb->size() : deviceSize;
    glViewport(0, 0, targetSize.width(), targetSize.height());
    glDisable(GL_BLEND); // the blend of two opaque desktops is itself opaque — replace the screen

    KWin::ShaderBinder binder(cs->shader.get());
    if (cs->iTimeLoc >= 0) {
        cs->shader->setUniform(cs->iTimeLoc, t);
    }
    if (cs->iResolutionLoc >= 0) {
        cs->shader->setUniform(cs->iResolutionLoc, QVector2D(float(deviceSize.width()), float(deviceSize.height())));
    }
    // Monotonic frame counter for glitch-style packs. Advance after the upload
    // so the first painted frame is iFrame == 0, matching the per-window path's
    // zero-based leg frame.
    if (cs->iFrameLoc >= 0) {
        cs->shader->setUniform(cs->iFrameLoc, tr.frameCount);
    }
    ++tr.frameCount;
    // The p_<name> pack parameters (slide direction, dissolve scale, etc.).
    for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams; ++slot) {
        if (cs->customParamsLoc[slot] >= 0) {
            cs->shader->setUniform(cs->customParamsLoc[slot], tr.customParams[slot]);
        }
    }
    // Color params (customColors[N]) — the pool a `"type":"color"` pack param's
    // p_<name> define resolves to. Bound here so desktop packs get tunable
    // colors, at parity with the per-window transition and surface contracts.
    for (int slot = 0; slot < PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors; ++slot) {
        if (cs->customColorsLoc[slot] >= 0) {
            cs->shader->setUniform(cs->customColorsLoc[slot], tr.customColors[slot]);
        }
    }
    if (cs->iFromDesktopLoc >= 0) {
        cs->shader->setUniform(cs->iFromDesktopLoc, 0);
        glActiveTexture(GL_TEXTURE0);
        tr.fromTex->bind();
    }
    if (cs->iToDesktopLoc >= 0) {
        cs->shader->setUniform(cs->iToDesktopLoc, 1);
        glActiveTexture(GL_TEXTURE1);
        tr.toTex->bind();
    }
    glActiveTexture(GL_TEXTURE0);

    Q_UNUSED(viewport)
    drawDesktopBlendQuad();
    if (targetFb) {
        KWin::GLFramebuffer::popFramebuffer();
    }
    return true;
}

void DesktopTransitionManager::endOutput(KWin::LogicalOutput* screen)
{
    m_active.erase(screen);
    if (m_active.empty() && m_fullScreenClaimed && KWin::effects) {
        KWin::effects->setActiveFullScreenEffect(nullptr);
        m_fullScreenClaimed = false;
    }
}

void DesktopTransitionManager::ensureGlContextCurrent()
{
    if (KWin::effects) {
        KWin::effects->makeOpenGLContextCurrent();
    }
}

void DesktopTransitionManager::scheduleRepaints() const
{
    if (!KWin::effects) {
        return; // only reached from postPaintScreen (effects live), guard for parity
    }
    // Keys are always non-null: begin() skips null outputs and outputRemoved()
    // reaps a disconnected one, so a null screen can never be stored.
    for (const auto& entry : m_active) {
        KWin::effects->addRepaint(entry.first->geometry());
    }
}

void DesktopTransitionManager::outputRemoved(KWin::LogicalOutput* screen)
{
    // A disconnected output must not linger as a key in m_active: paintOutput()
    // and scheduleRepaints() deref the key (UAF), and the fullscreen-effect claim
    // would never release once its output vanished mid-transition. endOutput()
    // drops the entry and releases the claim when the last transition goes.
    if (m_active.find(screen) == m_active.end()) {
        return; // no live transition for this output
    }
    // screenRemoved fires off the paint thread; endOutput() erases the entry and
    // frees its captured GLTextures, which want a current GL context (endOutput's
    // other caller, paintOutput, is already on the paint thread with one current).
    ensureGlContextCurrent();
    endOutput(screen);
}

void DesktopTransitionManager::desktopRemoved(KWin::VirtualDesktop* desktop)
{
    // A desktop removed mid-switch leaves any OutputTransition still referencing
    // it as `from` holding a dangling VirtualDesktop*: the deferred
    // captureDesktop(tr.from, ...) only ever compares the pointer (isOnDesktop),
    // never derefs it, so this is not a crash — but a freed pointer must not
    // linger and be compared against a live desktop. Drop every transition whose
    // outgoing desktop just vanished.
    bool erasedAny = false;
    for (auto it = m_active.begin(); it != m_active.end();) {
        if (it->second.from == desktop) {
            // Erasing frees the entry's captured GLTextures; desktopRemoved fires
            // off the paint thread, so make the context current before the first
            // free — only when there is actually something to free.
            if (!erasedAny) {
                ensureGlContextCurrent();
                erasedAny = true;
            }
            it = m_active.erase(it);
        } else {
            ++it;
        }
    }
    if (erasedAny && m_active.empty() && m_fullScreenClaimed && KWin::effects) {
        KWin::effects->setActiveFullScreenEffect(nullptr);
        m_fullScreenClaimed = false;
    }
}

void DesktopTransitionManager::invalidateShaderCache()
{
    // Fires from the AnimationShaderRegistry file watcher between frames, where the
    // compositor GL context is NOT current. m_shaderCache owns GLShaders whose
    // destruction issues glDelete* calls that want a current context — the same
    // discipline reset() applies. Teardown (!effects) reclaims them regardless.
    ensureGlContextCurrent();
    m_shaderCache.clear();
}

void DesktopTransitionManager::reset()
{
    // Teardown path (compositor reset / plugin unload). Make the compositor
    // context current so the captured GLTextures (m_active) and compiled
    // GLShaders (m_shaderCache) free on a live context; when KWin::effects is
    // already gone the driver is tearing GL down and reclaims them regardless.
    // Clearing m_shaderCache HERE — not leaving it for ~DesktopTransitionManager,
    // which deliberately can't make a context current — is what makes this the
    // real "release GL resources" path the header documents.
    ensureGlContextCurrent();
    m_active.clear();
    m_shaderCache.clear();
    if (m_fullScreenClaimed && KWin::effects) {
        KWin::effects->setActiveFullScreenEffect(nullptr);
        m_fullScreenClaimed = false;
    }
}

} // namespace PlasmaZones
