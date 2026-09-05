// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "transitionpasshelpers.h"

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>

#include <core/rendertarget.h>
#include <core/renderviewport.h>
#include <opengl/glframebuffer.h>
#include <opengl/gltexture.h>
#include <opengl/glvertexbuffer.h>

#include <QColor>
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
    switch (captureFormatFor(outputTarget)) {
    // Float and wide (10-bit and up) targets: keep the precision, add the
    // alpha channel. GL_RGB10_A2 is listed here on purpose: it is what KWin
    // hands a 10-bit SDR output, and its 2-bit alpha cannot carry coverage.
    case GL_RGBA16F:
    case GL_RGB16F:
    case GL_RGBA32F:
    case GL_RGB32F:
    case GL_R11F_G11F_B10F:
    case GL_RGB10_A2:
    case GL_RGBA16:
    case GL_RGB16:
        return GL_RGBA16F;
    default:
        return GL_RGBA8;
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
    static constexpr const char* kSource =
        "#version 450\n"
        "uniform mat4 modelViewProjectionMatrix;\n"
        "layout(location = 0) in vec2 position;\n"
        "layout(location = 1) in vec2 texCoord;\n"
        "layout(location = 0) out vec2 vTexCoord;\n"
        "void main() {\n"
        "    vTexCoord = texCoord;\n"
        "    gl_Position = modelViewProjectionMatrix * vec4(position, 0.0, 1.0);\n"
        "}\n";
    return kSource;
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
