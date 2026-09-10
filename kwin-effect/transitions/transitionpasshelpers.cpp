// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "transitionpasshelpers.h"

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>

#include "plasmazoneseffect/shader_internal.h"

#include <core/rendertarget.h>
#include <core/region.h>
#include <core/renderviewport.h>
#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <opengl/glframebuffer.h>
#include <opengl/gltexture.h>
#include <opengl/glvertexbuffer.h>

#include <scene/itemrenderer.h>
#include <scene/windowitem.h>
#include <scene/workspacescene.h>

#include <QColor>
#include <QList>
#include <QSize>
#include <QVector2D>

// Shared helpers of the screen-level transition passes (desktop + strip).
// Every function here is pure GL/data plumbing with no per-manager state;
// the rationale for each lives on its declaration in the header.
namespace PlasmaZones {

namespace TransitionPass {

GLenum captureFormatFor(const KWin::RenderTarget& outputTarget)
{
    const KWin::GLFramebuffer* const fb = outputTarget.framebuffer();
    const KWin::GLTexture* const targetTex = fb ? fb->colorAttachment() : nullptr;
    return targetTex ? targetTex->internalFormat() : GL_RGBA8;
}

GLenum alphaCaptureFormatFor(const KWin::RenderTarget& outputTarget)
{
    return alphaCaptureFormatForInternalFormat(captureFormatFor(outputTarget));
}

void clearAlpha(float alpha)
{
    // KWin's renderer leaves the scissor test off between windows, but a
    // third-party effect ordered after us may not, and a scissored clear
    // would stamp only a window's rect of the target. Save and restore what
    // is touched; the colour mask is restored to the all-on state KWin's
    // renderer expects rather than read back, because nothing in the paint
    // chain runs with a partial mask.
    const GLboolean scissorWas = glIsEnabled(GL_SCISSOR_TEST);
    GLfloat clearWas[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clearWas);
    if (scissorWas) {
        glDisable(GL_SCISSOR_TEST);
    }
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, alpha);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(clearWas[0], clearWas[1], clearWas[2], clearWas[3]);
    if (scissorWas) {
        glEnable(GL_SCISSOR_TEST);
    }
}

std::unique_ptr<KWin::GLTexture> allocateOutputTexture(const QSize& deviceSize, GLenum internalFormat)
{
    if (deviceSize.isEmpty()) {
        return nullptr;
    }
    std::unique_ptr<KWin::GLTexture> tex = KWin::GLTexture::allocate(internalFormat, deviceSize);
    if (!tex) {
        return nullptr;
    }
    tex->setFilter(GL_LINEAR);
    tex->setWrapMode(GL_CLAMP_TO_EDGE);
    return tex;
}

void drawOutputQuad(const KWin::RenderViewport& viewport)
{
    const KWin::Rect sr = viewport.scaledRenderRect();
    const float x0 = float(sr.left());
    const float y0 = float(sr.top());
    const float x1 = float(sr.right());
    const float y1 = float(sr.bottom());

    const std::array<KWin::GLVertex2D, 4> verts = {{
        {QVector2D(x0, y1), QVector2D(0.0f, 1.0f)}, // bottom-left
        {QVector2D(x1, y1), QVector2D(1.0f, 1.0f)}, // bottom-right
        {QVector2D(x0, y0), QVector2D(0.0f, 0.0f)}, // top-left
        {QVector2D(x1, y0), QVector2D(1.0f, 0.0f)}, // top-right
    }};
    KWin::GLVertexBuffer* const vbo = KWin::GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setVertices(verts);
    vbo->render(GL_TRIANGLE_STRIP);
}

const char* outputQuadVertexSource()
{
    return kOutputQuadVertexSource;
}

void drawSceneCursor(const KWin::RenderTarget& renderTarget, const KWin::RenderViewport& viewport)
{
    if (!KWin::effects) {
        return;
    }
    // The workspace scene is reached through any window item: the effects
    // API exposes no scene accessor, and Item::scene() on a member of the
    // scene IS the workspace scene. An empty stacking order means there is
    // no scene to draw the cursor over either.
    const QList<KWin::EffectWindow*> stack = KWin::effects->stackingOrder();
    KWin::WorkspaceScene* scene = nullptr;
    for (KWin::EffectWindow* w : stack) {
        if (w && w->windowItem()) {
            scene = qobject_cast<KWin::WorkspaceScene*>(w->windowItem()->scene());
            break;
        }
    }
    if (!scene || !scene->cursorItem()) {
        return;
    }
    // WorkspaceScene::updateCursor only moves the item while the cursor is
    // shown; hidden, its position is whatever the pointer was at when the
    // hide landed. Track the live pointer the way that slot does (the item's
    // own hotspot offset lives in its child, so the position IS the pointer).
    scene->cursorItem()->setPosition(KWin::effects->cursorPos());
    const ShaderInternal::ScopedGlState glStateGuard;
    scene->renderer()->renderItem(renderTarget, viewport, scene->cursorItem(), KWin::Effect::PAINT_SCREEN_TRANSFORMED,
                                  KWin::Region::infinite(), KWin::WindowPaintData{}, {}, {});
}

void translatePackParams(
    const PhosphorAnimationShaders::AnimationShaderEffect& eff, const QVariantMap& params,
    std::array<QVector4D, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomParams>& customParams,
    std::array<QVector4D, PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors>& customColors)
{
    namespace ASC = PhosphorAnimationShaders::AnimationShaderContract;
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
    for (int slot = 0; slot < ASC::kMaxCustomColors; ++slot) {
        // Write EVERY slot, zeroing on both miss branches (absent key AND
        // invalid color), exactly as the params loop above assigns every
        // slot unconditionally. Skipping a miss leaves whatever the caller's
        // array held — and the strip pass re-translates into its LIVE entry
        // on a mid-leg pack switch, so a skipped slot would hand the new
        // pack the old pack's color. Desktop callers pass value-initialised
        // arrays, for which the zero write is a no-op.
        const auto it = translated.constFind(ASC::colorKey(slot));
        if (it == translated.constEnd()) {
            customColors[slot] = QVector4D();
            continue;
        }
        const QColor c = it->value<QColor>();
        if (!c.isValid()) {
            customColors[slot] = QVector4D();
            continue;
        }
        customColors[slot] = QVector4D(c.redF(), c.greenF(), c.blueF(), c.alphaF());
    }
}

} // namespace TransitionPass

} // namespace PlasmaZones
