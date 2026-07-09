// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "desktoptransitionmanager.h"

#include "plasmazoneseffect.h"
#include "shadertransitionmanager.h"
#include "plasmazoneseffect/shader_internal.h"

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVector2D>

#include <array>

namespace PlasmaZones {

namespace {
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

// The desktop transition has no per-path motion duration wired yet (that is a
// motion-tree setting, a later phase); use a sensible built-in switch duration.
constexpr int kDefaultDesktopSwitchDurationMs = 300;
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
                                     const QString& effectId, int durationMs)
{
    if (!from || !to || from == to || effectId.isEmpty()) {
        return; // nothing to animate (no shader assigned, or a no-op switch)
    }
    const int duration = durationMs > 0 ? durationMs : kDefaultDesktopSwitchDurationMs;
    const qint64 nowMs = ShaderInternal::shaderClockNowMs();

    // Resolve the affected outputs: a specific output for a per-output switch
    // (Plasma 6.7 per-output desktops, #648), otherwise every output for a
    // global all-output switch.
    QList<KWin::LogicalOutput*> outputs;
    if (output) {
        outputs.append(output);
    } else {
        outputs = KWin::effects->screens();
    }

    for (KWin::LogicalOutput* screen : outputs) {
        if (!screen) {
            continue;
        }
        OutputTransition tr;
        tr.from = from;
        tr.to = to;
        tr.effectId = effectId;
        tr.startTimeMs = nowMs;
        tr.durationMs = duration;
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
    const QSize textureSize = (logicalGeometry.size() * scale).toSize();
    if (textureSize.isEmpty()) {
        return nullptr;
    }

    // Never leak the capture's GL state (blend/viewport/clear/active texture)
    // into the on-screen draw that follows in this same frame.
    const ShaderInternal::ScopedGlState glStateGuard;

    std::unique_ptr<KWin::GLTexture> tex = KWin::GLTexture::allocate(GL_RGBA8, textureSize);
    if (!tex) {
        return nullptr;
    }
    tex->setFilter(GL_LINEAR);
    tex->setWrapMode(GL_CLAMP_TO_EDGE);

    KWin::GLFramebuffer fbo(tex.get());
    if (!fbo.valid()) {
        return nullptr;
    }

    // Route every window of this desktop through the raw draw path: set the
    // effect's capturing guard so its own paintWindow/apply short-circuit (no
    // border/morph shader) and we capture the plain composited content.
    m_effect->m_capturingSnapshot = true;

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

    m_effect->m_capturingSnapshot = false;
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
        compiled.shader = std::move(shader);
    }
    return &compiled;
}

bool DesktopTransitionManager::paintOutput(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport,
                                           int mask, const KWin::Region& deviceRegion, KWin::LogicalOutput* screen)
{
    Q_UNUSED(mask)
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

    // Deferred capture: both desktops are still alive and renderable here (the
    // switch has completed, but off-desktop windows persist). Capture once.
    if (!tr.captured) {
        tr.fromTex = captureDesktop(tr.from, screen);
        tr.toTex = captureDesktop(tr.to, screen);
        tr.captured = true;
    }
    CompiledDesktopShader* cs = compiledShader(tr.effectId);
    if (!cs || !cs->shader || !tr.fromTex || !tr.toTex) {
        // Compile or capture failed — abandon the transition rather than paint a
        // black screen; the normal scene paints the settled desktop.
        endOutput(screen);
        return false;
    }

    const QSize deviceSize = (screen->geometryF().size() * screen->scale()).toSize();

    const ShaderInternal::ScopedGlState glStateGuard;
    glViewport(0, 0, deviceSize.width(), deviceSize.height());
    glDisable(GL_BLEND); // the blend of two opaque desktops is itself opaque — replace the screen

    KWin::ShaderBinder binder(cs->shader.get());
    if (cs->iTimeLoc >= 0) {
        cs->shader->setUniform(cs->iTimeLoc, t);
    }
    if (cs->iResolutionLoc >= 0) {
        cs->shader->setUniform(cs->iResolutionLoc, QVector2D(float(deviceSize.width()), float(deviceSize.height())));
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

    Q_UNUSED(renderTarget)
    Q_UNUSED(viewport)
    drawDesktopBlendQuad();
    return true;
}

void DesktopTransitionManager::endOutput(KWin::LogicalOutput* screen)
{
    m_active.erase(screen);
    if (m_active.empty() && m_fullScreenClaimed) {
        KWin::effects->setActiveFullScreenEffect(nullptr);
        m_fullScreenClaimed = false;
    }
}

void DesktopTransitionManager::scheduleRepaints() const
{
    for (const auto& entry : m_active) {
        if (KWin::LogicalOutput* screen = entry.first) {
            KWin::effects->addRepaint(screen->geometry());
        } else {
            KWin::effects->addRepaintFull();
        }
    }
}

void DesktopTransitionManager::reset()
{
    m_active.clear();
    if (m_fullScreenClaimed) {
        KWin::effects->setActiveFullScreenEffect(nullptr);
        m_fullScreenClaimed = false;
    }
}

} // namespace PlasmaZones
